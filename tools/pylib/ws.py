"""Minimal RFC 6455 WebSocket over asyncio streams: server handshake, client connect, framing. Stdlib only."""

import asyncio
import base64
import hashlib
import os
import struct
import time
import urllib.parse

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
OP_CONT, OP_TEXT, OP_BINARY, OP_CLOSE, OP_PING, OP_PONG = 0x0, 0x1, 0x2, 0x8, 0x9, 0xA
CONTROL_OPS = (OP_CLOSE, OP_PING, OP_PONG)
DEFAULT_MAX_MESSAGE = 64 * 1024 * 1024


class ConnectionClosed(Exception):
	def __init__(self, code=1006, reason=""):
		super().__init__("websocket closed (%s) %s" % (code, reason))
		self.code = code
		self.reason = reason


class HandshakeError(Exception):
	def __init__(self, status, message):
		super().__init__("%s %s" % (status, message))
		self.status = status


class ProtocolError(Exception):
	def __init__(self, code, message):
		super().__init__(message)
		self.code = code


def accept_key(key):
	digest = hashlib.sha1((key + GUID).encode("ascii")).digest()
	return base64.b64encode(digest).decode("ascii")


def apply_mask(data, key):
	if not data:
		return data
	n = len(data)
	stream = (key * (n // 4 + 1))[:n]
	return (int.from_bytes(data, "little") ^ int.from_bytes(stream, "little")).to_bytes(n, "little")


def encode_frame(opcode, payload, mask=False, fin=True):
	head = bytearray([(0x80 if fin else 0) | opcode])
	mask_bit = 0x80 if mask else 0
	n = len(payload)
	if n < 126:
		head.append(mask_bit | n)
	elif n < 65536:
		head.append(mask_bit | 126)
		head += struct.pack("!H", n)
	else:
		head.append(mask_bit | 127)
		head += struct.pack("!Q", n)
	if mask:
		key = os.urandom(4)
		return bytes(head) + key + apply_mask(payload, key)
	return bytes(head) + payload


async def read_frame(reader, expect_masked, max_size):
	b0, b1 = await reader.readexactly(2)
	if b0 & 0x70:
		raise ProtocolError(1002, "reserved bits set")
	fin, opcode, masked, n = bool(b0 & 0x80), b0 & 0x0F, bool(b1 & 0x80), b1 & 0x7F
	if masked != expect_masked:
		raise ProtocolError(1002, "frame masking is wrong for this direction")
	if n == 126:
		(n,) = struct.unpack("!H", await reader.readexactly(2))
	elif n == 127:
		(n,) = struct.unpack("!Q", await reader.readexactly(8))
	if opcode in CONTROL_OPS and (n > 125 or not fin):
		raise ProtocolError(1002, "invalid control frame")
	if n > max_size:
		raise ProtocolError(1009, "frame too large")
	key = await reader.readexactly(4) if masked else None
	payload = await reader.readexactly(n) if n else b""
	return fin, opcode, apply_mask(payload, key) if key else payload


def server_handshake(method, headers):
	"""Returns the 101 response bytes for a valid upgrade request; raises HandshakeError otherwise."""
	if method != "GET":
		raise HandshakeError(405, "websocket upgrade must be GET")
	if headers.get("upgrade", "").lower() != "websocket" or "upgrade" not in headers.get("connection", "").lower():
		raise HandshakeError(400, "not a websocket upgrade")
	if headers.get("sec-websocket-version") != "13":
		raise HandshakeError(426, "unsupported websocket version")
	key = headers.get("sec-websocket-key", "")
	if len(_b64_or_empty(key)) != 16:
		raise HandshakeError(400, "bad Sec-WebSocket-Key")
	return (
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Accept: %s\r\n\r\n" % accept_key(key)
	).encode("ascii")


def _b64_or_empty(text):
	try:
		return base64.b64decode(text.encode("ascii"), validate=True)
	except (ValueError, UnicodeEncodeError):
		return b""


class WebSocket:
	def __init__(self, reader, writer, is_client, max_message=DEFAULT_MAX_MESSAGE):
		self.reader = reader
		self.writer = writer
		self.is_client = is_client
		self.max_message = max_message
		self.closed = False
		self.close_code = None
		self.last_frame_at = time.monotonic()
		self._send_lock = asyncio.Lock()

	async def recv(self):
		"""Returns the next text (str) or binary (bytes) message; answers pings; raises ConnectionClosed."""
		parts, first_op = [], None
		while True:
			fin, opcode, payload = await self._read_frame()
			if opcode in CONTROL_OPS:
				await self._on_control(opcode, payload)
				continue
			if (opcode == OP_CONT) != (first_op is not None):
				await self._fail(1002, "unexpected continuation state")
			first_op = first_op if first_op is not None else opcode
			parts.append(payload)
			if sum(len(p) for p in parts) > self.max_message:
				await self._fail(1009, "message too large")
			if fin:
				return await self._finish_message(first_op, b"".join(parts))

	async def _read_frame(self):
		if self.closed:
			raise ConnectionClosed(self.close_code or 1006)
		try:
			frame = await read_frame(self.reader, not self.is_client, self.max_message)
			self.last_frame_at = time.monotonic()
			return frame
		except (asyncio.IncompleteReadError, ConnectionError, OSError):
			self._lost()
		except ProtocolError as e:
			await self._fail(e.code, str(e))

	async def _finish_message(self, opcode, data):
		if opcode == OP_BINARY:
			return data
		try:
			return data.decode("utf-8")
		except UnicodeDecodeError:
			await self._fail(1007, "invalid utf-8 in text message")

	async def _on_control(self, opcode, payload):
		if opcode == OP_PING:
			self._write_now(encode_frame(OP_PONG, payload, mask=self.is_client))
		elif opcode == OP_CLOSE:
			code = struct.unpack("!H", payload[:2])[0] if len(payload) >= 2 else 1005
			reason = payload[2:].decode("utf-8", "replace")
			if not self.closed:
				self._write_now(encode_frame(OP_CLOSE, payload[:2], mask=self.is_client))
			self._mark_closed(code)
			self._close_transport()
			raise ConnectionClosed(code, reason)

	async def _fail(self, code, reason):
		await self.close(code, reason)
		raise ConnectionClosed(code, reason)

	def _write_now(self, data):
		if not self.writer.is_closing():
			self.writer.write(data)

	async def send_text(self, text):
		await self.send(OP_TEXT, text.encode("utf-8"))

	async def send_bytes(self, data):
		await self.send(OP_BINARY, data)

	async def send(self, opcode, payload):
		if self.closed:
			raise ConnectionClosed(self.close_code or 1006)
		async with self._send_lock:
			try:
				self.writer.write(encode_frame(opcode, payload, mask=self.is_client))
				await self.writer.drain()
			except (ConnectionError, OSError):
				self._lost()

	def _lost(self):
		self._mark_closed(1006)
		self._close_transport()
		raise ConnectionClosed(1006, "connection lost")

	def ping(self, payload=b""):
		if not self.closed:
			self._write_now(encode_frame(OP_PING, payload, mask=self.is_client))

	async def close(self, code=1000, reason=""):
		if self.closed:
			return
		payload = struct.pack("!H", code) + reason.encode("utf-8")[:120]
		self._write_now(encode_frame(OP_CLOSE, payload, mask=self.is_client))
		self._mark_closed(code)
		try:
			await asyncio.wait_for(self.writer.drain(), 2)
		except (asyncio.TimeoutError, ConnectionError, OSError):
			pass
		self._close_transport()

	def abort(self):
		"""Drops the TCP connection without a close handshake (a killed browser tab)."""
		self._mark_closed(1006)
		self.writer.transport.abort()

	def _mark_closed(self, code):
		if not self.closed:
			self.closed = True
			self.close_code = code

	def _close_transport(self):
		try:
			self.writer.close()
		except (ConnectionError, OSError):
			pass


async def connect(url, headers=None, timeout=10.0, max_message=DEFAULT_MAX_MESSAGE):
	parts = urllib.parse.urlsplit(url)
	if parts.scheme != "ws":
		raise ValueError("only ws:// urls are supported: %r" % url)
	host, port = parts.hostname, parts.port or 80
	reader, writer = await asyncio.wait_for(asyncio.open_connection(host, port, limit=1 << 20), timeout)
	key = base64.b64encode(os.urandom(16)).decode("ascii")
	lines = [
		"GET %s HTTP/1.1" % (parts.path or "/") + ("?" + parts.query if parts.query else ""),
		"Host: %s:%d" % (host, port),
		"Upgrade: websocket",
		"Connection: Upgrade",
		"Sec-WebSocket-Key: %s" % key,
		"Sec-WebSocket-Version: 13",
	] + ["%s: %s" % kv for kv in (headers or {}).items()]
	writer.write(("\r\n".join(lines) + "\r\n\r\n").encode("ascii"))
	head = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), timeout)
	status, response_headers = parse_head(head.decode("latin-1"))
	if status != 101:
		writer.close()
		raise HandshakeError(status, "upgrade refused")
	if response_headers.get("sec-websocket-accept") != accept_key(key):
		writer.close()
		raise HandshakeError(status, "bad Sec-WebSocket-Accept")
	return WebSocket(reader, writer, is_client=True, max_message=max_message)


def parse_head(text):
	"""Parses an HTTP status or request head; returns (status_or_request_line_parts, lowercase headers)."""
	lines = text.split("\r\n")
	first = lines[0].split(" ", 2)
	headers = {}
	for line in lines[1:]:
		if ":" in line:
			name, _, value = line.partition(":")
			headers[name.strip().lower()] = value.strip()
	if first[0].startswith("HTTP/"):
		return int(first[1]), headers
	return first, headers
