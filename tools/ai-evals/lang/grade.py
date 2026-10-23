"""Grades what the agent left in a task's workspace (tools/ai-evals/lang/README.md "Grading"): the build, the agent's
own tests, the task's hidden tests, its program checks, its benchmark (time and memory), and the code (size, function
lengths, globals, the language's idioms). Works on a copy, so it can be run again:

	vendor\\python\\python.exe -I tools/ai-evals/lang/grade.py build/ai-evals/lang/<run>/<task>-<lang>
"""

import glob
import hashlib
import io
import json
import math
import os
import re
import shutil
import stat
import statistics
import subprocess
import sys
import tarfile
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402

CONFIG = "Development-windows-x64"
SOURCE_EXT = {"c4": (".cf", ".ch"), "go": (".go",)}

# Patterns that say how a program uses its language: counted in the code the agent wrote (tests left out).
IDIOMS = {
	"c4": {
		"good": [
			(r'include\s+"bedrock\.ch"', "Bedrock included"),
			(r"Core::(Array|HashMap|HashSet|List|Queue|Stack|PriorityQueue|LruCache)\s*<", "Bedrock containers"),
			(r"Core::(String|StringView|StringBuilder)\b", "Bedrock text"),
			(r"\bfor\s*\([^;()]*:[^;()]*\)", "range-for"),
			(r"\[operator\(", "operators as [operator]"),
			(r"\bdefer\b", "defer"),
			(r"\bextends\s+\w", "extension methods"),
			(r"\bconst\s+[\w:<>, ]+&", "const references"),
		],
		"bad": [
			(r"\b(malloc|calloc|realloc|free)\s*\(", "C heap calls"),
			(r"\b(strcpy|strcat|sprintf|strtok|atoi|atof)\s*\(", "C string calls"),
			(r"\bchar(8)?\s*\[\s*\d+\s*\]", "fixed char buffers"),
			(r"\bstd::", "C++ standard library names"),
			(r"#include\b", "#include"),
			(r"\busing\s+namespace\b", "using namespace"),
		],
	},
	"go": {
		"good": [
			(r"\bif err != nil\b", "errors checked"),
			(r"\bbufio\.", "buffered I/O"),
			(r"\bstrings\.Builder\b", "strings.Builder"),
			(r"\bdefer\b", "defer"),
			(r"\bfor\s+[\w, ]+:?=\s*range\b", "range loops"),
		],
		"bad": [
			(r"\bpanic\(", "panic"),
			(r"_\s*=\s*\w+\.\w+\(", "discarded results"),
			(r"\binterface\{\}", "interface{}"),
		],
	},
}


def task_data(task):
	return os.path.join(task["dir"], "data")


def expand(word, task, scratch):
	return word.replace("{data}", task_data(task)).replace("{tmp}", scratch)


def remove_tree(folder):
	"""`folder` and everything in it, read-only files too (git's objects), once Windows has let go of it."""
	def writable(func, path, _):
		os.chmod(path, stat.S_IWRITE)
		func(path)
	for _ in range(50):   # a deleted folder lingers until nothing holds it: rmtree can return before it is gone
		if not os.path.exists(folder):
			return
		shutil.rmtree(folder, onexc=writable)
		time.sleep(0.1)


def copy_workspace(ws, out):
	remove_tree(out)
	shutil.copytree(ws, out, ignore=shutil.ignore_patterns("build", "bin", ".ied", ".git"))


def changed_files(ws):
	"""What the agent changed against the fixture commit: [(added, removed, path)]."""
	common.run(["git", "add", "-A"], ws)
	code, out, _, _ = common.run(["git", "diff", "--cached", "--numstat", "HEAD"], ws)
	rows = []
	for line in out.splitlines():
		parts = line.split("\t")
		if len(parts) == 3:
			rows.append((int(parts[0]) if parts[0] != "-" else 0, int(parts[1]) if parts[1] != "-" else 0, parts[2]))
	return rows


def fixture_files(ws):
	code, out, _, _ = common.run(["git", "ls-tree", "-r", "--name-only", "HEAD"], ws)
	return set(out.splitlines())


# --- C4 ------------------------------------------------------------------------------------------------------------

