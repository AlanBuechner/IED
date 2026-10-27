"""A scriptable MCP server (docs/04 "MCP", spec 2025-06-18) for IED's MCP client checks
(tools/server-checks/tests/test_mcp.py): stdio by default (one JSON-RPC message a line on stdin and stdout), or
streamable HTTP with --http PORT (POST /mcp answered with JSON, or an event stream with --sse; Mcp-Session-Id; 202 for
notifications; DELETE ends the session). Stdlib only.

	python -I tools/fake-mcp/fake_mcp.py [--log FILE] [--page-size N] [--hang-initialize] [--exit-at-start CODE]
	                                    [--version V] [--name NAME] [--http PORT [--sse] [--forget-session-after N]]

Tools: echo {text}, add {a, b}, image {}, slow {seconds} (stops when cancelled), crash {} (the process exits with 3),
fail {} (an isError result), add_tool {} (a new tool extra_<n>, then notifications/tools/list_changed), ping_back {}
(asks the client ping first), resource {} (an embedded text resource and a resource link), hang {} (never answers).
Resources (resources/list, resources/read): fake://notes/1 (text) and fake://pixel (a PNG blob).
--log appends every message it reads, and its pid at start, as JSON lines, so a check sees what IED sent.
With --http it prints "LISTENING <url>" on stdout once it listens.
"""

import argparse
import base64
import http.server
import json
import os
import socketserver
import sys
import threading
import time
import uuid

# A 1x1 PNG.
PIXEL = base64.b64encode(bytes.fromhex(
	"89504e470d0a1a0a0000000d4948445200000001000000010806000000"
	"1f15c4890000000d49444154789c6360f80f000001010000ff1b5bd2c80000000049454e44ae426082")).decode()

TOOLS = [
	{"name": "echo", "title": "Echo", "description": "Say the text back. Example: echo({\"text\": \"hi\"}).",
		"inputSchema": {"type": "object", "properties": {"text": {"type": "string"}}, "required": ["text"]}, "annotations": {"readOnlyHint": True}},
	{"name": "add", "description": "Add two numbers.", "inputSchema": {"type": "object", "properties": {"a": {"type": "number"}, "b": {"type": "number"}}, "required": ["a", "b"]}},
	{"name": "image", "description": "A one-pixel picture.", "inputSchema": {"type": "object"}},
	{"name": "slow", "description": "Take a while.", "inputSchema": {"type": "object", "properties": {"seconds": {"type": "number"}}}},
	{"name": "crash", "description": "Exit the server.", "inputSchema": {"type": "object"}},
	{"name": "fail", "description": "Fail as a tool.", "inputSchema": {"type": "object"}},
	{"name": "add_tool", "description": "Add a tool and say the list changed.", "inputSchema": {"type": "object"}},
	{"name": "ping_back", "description": "Ping the client, then answer.", "inputSchema": {"type": "object"}},
	{"name": "resource", "description": "An embedded resource and a link.", "inputSchema": {"type": "object"}},
	{"name": "hang", "description": "Never answer.", "inputSchema": {"type": "object"}},
]

RESOURCES = [
	{"uri": "fake://notes/1", "name": "first-note", "title": "The first note", "mimeType": "text/plain"},
	{"uri": "fake://pixel", "name": "pixel", "description": "A one-pixel picture", "mimeType": "image/png"},
]


