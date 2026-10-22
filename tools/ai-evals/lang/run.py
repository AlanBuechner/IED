"""How well a real model works in a language through IED's agent (tools/ai-evals/lang/README.md): each task of
tasks/ is given, in each language it has, to an Agent-mode chat of a server started for it on a fresh copy of the
task's starting project; the turn's steps, tokens, time and tool calls are recorded, then grade.py builds the result,
runs the task's hidden tests and program checks, times its benchmark and reads the code.

	vendor\\python\\python.exe -I tools/ai-evals/lang/run.py --server build/bin/Development-windows-x64/ied-server/ied-server.exe --set tune
	vendor\\python\\python.exe -I tools/ai-evals/lang/run.py --server <exe> --tasks wordfreq,bugs --langs c4 --variant skills2

A variant (variants/<name>/) changes what the model is given without touching the tree: its home/ files go over a
copy of IED's plugins (prompts), its toolchain/ files over a copy of the dev C4 toolchain (skills, the C4 plugin's
system-message section, docs). Results go to build/ai-evals/lang/<run>/ (run.json, <task>-<lang>/agent.json,
grade.json, transcript.jsonl, summary.md); workspaces to %TEMP%/ied-langeval/runs/<run>/.
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
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import api_digest  # noqa: E402
import common  # noqa: E402
import grade  # noqa: E402
import report  # noqa: E402
import slots  # noqa: E402
from proxy import ModelProxy  # noqa: E402

sys.path.insert(0, os.path.join(common.REPO, "tools", "server-checks"))
sys.path.insert(0, os.path.join(common.REPO, "tools", "conformance"))

from server import IedServer  # noqa: E402
from frames import FrameChecker  # noqa: E402
from iedclient import Client  # noqa: E402

AUTO_ANSWER = "I am not available to answer. Make the reasonable choice yourself, say which you chose, and continue."
HOME_PARTS = ("plugins", "frontend", "shared")
GIT_IDENTITY = ["-c", "user.name=eval", "-c", "user.email=eval@localhost", "-c", "commit.gpgsign=false"]


# --- The variant's toolchain and home ------------------------------------------------------------------------------

def variant_folder(name):
	return name if os.path.isabs(name) else os.path.join(common.VARIANTS, name)


def prepare_toolchain(variant, base=common.DEV_TOOLCHAIN, name=None):
	"""A copy of a toolchain (the dev one, or a frozen copy so runs days apart compare) with the variant's toolchain/
	files over it, in a folder of its own (`name`, by default the variant's): a run names its own, so two runs of one
	variant never rewrite the files the other's servers read."""
	target = os.path.join(common.WORK, "toolchains", name or os.path.basename(variant))
	common.mirror(base, target, ignore=lambda rel: rel.endswith(".pdb") and not rel.startswith("tools"))
	common.overlay(os.path.join(variant, "toolchain"), target)
	described = common.read_json(os.path.join(variant, "variant.json"), {})
	for digest in described.get("apiDigests", []):
		api_digest.write_digest(os.path.join(target, digest["library"]), os.path.join(target, digest["out"]), digest["title"])
	for source, destination in described.get("toolchainCopies", []):
		common.overlay(os.path.join(target, source), os.path.join(target, destination))
	return target


def prepare_home(variant, name=None):
	"""A copy of IED's home (plugins, frontend, shared) with the variant's home/ files over it, named as the toolchain."""
	target = os.path.join(common.WORK, "homes", name or os.path.basename(variant))
	for part in HOME_PARTS:
		common.mirror(os.path.join(common.REPO, part), os.path.join(target, part), ignore=lambda rel: os.path.basename(rel) in ("target", "__pycache__", "node_modules"))
	common.overlay(os.path.join(variant, "home"), target)
	return target


# --- A task's workspace --------------------------------------------------------------------------------------------

def toolchain_line(toolchain):
	return 'toolchain = "%s"' % toolchain.replace("\\", "/")


def write_starter(starter, ws, project):
	"""The variant's starter files (its starter/ folder) over what nuke init wrote, {name} the project's name."""
	for folder, _, files in os.walk(starter):
		for name in files:
			source = os.path.join(folder, name)
			target = os.path.join(ws, os.path.relpath(source, starter))
			os.makedirs(os.path.dirname(target), exist_ok=True)
			with open(source, encoding="utf-8") as f:
				text = f.read().replace("{name}", project)
			with open(target, "w", encoding="utf-8", newline="\n") as f:
				f.write(text)


def prepare_c4(task, spec, ws, toolchain, starter):
	fixture = os.path.join(task["dir"], "c4", "fixture")
	if os.path.isdir(fixture):
		shutil.copytree(fixture, ws)
		manifest = os.path.join(ws, "nuke.toml")
		with open(manifest, encoding="utf-8") as f:
			text = f.read()
		with open(manifest, "w", encoding="utf-8", newline="\n") as f:
			f.write(re.sub(r'^toolchain\s*=\s*".*"$', toolchain_line(toolchain), text, flags=re.M))
	else:
		os.makedirs(ws)
		code, out, err, _ = common.nuke(["init", spec["project"], "--toolchain", toolchain.replace("\\", "/")], ws)
		if code != 0:
			raise RuntimeError("nuke init failed: %s%s" % (out, err))
		if starter:
			write_starter(starter, ws, spec["project"])
	code, out, err, _ = common.nuke(["init", "--ied"], ws)
	if code != 0:
		raise RuntimeError("nuke init --ied failed: %s%s" % (out, err))
	solutions = [name for name in os.listdir(ws) if name.endswith(".iedsln")]
	return os.path.join(ws, solutions[0])


def prepare_go(task, spec, ws):
	fixture = os.path.join(task["dir"], "go", "fixture")
	if os.path.isdir(fixture):
		shutil.copytree(fixture, ws)
	else:
		os.makedirs(ws)
	if not os.path.exists(os.path.join(ws, "go.mod")):
		code, out, err, _ = common.run(["go", "mod", "init", spec.get("module", spec["project"])], ws)
		if code != 0:
			raise RuntimeError("go mod init failed: %s%s" % (out, err))
	return ws


def commit_fixture(ws):
	"""The starting state as a commit, so the grader can see what the agent changed."""
	ignore = os.path.join(ws, ".gitignore")
	lines = open(ignore, encoding="utf-8").read().splitlines() if os.path.exists(ignore) else []
	for entry in ("build/", ".ied/local/", "bin/"):
		if entry not in lines:
			lines.append(entry)
	with open(ignore, "w", encoding="utf-8", newline="\n") as f:
		f.write("\n".join(lines) + "\n")
	for words in (["init", "-q"], ["add", "-A"], ["commit", "-q", "-m", "fixture"]):
		code, out, err, _ = common.run(["git"] + GIT_IDENTITY + words, ws)
		if code != 0:
			raise RuntimeError("git %s failed: %s%s" % (words[0], out, err))


def copy_fixture_files(task, ws):
	"""Files of the task's own folder the starting projects share (`fixture_files`: [[from, to]]), stored once."""
	for source, destination in task.get("fixture_files", []):
		target = os.path.join(ws, destination)
		os.makedirs(os.path.dirname(target), exist_ok=True)
		shutil.copyfile(os.path.join(task["dir"], source), target)


def prepare_workspace(task, lang, ws, toolchain, starter=None):
	spec = task[lang]
	opened = prepare_c4(task, spec, ws, toolchain, starter) if lang == "c4" else prepare_go(task, spec, ws)
	copy_fixture_files(task, ws)
	commit_fixture(ws)
	return opened


# --- One agent turn ------------------------------------------------------------------------------------------------

def put_tools_on_path():
	"""Nuke (the repository's bin/) and Go's tools (gopls) where the servers, their build tasks and shell_run find them,
	as on a machine set up for both."""
	extra = [os.path.join(common.REPO, "bin"), os.path.join(os.path.expanduser("~"), "go", "bin")]
	os.environ["PATH"] = os.pathsep.join(extra + [os.environ.get("PATH", "")])


def turn_transcript(roots, chat_id):
	"""The finished turn as saved (turns.jsonl: every message the model was sent), looked for under `roots`."""
	for root in roots:
		for folder, _, files in os.walk(root):
			if "turns.jsonl" in files and os.path.basename(folder) == chat_id:
				with open(os.path.join(folder, "turns.jsonl"), encoding="utf-8") as f:
					return [json.loads(line) for line in f if line.strip()]
	return []


async def agent_turn(server, cookie, task, prompt, job_dir, roots):
	"""Sends the task and follows the turn to its end: the turn's record for agent.json."""
	client = await Client.connect(server.ws_url, FrameChecker(), headers={"Cookie": cookie})
	limit = task.get("timeout_minutes", 45) * 60
	try:
		chat_id = (await client.request("chat.create", {"wsId": "w1", "mode": task.get("mode", "agent"), "permission": "auto", "title": task["id"]}))["chatId"]
		topic = "chat:" + chat_id
		await client.sub([topic])
		await client.next_frame(topic)
		started = time.time()
		await client.request("chat.send", {"chatId": chat_id, "text": prompt}, timeout=30)
		stats, approvals, questions, end, timed_out = [], 0, [], None, False
		while end is None:
			remaining = limit - (time.time() - started)
			if remaining <= 0:
				timed_out = True
				await client.request("chat.stop", {"chatId": chat_id}, timeout=30)
				end = (await client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=120))[-1]["data"]
				break
			try:
				frame = await client.next_frame(topic, timeout=min(remaining, 60))
			except asyncio.TimeoutError:
				continue
			kind, data = frame.get("kind"), frame.get("data") or {}
			if kind == "turn.stats":
				stats.append({k: data.get(k) for k in ("tokensIn", "tokensOut", "ttftMs", "genMs")} | {"atMs": round((time.time() - started) * 1000)})
			elif kind == "approval.request":
				approvals += 1
				await client.request("chat.approve", {"chatId": chat_id, "approvalId": data["approvalId"], "decision": "allow"}, timeout=30)
			elif kind == "question.request":
				questions.append(data.get("question", ""))
				await client.request("chat.answer", {"chatId": chat_id, "questionId": data["questionId"], "answer": AUTO_ANSWER}, timeout=30)
			elif kind == "turn.end":
				end = data
		seconds = time.time() - started
		history = (await client.request("chat.history", {"chatId": chat_id}, timeout=60))["turns"][-1]
	finally:
		client.abort()
	segments = history["segments"]
	calls = [{"tool": s["tool"], "args": s.get("argsText", ""), "state": s.get("state"), "result": ((s.get("result") or {}).get("preview") or "")[:600]} for s in segments if s["type"] == "tool"]
	transcript = turn_transcript(roots, chat_id)
	with open(os.path.join(job_dir, "transcript.jsonl"), "w", encoding="utf-8", newline="\n") as f:
		for turn in transcript:
			f.write(json.dumps(turn) + "\n")
	return {
		"stop": end.get("stop"), "timedOut": timed_out, "recoveries": end.get("recoveries", []), "seconds": round(seconds, 1),
		"steps": len(stats), "stepStats": stats, "tokensOut": end.get("tokensOut"), "lastPrompt": end.get("tokensIn"),
		"approvals": approvals, "questions": questions, "calls": calls,
		"thinkingChars": sum(len(s.get("text", "")) for s in segments if s["type"] == "thinking"),
		"answer": "\n".join(s.get("text", "") for s in segments if s["type"] == "text"),
		"segments": [{k: v for k, v in s.items() if k in ("type", "text", "tool", "state", "note", "kind")} for s in segments if s["type"] not in ("tool", "thinking")],
	}