def c4_build(folder):
	code, out, err, seconds = common.nuke(["build"], folder)
	text = out + err
	return {"ok": code == 0, "seconds": round(seconds, 1), "errors": len(re.findall(r"\) (?:fatal )?error: ", text)),
		"warnings": len(re.findall(r"\) warning: ", text)), "log": text[-4000:] if code != 0 else ""}


def c4_test_results(folder, project):
	path = os.path.join(folder, "build", "int", CONFIG, project, "tests", "results.json")
	return common.read_json(path)


def c4_tests(folder, project, filter_glob=None):
	results = os.path.join(folder, "build", "int", CONFIG, project, "tests", "results.json")
	if os.path.exists(results):
		os.remove(results)
	code, out, err, seconds = common.nuke(["test"] + (["--filter", filter_glob] if filter_glob else []), folder)
	data = common.read_json(results)
	if data is None:
		said = out + err
		ran = re.findall(r"^\s*ok (\S+)", said, re.M)
		if "[TEST] " in said:
			return {"compiled": True, "crashed": True, "total": len(ran) + 1, "passed": len(ran), "log": said[-3000:]}
		compiled = not any(words in said for words in ("no test project", "0 test project", "no project has tests"))
		return {"compiled": False if compiled else None, "total": 0, "passed": 0, "log": said[-3000:]}
	return {"compiled": True, "total": data.get("total", 0), "passed": data.get("passed", 0),
		"failedCases": [c["name"] for c in data.get("cases", []) if not c.get("passed")][:20]}


def hidden_test_count(folder, lang):
	"""How many hidden tests the task has, so a run a crash cut short still counts the ones it never reached."""
	pattern = r"\[test\(\s*\"hidden" if lang == "c4" else r"^func TestHidden"
	count = 0
	for root, _, files in os.walk(folder):
		for name in files:
			with open(os.path.join(root, name), encoding="utf-8") as f:
				count += len(re.findall(pattern, f.read(), re.M))
	return count


def with_hidden_total(run, total):
	"""A hidden run over all the task's hidden tests: those it did not run (a crash ended it) count as failed."""
	if run.get("compiled") and total > run["total"]:
		run["notRun"] = total - run["total"]
		run["total"] = total
	return run


def c4_program(folder, project):
	return os.path.join(folder, "build", "bin", CONFIG, project, project + ".exe")


def remove_agent_tests(folder, fixture, lang):
	"""Takes out the test files the agent added (for a hidden run its own tests would break)."""
	removed = []
	for path in common.source_files(folder, SOURCE_EXT[lang]):
		normalized = path.replace("\\", "/")
		is_test = ("/tests/" in "/" + normalized or normalized.startswith("tests/")) if lang == "c4" else normalized.endswith("_test.go")
		if is_test and normalized not in fixture and "hidden" not in os.path.basename(normalized):
			os.remove(os.path.join(folder, path))
			removed.append(normalized)
	return removed


def grade_c4(task, spec, ws, out, fixture):
	project = spec["project"]
	copy_workspace(ws, out)
	result = {"build": c4_build(out)}
	result["ownTests"] = c4_tests(out, project) if result["build"]["ok"] else {"compiled": False, "total": 0, "passed": 0}
	hidden = os.path.join(task["dir"], "c4", "hidden")
	if os.path.isdir(hidden):
		common.overlay(hidden, out)
		run = c4_tests(out, project, "hidden*")
		if not run["compiled"]:
			removed = remove_agent_tests(out, fixture, "c4")
			if removed:
				run = c4_tests(out, project, "hidden*")
				run["agentTestsRemoved"] = removed
		result["hiddenTests"] = with_hidden_total(run, hidden_test_count(hidden, "c4"))
	result["program"] = c4_program(out, project) if result["build"]["ok"] else None
	return result


# --- Go ------------------------------------------------------------------------------------------------------------

def go_tests(folder, pattern=None, exclude=None):
	argv = ["go", "test", "-json", "-count=1"] + (["-run", pattern] if pattern else []) + ["./..."]
	code, out, err, _ = common.run(argv, folder, timeout=900)
	passed, failed, build_failed = set(), set(), False
	for line in out.splitlines():
		try:
			event = json.loads(line)
		except ValueError:
			continue
		name = event.get("Test")
		if event.get("Action") == "build-fail" or (event.get("Action") == "fail" and not name and "build failed" in event.get("Output", "")):
			build_failed = True
		if not name or "/" in name or (exclude and re.match(exclude, name)):
			continue
		if event.get("Action") == "pass":
			passed.add(name)
		elif event.get("Action") == "fail":
			failed.add(name)
	if "[build failed]" in out or "[setup failed]" in out:
		build_failed = True
	result = {"compiled": not build_failed, "total": len(passed) + len(failed), "passed": len(passed), "failedCases": sorted(failed)[:20]}
	if build_failed:
		result["log"] = (out + err)[-3000:]
	return result


