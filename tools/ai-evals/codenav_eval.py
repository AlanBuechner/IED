"""How a real model navigates code with IED's tools (docs/13 "Navigating code"): it is asked questions about the IED
server's own sources, each in its own read-only Ask chat of a server started for the run, and every turn's tool calls,
answer and time are recorded. The summary scores each turn: whether it used the tool the question calls for, and the
misuses seen in practice - a text search for an identifier, a regex sent as literal text, a whole source file read,
failed calls. The model is a profile of the owner's app settings (`ai.models`, read at run time, so its address never
leaves that file).

	python -I tools/ai-evals/codenav_eval.py --server build/bin/Development-windows-x64/ied-server/ied-server.exe --setup
	python -I tools/ai-evals/codenav_eval.py --server <exe> --model qwen3.8-27b@lan --only members,trace

--setup makes the workspace first: the server's sources (src/ and the built-in plugins' code its nuke.toml names) copied
into a Nuke workspace, `nuke init --ied`, `nuke build ied-server` (so C4LSP gets the project's description). Results go
to build/ai-evals/<time>/ (report.json, summary.md).
"""

import argparse
import asyncio
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools", "pylib"))
sys.path.insert(0, os.path.join(REPO, "tools", "server-checks"))
sys.path.insert(0, os.path.join(REPO, "tools", "server-checks", "tests"))

import jsonc  # noqa: E402
from server import EXECUTABLE_ENV, IedServer  # noqa: E402
from frames import FrameChecker  # noqa: E402
from iedclient import Client  # noqa: E402
from test_chat import make_home  # noqa: E402

WORKSPACE = os.path.join(REPO, "build", "ai-evals", "Srv")
TURN_SECONDS = 1200

ERROR_CODES = ["BadRequest", "NotFound", "Forbidden", "Untrusted", "Conflict", "Busy", "Cancelled", "Timeout", "Unavailable", "Internal", "PluginError"]

# Each question: its key, the tools a good route uses (any of them), the facts a right answer states (names, as written
# in the code: the quality score is the share found), and what it asks.
QUESTIONS = [
	("serving", ["code_read_symbol"], ["ServingPath", "ProfileServing", "ProjectFor"],
		"How does the language server pool decide which language server serves a document? Show me the function that does it and explain the rule."),
	("members", ["code_members"], ["RequestFor", "ServingFor", "SymbolsFor", "NamedText", "StartFor"],
		"List the members of the LspPool class with their signatures, and tell me which ones are meant for plugin code."),
	("callers", ["code_references", "code_call_hierarchy"], ["WriteThrough", "OnDiskWrite", "EditMethod", "ServerEdit"],
		"Which functions call Documents::ApplyEdit? Give me each caller with its file and line."),
	("request", ["code_read_symbol"], ["Ask", "cancelRequest", "Timeout"],
		"Show me LanguageServer::Request and explain how it waits for the server's answer and what happens on a timeout."),
	("errors", ["code_read_symbol", "code_members"], ERROR_CODES + ["rpcerror.ch"],
		"What error codes can an RpcError carry, and where are they defined?"),
	("change", ["code_references", "code_call_hierarchy"], ["pool.ch", "RequestMethod", "RequestFor"],
		"I want to change the parameters of WithTextDocument. Which places would I need to update?"),
	("trace", ["code_call_hierarchy"], ["Register", "RequestMethod", "Asked", "WriteLoop", "FrameMessage"],
		"Trace what happens when a client sends the lsp.request method: from where the method is registered down to the message written to the language server process."),
	("paste", ["code_read_symbol"], ["ReadToolArguments", "PasteArguments", "Check"],
		"What does the chat's turn run do with a tool call's arguments before it runs the tool? Point me at the code."),
]

SOURCE_EXTENSIONS = (".cf", ".ch", ".c", ".h", ".cpp", ".hpp", ".cc", ".rs", ".go", ".py", ".js", ".ts", ".cs")
IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*((::|\.|->)[A-Za-z_][A-Za-z0-9_]*)*$")
REGEX_SIGNS = re.compile(r"\||\.\*|\\[bswd(]|\[.+\]")


