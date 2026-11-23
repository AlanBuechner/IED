"""c4-query against the fixture programs: each case runs the tool on a program of fixtures/ with a query, and what it prints
(with the program's folder taken off the file names) and its exit code must be what the case says. C4Api.dll must be beside
the tool.

	python -I check_query.py <c4-query.exe>
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "fixtures")

# main.cf of `ring`: line 6 `ring.Push(3);` (Push at column 7), line 7 `int32 total = ring.count;` (count at 21), line 5 `Game::Ring ring;` (Ring at 8)
RING_PUSH = (
	"function Game::Ring::Push\n"
	"  declaration: public void Game::Ring::Push(int32 value)\n"
	"  type: void\n"
	"  declared at: ring.ch(7,15)\n"
)
RING_COUNT = (
	"variable Game::Ring::count\n"
	"  declaration: public int32 Game::Ring::count\n"
	"  type: int32\n"
	"  declared at: ring.ch(5,16)\n"
)
RING_USES = "main.cf(7,21)\nring.ch(5,16)\nring.ch(7,35)\nring.ch(7,43)\n"
RING_MEMBERS = (
	"public variable count: int32 (ring.ch:5)\n"
	"public function static Capacity: static int32 () (ring.ch:6)\n"
	"public function Push: void (int32 value) (ring.ch:7)\n"
)

# (the program, the query words, the output, the exit code)
CASES = [
	("ring", ["diagnostics"], "", 0),
	("ring", ["resolve", "main.cf", "6", "7"], RING_PUSH, 0),
	("ring", ["resolve", "main.cf", "7", "21"], RING_COUNT, 0),
	("ring", ["resolve", "main.cf", "4", "2"], "", 1),
	("ring", ["type", "main.cf", "7", "21"], "int32\n", 0),
	("ring", ["type", "main.cf", "7", "8"], "int32\n", 0),
	("ring", ["references", "main.cf", "7", "21"], RING_USES, 0),
	("ring", ["members", "Game::Ring"], RING_MEMBERS, 0),
	("ring", ["members", "Game::Missing"], "", 1),
	("broken", ["diagnostics"], "main.cf(3,9): error: no variable named missing\n", 1),
	("ring", ["resolve", "main.cf"], "", 2),
]


def check_case(tool, program, words, expected, code):
	"""The problems of one case ([] when it passes)."""
	root = os.path.join(FIXTURES, program)
	run = subprocess.run([tool, root] + words, capture_output=True, text=True, timeout=120)
	printed = run.stdout.replace("\r\n", "\n").replace(root.replace("\\", "/") + "/", "")
	name = f"{program} {' '.join(words)}"
	problems = []
	if printed != expected:
		problems.append(f"{name}: printed\n{printed}\nexpected\n{expected}")
	if run.returncode != code:
		problems.append(f"{name}: exit code {run.returncode}, expected {code} ({run.stderr.strip()})")
	return problems


def main(argv):
	if len(argv) != 1:
		print(__doc__)
		return 2
	tool = os.path.abspath(argv[0])
	problems = [] if os.path.isfile(os.path.join(os.path.dirname(tool), "C4Api.dll")) else [f"no C4Api.dll beside {tool}"]
	for program, words, expected, code in CASES:
		problems += check_case(tool, program, words, expected, code)
	for problem in problems:
		print(problem)
	print(f"c4-query: {len(CASES)} case(s), {len(problems)} problem(s)")
	return 1 if problems else 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