def grade_go(task, spec, ws, out, fixture):
	project = spec["project"]
	copy_workspace(ws, out)
	exe = os.path.join(out, "bin", project + ".exe")
	code, o, e, seconds = common.run(["go", "build", "-o", exe, spec.get("main", ".")], out, timeout=600)
	build = {"ok": code == 0, "seconds": round(seconds, 1), "errors": len(re.findall(r"\.go:\d+:\d+: ", o + e)) if code != 0 else 0, "log": (o + e)[-4000:] if code != 0 else ""}
	if code == 0 or spec.get("library"):
		code, o, e, _ = common.run(["go", "vet", "./..."], out, timeout=600)
		build["warnings"] = len(re.findall(r"\.go:\d+:\d+: ", o + e))
		code, o, e, _ = common.run(["gofmt", "-l", "."], out)
		build["unformatted"] = len([line for line in o.splitlines() if line.strip()])
		if spec.get("library"):
			code, o, e, _ = common.run(["go", "build", "./..."], out, timeout=600)
			build["ok"] = code == 0
			build["log"] = (o + e)[-4000:] if code != 0 else ""
	result = {"build": build}
	result["ownTests"] = go_tests(out, exclude=r"^TestHidden") if build["ok"] else {"compiled": False, "total": 0, "passed": 0}
	hidden = os.path.join(task["dir"], "go", "hidden")
	if os.path.isdir(hidden):
		common.overlay(hidden, out)
		run = go_tests(out, pattern="^TestHidden")
		if not run["compiled"]:
			removed = remove_agent_tests(out, fixture, "go")
			if removed:
				run = go_tests(out, pattern="^TestHidden")
				run["agentTestsRemoved"] = removed
		result["hiddenTests"] = with_hidden_total(run, hidden_test_count(hidden, "go"))
	result["program"] = exe if os.path.exists(exe) and not spec.get("library") else None
	return result


# --- Program checks and the benchmark ------------------------------------------------------------------------------

def normalize(text):
	return "\n".join(line.rstrip() for line in text.replace("\r\n", "\n").strip("\n").split("\n"))


def run_checks(task, program, scratch):
	results = []
	for check in task.get("checks", []):
		row = {"name": check["name"], "ok": False}
		if not program:
			row["why"] = "no program"
			results.append(row)
			continue
		stdin = os.path.join(task_data(task), check["stdin"]) if check.get("stdin") else None
		ran = common.measure([program] + [expand(w, task, scratch) for w in check.get("args", [])], scratch, timeout=check.get("timeout", 60), stdin_path=stdin)
		row["seconds"] = round(ran["seconds"], 4)
		row["peakMB"] = round((ran["peakWorkingSet"] or 0) / 1048576, 1)
		problems = []
		if ran["exit"] is None:
			problems.append("timed out")
		elif ran["exit"] != check.get("exit", 0):
			problems.append("exit %s, wanted %s" % (ran["exit"], check.get("exit", 0)))
		wanted = check.get("stdout")
		if check.get("stdout_file"):
			with open(os.path.join(task["dir"], check["stdout_file"]), encoding="utf-8") as f:
				wanted = f.read()
		if wanted is not None and normalize(ran["stdout"]) != normalize(wanted):
			problems.append("stdout differs: got %r" % normalize(ran["stdout"])[:300])
		if check.get("json_file"):
			problems += json_differences(os.path.join(task["dir"], check["json_file"]), os.path.join(scratch, check["json_output"]) if check.get("json_output") else None, ran["stdout"])
		for needle in check.get("stdout_contains", []):
			if needle not in ran["stdout"]:
				problems.append("stdout lacks %r" % needle)
		if check.get("stderr_contains") and check["stderr_contains"] not in ran["stderr"]:
			problems.append("stderr lacks %r (got %r)" % (check["stderr_contains"], ran["stderr"][:200]))
		row["ok"] = not problems
		if problems:
			row["why"] = "; ".join(problems)
		results.append(row)
	return results


