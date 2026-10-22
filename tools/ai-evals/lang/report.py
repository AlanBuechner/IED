"""The language eval's tables (tools/ai-evals/lang/README.md "Metrics"): a run's summary.md, and runs side by side.

	vendor\\python\\python.exe -I tools/ai-evals/lang/report.py <run>                 (re)writes the run's summary.md
	vendor\\python\\python.exe -I tools/ai-evals/lang/report.py --compare <run> <run> ...  per task and language, each run's numbers
"""

import argparse
import json
import os
import re
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402

NAV = {"code_symbols", "code_read_symbol", "code_definition", "code_references", "code_members", "code_call_hierarchy", "code_hover", "code_implementations", "code_type_definition", "code_type_hierarchy", "code_expand_macro", "code_custom"}
READ = {"fs_read", "fs_list", "fs_glob", "search_text"}
EDIT = {"fs_edit", "fs_write"}
BUILD_TOOLS = {"build_run", "problems_list"}
SKILLS = {"skill_search", "skill_read"}
BUILD_COMMAND = re.compile(r"\b(nuke(\.bat|\.cmd)?\s+(build|test|run|rebuild)|nuke\s*$|go\s+(build|test|run|vet))\b")

# Rates of the owner's model server with one request at a time (prompt tokens and generated tokens a second), for
# the time a run would have taken alone when it shared the server.
SOLO_PREFILL_RATE = 900.0
SOLO_GENERATE_RATE = 30.0


def category(call):
	tool = call["tool"]
	if tool in NAV:
		return "nav"
	if tool in READ:
		return "docs" if is_docs_read(call) else "read"
	if tool in EDIT:
		return "edit"
	if tool in BUILD_TOOLS or (tool == "shell_run" and BUILD_COMMAND.search(command_of(call))):
		return "build"
	if tool in SKILLS:
		return "skill"
	if tool == "shell_run":
		return "shell"
	return "other"


def args_of(call):
	try:
		return json.loads(call.get("args") or "{}")
	except ValueError:
		return {}


def command_of(call):
	return str(args_of(call).get("command", ""))


def is_docs_read(call):
	path = str(args_of(call).get("path", "")).replace("\\", "/").lower()
	return "/toolchains/" in path or "/docs/" in path and "toolchain" in path


def failed(call):
	return call.get("state") in ("error", "denied")


def build_failed(call):
	"""A build or test call that reported failure."""
	result = call.get("result", "")
	if failed(call):
		return True
	return bool(re.search(r"\b(failed|error:|errors?\b(?! 0)|exit code [1-9]|exited with [1-9]|FAIL\b)", result, re.I)) and not re.search(r"\b0 errors?\b|succeeded|passed\b(?!.*failed)", result, re.I)


COMPILE_ERROR = re.compile(r"(?:^|\s)([\w./\\-]+\.(?:cf|ch|go))(?::(\d+):(\d+):| \((\d+):(\d+)\)) (?:fatal )?error: ([^\r\n]+)")


def tool_results(job_dir):
	"""Every tool call of the turn with its whole result, from the saved messages: [{tool, args, result}]."""
	path = os.path.join(job_dir, "transcript.jsonl")
	if not os.path.exists(path):
		return []
	with open(path, encoding="utf-8") as f:
		turns = [json.loads(line) for line in f if line.strip()]
	calls, by_id = [], {}
	for turn in turns:
		for message in turn.get("messages", []):
			for call in message.get("tool_calls") or []:
				entry = {"tool": call.get("function", {}).get("name", "?"), "args": call.get("function", {}).get("arguments", ""), "result": ""}
				by_id[call.get("id")] = entry
				calls.append(entry)
			if message.get("role") == "tool" and message.get("tool_call_id") in by_id:
				content = message.get("content", "")
				by_id[message["tool_call_id"]]["result"] = content if isinstance(content, str) else json.dumps(content)
	return calls


def compile_errors(calls):
	"""The compiler errors the agent's builds reported: [(file, line, message)], each once per call."""
	found = []
	for call in calls:
		if category(call) != "build":
			continue
		seen = set()
		for match in COMPILE_ERROR.finditer(call.get("result", "")):
			key = (os.path.basename(match.group(1)), match.group(2) or match.group(4), match.group(6).strip())
			if key not in seen:
				seen.add(key)
				found.append(key)
	return found


def error_kind(message):
	"""A compiler message with its names taken out, so the same mistake counts once: "no matching overload of X"."""
	return re.sub(r"'[^']*'|\"[^\"]*\"|\b[A-Z]\w*::\w+(::\w+)*|\b(of|named|for) \w+", lambda m: (m.group(1) + " X") if m.group(1) else "X", message)[:120]


