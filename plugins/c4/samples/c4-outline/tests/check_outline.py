"""The c4-outline tools against their fixtures: each fixtures/<name>.cf is outlined by each tool - with the arguments
its first line names after `// c4-outline:` - and what a tool prints must be fixtures/<name>.expected; it exits 1 when
that has an error, else 0, and 2 with its usage when given no file. C4Api.dll must be beside each program (the C4Api
library's copy, or the C4 project's own).

	python -I check_outline.py <tool.exe>...
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


def check_fixture(tool, source):
	"""The problems of one tool on one fixture ([] when it passes)."""
	name = os.path.basename(source)
	run = subprocess.run([tool] + arguments_of(source) + [name], cwd=FIXTURES, capture_output=True, text=True, timeout=60)
	expected = expected_of(source)
	problems = []
	if run.stdout.replace("\r\n", "\n") != expected:
		problems.append(f"{os.path.basename(tool)} {name}: printed\n{run.stdout}\nexpected\n{expected}")
	wanted = 1 if ": error:" in expected else 0
	if run.returncode != wanted:
		problems.append(f"{os.path.basename(tool)} {name}: exit code {run.returncode}, expected {wanted} ({run.stderr.strip()})")
	return problems


def check_tool(tool, sources):
	"""The problems of one tool: its DLL, every fixture, its usage."""
	problems = [] if os.path.isfile(os.path.join(os.path.dirname(tool), "C4Api.dll")) else [f"no C4Api.dll beside {tool}"]
	for source in sources:
		problems += check_fixture(tool, source)
	usage = subprocess.run([tool], capture_output=True, text=True, timeout=60)
	if usage.returncode != 2 or "usage:" not in usage.stderr:
		problems.append(f"{os.path.basename(tool)} with no file: exit code {usage.returncode}, expected 2 with the usage")
	return problems


def main(argv):
	if not argv:
		print(__doc__)
		return 2
	sources = sorted(glob.glob(os.path.join(FIXTURES, "*.cf")))
	problems = []
	for tool in argv:
		problems += check_tool(os.path.abspath(tool), sources)
	for problem in problems:
		print(problem)
	print(f"c4-outline: {len(argv)} tool(s), {len(sources)} fixture(s), {len(problems)} problem(s)")
	return 1 if problems else 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