def settings_profile(path, name):
	with open(path, encoding="utf-8") as f:
		models = jsonc.loads(f.read()).get("ai.models", {})
	if not models:
		raise SystemExit("no ai.models in %s (a Microsoft Store Python reads its own private copy of AppData: run vendor/python/python.exe, or pass --settings)" % path)
	name = name or next(iter(models))
	if name not in models:
		raise SystemExit("no model %s in %s (it has %s)" % (name, path, ", ".join(models)))
	return name, models[name]


def setup(workspace):
	"""The server's sources as their own Nuke workspace, its IED solution and a build, for C4LSP's project options."""
	with open(os.path.join(REPO, "src", "nuke.toml"), encoding="utf-8") as f:
		sources = re.search(r"^sources\s*=\s*\[(.*?)\]", f.read(), re.M).group(1)
	shutil.rmtree(workspace, ignore_errors=True)
	shutil.copytree(os.path.join(REPO, "src"), os.path.join(workspace, "src"), ignore=shutil.ignore_patterns("tests"))
	for source in re.findall(r'"\.\./(plugins/[^"]+)"', sources):
		shutil.copytree(os.path.join(REPO, source), os.path.join(workspace, source))
	with open(os.path.join(workspace, "nuke.toml"), "w", encoding="utf-8", newline="") as f:
		f.write('[workspace]\nname = "Srv"\ntoolchain = "dev"\nprojects = ["src"]\n')
	for words in (["init", "--ied"], ["build", "ied-server"]):
		done = subprocess.run(["cmd", "/c", os.path.join(REPO, "nuke.bat")] + words, cwd=workspace, capture_output=True, text=True)
		if done.returncode != 0:
			raise SystemExit("nuke %s failed:\n%s%s" % (" ".join(words), done.stdout[-3000:], done.stderr[-3000:]))


def flags(call):
	"""The misuses one call shows."""
	found = []
	try:
		args = json.loads(call["args"] or "{}")
	except ValueError:
		return ["arguments not JSON"]
	if call["state"] in ("error", "denied"):
		found.append("failed: " + call["result"][:120].replace("\n", " "))
	if call["tool"] == "search_text":
		pattern = args.get("pattern", "")
		if IDENTIFIER.match(pattern) and not args.get("regex"):
			found.append("text search for an identifier (code_symbols / code_references)")
		if REGEX_SIGNS.search(pattern) and not args.get("regex"):
			found.append("a regex sent as literal text")
	if call["tool"] == "fs_read" and args.get("path", "").endswith(SOURCE_EXTENSIONS) and "offset" not in args and "limit" not in args:
		found.append("a whole source file read (code_read_symbol)")
	return found


def quality(turn, facts):
	"""The facts the answer states, and those it leaves out."""
	missing = [fact for fact in facts if fact not in turn.get("answer", "")]
	return len(facts) - len(missing), missing