class Server:
	def __init__(self, args, send):
		self.args = args
		self.send = send              # writes one message to the client
		self.tools = [dict(t) for t in TOOLS]
		self.cancelled = set()
		self.pending = {}             # our requests' ids -> event
		self.lock = threading.Lock()
		self.next_id = 0

	def log(self, entry):
		if not self.args.log:
			return
		with self.lock, open(self.args.log, "a", encoding="utf-8") as f:
			f.write(json.dumps(entry) + "\n")

	def result(self, rid, value):
		self.send({"jsonrpc": "2.0", "id": rid, "result": value})

	def error(self, rid, code, message):
		self.send({"jsonrpc": "2.0", "id": rid, "error": {"code": code, "message": message}})

	def notify(self, method, params=None):
		message = {"jsonrpc": "2.0", "method": method}
		if params is not None:
			message["params"] = params
		self.send(message)

	def request_client(self, method):
		"""Asks the client `method` and waits for its answer (at most 10 s)."""
		with self.lock:
			self.next_id += 1
			rid = "s%d" % self.next_id
			done = threading.Event()
			self.pending[rid] = [done, None]
		self.send({"jsonrpc": "2.0", "id": rid, "method": method})
		done.wait(10)
		with self.lock:
			return self.pending.pop(rid)[1]

	def handle(self, message):
		"""One message from the client; True when it was a request this call answered (or will never answer)."""
		self.log({"pid": os.getpid(), "message": message})
		method = message.get("method")
		rid = message.get("id")
		if method is None:
			with self.lock:
				waiting = self.pending.get(rid)
				if waiting:
					waiting[1] = message
					waiting[0].set()
			return
		if rid is None:
			if method == "notifications/cancelled":
				self.cancelled.add(message.get("params", {}).get("requestId"))
			return
		if method == "initialize":
			if self.args.hang_initialize:
				return
			self.result(rid, {"protocolVersion": self.args.version, "capabilities": {"tools": {"listChanged": True}, "resources": {"listChanged": True}, "logging": {}},
				"serverInfo": {"name": self.args.name, "version": "1.2.3"}, "instructions": "Fake tools for IED's checks: echo says text back."})
		elif method == "ping":
			self.result(rid, {})
		elif method == "tools/list":
			start = int(message.get("params", {}).get("cursor") or 0)
			size = self.args.page_size
			page = {"tools": self.tools[start:start + size]}
			if start + size < len(self.tools):
				page["nextCursor"] = str(start + size)
			self.result(rid, page)
		elif method == "tools/call":
			self.call(rid, message.get("params", {}))
		elif method == "resources/list":
			self.result(rid, {"resources": RESOURCES})
		elif method == "resources/read":
			uri = message.get("params", {}).get("uri")
			if uri == "fake://notes/1":
				self.result(rid, {"contents": [{"uri": uri, "mimeType": "text/plain", "text": "The first note says: ship it."}]})
			elif uri == "fake://pixel":
				self.result(rid, {"contents": [{"uri": uri, "mimeType": "image/png", "blob": PIXEL}]})
			else:
				self.error(rid, -32002, "no resource " + str(uri))
		else:
			self.error(rid, -32601, "no method " + method)

	def call(self, rid, params):
		name = params.get("name")
		arguments = params.get("arguments") or {}
		if name == "echo":
			self.notify("notifications/message", {"level": "info", "data": "echoing " + str(arguments.get("text"))})
			self.result(rid, {"content": [{"type": "text", "text": "echo: " + str(arguments.get("text"))}]})
		elif name == "add":
			total = arguments.get("a", 0) + arguments.get("b", 0)
			self.result(rid, {"content": [{"type": "text", "text": str(total)}], "structuredContent": {"sum": total}})
		elif name == "image":
			self.result(rid, {"content": [{"type": "text", "text": "a pixel"}, {"type": "image", "mimeType": "image/png", "data": PIXEL}]})
		elif name == "slow":
			deadline = time.time() + float(arguments.get("seconds", 30))
			while time.time() < deadline:
				if rid in self.cancelled:
					self.log({"pid": os.getpid(), "cancelled": rid})
					return
				time.sleep(0.05)
			self.result(rid, {"content": [{"type": "text", "text": "done waiting"}]})
		elif name == "crash":
			sys.stderr.write("fake_mcp: crashing as asked\n")
			sys.stderr.flush()
			os._exit(3)
		elif name == "fail":
			self.result(rid, {"content": [{"type": "text", "text": "it went wrong"}], "isError": True})
		elif name == "add_tool":
			n = len(self.tools) + 1
			self.tools.append({"name": "extra_%d" % n, "description": "A tool added later.", "inputSchema": {"type": "object"}})
			self.notify("notifications/tools/list_changed")
			self.result(rid, {"content": [{"type": "text", "text": "added extra_%d" % n}]})
		elif name == "ping_back":
			answer = self.request_client("ping")
			self.result(rid, {"content": [{"type": "text", "text": "pinged: " + json.dumps(answer.get("result") if answer else None)}]})
		elif name == "resource":
			self.result(rid, {"content": [
				{"type": "resource", "resource": {"uri": "fake://notes/1", "mimeType": "text/plain", "text": "a note"}},
				{"type": "resource_link", "uri": "fake://notes/2", "name": "second note", "description": "not read"}]})
		elif name == "hang":
			return
		else:
			self.error(rid, -32602, "no tool " + str(name))