def json_differences(expected_path, output_path, stdout):
	"""The program's JSON (a file it wrote, else its stdout) against the expected, by value (floats to 1e-6)."""
	with open(expected_path, encoding="utf-8") as f:
		expected = json.load(f)
	try:
		if output_path:
			with open(output_path, encoding="utf-8") as f:
				got = json.load(f)
		else:
			got = json.loads(stdout)
	except (OSError, ValueError) as problem:
		return ["no JSON output: %s" % problem]
	differences = []

	def walk(a, b, at):
		if len(differences) >= 5:
			return
		if isinstance(a, dict) and isinstance(b, dict):
			for key in sorted(set(a) | set(b)):
				if key not in b:
					differences.append("%s/%s missing" % (at, key))
				elif key not in a:
					differences.append("%s/%s unexpected" % (at, key))
				else:
					walk(a[key], b[key], "%s/%s" % (at, key))
		elif isinstance(a, list) and isinstance(b, list):
			if len(a) != len(b):
				differences.append("%s: %d elements, wanted %d" % (at, len(b), len(a)))
			for i, (x, y) in enumerate(zip(a, b)):
				walk(x, y, "%s/%d" % (at, i))
		elif isinstance(a, (int, float)) and isinstance(b, (int, float)) and not isinstance(a, bool) and not isinstance(b, bool):
			if abs(a - b) > 1e-6 * max(1.0, abs(a)):
				differences.append("%s: %r, wanted %r" % (at, b, a))
		elif a != b:
			differences.append("%s: %r, wanted %r" % (at, b, a))

	walk(expected, got, "")
	return differences


def answer_facts(task, answer):
	"""For a question task: the facts (names, as code writes them) a right answer states, found in the answer."""
	facts = task.get("answer_facts", [])
	found = [fact for fact in facts if re.search(fact, answer)]
	return {"found": len(found), "total": len(facts), "missing": [f for f in facts if f not in found]}


def run_mutants(task, lang, out, spec):
	"""Mutation testing of the agent's own tests: each mutant changes one place of the code under test; the tests kill
	it when they fail (or stop compiling) with it. Only meaningful when the tests pass on the original."""
	results = []
	for mutant in task.get("mutants", []):
		relative = mutant[lang + "_file"] if lang + "_file" in mutant else mutant["file"]
		path = os.path.join(out, relative.replace("{lang_ext}", SOURCE_EXT[lang][0]))
		with open(path, encoding="utf-8") as f:
			original = f.read()
		find = mutant[lang + "_find"] if lang + "_find" in mutant else mutant["find"]
		replace = mutant[lang + "_replace"] if lang + "_replace" in mutant else mutant["replace"]
		if original.count(find) != 1:
			results.append({"name": mutant["name"], "killed": None, "why": "the mutant's text is found %d times" % original.count(find)})
			continue
		with open(path, "w", encoding="utf-8", newline="") as f:
			f.write(original.replace(find, replace))
		try:
			run = c4_tests(out, spec["project"]) if lang == "c4" else go_tests(out, exclude=r"^TestHidden")
		finally:
			with open(path, "w", encoding="utf-8", newline="") as f:
				f.write(original)
		killed = run["compiled"] is False or run["passed"] < run["total"]
		results.append({"name": mutant["name"], "killed": killed})
	return results


def start_code(ws, lang, task):
	"""The code metrics of the starting project (the fixture commit), which a refactor's progress is measured from."""
	archive = subprocess.run(["git", "archive", "HEAD"], cwd=ws, capture_output=True)
	with tempfile.TemporaryDirectory() as start:
		with tarfile.open(fileobj=io.BytesIO(archive.stdout)) as tar:
			tar.extractall(start, filter="data")
		return code_metrics(start, lang, task)


def covered(start, now, target):
	"""How much of the way from `start` down to `target` `now` has gone, in [0, 1]."""
	if now <= target:
		return 1.0
	return max(0.0, min(1.0, (start - now) / (start - target))) if start > target else 0.0