async def run_job(job, settings, config):
	"""One task in one language: workspace, server, turn, grade."""
	task, lang, job_dir, ws = job["task"], job["lang"], job["dir"], job["ws"]
	os.makedirs(job_dir, exist_ok=True)
	record = {"task": task["id"], "lang": lang, "repeat": job["repeat"], "workspace": ws, "category": task.get("category"), "set": task.get("set")}
	try:
		opened = prepare_workspace(task, lang, ws, config["toolchain"], config.get("starter"))
		async with slots.Slot(config["slots"], os.path.basename(job_dir)):
			await run_turn(task, lang, job_dir, ws, opened, settings, config, record)
	except Exception as failed:
		record["error"] = "".join(traceback.format_exception_only(failed)).strip()
		record["trace"] = traceback.format_exc()
	common.write_json(os.path.join(job_dir, "agent.json"), record)
	if "error" not in record:
		try:
			common.write_json(os.path.join(job_dir, "grade.json"), grade.grade(task, lang, ws, os.path.join(os.path.dirname(ws), "grade"), config["toolchain"], record.get("answer", "")))
		except Exception:
			common.write_json(os.path.join(job_dir, "grade.json"), {"error": traceback.format_exc()})
	print("%s-%s r%d: %s in %ss, %s steps, %d calls" % (task["id"], lang, job["repeat"], record.get("stop", record.get("error", "?")), record.get("seconds", "-"), record.get("steps", "-"), len(record.get("calls", []))), flush=True)


