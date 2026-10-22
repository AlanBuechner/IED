"""Checks the tasks themselves: each language's reference solution (tasks/<id>/<lang>/reference/, the files a good
answer leaves over the starting project) must grade near 1, and the untouched starting project must not. Also gives
the reference's benchmark time and memory, which the summary compares the agent's against.

	vendor\\python\\python.exe -I tools/ai-evals/lang/selftest.py [--tasks a,b] [--langs c4,go]
"""

import argparse
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402
import grade  # noqa: E402
import run as runner  # noqa: E402


def prepare(task, lang, ws, toolchain, with_reference):
	runner.prepare_workspace(task, lang, ws, toolchain)
	if with_reference:
		common.overlay(os.path.join(task["dir"], lang, "reference"), ws)


def check(task, lang, toolchain, root):
	results = {}
	for name, with_reference in (("reference", True), ("fixture", False)):
		folder = os.path.join(root, "%s-%s-%s" % (task["id"], lang, name))
		shutil.rmtree(folder, ignore_errors=True)
		ws = os.path.join(folder, "ws")
		prepare(task, lang, ws, toolchain, with_reference)
		results[name] = grade.grade(task, lang, ws, os.path.join(folder, "grade"))
	return results


def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--tasks")
	parser.add_argument("--langs", default="c4,go")
	parser.add_argument("--variant", default="baseline")
	args = parser.parse_args()
	toolchain = runner.prepare_toolchain(runner.variant_folder(args.variant))
	root = os.path.join(common.WORK, "selftest", time.strftime("%Y%m%d-%H%M%S"))
	references = common.read_json(os.path.join(common.HERE, "references.json"), {})
	bad = 0
	for task in common.load_tasks(args.tasks):
		for lang in common.languages_of(task, args.langs.split(",")):
			if not os.path.isdir(os.path.join(task["dir"], lang, "reference")):
				print("%s-%s: no reference" % (task["id"], lang))
				continue
			results = check(task, lang, toolchain, root)
			ref, fix = results["reference"], results["fixture"]
			problems = []
			if ref["score"] < 0.95:
				problems.append("reference scores %.2f" % ref["score"])
			if fix["score"] > 0.6:
				problems.append("the untouched start scores %.2f" % fix["score"])
			if ref.get("bench") and ref["bench"].get("correct") is False:
				problems.append("reference bench output is wrong")
			bad += bool(problems)
			bench = ref.get("bench") or {}
			print("%s-%s: reference %.2f (hidden %s, checks %s, build %s), start %.2f; bench %ss %sMB%s" % (task["id"], lang, ref["score"],
				"%d/%d" % (ref["hiddenTests"]["passed"], ref["hiddenTests"]["total"]) if ref.get("hiddenTests") else "-",
				"%d/%d" % (sum(1 for c in ref["checks"] if c["ok"]), len(ref["checks"])), ref["build"]["ok"], fix["score"],
				bench.get("seconds"), bench.get("peakPrivateMB"), (" PROBLEMS: " + "; ".join(problems)) if problems else ""), flush=True)
			for c in ref["checks"]:
				if not c["ok"]:
					print("   check %s: %s" % (c["name"], c.get("why")))
			if ref.get("hiddenTests", {}).get("failedCases") or ref.get("hiddenTests", {}).get("log"):
				print("   hidden: %s %s" % (ref["hiddenTests"].get("failedCases"), ref["hiddenTests"].get("log", "")[-1500:]))
			if not ref["build"]["ok"]:
				print("   build: " + ref["build"]["log"][-1500:])
			if bench.get("seconds") is not None:
				references.setdefault(task["id"], {})[lang] = {"benchSeconds": bench["seconds"], "benchMB": bench.get("peakPrivateMB")}
				start = (fix.get("bench") or {}).get("seconds")
				if start:
					references[task["id"]][lang]["startSeconds"] = start
	common.write_json(os.path.join(common.HERE, "references.json"), references)
	raise SystemExit(1 if bad else 0)


if __name__ == "__main__":
	main()