def requirements(task, result, start):
	"""The task's own demands on the code (a refactor's, an optimization's): each met or not, and its progress in
	[0, 1] - how far from the starting project (`start`, its code metrics) toward the demand the change went, so a
	refactor half done scores half."""
	wanted = task.get("requirements", {})
	code, own = result["code"], result.get("ownTests", {})
	found = []
	if "globals_max" in wanted:
		progress = covered(start["globals"], code["globals"], wanted["globals_max"])
		found.append(("at most %d globals" % wanted["globals_max"], code["globals"] <= wanted["globals_max"], progress))
	if "function_lines_max" in wanted:
		longest = code["longestFunction"][1] if code["longestFunction"] else 0
		was = start["longestFunction"][1] if start["longestFunction"] else 0
		found.append(("functions of at most %d lines" % wanted["function_lines_max"], longest <= wanted["function_lines_max"], covered(was, longest, wanted["function_lines_max"])))
	if "own_tests_min" in wanted:
		passed, total = own.get("passed", 0), own.get("total", 0)
		share = min(1.0, passed / wanted["own_tests_min"]) * (1.0 if passed == total else 0.5)
		found.append(("at least %d passing tests of its own" % wanted["own_tests_min"], passed >= wanted["own_tests_min"] and passed == total, share))
	if "speedup_min" in wanted:
		bench = result.get("bench") or {}
		speedup = bench.get("speedup")
		correct = bool(speedup) and bench.get("correct") is not False
		progress = max(0.0, min(1.0, math.log(speedup) / math.log(wanted["speedup_min"]))) if correct and speedup > 1 else 0.0
		found.append(("%gx faster than the start" % wanted["speedup_min"], correct and speedup >= wanted["speedup_min"], progress))
	return [{"name": name, "met": ok, "progress": round(progress, 3)} for name, ok, progress in found]


def compare_bench(task, lang, bench):
	"""The benchmark against the self-test's: speedup over the untouched start, time and memory over the reference."""
	if not bench or not bench.get("seconds"):
		return
	known = common.read_json(os.path.join(common.HERE, "references.json"), {}).get(task["id"], {}).get(lang, {})
	if known.get("startSeconds"):
		bench["speedup"] = round(known["startSeconds"] / bench["seconds"], 2)
	if known.get("benchSeconds"):
		bench["vsReference"] = round(bench["seconds"] / known["benchSeconds"], 2)
	if known.get("benchMB"):
		bench["memoryVsReference"] = round(bench["peakPrivateMB"] / known["benchMB"], 2)


def bench_input(task):
	"""The benchmark's input folder, made once by the task's generator: {input files, expected}."""
	bench = task["bench"]
	folder = os.path.join(common.WORK, "bench", task["id"])
	stamp = os.path.join(folder, "done")
	if not os.path.exists(stamp):
		shutil.rmtree(folder, ignore_errors=True)
		os.makedirs(folder)
		code, out, err, _ = common.run([sys.executable, "-I", os.path.join(task["dir"], bench["generate"]), folder], folder, timeout=1800)
		if code != 0:
			raise RuntimeError("bench generator failed: " + out + err)
		open(stamp, "w").close()
	return folder


def run_bench(task, program, scratch):
	bench = task.get("bench")
	if not bench or not program:
		return None
	folder = bench_input(task)
	argv = [program] + [w.replace("{bench}", folder) for w in bench.get("args", [])]
	runs = []
	for _ in range(bench.get("runs", 3)):
		ran = common.measure(argv, scratch, timeout=bench.get("timeout", 300), stdin_path=os.path.join(folder, bench["stdin"]) if bench.get("stdin") else None)
		runs.append(ran)
		if ran["exit"] != 0:
			break
	expected_path = os.path.join(folder, "expected")
	correct = None
	if os.path.exists(expected_path):
		with open(expected_path, encoding="utf-8") as f:
			correct = all(normalize(r["stdout"]) == normalize(f.read()) for r in runs[:1])
	ok = all(r["exit"] == 0 for r in runs)
	return {"ok": ok, "correct": correct, "seconds": round(statistics.median(r["seconds"] for r in runs), 3) if ok else None,
		"cpu": round(statistics.median(r["cpu"] for r in runs), 3) if ok else None,
		"peakWorkingSetMB": round(max(r["peakWorkingSet"] for r in runs) / 1048576, 1), "peakPrivateMB": round(max(r["peakPrivate"] for r in runs) / 1048576, 1),
		"stderr": runs[-1]["stderr"][-500:] if not ok else ""}


# --- Reading the code ----------------------------------------------------------------------------------------------