async def run_turn(task, lang, job_dir, ws, opened, settings, config, record):
	"""The task's turn in a server started for it on the workspace; fills `record`."""
	data = os.path.join(os.path.dirname(ws), "data")
	server = IedServer(data=data, extra=("--home", config["home"], "--workspace", opened), executable=config["server"], static=False)
	if not server.started:
		raise RuntimeError("the server did not start: " + server.error)
	try:
		cookie = server.sign_in()
		client = await Client.connect(server.ws_url, FrameChecker(), headers={"Cookie": cookie})
		await client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		for key, value in settings(task, os.path.basename(job_dir), job_dir).items():
			await client.request("settings.set", {"tier": "app", "key": key, "value": value})
		client.abort()
		prompt = task[lang]["prompt"].strip()
		record.update(await agent_turn(server, cookie, task, prompt, job_dir, [ws, data]))
	finally:
		server.stop(remove_data=False)


def model_settings(args, name, profile, proxy):
	def settings(task, job_key, job_dir):
		model = dict(profile)
		model["endpoint"] = proxy.endpoint_for(job_key, job_dir)
		if task.get("context_window"):
			model["contextWindow"] = task["context_window"]
		role = {"model": name, "temperature": args.temperature}
		if args.effort:
			role["effort"] = args.effort
		return {"ai.models": {name: model}, "ai.groups": {"eval": {"primary": role}}, "ai.defaultGroup": "eval", "ai.maxSteps": task.get("max_steps", args.max_steps)}
	return settings