def prefill_tokens(stats):
	"""About how many prompt tokens the server had to read: the first prompt, then each prompt's growth."""
	total, last = 0, 0
	for step in stats:
		prompt = step.get("tokensIn") or 0
		total += prompt if prompt < last or not last else prompt - last
		last = prompt
	return total


def agent_metrics(agent, full_calls):
	calls = agent.get("calls", [])
	stats = agent.get("stepStats", [])
	categories = {}
	for call in calls:
		key = category(call)
		categories[key] = categories.get(key, 0) + 1
	builds = [c for c in calls if category(c) == "build"]
	build_failures = [c for c in builds if build_failed(c)]
	repeats = 0
	seen = set()
	for call in calls:
		key = (call["tool"], call.get("args"))
		if key in seen and call["tool"] not in ("build_run", "shell_run", "problems_list"):
			repeats += 1
		seen.add(key)
	prefill_seconds = sum((s.get("ttftMs") or 0) for s in stats) / 1000
	generate_seconds = (stats[-1].get("genMs") or 0) / 1000 if stats else 0
	tools_seconds = max(0.0, agent.get("seconds", 0) - prefill_seconds - generate_seconds)
	prefill = prefill_tokens(stats)
	out = agent.get("tokensOut") or 0
	return {"seconds": agent.get("seconds", 0), "steps": agent.get("steps", 0), "calls": len(calls), "failedCalls": len([c for c in calls if failed(c)]),
		"repeatedCalls": repeats, "builds": len(builds), "failedBuilds": len(build_failures), "categories": categories,
		"promptTokens": sum((s.get("tokensIn") or 0) for s in stats), "prefillTokens": prefill, "peakPrompt": max([(s.get("tokensIn") or 0) for s in stats] or [0]),
		"tokensOut": out, "thinkingChars": agent.get("thinkingChars", 0),
		"prefillSeconds": round(prefill_seconds), "generateSeconds": round(generate_seconds), "toolSeconds": round(tools_seconds),
		"soloSeconds": round(prefill / SOLO_PREFILL_RATE + out / SOLO_GENERATE_RATE + tools_seconds),
		"finished": agent.get("stop") == "finished", "stop": agent.get("stop"), "compressed": any("compress" in r for r in agent.get("recoveries", [])),
		"recoveries": len(agent.get("recoveries", [])), "skillReads": [args_of(c).get("name") for c in calls if c["tool"] == "skill_read"],
		"compileErrors": compile_errors(full_calls)}


def model_metrics(job_dir):
	"""What the model server says the job cost (the recording proxy's model.jsonl): tokens read, cached and generated,
	and the milliseconds it spent on each."""
	path = os.path.join(job_dir, "model.jsonl")
	if not os.path.exists(path):
		return None
	with open(path, encoding="utf-8") as f:
		entries = [json.loads(line) for line in f if line.strip()]
	completions = [e for e in entries if e.get("path", "").endswith("/chat/completions")]
	timings = [e.get("timings") or {} for e in completions]
	return {"requests": len(completions), "failedRequests": len([e for e in completions if e.get("error") or (e.get("status") or 0) >= 400]),
		"promptRead": sum(t.get("prompt_n") or 0 for t in timings), "promptCached": sum(t.get("cache_n") or 0 for t in timings),
		"generated": sum(t.get("predicted_n") or 0 for t in timings), "promptMs": round(sum(t.get("prompt_ms") or 0 for t in timings)),
		"generateMs": round(sum(t.get("predicted_ms") or 0 for t in timings)), "firstSystemChars": completions[0].get("systemChars") if completions else None,
		"firstTools": completions[0].get("tools") if completions else None}


