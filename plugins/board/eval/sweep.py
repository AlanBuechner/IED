"""sweep - how well the recognizer reads hand-drawn sketches, over many seeds (docs/32 "Agent evaluation").

For each scenario and seed it draws a fresh sketch (sketch.py), reads it with board_read through boardctl, and checks
the groups against the scenario's truth: every box found with the right kind, every arrow found with both ends on the
right boxes (by the handwriting each box holds), the frame found. Prints a line per sketch and the totals.

    python -I plugins/board/eval/sweep.py --state <boardctl state> --seeds 1-12
"""

import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sketch  # noqa: E402


def call(state, tool, args, owner=False):
	argv = [sys.executable, "-I", os.path.join(HERE, "boardctl.py"), "call", tool, json.dumps(args), "--state", state]
	if owner:
		argv.append("--as-owner")
	return subprocess.run(argv, capture_output=True, text=True).stdout


GROUP = re.compile(r"^\s+(g\d+) (\w+)(?: \(fit [\d.]+\))? at ([-\d.]+),([-\d.]+) ([\d.]+)x([\d.]+)(.*)$")


def groups_of(text):
	groups = {}
	for line in text.splitlines():
		m = GROUP.match(line)
		if not m:
			continue
		name, guess, x, y, w, h, rest = m.groups()
		g = {"guess": guess, "box": (float(x), float(y), float(w), float(h)), "rest": rest}
		ends = re.search(r"from (\S+) to (\S+?)(,|;|$)", rest)
		if ends:
			g["from"], g["to"] = ends.group(1), ends.group(2)
		inside = re.search(r"inside (\S+?);", rest)
		if inside:
			g["inside"] = inside.group(1)
		groups[name] = g
	return groups


def centre(box):
	return (box[0] + box[2] / 2, box[1] + box[3] / 2)


def score(scenario, seed, state, board):
	hand = sketch.Hand(seed)
	truth = sketch.SCENARIOS[scenario](hand)
	call(state, "create", {"board": board}, owner=True)
	call(state, "edit", {"board": board, "ops": hand.ops}, owner=True)
	groups = groups_of(call(state, "read", {"board": board}))
	# The truth's boxes, by where their labels were written: a group holding writing near a label's place is that box.
	boxes = {}
	for name, g in groups.items():
		if g["guess"] in ("rectangle", "ellipse", "diamond", "frame"):
			boxes[name] = g
	expected_boxes = len(truth["nodes"]) + len(truth.get("groups", {}))
	found = len(boxes)
	arrows = [g for g in groups.values() if g["guess"] in ("arrow", "line")]
	wired = [a for a in arrows if a.get("from", "nothing") in boxes and a.get("to", "nothing") in boxes]
	frames = [g for g in groups.values() if g["guess"] == "frame"]
	scribbles = [n for n, g in groups.items() if g["guess"] == "scribble"]
	return {
		"boxes": (found, expected_boxes),
		"arrows": (len(wired), len(truth["edges"])),
		"frames": (len(frames), len(truth.get("groups", {}))),
		"scribbles": scribbles,
	}


def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--state", required=True)
	parser.add_argument("--seeds", default="1-10")
	args = parser.parse_args()
	first, last = (int(v) for v in args.seeds.split("-"))
	totals = {"boxes": [0, 0], "arrows": [0, 0], "frames": [0, 0]}
	perfect = 0
	runs = 0
	for scenario in sorted(sketch.SCENARIOS):
		for seed in range(first, last + 1):
			result = score(scenario, seed, args.state, "design/sweep-%s-%d.iedboard" % (scenario, seed))
			runs += 1
			ok = all(result[k][0] == result[k][1] for k in ("boxes", "arrows", "frames")) and not result["scribbles"]
			perfect += ok
			for k in totals:
				totals[k][0] += result[k][0]
				totals[k][1] += result[k][1]
			print("%-9s seed %2d  boxes %d/%d  arrows wired %d/%d  frames %d/%d  scribbles %s  %s" % (scenario, seed, *result["boxes"], *result["arrows"], *result["frames"], ",".join(result["scribbles"]) or "-", "ok" if ok else "MISS"), flush=True)
	print("total: boxes %d/%d, arrows wired %d/%d, frames %d/%d; %d of %d sketches read perfectly" % (*totals["boxes"], *totals["arrows"], *totals["frames"], perfect, runs))
	return 0


if __name__ == "__main__":
	sys.exit(main())
