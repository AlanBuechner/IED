"""The spans benchmark: 20,000 commands (adds and removes over a wide range, membership queries, counts) in
<out>/input.txt, and the reference's answers in <out>/expected."""

import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import spans_ref  # noqa: E402

COMMANDS = 20000
LIMIT = 1000000


def main():
	out = sys.argv[1]
	rng = random.Random(77)
	lines = []
	for i in range(COMMANDS):
		roll = rng.random()
		start = rng.randrange(LIMIT)
		if roll < 0.55:
			lines.append("add %d %d" % (start, start + rng.randrange(1, 20000)))
		elif roll < 0.75:
			lines.append("remove %d %d" % (start, start + rng.randrange(1, 60000)))
		elif roll < 0.999:
			lines.append("contains %d" % start)
		else:
			lines.append(rng.choice(["count", "covered"]))
	lines += ["count", "covered"]
	with open(os.path.join(out, "input.txt"), "w", encoding="utf-8", newline="\n") as f:
		f.write("\n".join(lines) + "\n")
	answers = []
	spans_ref.run(lines, answers)
	with open(os.path.join(out, "expected"), "w", encoding="utf-8", newline="\n") as f:
		f.write("".join(a + "\n" for a in answers))


if __name__ == "__main__":
	main()
