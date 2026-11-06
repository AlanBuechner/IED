"""boardctl - drive the planning board's AI tools from a command line (docs/32 "Agent evaluation").

    python -I plugins/board/eval/boardctl.py serve --server <ied-server.exe> --workspace <folder> --state <dir>
    python -I plugins/board/eval/boardctl.py call <tool> '<json args>' --state <dir>
    python -I plugins/board/eval/boardctl.py ui --state <dir>       (prints a URL that signs a browser in)

`serve` starts a private ied-server on a scratch workspace (its own data folders, a Claude command that is signed
out so nothing spends the owner's plan) and keeps running; `call` runs one board tool through the board.query binding
exactly as a chat's tool call does and prints the text the model would get, then the absolute path of the picture
board_view made (an agent reads it as an image). Tools: list, create, read, view, edit, recognize, export, import,
plan, revert - their arguments are the board_* tools' (the manifest's inputSchema).
"""

import argparse
import asyncio
import json
import os
import secrets
import shutil
import subprocess
import sys
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(REPO, "tools", "pylib"))
sys.path.insert(0, os.path.join(REPO, "tools", "conformance"))
sys.path.insert(0, os.path.join(REPO, "tools", "server-checks"))

from frames import FrameChecker  # noqa: E402
from routed import RoutedClient  # noqa: E402


def state_file(state):
	return os.path.join(state, "server.json")


def seed_settings(app_data):
	os.makedirs(app_data, exist_ok=True)
	settings = {"ai.claude.command": [sys.executable, "-I", "-c", "raise SystemExit(1)"]}
	with open(os.path.join(app_data, "settings.json"), "w", encoding="utf-8") as f:
		json.dump(settings, f)


def serve(args):
	os.makedirs(args.state, exist_ok=True)
	data = os.path.join(args.state, "data")
	app_data = os.path.join(args.state, "app-data")
	seed_settings(app_data)
	control = "boardctl-" + secrets.token_hex(4)
	argv = [os.path.abspath(args.server), "--host", "127.0.0.1", "--port", str(args.port), "--data", data, "--app-data", app_data, "--control", control, "--static", os.path.join(REPO, "frontend"), "--workspace", os.path.abspath(args.workspace)]
	process = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
	listening = ""
	opening = ""
	for line in process.stdout:
		line = line.strip()
		if args.verbose:
			print(line, flush=True)
		if line.startswith("LISTENING "):
			listening = line
		elif line.startswith("OPEN "):
			opening = line
		if listening and opening:
			break
	if not listening.startswith("LISTENING "):
		print("the server did not start: %r %r" % (listening, opening))
		return 1
	url = urllib.parse.urlsplit(listening.split(" ", 1)[1])
	launch = opening.split(" ", 1)[1] if opening.startswith("OPEN ") else ""
	token = urllib.parse.parse_qs(urllib.parse.urlsplit(launch).query)["t"][0]
	cookie = exchange(url.hostname, url.port, token)
	with open(state_file(args.state), "w", encoding="utf-8") as f:
		json.dump({"host": url.hostname, "port": url.port, "cookie": cookie, "pid": process.pid, "workspace": os.path.abspath(args.workspace), "control": control, "state": os.path.abspath(args.state)}, f)
	print("serving on %s:%d (pid %d), workspace %s" % (url.hostname, url.port, process.pid, args.workspace), flush=True)
	for line in process.stdout:
		if args.verbose:
			print(line.rstrip(), flush=True)
	return process.wait()


def exchange(host, port, token):
	import http.client
	connection = http.client.HTTPConnection(host, port, timeout=30)
	connection.request("GET", "/?t=" + token, headers={"Host": "%s:%d" % (host, port)})
	response = connection.getresponse()
	cookies = [value for name, value in response.getheaders() if name.lower() == "set-cookie"]
	connection.close()
	return cookies[0].split(";", 1)[0]


def load_state(state):
	with open(state_file(state), encoding="utf-8") as f:
		return json.load(f)


async def call_tool(info, tool, tool_args, timeout, by="ai"):
	client = await RoutedClient.connect("ws://%s:%d/ws" % (info["host"], info["port"]), FrameChecker(), headers={"Cookie": info["cookie"]}, timeout=timeout)
	try:
		ws_id = client.default
		reply = await client.request("plugins.call", {"name": "board.query", "wsId": ws_id, "args": {"wsId": ws_id, "tool": tool, "args": tool_args, "by": by}}, timeout=timeout)
		return reply
	finally:
		await client.close()


def call(args):
	info = load_state(args.state)
	tool_args = json.loads(args.args) if args.args else {}
	if args.args_file:
		with open(args.args_file, encoding="utf-8") as f:
			tool_args = json.load(f)
	try:
		reply = asyncio.run(call_tool(info, args.tool, tool_args, args.timeout, "owner" if args.as_owner else "ai"))
	except Exception as failed:  # the tool's refusal reaches the model as an error text: print it the same way
		print("ERROR: %s" % failed)
		return 1
	result = reply.get("result", reply) if isinstance(reply, dict) else reply
	print(result.get("text", ""))
	if result.get("image"):
		print("IMAGE: %s" % local_image(info, result["image"]))
	return 0


def local_image(info, relative):
	"""A folder workspace keeps .ied/local in the server's data folder; find the picture there."""
	direct = os.path.join(info["workspace"], relative.replace("/", os.sep))
	if os.path.exists(direct):
		return direct
	name = os.path.basename(relative)
	for base, _, files in os.walk(info["state"]):
		if name in files:
			return os.path.join(base, name)
	return direct


def ui(args):
	"""A one-time URL that signs a browser in to the server (its control pipe's `url`)."""
	from server import ControlPipe
	info = load_state(args.state)
	pipe = ControlPipe(info["control"])
	try:
		reply = pipe.call("url", {})
	finally:
		pipe.close()
	result = reply.get("result", {})
	print(result.get("url") or result)
	return 0


def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	sub = parser.add_subparsers(dest="command", required=True)
	s = sub.add_parser("serve")
	s.add_argument("--server", required=True)
	s.add_argument("--workspace", required=True)
	s.add_argument("--state", required=True)
	s.add_argument("--port", type=int, default=0)
	s.add_argument("--verbose", action="store_true")
	c = sub.add_parser("call")
	c.add_argument("tool")
	c.add_argument("args", nargs="?", default="")
	c.add_argument("--args-file")
	c.add_argument("--state", required=True)
	c.add_argument("--timeout", type=float, default=60.0)
	c.add_argument("--as-owner", action="store_true", help="make the change as the owner (drawing a sketch), not the AI")
	u = sub.add_parser("ui")
	u.add_argument("--state", required=True)
	args = parser.parse_args()
	return {"serve": serve, "call": call, "ui": ui}[args.command](args)


if __name__ == "__main__":
	sys.exit(main())
