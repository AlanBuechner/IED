"""HTTP front: /ws upgrade with Host and Origin checks (docs/11), /healthz, blobs, static files, the debug console."""

import asyncio
import json
import pathlib
import re
import urllib.parse

import ws
from session import Session

HERE = pathlib.Path(__file__).resolve().parent
CONSOLE_DIR = HERE / "console"
REPO = HERE.parents[1]
UI_KIT_DIR = REPO / "shared" / "ui-kit"
VENDOR_DIR = REPO / "vendor" / "web"
CODICONS_DIR = REPO / "third_party" / "codicons" / "src" / "icons"
CODICON_MAPPING = REPO / "third_party" / "codicons" / "src" / "template" / "mapping.json"
CODICON_FILE = re.compile(r"^[a-z0-9-]+\.svg$")
WORKSPACE_PLUGIN = re.compile(r"^/w/([^/]+)/plugins/(.*)$")
LOOPBACK_NAMES = {"localhost", "127.0.0.1", "[::1]"}
WILDCARD_BINDS = {"0.0.0.0", "::", ""}
MAX_UPLOAD = 64 * 1024 * 1024
CONTENT_TYPES = {
	".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".mjs": "text/javascript; charset=utf-8",
	".css": "text/css; charset=utf-8", ".json": "application/json", ".map": "application/json", ".svg": "image/svg+xml",
	".png": "image/png", ".ico": "image/x-icon", ".woff2": "font/woff2", ".ttf": "font/ttf", ".wasm": "application/wasm",
	".txt": "text/plain; charset=utf-8", ".md": "text/plain; charset=utf-8",
}
REASONS = {200: "OK", 201: "Created", 302: "Found", 400: "Bad Request", 403: "Forbidden", 404: "Not Found", 405: "Method Not Allowed", 413: "Payload Too Large", 426: "Upgrade Required"}


class HttpError(Exception):
	def __init__(self, status, message):
		super().__init__(message)
		self.status = status
		self.message = message


class Response:
	def __init__(self, status, body=b"", content_type="text/plain; charset=utf-8", headers=None):
		self.status = status
		self.body = body if isinstance(body, bytes) else body.encode("utf-8")
		self.content_type = content_type
		self.headers = headers or {}

	def encode(self):
		headers = dict({"Cache-Control": "no-store"}, **self.headers)
		lines = ["HTTP/1.1 %d %s" % (self.status, REASONS.get(self.status, "Status")), "Content-Type: " + self.content_type,
			"Content-Length: %d" % len(self.body), "Connection: close"]
		lines += ["%s: %s" % kv for kv in headers.items()]
		return ("\r\n".join(lines) + "\r\n\r\n").encode("latin-1") + self.body


def json_response(obj, status=200):
	return Response(status, json.dumps(obj), "application/json")


