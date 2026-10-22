"""A recording proxy between IED and the model server: each job's model profile points at
http://127.0.0.1:<port>/<job>/..., and every request is passed on to the real server (whose address stays in the
app settings) and its streamed answer back, while the proxy notes per request what was sent (messages, tools,
bytes) and what llama.cpp says it cost (`timings`: prompt tokens read and taken from the cache, tokens generated, and
the milliseconds of each). The first request of a job is kept whole: the system message and tool definitions the
model saw."""

import http.client
import http.server
import json
import os
import threading
import time
import urllib.parse

HOP_HEADERS = {"connection", "keep-alive", "proxy-authenticate", "proxy-authorization", "te", "trailers", "transfer-encoding", "upgrade", "host", "content-length"}


class Recorder:
	"""Where each job's notes go: <folder>/model.jsonl, <folder>/first-request.json."""

	def __init__(self):
		self.folders = {}
		self.lock = threading.Lock()

	def register(self, job, folder):
		self.folders[job] = folder

	def note(self, job, entry, body):
		folder = self.folders.get(job)
		if not folder:
			return
		with self.lock:
			first = os.path.join(folder, "first-request.json")
			if body is not None and entry.get("path", "").endswith("/chat/completions") and not os.path.exists(first):
				with open(first, "w", encoding="utf-8", newline="\n") as f:
					json.dump(body, f, indent="\t")
			with open(os.path.join(folder, "model.jsonl"), "a", encoding="utf-8", newline="\n") as f:
				f.write(json.dumps(entry) + "\n")


def summarize_request(body):
	if not isinstance(body, dict):
		return {}
	messages = body.get("messages") or []
	return {"messages": len(messages), "tools": len(body.get("tools") or []), "chars": sum(len(json.dumps(m)) for m in messages),
		"systemChars": sum(len(m.get("content") or "") for m in messages if m.get("role") == "system" and isinstance(m.get("content"), str))}


def timings_of(line):
	"""The `timings` (and `usage`) of an SSE data line, when it has them."""
	if not line.startswith(b"data:"):
		return None
	try:
		event = json.loads(line[5:].strip() or b"null")
	except ValueError:
		return None
	if isinstance(event, dict) and ("timings" in event or event.get("usage")):
		return {"timings": event.get("timings"), "usage": event.get("usage")}
	return None


def make_handler(upstream, recorder):
	target = urllib.parse.urlsplit(upstream)

	class Handler(http.server.BaseHTTPRequestHandler):
		protocol_version = "HTTP/1.1"

		def log_message(self, *args):
			pass

		def forward(self, method):
			_, job, rest = self.path.split("/", 2) if self.path.count("/") >= 2 else ("", self.path.strip("/"), "")
			path = "/" + rest
			length = int(self.headers.get("Content-Length") or 0)
			raw = self.rfile.read(length) if length else b""
			body = None
			if raw:
				try:
					body = json.loads(raw)
				except ValueError:
					body = None
			headers = {k: v for k, v in self.headers.items() if k.lower() not in HOP_HEADERS}
			connection = (http.client.HTTPSConnection if target.scheme == "https" else http.client.HTTPConnection)(target.hostname, target.port, timeout=3600)
			started = time.time()
			entry = {"at": round(started, 3), "method": method, "path": path}
			entry.update(summarize_request(body))
			try:
				connection.request(method, path, body=raw if raw else None, headers=headers)
				response = connection.getresponse()
				self.send_response(response.status)
				for key, value in response.getheaders():
					if key.lower() not in HOP_HEADERS:
						self.send_header(key, value)
				first_byte, pending = None, b""
				if response.getheader("Content-Length") is not None:
					whole = response.read()
					first_byte = time.time()
					self.send_header("Content-Length", str(len(whole)))
					self.send_header("Connection", "close")
					self.end_headers()
					self.wfile.write(whole)
					found = timings_of(b"data:" + whole) if path.endswith("/completions") else None
					if found:
						entry.update(found)
					entry["status"] = response.status
					return
				self.send_header("Transfer-Encoding", "chunked")
				self.send_header("Connection", "close")
				self.end_headers()
				while True:
					chunk = response.read1(65536)
					if not chunk:
						break
					if first_byte is None:
						first_byte = time.time()
					pending += chunk
					*lines, pending = pending.split(b"\n")
					for line in lines:
						found = timings_of(line.strip())
						if found:
							entry.update(found)
					self.wfile.write(b"%x\r\n%s\r\n" % (len(chunk), chunk))
				found = timings_of(pending.strip())
				if found:
					entry.update(found)
				self.wfile.write(b"0\r\n\r\n")
				entry["status"] = response.status
				if response.status >= 400 and not entry.get("timings"):
					entry["error"] = pending[-500:].decode("utf-8", "replace")
			except Exception as problem:
				entry["error"] = repr(problem)
			finally:
				connection.close()
				entry["seconds"] = round(time.time() - started, 3)
				entry["firstByteSeconds"] = round(first_byte - started, 3) if first_byte else None
				recorder.note(job, entry, body)

		def do_POST(self):
			self.forward("POST")

		def do_GET(self):
			self.forward("GET")

	return Handler


class ModelProxy:
	def __init__(self, upstream):
		"""`upstream`: the real endpoint (its scheme, host and port are used; paths pass through)."""
		self.recorder = Recorder()
		self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), make_handler(upstream, self.recorder))
		self.server.daemon_threads = True
		self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
		self.thread.start()
		self.upstream_path = urllib.parse.urlsplit(upstream).path.rstrip("/")

	def endpoint_for(self, job, folder):
		"""The endpoint a job's profile names: this proxy, the job, and the real endpoint's path."""
		self.recorder.register(job, folder)
		return "http://127.0.0.1:%d/%s%s" % (self.server.server_address[1], job, self.upstream_path)

	def close(self):
		self.server.shutdown()