def job_row(job_dir):
	agent = common.read_json(os.path.join(job_dir, "agent.json"), {})
	grade = common.read_json(os.path.join(job_dir, "grade.json"), {})
	row = {"job": os.path.basename(job_dir), "task": agent.get("task"), "lang": agent.get("lang"), "set": agent.get("set"), "category": agent.get("category")}
	if "error" in agent:
		row["error"] = agent["error"]
		return row
	row.update(agent_metrics(agent, tool_results(job_dir)))
	server = model_metrics(job_dir)
	if server and server["requests"]:
		row["server"] = server
		if server["promptRead"] or server["generated"]:
			row["prefillTokens"] = server["promptRead"]
			row["tokensOut"] = server["generated"] or row["tokensOut"]
			row["soloSeconds"] = round(server["promptRead"] / SOLO_PREFILL_RATE + row["tokensOut"] / SOLO_GENERATE_RATE + row["toolSeconds"])
	if "error" in grade:
		row["gradeError"] = grade["error"][-300:]
		return row
	row["score"] = grade.get("score")
	hidden = grade.get("hiddenTests")
	row["hidden"] = "%d/%d" % (hidden["passed"], hidden["total"]) if hidden and hidden.get("compiled") else ("no compile" if hidden else "-")
	checks = grade.get("checks") or []
	row["checksText"] = "%d/%d" % (sum(1 for c in checks if c["ok"]), len(checks)) if checks else "-"
	own = grade.get("ownTests", {})
	row["ownTests"] = "%d/%d" % (own.get("passed", 0), own.get("total", 0))
	row["buildOk"] = grade.get("build", {}).get("ok")
	row["warnings"] = grade.get("build", {}).get("warnings", 0)
	bench = grade.get("bench")
	if bench:
		row["benchSeconds"] = bench.get("seconds")
		row["benchCorrect"] = bench.get("correct")
		row["benchMB"] = bench.get("peakPrivateMB")
	code = grade.get("code", {})
	row["code"] = {k: code.get(k) for k in ("lines", "functions", "meanFunctionLines", "functionsOver40", "globals", "longestFunction")}
	row["idiomsGood"] = sum(code.get("idioms", {}).get("good", {}).values())
	row["idiomsBad"] = sum(code.get("idioms", {}).get("bad", {}).values())
	row["taskIdiomsMissing"] = code.get("taskIdiomsMissing", [])
	row["changedLines"] = sum(a + r for a, r, _ in grade.get("changed", []))
	row["falseSuccess"] = row["finished"] and (row["score"] or 0) < 0.8
	return row


def rows_of(run_dir):
	return [job_row(os.path.join(run_dir, name)) for name in sorted(os.listdir(run_dir)) if os.path.isfile(os.path.join(run_dir, name, "agent.json"))]


def fmt(value, digits=0):
	if value is None:
		return "-"
	if isinstance(value, bool):
		return "yes" if value else "no"
	if isinstance(value, float):
		return ("%." + str(digits) + "f") % value
	return str(value)


def totals(rows):
	good = [r for r in rows if "error" not in r and "gradeError" not in r]
	if not good:
		return {}
	sums = {k: sum(r.get(k) or 0 for r in good) for k in ("seconds", "soloSeconds", "steps", "calls", "failedCalls", "repeatedCalls", "builds", "failedBuilds", "promptTokens", "prefillTokens", "tokensOut")}
	sums["jobs"] = len(rows)
	sums["errors"] = len(rows) - len(good)
	sums["meanScore"] = round(statistics.mean(r.get("score") or 0 for r in good), 3)
	sums["finished"] = len([r for r in good if r["finished"]])
	sums["falseSuccess"] = len([r for r in good if r.get("falseSuccess")])
	sums["callEfficiency"] = round(1 - (sums["failedCalls"] + sums["repeatedCalls"]) / max(sums["calls"], 1), 3)
	return sums


