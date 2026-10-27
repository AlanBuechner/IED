"""c4-outline against its fixtures: each fixtures/<name>.cf is outlined - with the arguments its first line names after
`// c4-outline:` - and what the tool prints must be fixtures/<name>.expected; it exits 1 when that has an error, else 0.
C4Api.dll must be beside the program (the C4Api library's copy).

	python -I check_outline.py <c4-outline.exe>
"""

import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "fixtures")
ARGUMENTS = "// c4-outline:"


def arguments_of(source):
	with open(source, encoding="utf-8") as f:
		first = f.readline().strip()
	return first[len(ARGUMENTS):].split() if first.startswith(ARGUMENTS) else []


def expected_of(source):
	with open(os.path.splitext(source)[0] + ".expected", encoding="utf-8") as f:
		return f.read()


def check(tool, source):
	"""The problems of one fixture ([] when it passes)."""
	name = os.path.basename(source)
	run = subprocess.run([tool] + arguments_of(source) + [name], cwd=FIXTURES, capture_output=True, text=True, timeout=60)
	expected = expected_of(source)
	problems = []
	if run.stdout.replace("\r\n", "\n") != expected:
		problems.append(f"{name}: printed\n{run.stdout}\nexpected\n{expected}")
	wanted = 1 if ": error:" in expected else 0
	if run.returncode != wanted:
		problems.append(f"{name}: exit code {run.returncode}, expected {wanted} ({run.stderr.strip()})")
	return problems


def main(argv):
	if len(argv) != 1:
		print(__doc__)
		return 2
	tool = os.path.abspath(argv[0])
	problems = [] if os.path.isfile(os.path.join(os.path.dirname(tool), "C4Api.dll")) else [f"no C4Api.dll beside {tool}"]
	sources = sorted(glob.glob(os.path.join(FIXTURES, "*.cf")))
	for source in sources:
		problems += check(tool, source)
	usage = subprocess.run([tool], capture_output=True, text=True, timeout=60)
	if usage.returncode != 2 or "usage:" not in usage.stderr:
		problems.append(f"no file: exit code {usage.returncode}, expected 2 with the usage")
	for problem in problems:
		print(problem)
	print(f"c4-outline: {len(sources)} fixture(s), {len(problems)} problem(s)")
	return 1 if problems else 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
