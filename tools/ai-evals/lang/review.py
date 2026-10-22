"""Blind code review of what the agent wrote (tools/ai-evals/lang/README.md "Review"): packs each job's change - the
task's prompt and the diff against the starting project - under a random id, so a reviewer scores the code without
knowing the run, the variant or the model; then merges the reviewers' scores back.

	vendor\\python\\python.exe -I tools/ai-evals/lang/review.py pack <run> [<run> ...]    writes build/ai-evals/lang/reviews/<batch>/
	vendor\\python\\python.exe -I tools/ai-evals/lang/review.py merge <batch>              adds review.json to each job folder

A packet is <id>.md; the reviewer writes <id>.json: {"correctness": 1-5, "idiomatic": 1-5, "design": 1-5,
"tests": 1-5, "notes": "..."} by the rubric in RUBRIC.
"""

import json
import os
import secrets
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402

MOST_DIFF_BYTES = 60000

RUBRIC = """Score the change, as a senior engineer in its language would review it, 1 (bad) to 5 (excellent):
- correctness: does the code do what the task asks, including edge cases and errors? (Do not run it; read it.)
- idiomatic: does it use the language and its standard library the way experienced users do, instead of
  reinventing what the library has or writing another language's habits in this one?
- design: are functions small and single-purpose, names clear, no globals or duplication, the structure fit for
  the codebase it changes?
- tests: do its own tests cover the behavior and edge cases it adds (1 when there are none)?
Then one or two sentences of notes naming the most important strengths and problems. C4 is a C++-like language
with its own standard library, Bedrock (Core::Array, Core::HashMap, Core::String, Core::StringView,
Core::StringBuilder, Core::Format, FS::File, Platform::WriteOutput); `[operator("create")]` methods are its
constructors, `[test("name")]` functions its unit tests. Judge the code, not the language."""


def diff_of(ws):
	"""What the agent changed against the fixture commit, new files included."""
	common.run(["git", "add", "-A"], ws)
	# Data and built programs left out: `go build` writes its program beside the sources, which says nothing of the code.
	code, out, err, _ = common.run(["git", "diff", "--cached", "HEAD", "--", ".", ":(exclude)*.log", ":(exclude)*.csv", ":(exclude)*.txt",
		":(exclude)*.exe", ":(exclude)*.pdb", ":(exclude)*.dll", ":(exclude)*.test"], ws)
	return out


def pack(runs):
	batch = os.path.join(common.RESULTS, "reviews", time.strftime("%Y%m%d-%H%M%S"))
	os.makedirs(batch)
	key = {}
	for run in runs:
		run_dir = run if os.path.isabs(run) else os.path.join(common.RESULTS, run)
		for job in sorted(os.listdir(run_dir)):
			agent = common.read_json(os.path.join(run_dir, job, "agent.json"))
			if not agent or "error" in agent or not os.path.isdir(agent["workspace"]):
				continue
			task = common.load_task(agent["task"])
			diff = diff_of(agent["workspace"])
			if not diff.strip():
				continue
			packet = secrets.token_hex(4)
			key[packet] = {"run": os.path.basename(run_dir), "job": job}
			with open(os.path.join(batch, packet + ".md"), "w", encoding="utf-8", newline="\n") as f:
				f.write("# Packet %s (%s)\n\n## The task\n\n%s\n\n## The change\n\n```diff\n%s\n```\n" % (packet, "C4" if agent["lang"] == "c4" else "Go",
					task[agent["lang"]]["prompt"].strip(), diff[:MOST_DIFF_BYTES] + ("\n[... the diff goes on]" if len(diff) > MOST_DIFF_BYTES else "")))
	common.write_json(os.path.join(batch, "key.json"), key)
	with open(os.path.join(batch, "RUBRIC.md"), "w", encoding="utf-8", newline="\n") as f:
		f.write(RUBRIC + "\n")
	print("%d packets in %s" % (len(key), batch))


def merge(batch):
	folder = batch if os.path.isabs(batch) else os.path.join(common.RESULTS, "reviews", batch)
	key = common.read_json(os.path.join(folder, "key.json"), {})
	merged = 0
	for packet, where in key.items():
		review = common.read_json(os.path.join(folder, packet + ".json"))
		if review is None:
			continue
		common.write_json(os.path.join(common.RESULTS, where["run"], where["job"], "review.json"), dict(review, packet=packet))
		merged += 1
	print("merged %d of %d reviews" % (merged, len(key)))


if __name__ == "__main__":
	if len(sys.argv) < 3 or sys.argv[1] not in ("pack", "merge"):
		raise SystemExit(__doc__)
	(pack if sys.argv[1] == "pack" else lambda runs: merge(runs[0]))(sys.argv[2:])
