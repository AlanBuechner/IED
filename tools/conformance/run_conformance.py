"""Runs the protocol v1 conformance suite against a server.

	python -I tools/conformance/run_conformance.py                       # spawns the mock server on a free port
	python -I tools/conformance/run_conformance.py --url ws://127.0.0.1:8765/ws
	python -I tools/conformance/run_conformance.py -k resume -v

The server must implement the testhooks.* methods (tools/conformance/README.md). Exit code 0 = conformant.
"""

import argparse
import os
import pathlib
import subprocess
import sys
import unittest

HERE = pathlib.Path(__file__).resolve().parent
TOOLS = HERE.parent
sys.path.insert(0, str(TOOLS / "pylib"))
sys.path.insert(0, str(HERE))

from harness import URL_ENV  # noqa: E402


def spawn_mock(verbose):
	argv = [sys.executable, "-I", str(TOOLS / "mock-server" / "mock_server.py"), "--port", "0", "--time-scale", "0"]
	if not verbose:
		argv.append("--quiet")
	process = subprocess.Popen(argv, stdout=subprocess.PIPE, text=True)
	line = process.stdout.readline().strip()
	if not line.startswith("LISTENING "):
		process.kill()
		raise RuntimeError("mock server did not start (got %r)" % line)
	return process, line.split(" ", 1)[1].replace("http://", "ws://") + "/ws"


def run_suite(url, pattern, verbosity):
	os.environ[URL_ENV] = url
	loader = unittest.TestLoader()
	if pattern:
		loader.testNamePatterns = ["*%s*" % pattern]
	suite = loader.discover(str(HERE / "tests"), top_level_dir=str(HERE / "tests"))
	result = unittest.TextTestRunner(verbosity=verbosity).run(suite)
	return 0 if result.wasSuccessful() and result.testsRun else 1


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--url", help="ws:// URL of the server's /ws endpoint (default: spawn the mock server)")
	parser.add_argument("-k", dest="pattern", help="only run tests whose name contains this")
	parser.add_argument("-v", dest="verbose", action="store_true")
	args = parser.parse_args(argv)
	process, url = (None, args.url) if args.url else spawn_mock(args.verbose)
	try:
		sys.stdout.write("conformance against %s\n" % url)
		return run_suite(url, args.pattern, 2 if args.verbose else 1)
	finally:
		if process is not None:
			process.terminate()
			process.wait(10)


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