FUNCTION_START = {
	"c4": re.compile(r"^[ \t]*(?:\[[^\]]*\]\s*)*(?:(?:public|private|protected|static|virtual|override|inline|const|template<[^>]*>)\s+)*[\w:<>,\*&\[\] ]+?[ \t\*&]([~\w:]+)\s*\([^;{}]*\)\s*(?:const\s*)?(?:extends\s+[\w:<>]+\s*)?(?:noexcept\s*)?(?:->\s*[\w:<>\*& ]+\s*)?\{", re.M),
	"go": re.compile(r"^func\s+(?:\([^)]*\)\s*)?(\w+)", re.M),
}
CONTROL_WORDS = {"if", "for", "while", "switch", "catch", "return", "else", "do", "sizeof", "foreach"}


def function_lengths(text, lang):
	"""(name, lines) of each function body."""
	clean = common.strip_c_comments(text)
	clean = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', clean)
	clean = re.sub(r"'(?:\\.|[^'\\\n])'", "' '", clean)
	found = []
	for match in FUNCTION_START[lang].finditer(clean):
		name = match.group(1)
		if name in CONTROL_WORDS:
			continue
		start = clean.find("{", match.start())
		if start < 0:
			continue
		depth, i = 0, start
		while i < len(clean):
			if clean[i] == "{":
				depth += 1
			elif clean[i] == "}":
				depth -= 1
				if depth == 0:
					break
			i += 1
		found.append((name, clean.count("\n", start, i) + 1))
	return found


def globals_count(text, lang):
	"""File-scope mutable variables (outside any brace for C4 except namespaces; `var` at package level for Go)."""
	clean = common.strip_c_comments(text)
	if lang == "go":
		return len(re.findall(r"^var\s", clean, re.M)) + sum(len(re.findall(r"^\t\w+\s", block)) for block in re.findall(r"^var\s*\((.*?)^\)", clean, re.M | re.S))
	count, depth, namespace_depths = 0, 0, set()
	for line in clean.split("\n"):
		stripped = line.strip()
		if depth - len(namespace_depths) == 0 and re.match(r"^(static\s+)?(?!const\b|return\b|namespace\b|include\b|class\b|struct\b|enum\b|interface\b|template\b|typedef\b|using\b|alias\b)[\w:<>,\*& ]+\s+\w+\s*(=[^;]*)?;$", stripped) and "(" not in stripped.split("=")[0]:
			count += 1
		if re.match(r"^namespace\b", stripped):
			namespace_depths.add(depth + 1)
		for ch in stripped:
			if ch == "{":
				depth += 1
			elif ch == "}":
				namespace_depths.discard(depth)
				depth -= 1
	return count


def code_metrics(folder, lang, task):
	files = [f for f in common.source_files(folder, SOURCE_EXT[lang]) if not is_test_file(f, lang)]
	lines, comments, functions, globals_found = 0, 0, [], 0
	idiom_counts = {"good": {}, "bad": {}}
	extra = task.get("idioms", {}).get(lang, {})
	patterns = {kind: IDIOMS[lang][kind] + [tuple(p) for p in extra.get(kind, [])] for kind in ("good", "bad")}
	for path in files:
		with open(os.path.join(folder, path), encoding="utf-8", errors="replace") as f:
			text = f.read()
		lines += len([l for l in text.split("\n") if l.strip()])
		comments += len(re.findall(r"^\s*//", text, re.M))
		functions += function_lengths(text, lang)
		globals_found += globals_count(text, lang)
		clean = common.strip_c_comments(text)
		for kind in ("good", "bad"):
			for pattern, label in patterns[kind]:
				hits = len(re.findall(pattern, clean))
				if hits:
					idiom_counts[kind][label] = idiom_counts[kind].get(label, 0) + hits
	lengths = [n for _, n in functions]
	return {"files": len(files), "lines": lines, "commentLines": comments, "functions": len(functions),
		"longestFunction": max(functions, key=lambda f: f[1]) if functions else None, "meanFunctionLines": round(statistics.mean(lengths), 1) if lengths else 0,
		"functionsOver40": len([n for n in lengths if n > 40]), "globals": globals_found, "idioms": idiom_counts,
		"taskIdiomsFound": [label for _, label in extra.get("good", []) if label in idiom_counts["good"]],
		"taskIdiomsMissing": [label for _, label in extra.get("good", []) if label not in idiom_counts["good"]],
		"taskAntiIdioms": [label for _, label in extra.get("bad", []) if label in idiom_counts["bad"]]}


