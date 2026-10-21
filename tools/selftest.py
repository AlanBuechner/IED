"""Runs every tools/ check: library unit tests, schema-check, mock-server fixtures, conformance vs the mock, fake-model scripts.

	python -I tools/selftest.py
"""

import pathlib
import subprocess
import sys
import time

TOOLS = pathlib.Path(__file__).resolve().parent
PY = [sys.executable, "-I"]

STAGES = [
	("pylib unit tests", PY + ["-m", "unittest", "discover", "-s", str(TOOLS / "pylib" / "tests"), "-t", str(TOOLS / "pylib" / "tests")]),
	("schema-check", PY + [str(TOOLS / "schema-check" / "schema_check.py")]),
	("mock-server fixtures", PY + ["-m", "unittest", "discover", "-s", str(TOOLS / "mock-server" / "tests"), "-t", str(TOOLS / "mock-server" / "tests")]),
	("conformance vs mock server", PY + [str(TOOLS / "conformance" / "run_conformance.py")]),
	("fake-model scripts", PY + ["-m", "unittest", "discover", "-s", str(TOOLS / "fake-model" / "tests"), "-t", str(TOOLS / "fake-model" / "tests")]),
	("installer", PY + ["-m", "unittest", "discover", "-s", str(TOOLS / "install" / "tests"), "-t", str(TOOLS / "install" / "tests")]),
]


def main():
	results = []
	for name, argv in STAGES:
		sys.stdout.write("== %s\n" % name)
		sys.stdout.flush()
		started = time.monotonic()
		code = subprocess.call(argv, cwd=str(TOOLS.parent))
		results.append((name, code, time.monotonic() - started))
	sys.stdout.write("\n")
	for name, code, seconds in results:
		sys.stdout.write("%-4s %-30s %6.1fs\n" % ("ok" if code == 0 else "FAIL", name, seconds))
	return 0 if all(code == 0 for _, code, _ in results) else 1


if __name__ == "__main__":
	sys.exit(main())