class Front:
	def __init__(self, server):
		self.server = server
		config = server.config
		self.static_root = pathlib.Path(config.static_dir).resolve() if config.static_dir else None
		self.allowed_names = LOOPBACK_NAMES | {config.host.lower()} | {h.lower() for h in config.allow_hosts}

	async def handle(self, reader, writer):
		peer = "%s:%s" % writer.get_extra_info("peername")[:2]
		try:
			head = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 30)
			(method, target, _), headers = ws.parse_head(head.decode("latin-1"))
		except (asyncio.IncompleteReadError, asyncio.LimitOverrunError, asyncio.TimeoutError, ConnectionError, ValueError):
			writer.close()
			return
		path = urllib.parse.unquote(urllib.parse.urlsplit(target).path)
		try:
			self._check_host(headers)
			if path == "/ws":
				await self._websocket(reader, writer, method, headers, peer)
				return
			response = await self._route(method, path, headers, reader)
		except HttpError as e:
			response = Response(e.status, e.message + "\n")
		except ws.HandshakeError as e:
			response = Response(e.status, str(e) + "\n")
		await self._send(writer, response)

	async def _send(self, writer, response):
		try:
			writer.write(response.encode())
			await writer.drain()
		except (ConnectionError, OSError):
			pass
		finally:
			writer.close()

	def _check_host(self, headers):
		if self.server.config.host in WILDCARD_BINDS:
			return
		host = headers.get("host", "")
		name = host.split("]")[0] + "]" if host.startswith("[") else host.rpartition(":")[0] or host
		if name.lower() not in self.allowed_names:
			raise HttpError(403, "Host %r is not allowed (DNS rebinding protection; see --allow-host)" % host)

	def _check_origin(self, headers):
		origin = headers.get("origin")
		if origin is None:
			return
		allowed = {"http://" + headers.get("host", ""), "https://" + headers.get("host", "")} | set(self.server.config.allow_origins)
		if origin not in allowed:
			raise HttpError(403, "Origin %r is not allowed (see --allow-origin)" % origin)

	async def _websocket(self, reader, writer, method, headers, peer):
		self._check_origin(headers)
		writer.write(ws.server_handshake(method, headers))
		sock = ws.WebSocket(reader, writer, is_client=False)
		self.server.log("ws connected %s" % peer)
		await Session(self.server, sock, peer).run()
		self.server.log("ws closed %s (%s)" % (peer, sock.close_code))

	async def _route(self, method, path, headers, reader):
		if path == "/healthz":
			return json_response({"ok": True, "serverVersion": self.server.version, "sessionEpoch": self.server.epoch})
		if path == "/api/upload":
			return await self._upload(method, headers, reader)
		if path.startswith("/api/blob/"):
			return self._blob(method, path[len("/api/blob/"):])
		if method != "GET":
			raise HttpError(405, "method not allowed")
		if path == "/_mock" or path.startswith("/_mock/"):
			return _file(CONSOLE_DIR, path[len("/_mock"):])
		if path.startswith("/plugins/"):
			return self._plugin_file(None, path[len("/plugins/"):])
		match = WORKSPACE_PLUGIN.match(path)
		if match:
			return self._plugin_file(match.group(1), match.group(2))
		if path.startswith("/ui-kit/"):
			return _file(UI_KIT_DIR, path[len("/ui-kit"):])
		if path.startswith("/codicons/"):
			return self._codicon(path[len("/codicons/"):])
		if path.startswith("/vendor/"):
			response = _file(VENDOR_DIR, path[len("/vendor"):])
			response.headers["Cache-Control"] = "max-age=86400"
			return response
		if self.static_root is None:
			return Response(302, "", headers={"Location": "/_mock/"})
		return _file(self.static_root, path)

	def _plugin_file(self, ws_id, rest):
		plugin_id, _, rel = rest.partition("/")
		known_ws = ws_id is None or ws_id in self.server.workspace.items
		target = self.server.plugins.file_of(plugin_id, rel) if known_ws and rel else None
		if target is None:
			raise HttpError(404, "not found")
		return Response(200, target.read_bytes(), CONTENT_TYPES.get(target.suffix.lower(), "application/octet-stream"))

	def _codicon(self, name):
		target = CODICONS_DIR / name
		if CODICON_FILE.match(name) and not target.is_file():
			target = _codicon_alias(name[:-len(".svg")])
		if not CODICON_FILE.match(name) or target is None or not target.is_file():
			raise HttpError(404, "not found")
		return Response(200, target.read_bytes(), CONTENT_TYPES[".svg"], {"Cache-Control": "max-age=86400"})

	async def _upload(self, method, headers, reader):
		if method != "PUT":
			raise HttpError(405, "use PUT")
		self._check_origin(headers)
		length = int(headers.get("content-length", "-1"))
		if length < 0 or length > MAX_UPLOAD:
			raise HttpError(413 if length > MAX_UPLOAD else 400, "Content-Length required (max %d)" % MAX_UPLOAD)
		data = await reader.readexactly(length)
		blob_id = self.server.blobs.put(headers.get("content-type", "application/octet-stream"), data)
		return json_response({"blobId": blob_id, "size": length}, 201)

	def _blob(self, method, blob_id):
		if method != "GET":
			raise HttpError(405, "use GET")
		blob = self.server.blobs.get(blob_id)
		if blob is None:
			raise HttpError(404, "no blob %r" % blob_id)
		return Response(200, blob[1], blob[0])


def _codicon_alias(name):
	"""The icon file an alias names (mapping.json: "run" draws "play"); None when there is none."""
	for names in json.loads(CODICON_MAPPING.read_text(encoding="utf-8")).values():
		if name in names:
			return next((CODICONS_DIR / (n + ".svg") for n in names if CODICON_FILE.match(n + ".svg") and (CODICONS_DIR / (n + ".svg")).is_file()), None)
	return None


def _file(root, rel):
	target = (root / rel.lstrip("/")).resolve()
	if target != root and root not in target.parents:
		raise HttpError(403, "path escapes the static root")
	if target.is_dir():
		target = target / "index.html"
	if not target.is_file():
		raise HttpError(404, "not found: %s" % rel)
	return Response(200, target.read_bytes(), CONTENT_TYPES.get(target.suffix.lower(), "application/octet-stream"))