def run_stdio(args):
	out_lock = threading.Lock()

	def send(message):
		with out_lock:
			sys.stdout.write(json.dumps(message) + "\n")
			sys.stdout.flush()

	server = Server(args, send)
	server.log({"pid": os.getpid(), "started": True})
	sys.stderr.write("fake_mcp %s on stdio\n" % args.name)
	sys.stderr.flush()
	for line in sys.stdin:
		line = line.strip()
		if not line:
			continue
		message = json.loads(line)
		# a call runs apart, so a cancel or another call can come while it waits
		threading.Thread(target=server.handle, args=(message,), daemon=True).start()


def run_http(args):
	sessions = {}
	counter = {"posts": 0}

	class Handler(http.server.BaseHTTPRequestHandler):
		protocol_version = "HTTP/1.1"

		def log_message(self, *_):
			pass

		def do_GET(self):
			self.send_response(405)
			self.send_header("Content-Length", "0")
			self.end_headers()

		def do_DELETE(self):
			sessions.pop(self.headers.get("Mcp-Session-Id"), None)
			self.send_response(200)
			self.send_header("Content-Length", "0")
			self.end_headers()

		def do_POST(self):
			body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
			message = json.loads(body)
			session = self.headers.get("Mcp-Session-Id")
			counter["posts"] += 1
			if message.get("method") != "initialize":
				if session not in sessions:
					self.plain(404, b"unknown session")
					return
				if args.forget_session_after and counter["posts"] > args.forget_session_after:
					sessions.pop(session, None)
					counter["posts"] = 0
					self.plain(404, b"session expired")
					return
			sent = []
			done = threading.Event()

			def send(reply):
				sent.append(reply)
				if "id" in reply and ("result" in reply or "error" in reply) and reply.get("id") == message.get("id"):
					done.set()

			server = sessions.get(session) or Server(args, send)
			server.send = send
			if "id" not in message or "method" not in message:
				server.handle(message)
				self.plain(202, b"")
				return
			if message.get("method") == "initialize":
				session = uuid.uuid4().hex
				sessions[session] = server
			threading.Thread(target=server.handle, args=(message,), daemon=True).start()
			done.wait(60)
			if args.sse:
				self.send_response(200)
				self.send_header("Content-Type", "text/event-stream")
				self.send_header("Mcp-Session-Id", session)
				self.send_header("Connection", "close")
				self.end_headers()
				for reply in sent:
					self.wfile.write(("event: message\ndata: " + json.dumps(reply) + "\n\n").encode())
				self.wfile.flush()
				self.close_connection = True
				return
			answer = [r for r in sent if r.get("id") == message.get("id")]
			data = json.dumps(answer[0] if answer else {}).encode()
			self.send_response(200)
			self.send_header("Content-Type", "application/json")
			self.send_header("Mcp-Session-Id", session)
			self.send_header("Content-Length", str(len(data)))
			self.end_headers()
			self.wfile.write(data)

		def plain(self, status, data):
			self.send_response(status)
			self.send_header("Content-Length", str(len(data)))
			self.end_headers()
			self.wfile.write(data)

	class Threaded(socketserver.ThreadingMixIn, http.server.HTTPServer):
		daemon_threads = True

	httpd = Threaded(("127.0.0.1", args.http), Handler)
	print("LISTENING http://127.0.0.1:%d/mcp" % httpd.server_address[1], flush=True)
	httpd.serve_forever()


def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--log")
	parser.add_argument("--page-size", type=int, default=4)
	parser.add_argument("--hang-initialize", action="store_true")
	parser.add_argument("--exit-at-start", type=int)
	parser.add_argument("--version", default="2025-06-18")
	parser.add_argument("--name", default="fake-mcp")
	parser.add_argument("--http", type=int)
	parser.add_argument("--sse", action="store_true")
	parser.add_argument("--forget-session-after", type=int, default=0)
	args = parser.parse_args()
	if args.exit_at_start is not None:
		sys.stderr.write("fake_mcp: exiting at start as asked\n")
		sys.stderr.flush()
		sys.exit(args.exit_at_start)
	if args.http is not None:
		run_http(args)
	else:
		run_stdio(args)


if __name__ == "__main__":
	main()