def summary(run_dir):
	run = common.read_json(os.path.join(run_dir, "run.json"), {})
	rows = rows_of(run_dir)
	lines = ["# Language eval: %s" % run.get("id", os.path.basename(run_dir)), "",
		"Variant `%s`%s; model `%s` at temperature %s, effort %s, %s at once; commit %s; started %s." % (run.get("variant"), (" (" + run["label"] + ")") if run.get("label") else "",
			run.get("model"), run.get("temperature"), run.get("effort"), run.get("parallel"), run.get("commit"), run.get("started")), "",
		"Score: hidden tests 0.45, program checks 0.35, clean build 0.1, own passing tests 0.1. Solo s: the time alone on the model server (prompt read at %d tokens/s, output at %d/s, plus tool time). Prefill: prompt tokens the server read (cache reuse left out); Prompt: all prompt tokens sent." % (SOLO_PREFILL_RATE, SOLO_GENERATE_RATE), "",
		"| Job | Score | Hidden | Checks | Own tests | Stop | Wall s | Solo s | Steps | Calls | Failed | Repeated | Builds (failed) | Prefill k | Prompt k | Out k | Peak ctx k | Bench s | Bench MB | Lines | Fn>40 | Globals | Idioms +/- |",
		"|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
	for r in rows:
		if "error" in r:
			lines.append("| %s | run failed: %s |" % (r["job"], r["error"][:200].replace("|", "/")))
			continue
		if "gradeError" in r:
			lines.append("| %s | grade failed: %s |" % (r["job"], r["gradeError"][-200:].replace("|", "/").replace("\n", " ")))
			continue
		code = r.get("code", {})
		lines.append("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s (%s) | %.1f | %.0f | %.1f | %.1f | %s | %s | %s | %s | %s | %s/%s |" % (
			r["job"], fmt(r["score"], 2), r["hidden"], r["checksText"], r["ownTests"], r["stop"] + (" (false success)" if r.get("falseSuccess") else ""), fmt(r["seconds"]), r["soloSeconds"], r["steps"], r["calls"], r["failedCalls"], r["repeatedCalls"],
			r["builds"], r["failedBuilds"], r["prefillTokens"] / 1000, r["promptTokens"] / 1000, r["tokensOut"] / 1000, r["peakPrompt"] / 1000,
			fmt(r.get("benchSeconds"), 3) + ("" if r.get("benchCorrect") in (None, True) else " (wrong)"), fmt(r.get("benchMB"), 1), fmt(code.get("lines")), fmt(code.get("functionsOver40")), fmt(code.get("globals")), r["idiomsGood"], r["idiomsBad"]))
	for lang in common.LANGUAGES:
		t = totals([r for r in rows if r.get("lang") == lang])
		if t:
			lines += ["", "**%s:** %d jobs (%d failed to run), mean score %.3f, %d finished (%d false successes), %ds wall, %ds solo, %d steps, %d calls (%d failed, %d repeated; efficiency %.3f), %d builds (%d failed), prefill %.0fk, prompt %.0fk, out %.1fk tokens." % (
				lang, t["jobs"], t["errors"], t["meanScore"], t["finished"], t["falseSuccess"], t["seconds"], t["soloSeconds"], t["steps"], t["calls"], t["failedCalls"], t["repeatedCalls"], t["callEfficiency"], t["builds"], t["failedBuilds"],
				t["prefillTokens"] / 1000, t["promptTokens"] / 1000, t["tokensOut"] / 1000)]
	kinds = {}
	for r in rows:
		for _, _, message in r.get("compileErrors", []):
			key = (r.get("lang"), error_kind(message))
			kinds[key] = kinds.get(key, 0) + 1
	if kinds:
		lines += ["", "## Compiler errors the agent met", "", "| Lang | Count | Error (names taken out) |", "|---|---|---|"]
		lines += ["| %s | %d | %s |" % (lang, count, kind.replace("|", "/")) for (lang, kind), count in sorted(kinds.items(), key=lambda item: -item[1])[:40]]
	lines += ["", "## Tool use", "", "| Job | nav | read | docs | skill | edit | build | shell | other | skills read |", "|---|---|---|---|---|---|---|---|---|---|"]
	for r in rows:
		if "categories" in r:
			c = r["categories"]
			lines.append("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (r["job"], c.get("nav", 0), c.get("read", 0), c.get("docs", 0), c.get("skill", 0), c.get("edit", 0), c.get("build", 0), c.get("shell", 0), c.get("other", 0), ", ".join(str(s) for s in r["skillReads"])))
	return "\n".join(lines) + "\n"


def write_summary(run_dir):
	with open(os.path.join(run_dir, "summary.md"), "w", encoding="utf-8", newline="\n") as f:
		f.write(summary(run_dir))
	common.write_json(os.path.join(run_dir, "rows.json"), rows_of(run_dir))


def compare(run_dirs):
	"""Per task and language, each run's score, solo time, steps, calls, out tokens and bench time."""
	runs = [(os.path.basename(d), {(r.get("task"), r.get("lang")): r for r in rows_of(d)}) for d in run_dirs]
	keys = sorted({k for _, rows in runs for k in rows})
	header = "| Task | Lang | " + " | ".join(name for name, _ in runs) + " |"
	lines = ["# Runs compared", "", "Each cell: score / solo seconds / steps / calls / output k tokens / bench s.", "", header, "|---|---|" + "---|" * len(runs)]
	for key in keys:
		cells = []
		for _, rows in runs:
			r = rows.get(key)
			if not r:
				cells.append("")
			elif "error" in r or "gradeError" in r:
				cells.append("failed")
			else:
				cells.append("%s / %s / %s / %s / %.1f / %s" % (fmt(r.get("score"), 2), r["soloSeconds"], r["steps"], r["calls"], r["tokensOut"] / 1000, fmt(r.get("benchSeconds"), 3)))
		lines.append("| %s | %s | %s |" % (key[0], key[1], " | ".join(cells)))
	for lang in common.LANGUAGES:
		cells = []
		for _, rows in runs:
			t = totals([r for r in rows.values() if r.get("lang") == lang])
			cells.append("%.3f / %s / %s / %s / %.1f" % (t["meanScore"], t["soloSeconds"], t["steps"], t["calls"], t["tokensOut"] / 1000) if t else "")
		if any(cells):
			lines.append("| **all** | %s | %s |" % (lang, " | ".join(cells)))
	return "\n".join(lines) + "\n"


def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("runs", nargs="+")
	parser.add_argument("--compare", action="store_true")
	args = parser.parse_args()
	dirs = [r if os.path.isabs(r) else (r if os.path.isdir(r) else os.path.join(common.RESULTS, r)) for r in args.runs]
	if args.compare:
		sys.stdout.write(compare(dirs))
	else:
		for d in dirs:
			write_summary(d)
			sys.stdout.write(summary(d))


if __name__ == "__main__":
	main()
