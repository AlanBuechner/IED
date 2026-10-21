"""Runs the server checks against an ied-server executable: what only the real server does - its access rules
(docs/11) and file watching. Each test starts its own server.

	python -I tools/server-checks/run_server_checks.py --server build/bin/Development-windows-x64/ied-server/ied-server.exe
	python -I tools/server-checks/run_server_checks.py --server <exe> -k lockout -v

Exit code 0 = every check passed.
"""

import argparse
import os
import pathlib
import sys
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from server import CLI_ENV, EXECUTABLE_ENV  # noqa: E402


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--server", required=True, help="the ied-server executable")
	parser.add_argument("--cli", help="the ied executable (the CLI checks are skipped without it)")
	parser.add_argument("-k", dest="pattern", help="only run tests whose name contains this")
	parser.add_argument("-v", dest="verbose", action="store_true")
	args = parser.parse_args(argv)
	os.environ[EXECUTABLE_ENV] = os.path.abspath(args.server)
	if args.cli:
		os.environ[CLI_ENV] = os.path.abspath(args.cli)
	loader = unittest.TestLoader()
	if args.pattern:
		loader.testNamePatterns = ["*%s*" % args.pattern]
	suite = loader.discover(str(HERE / "tests"), top_level_dir=str(HERE / "tests"))
	result = unittest.TextTestRunner(verbosity=2 if args.verbose else 1).run(suite)
	return 0 if result.wasSuccessful() and result.testsRun else 1


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