def is_test_file(path, lang):
	normalized = path.replace("\\", "/")
	return normalized.endswith("_test.go") if lang == "go" else ("/tests/" in "/" + normalized)


# --- The grade -----------------------------------------------------------------------------------------------------

def score(result, task):
	"""Quality in [0, 1]: hidden tests 0.45, program checks 0.35, a clean build 0.1, own passing tests 0.1, and where
	the task has them its mutants killed 0.45 and its requirements' progress 0.25 (weights are normalized; a task with
	`requirements.gate` counts its checks and build only as far as its requirements went). A question's is the share
	of its facts the answer states."""
	if task.get("answer_facts"):
		facts = result["answer"]
		return round(facts["found"] / max(facts["total"], 1), 3)
	parts = []
	mutants = [m for m in result.get("mutants") or [] if m["killed"] is not None]
	if mutants:
		own = result.get("ownTests", {})
		valid = own.get("total") and own["passed"] == own["total"]
		parts.append((0.45, sum(1 for m in mutants if m["killed"]) / len(mutants) if valid else 0.0))
	reqs = result.get("requirements") or []
	progress = sum(r.get("progress", 1.0 if r["met"] else 0.0) for r in reqs) / len(reqs) if reqs else 1.0
	if reqs:
		parts.append((task.get("requirements", {}).get("weight", 0.25), progress))
	# A refactor's behavior and build count as far as the refactor went: an untouched program keeps both and earns nothing.
	gate = progress if task.get("requirements", {}).get("gate") else 1.0
	hidden = result.get("hiddenTests")
	if hidden is not None:
		parts.append((0.45, hidden["passed"] / hidden["total"] if hidden.get("total") else 0.0))
	checks = result.get("checks") or []
	if checks:
		parts.append((0.35, gate * sum(1 for c in checks if c["ok"]) / len(checks)))
	if result.get("bench") is not None and task.get("bench", {}).get("must_be_correct", True) and result["bench"].get("correct") is False:
		parts.append((0.1, 0.0))
	build = result["build"]
	parts.append((0.1, gate * (1.0 if build["ok"] and not build.get("warnings") else 0.5 if build["ok"] else 0.0)))
	own = result.get("ownTests", {})
	parts.append((0.1, 1.0 if own.get("total") and own["passed"] == own["total"] else 0.5 if own.get("total") else 0.0))
	weight = sum(w for w, _ in parts)
	return round(sum(w * v for w, v in parts) / weight, 3)


def grade(task, lang, ws, out, toolchain=None, answer=""):
	spec = task[lang]
	fixture = fixture_files(ws)
	result = {"lang": lang, "changed": changed_files(ws)}
	result.update(grade_c4(task, spec, ws, out, fixture) if lang == "c4" else grade_go(task, spec, ws, out, fixture))
	scratch = os.path.join(out, "_scratch")
	os.makedirs(scratch, exist_ok=True)
	result["checks"] = run_checks(task, result["program"], scratch)
	result["bench"] = run_bench(task, result["program"], scratch)
	compare_bench(task, lang, result["bench"])
	result["code"] = code_metrics(ws, lang, task)
	if task.get("mutants") and result["build"]["ok"]:
		copy_workspace(ws, out)
		result["mutants"] = run_mutants(task, lang, out, spec)
	if task.get("requirements"):
		result["requirements"] = requirements(task, result, start_code(ws, lang, task))
	if task.get("answer_facts"):
		result["answer"] = answer_facts(task, answer)
	result["score"] = score(result, task)
	return result


def main():
	job_dir = os.path.abspath(sys.argv[1])
	agent = common.read_json(os.path.join(job_dir, "agent.json"))
	task = common.load_task(agent["task"])
	result = grade(task, agent["lang"], agent["workspace"], os.path.join(os.path.dirname(agent["workspace"]), "grade"), answer=agent.get("answer", ""))
	common.write_json(os.path.join(job_dir, "grade.json"), result)
	print(json.dumps({k: result[k] for k in ("score", "build", "hiddenTests", "checks", "bench") if k in result}, indent=1))


if __name__ == "__main__":
	main()