def summary(report, model, temperature):
	lines = ["# Code navigation eval", "", "Model `%s` at temperature %s, %s." % (model, temperature, time.strftime("%Y-%m-%d %H:%M")), "",
		"Quality: the facts a right answer states, found in the answer. Steps: requests to the model. Prompt: the last request's prompt tokens (the context the turn ended with). Out: completion tokens over the turn, reasoning included.", "",
		"| Question | Quality | Seconds | Steps | Calls | Prompt | Out | Thinking chars | Answer chars | Expected tool used | Misuses |", "|---|---|---|---|---|---|---|---|---|---|---|"]
	details = []
	totals = {"found": 0, "facts": 0, "seconds": 0, "steps": 0, "calls": 0, "prompt": 0, "out": 0, "thinking": 0, "answer": 0}
	for key, expected, facts, _ in QUESTIONS:
		turn = report.get(key)
		if not turn:
			continue
		if "error" in turn:
			lines.append("| %s | - | - | - | - | - | - | - | - | - | run failed: %s |" % (key, turn["error"]))
			continue
		calls = turn["calls"]
		used = {c["tool"] for c in calls}
		misuses = [(i + 1, c["tool"], f) for i, c in enumerate(calls) for f in flags(c)]
		found, missing = quality(turn, facts)
		row = {"found": found, "facts": len(facts), "seconds": turn["seconds"], "steps": turn.get("steps", 0), "calls": len(calls), "prompt": turn.get("tokensIn") or 0,
			"out": turn.get("tokensOut") or 0, "thinking": turn.get("thinking", 0), "answer": len(turn.get("answer", ""))}
		for name in totals:
			totals[name] += row[name]
		lines.append("| %s | %d/%d | %d | %d | %d | %d | %d | %d | %d | %s | %d |" % (key, found, len(facts), row["seconds"], row["steps"], row["calls"], row["prompt"], row["out"], row["thinking"], row["answer"],
			"yes" if used & set(expected) else "no (" + " or ".join(expected) + ")", len(misuses)))
		details += ["", "## %s" % key, "", turn["question"], ""] + ["%d. `%s` %s %s" % (i + 1, c["tool"], c["args"][:160], "" if c["state"] == "ok" else "(" + c["state"] + ")") for i, c in enumerate(calls)]
		details += [""] + ["- call %d (%s): %s" % (n, tool, f) for n, tool, f in misuses]
		if missing:
			details.append("- the answer leaves out: " + ", ".join(missing))
	lines.append("| **all** | %d/%d | %d | %d | %d | %d | %d | %d | %d | | |" % (totals["found"], totals["facts"], totals["seconds"], totals["steps"], totals["calls"], totals["prompt"], totals["out"], totals["thinking"], totals["answer"]))
	turns = [t for t in report.values() if "error" not in t]
	sizes = {}
	for turn in turns:
		for tool, (count, chars) in turn.get("resultChars", {}).items():
			entry = sizes.setdefault(tool, [0, 0])
			entry[0] += count
			entry[1] += chars
	lines += ["", "Time: %ds reading prompts, %ds generating, %ds in tools and between. The first request's prompt (system message, tool definitions, question): %s tokens." % (
		sum(t.get("prefillSeconds", 0) for t in turns), sum(t.get("generationSeconds", 0) for t in turns), sum(t.get("otherSeconds", 0) for t in turns),
		"/".join(str(t.get("firstPrompt", 0)) for t in turns)), "", "| Tool | Calls | Result chars | Per call |", "|---|---|---|---|"]
	for tool, (count, chars) in sorted(sizes.items(), key=lambda item: -item[1][1]):
		lines.append("| %s | %d | %d | %d |" % (tool, count, chars, chars // max(count, 1)))
	return "\n".join(lines + details) + "\n"


def result_sizes(workspace, chat_id):
	"""The characters each tool's results put into the context, from the chat's saved turn: {tool: [calls, chars]}."""
	path = os.path.join(workspace, ".ied", "local", "chats", chat_id, "turns.jsonl")
	if not os.path.exists(path):
		return {}
	with open(path, encoding="utf-8") as f:
		turn = json.loads(f.read().splitlines()[-1])
	names = {}
	sizes = {}
	for message in turn.get("messages", []):
		for call in message.get("tool_calls") or []:
			names[call.get("id")] = call.get("function", {}).get("name", "?")
		if message.get("role") == "tool":
			entry = sizes.setdefault(names.get(message.get("tool_call_id"), "?"), [0, 0])
			entry[0] += 1
			entry[1] += len(message.get("content", ""))
	return sizes


def timing(stats, seconds):
	"""Seconds spent reading prompts (time to first token), generating, and in between (tools, the client)."""
	prefill = sum(s.get("ttftMs") or 0 for s in stats) / 1000
	generation = (stats[-1].get("genMs") or 0) / 1000 if stats else 0
	return round(prefill), round(generation), max(0, round(seconds - prefill - generation))


async def ask(server, cookie, workspace, key, question, report):
	client = await Client.connect(server.ws_url, FrameChecker(), headers={"Cookie": cookie})
	try:
		chat_id = (await client.request("chat.create", {"wsId": "w1", "mode": "ask", "permission": "plan", "title": key}))["chatId"]
		topic = "chat:" + chat_id
		await client.sub([topic])
		await client.next_frame(topic)
		started = time.time()
		await client.request("chat.send", {"chatId": chat_id, "text": question})
		frames = await client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=TURN_SECONDS)
		seconds = time.time() - started
		end = frames[-1]["data"]
		stats = [f["data"] for f in frames if f.get("kind") == "turn.stats"]
		steps = len(stats)
		turn = (await client.request("chat.history", {"chatId": chat_id}))["turns"][-1]
		calls = [{"tool": s["tool"], "args": s.get("argsText", ""), "state": s.get("state"), "result": ((s.get("result") or {}).get("preview") or "")[:400]} for s in turn["segments"] if s["type"] == "tool"]
		answer = "\n".join(s.get("text", "") for s in turn["segments"] if s["type"] == "text")
		thinking = sum(len(s.get("text", "")) for s in turn["segments"] if s["type"] == "thinking")
		prefill, generation, between = timing(stats, seconds)
		report[key] = {"question": question, "stop": end["stop"], "seconds": round(seconds), "steps": steps, "tokensIn": end.get("tokensIn"), "tokensOut": end.get("tokensOut"),
			"firstPrompt": stats[0].get("tokensIn") if stats else 0, "prefillSeconds": prefill, "generationSeconds": generation, "otherSeconds": between,
			"resultChars": result_sizes(workspace, chat_id), "thinking": thinking, "calls": calls, "answer": answer,
			"stepStats": [{"tokensIn": s.get("tokensIn"), "ttftMs": s.get("ttftMs"), "tokensOut": s.get("tokensOut"), "genMs": s.get("genMs")} for s in stats]}
		print("%s: %s in %ds, %d steps, %d calls, %s out" % (key, end["stop"], time.time() - started, steps, len(calls), end.get("tokensOut")), flush=True)
	finally:
		client.abort()


async def run(args, name, profile, out):
	os.environ[EXECUTABLE_ENV] = os.path.abspath(args.server)
	home = make_home(os.path.join(out, "home"))
	for demo in ("demo", "democode"):
		shutil.rmtree(os.path.join(home, "plugins", demo), ignore_errors=True)
	server = IedServer(data=os.path.join(out, "data"), extra=("--home", home, "--workspace", args.workspace))
	assert server.started, server.error
	try:
		cookie = server.sign_in()
		client = await Client.connect(server.ws_url, FrameChecker(), headers={"Cookie": cookie})
		await client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		await client.request("settings.set", {"tier": "app", "key": "ai.models", "value": {name: profile}})
		await client.request("settings.set", {"tier": "app", "key": "ai.groups", "value": {"eval": {"primary": {"model": name, "temperature": args.temperature}}}})
		await client.request("settings.set", {"tier": "app", "key": "ai.defaultGroup", "value": "eval"})
		client.abort()
		report = {}
		limit = asyncio.Semaphore(args.parallel)

		async def bounded(key, question):
			async with limit:
				try:
					await ask(server, cookie, args.workspace, key, question, report)
				except Exception as failed:
					report[key] = {"question": question, "error": repr(failed)}
					print("%s: failed: %r" % (key, failed), flush=True)

		chosen = [(key, question) for key, _, _, question in QUESTIONS if not args.only or key in args.only.split(",")]
		await asyncio.gather(*[bounded(key, question) for key, question in chosen])
		return report
	finally:
		server.stop(remove_data=False)


def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--server", required=True, help="the ied-server executable")
	parser.add_argument("--workspace", default=WORKSPACE, help="the IED server's sources as a Nuke workspace (default build/ai-evals/Srv)")
	parser.add_argument("--setup", action="store_true", help="make the workspace first")
	parser.add_argument("--settings", default=os.path.join(os.environ.get("APPDATA", ""), "IED", "settings.json"), help="the app settings holding ai.models")
	parser.add_argument("--model", help="the ai.models profile (default: the first)")
	parser.add_argument("--temperature", type=float, default=0.0, help="0 (the default) for runs that compare")
	parser.add_argument("--parallel", type=int, default=1, help="turns at once; more than 1 makes them share the model server, so times do not compare")
	parser.add_argument("--only", help="question keys, comma-separated: " + ", ".join(q[0] for q in QUESTIONS))
	args = parser.parse_args()
	name, profile = settings_profile(args.settings, args.model)
	if args.setup:
		setup(args.workspace)
	out = os.path.join(REPO, "build", "ai-evals", time.strftime("%Y%m%d-%H%M%S"))
	os.makedirs(out)
	report = asyncio.run(run(args, name, profile, out))
	with open(os.path.join(out, "report.json"), "w", encoding="utf-8") as f:
		json.dump(report, f, indent="\t")
	with open(os.path.join(out, "summary.md"), "w", encoding="utf-8") as f:
		f.write(summary(report, name, args.temperature))
	print("results in " + out)


if __name__ == "__main__":
	main()
