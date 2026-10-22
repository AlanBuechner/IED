"""Runs SearXNG for IED's web search plugin (docs/29 "SearXNG"): its WSGI app under waitress on a loopback port the
system picks, written to --port-file once SearXNG has started, so the plugin knows where to search. Run by the private
Python (python -I serve.py ...); SearXNG and its packages come in by path, its caches go to the state folder, and its
output goes to --log.

	python -I serve.py --searxng DIR --packages DIR --settings FILE --state DIR --port-file FILE --log FILE
"""

import argparse
import os
import secrets
import sys
import tempfile
import types


def parse(argv):
	parser = argparse.ArgumentParser(prog="serve.py", description=__doc__)
	parser.add_argument("--searxng", required=True, help="SearXNG's source folder (searx/ inside)")
	parser.add_argument("--packages", required=True, help="the folder pip installed SearXNG's packages into")
	parser.add_argument("--settings", required=True, help="the settings.yml the plugin wrote")
	parser.add_argument("--state", required=True, help="where SearXNG keeps its caches")
	parser.add_argument("--port-file", required=True, help="written with the port once SearXNG listens")
	parser.add_argument("--log", required=True, help="SearXNG's output")
	parser.add_argument("--threads", type=int, default=8)
	return parser.parse_args(argv)


def redirect_output(log):
	stream = open(log, "w", encoding="utf-8", buffering=1, errors="replace")
	sys.stdout = sys.stderr = stream


def prepare(options):
	sys.path[:0] = [options.packages, options.searxng]
	os.environ["SEARXNG_SETTINGS_PATH"] = options.settings
	os.environ.setdefault("SEARXNG_SECRET", secrets.token_hex(32))
	cache = os.path.join(options.state, "cache")
	os.makedirs(cache, exist_ok=True)
	os.environ["TMP"] = os.environ["TEMP"] = cache
	tempfile.tempdir = cache
	# SearXNG imports pwd (POSIX only) for a message about Valkey, which IED does not use.
	sys.modules.setdefault("pwd", types.ModuleType("pwd"))
	# curl_cffi needs add_reader, which the default (proactor) loop on Windows lacks.
	import asyncio
	if hasattr(asyncio, "WindowsSelectorEventLoopPolicy"):
		asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())


def write_port(path, port):
	partial = path + ".part"
	with open(partial, "w", encoding="utf-8") as f:
		f.write(f"{port}\n")
	os.replace(partial, path)


def main(argv):
	options = parse(argv)
	redirect_output(options.log)
	prepare(options)
	import waitress
	from searx.webapp import app
	server = waitress.create_server(app, host="127.0.0.1", port=0, threads=options.threads, ident="IED SearXNG")
	print(f"SearXNG listens on http://127.0.0.1:{server.effective_port}", flush=True)
	write_port(options.port_file, server.effective_port)
	server.run()
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