async def run_all(jobs, settings, config, parallel):
	limit = asyncio.Semaphore(parallel)

	async def bounded(job):
		async with limit:
			await run_job(job, settings, config)

	await asyncio.gather(*[bounded(job) for job in jobs])


def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--server", required=True, help="the ied-server executable")
	parser.add_argument("--tasks", help="task ids, comma-separated (default: all, or --set)")
	parser.add_argument("--set", dest="task_set", choices=["tune", "holdout"], help="only the tasks of this set")
	parser.add_argument("--langs", default="c4", help="languages, comma-separated: c4, go")
	parser.add_argument("--variant", default="baseline", help="variants/<name> (or a folder): files over IED's home and the toolchain")
	parser.add_argument("--label", default="", help="a note for the run's record")
	parser.add_argument("--base-toolchain", default=common.DEV_TOOLCHAIN, help="the toolchain the variant's files go over (default: dev, which other work refreshes; a frozen copy keeps runs comparable)")
	parser.add_argument("--settings", default=os.path.join(os.environ.get("APPDATA", ""), "IED", "settings.json"), help="the app settings holding ai.models")
	parser.add_argument("--model", help="the ai.models profile (default: the first)")
	parser.add_argument("--temperature", type=float, default=0.6, help="0.6 (the default) as Qwen3 asks for its thinking mode; greedy decoding (0) falls into endless repetition on long replies")
	parser.add_argument("--effort", choices=["low", "medium", "high"], default="medium")
	parser.add_argument("--max-steps", type=int, default=150)
	parser.add_argument("--parallel", type=int, default=1, help="tasks at once (they share the model server)")
	parser.add_argument("--repeat", type=int, default=1)
	parser.add_argument("--slots", type=int, default=4, help="model server slots every run on this machine shares (lock files in %%TEMP%%/ied-langeval/slots): a job waits for one")
	args = parser.parse_args()
	name, profile = common.settings_profile(args.settings, args.model)
	put_tools_on_path()
	tasks = common.load_tasks(args.tasks, args.task_set)
	variant = variant_folder(args.variant)
	run_id = time.strftime("%Y%m%d-%H%M%S") + "-" + os.path.basename(variant)
	out = os.path.join(common.RESULTS, run_id)
	os.makedirs(out)
	print("preparing the %s toolchain and home" % os.path.basename(variant), flush=True)
	starter = os.path.join(variant, "starter")
	config = {"server": os.path.abspath(args.server), "toolchain": prepare_toolchain(variant, args.base_toolchain, run_id), "home": prepare_home(variant, run_id), "slots": args.slots,
		"starter": starter if os.path.isdir(starter) else None}
	jobs = []
	for repeat in range(1, args.repeat + 1):
		for task in tasks:
			for lang in common.languages_of(task, args.langs.split(",")):
				key = "%s-%s" % (task["id"], lang) + ("-r%d" % repeat if args.repeat > 1 else "")
				jobs.append({"task": task, "lang": lang, "repeat": repeat, "dir": os.path.join(out, key), "ws": os.path.join(common.WORK, "runs", run_id, key, "ws")})
	common.write_json(os.path.join(out, "run.json"), {"id": run_id, "variant": os.path.basename(variant), "label": args.label, "model": name, "temperature": args.temperature,
		"effort": args.effort, "parallel": args.parallel, "server": config["server"], "baseToolchain": args.base_toolchain,
		"toolchainBuilt": common.read_json(os.path.join(args.base_toolchain, "toolchain.json"), {}).get("built"), "started": time.strftime("%Y-%m-%d %H:%M:%S"),
		"jobs": [os.path.basename(j["dir"]) for j in jobs], "commit": subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=common.REPO, capture_output=True, text=True).stdout.strip()})
	proxy = ModelProxy(profile["endpoint"])
	try:
		asyncio.run(run_all(jobs, model_settings(args, name, profile, proxy), config, args.parallel))
	finally:
		proxy.close()
	report.write_summary(out)
	print("results in " + out)


if __name__ == "__main__":
	main()
