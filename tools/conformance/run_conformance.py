"""Runs the protocol v1 conformance suite against a server.

	python -I tools/conformance/run_conformance.py                       # spawns the mock server on a free port
	python -I tools/conformance/run_conformance.py --server build/bin/Development-windows-x64/ied-server/ied-server.exe
	python -I tools/conformance/run_conformance.py --url ws://127.0.0.1:8765/ws
	python -I tools/conformance/run_conformance.py -k resume -v

The server must implement the testhooks.* methods (tools/conformance/README.md). Exit code 0 = conformant.
"""

import argparse
import http.client
import os
import pathlib
import subprocess
import sys
import unittest
import urllib.parse

HERE = pathlib.Path(__file__).resolve().parent
TOOLS = HERE.parent
sys.path.insert(0, str(TOOLS / "pylib"))
sys.path.insert(0, str(HERE))

from harness import URL_ENV  # noqa: E402
from iedclient import COOKIE_ENV  # noqa: E402


def spawn(argv, verbose):
	"""Starts a server that prints "LISTENING http://<host>:<port>" first; the process and its /ws URL."""
	if not verbose:
		argv.append("--quiet")
	process = subprocess.Popen(argv, stdout=subprocess.PIPE, text=True)
	line = process.stdout.readline().strip()
	if not line.startswith("LISTENING "):
		process.kill()
		raise RuntimeError("the server did not start (got %r)" % line)
	return process, line.split(" ", 1)[1].replace("http://", "ws://") + "/ws"


def spawn_mock(verbose):
	return spawn([sys.executable, "-I", str(TOOLS / "mock-server" / "mock_server.py"), "--port", "0", "--time-scale", "0"], verbose)


def sign_in(process):
	"""Reads the server's "OPEN <url>?t=<token>" line and spends the token: the session cookie, "name=value"."""
	line = process.stdout.readline().strip()
	if not line.startswith("OPEN "):
		raise RuntimeError("the server printed no launch token (got %r)" % line)
	url = urllib.parse.urlsplit(line.split(" ", 1)[1])
	connection = http.client.HTTPConnection(url.hostname, url.port, timeout=10)
	connection.request("GET", "%s?%s" % (url.path, url.query))
	response = connection.getresponse()
	cookie = response.getheader("Set-Cookie", "")
	connection.close()
	if response.status != 303 or not cookie:
		raise RuntimeError("the launch token was refused (%d)" % response.status)
	return cookie.split(";", 1)[0]


def spawn_server(executable, verbose):
	process, url = spawn([executable, "--port", "0", "--test-hooks"], verbose)
	try:
		os.environ[COOKIE_ENV] = sign_in(process)
	except Exception:
		process.kill()
		raise
	return process, url


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
	parser.add_argument("--server", help="an ied-server executable to start with --test-hooks on a free port (signs in with its launch token)")
	parser.add_argument("--cookie", help="with --url: the session cookie (name=value) a server that authenticates wants")
	parser.add_argument("-k", dest="pattern", help="only run tests whose name contains this")
	parser.add_argument("-v", dest="verbose", action="store_true")
	args = parser.parse_args(argv)
	if args.url:
		process, url = None, args.url
		if args.cookie:
			os.environ[COOKIE_ENV] = args.cookie
	elif args.server:
		process, url = spawn_server(os.path.abspath(args.server), args.verbose)
	else:
		process, url = spawn_mock(args.verbose)
	try:
		sys.stdout.write("conformance against %s\n" % url)
		return run_suite(url, args.pattern, 2 if args.verbose else 1)
	finally:
		if process is not None:
			process.terminate()
			process.wait(10)


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
