"""IED mock server: protocol v1 (docs/10) over WebSocket with in-memory fixture data. Stdlib only.

	python -I tools/mock-server/mock_server.py [--port 8765] [--static frontend] [--time-scale 1.0]

Prints 'LISTENING http://<host>:<port>' on stdout once ready (use --port 0 for a free port).
Open http://127.0.0.1:8765/_mock/ for the debug console. See tools/mock-server/README.md.
"""

import argparse
import asyncio
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "pylib"))
sys.path.insert(0, str(HERE))

from front import Front  # noqa: E402
from state import MB, Config, Server  # noqa: E402


def parse_args(argv):
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--host", default="127.0.0.1", help="bind address; 0.0.0.0 for phones on the LAN (no auth in the mock)")
	parser.add_argument("--port", type=int, default=8765)
	parser.add_argument("--static", dest="static_dir", help="serve this folder at / (e.g. frontend)")
	parser.add_argument("--allow-origin", dest="allow_origins", action="append", default=[], help="extra Origin allowed to open /ws")
	parser.add_argument("--allow-host", dest="allow_hosts", action="append", default=[], help="extra Host header name allowed")
	parser.add_argument("--time-scale", type=float, default=1.0, help="multiplies fixture delays; 0 = as fast as possible")
	parser.add_argument("--idle-timeout", type=float, default=60.0, help="seconds of silence before a socket is closed")
	parser.add_argument("--outbound-budget-mb", type=float, default=4.0, help="per-client queue budget before a gap")
	parser.add_argument("--coalesce-kb", type=float, default=256.0, help="queued bytes before output events are merged")
	parser.add_argument("--tree-files", type=int, default=0, help="add N generated files under generated/")
	parser.add_argument("--frames-log", help="append every frame (both directions) to this .frames.jsonl file")
	parser.add_argument("--no-validate", dest="validate_frames", action="store_false", help="skip schema validation of frames")
	parser.add_argument("--quiet", action="store_true")
	args = parser.parse_args(argv)
	return Config(
		host=args.host, port=args.port, static_dir=args.static_dir, allow_origins=args.allow_origins,
		allow_hosts=args.allow_hosts, time_scale=args.time_scale, idle_timeout=args.idle_timeout,
		outbound_budget=int(args.outbound_budget_mb * MB), coalesce_bytes=int(args.coalesce_kb * 1024),
		validate_frames=args.validate_frames, frames_log=args.frames_log, tree_files=args.tree_files, quiet=args.quiet,
	)


async def start(config):
	"""Starts the server; returns (server, listener, port). Used by in-process tests."""
	server = Server(config)
	listener = await asyncio.start_server(Front(server).handle, config.host, config.port)
	return server, listener, listener.sockets[0].getsockname()[1]


async def serve(config):
	server, listener, port = await start(config)
	sys.stdout.write("LISTENING http://%s:%d\n" % (config.host, port))
	sys.stdout.flush()
	server.log("epoch %s, console at http://%s:%d/_mock/" % (server.epoch, config.host, port))
	async with listener:
		await listener.serve_forever()


def main(argv):
	try:
		asyncio.run(serve(parse_args(argv)))
	except KeyboardInterrupt:
		pass
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
