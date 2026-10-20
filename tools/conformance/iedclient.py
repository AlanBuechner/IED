"""A strict protocol v1 test client: validates every server frame, tracks per-topic cursors, counts duplicates."""

import asyncio
import itertools
import json
import os
import secrets

import ws

DEFAULT_TIMEOUT = 5.0
COOKIE_ENV = "IED_CONFORMANCE_COOKIE"


def auth_headers():
	"""The session cookie a server that authenticates wants on every socket (docs/11); none for the mock."""
	cookie = os.environ.get(COOKIE_ENV)
	return {"Cookie": cookie} if cookie else {}


class RpcFailure(Exception):
	def __init__(self, error):
		super().__init__("%s: %s" % (error.get("code"), error.get("message")))
		self.code = error.get("code")
		self.message = error.get("message")
		self.data = error.get("data")


class TopicState:
	def __init__(self):
		self.frames = []
		self.queue = asyncio.Queue()
		self.cursor = None
		self.duplicates = []
		self.applied = []

	def take(self, frame):
		self.frames.append(frame)
		if frame["t"] == "snap":
			self.cursor = frame["seq"]
		elif frame["t"] == "evt":
			if self.cursor is not None and frame["seq"] <= self.cursor:
				self.duplicates.append(frame["seq"])
			else:
				self.cursor = frame["seq"]
				self.applied.append(frame)
		self.queue.put_nowait(frame)


def new_id(prefix):
	return "%s-%s" % (prefix, secrets.token_hex(4))


class Client:
	def __init__(self, sock, checker, client_id, device_id):
		self.sock = sock
		self.checker = checker
		self.client_id = client_id
		self.device_id = device_id
		self.welcome = None
		self.bye = None
		self.violations = []
		self.topics = {}
		self.pending = {}
		self.pongs = asyncio.Queue()
		self.closed = asyncio.Event()
		self.close_code = None
		self.ids = itertools.count(1)
		self.reading = asyncio.Event()
		self.reading.set()
		self.reader = None

	@classmethod
	async def connect(cls, url, checker, client_id=None, device_id=None, device_class="desktop", resume=None, proto=1, timeout=DEFAULT_TIMEOUT, headers=None):
		sock = await ws.connect(url, headers=auth_headers() if headers is None else headers, timeout=timeout)
		client = cls(sock, checker, client_id or new_id("c"), device_id or new_id("d"))
		hello = {"t": "hello", "proto": proto, "clientId": client.client_id, "deviceId": client.device_id, "deviceClass": device_class}
		if resume is not None:
			hello["resume"] = resume
		await client.send(hello)
		first = await client._receive(timeout)
		if first is not None and first["t"] == "welcome":
			client.welcome = first
		elif first is not None and first["t"] == "bye":
			client.bye = first
		elif first is not None:
			client.violations.append("first server frame was %r, expected welcome" % first["t"])
		client.reader = asyncio.ensure_future(client._read_loop())
		return client

	def topic(self, key):
		if key not in self.topics:
			self.topics[key] = TopicState()
		return self.topics[key]

	def cursors(self):
		return {k: t.cursor for k, t in self.topics.items() if t.cursor is not None}

	async def send(self, frame):
		await self.sock.send_text(json.dumps(frame))

	async def _receive(self, timeout=None):
		try:
			text = await asyncio.wait_for(self.sock.recv(), timeout) if timeout else await self.sock.recv()
		except ws.ConnectionClosed as e:
			self.close_code = e.code
			self.closed.set()
			return None
		frame = json.loads(text)
		problems = self.checker.problems(frame)
		if problems:
			self.violations.append("invalid server frame %s: %s" % (text[:160], problems[0]))
		return frame

	async def _read_loop(self):
		while True:
			await self.reading.wait()
			frame = await self._receive()
			if frame is None:
				for future in self.pending.values():
					if not future.done():
						future.set_exception(ws.ConnectionClosed(self.close_code or 1006))
				return
			self._dispatch(frame)

	def _dispatch(self, frame):
		kind = frame.get("t")
		if kind == "res":
			future = self.pending.pop(frame["id"], None)
			if future is None:
				self.violations.append("res for unknown or already answered id %r" % frame["id"])
			elif not future.done():
				future.set_result(frame)
		elif kind in ("evt", "snap", "gap"):
			self.topic(frame["topic"]).take(frame)
		elif kind == "pong":
			self.pongs.put_nowait(frame)
		elif kind == "bye":
			self.bye = frame
		else:
			self.violations.append("unexpected server frame type %r" % kind)

	async def request_frame(self, method, params=None, op_id=None, timeout=DEFAULT_TIMEOUT, req_id=None):
		req_id = req_id if req_id is not None else next(self.ids)
		future = asyncio.get_running_loop().create_future()
		self.pending[req_id] = future
		frame = {"t": "req", "id": req_id, "method": method}
		if params is not None:
			frame["params"] = params
		if op_id is not None:
			frame["opId"] = op_id
		await self.send(frame)
		return await asyncio.wait_for(future, timeout)

	async def request(self, method, params=None, op_id=None, timeout=DEFAULT_TIMEOUT):
		frame = await self.request_frame(method, params, op_id, timeout)
		if not frame["ok"]:
			raise RpcFailure(frame["error"])
		return frame["result"]

	def start_request(self, method, params=None, op_id=None):
		"""Sends a request without waiting; returns (req_id, future of the res frame)."""
		req_id = next(self.ids)
		future = asyncio.get_running_loop().create_future()
		self.pending[req_id] = future
		frame = {"t": "req", "id": req_id, "method": method}
		if params is not None:
			frame["params"] = params
		if op_id is not None:
			frame["opId"] = op_id
		return req_id, future, asyncio.ensure_future(self.send(frame))

	async def sub(self, topics, start=None, timeout=DEFAULT_TIMEOUT):
		req_id = next(self.ids)
		future = asyncio.get_running_loop().create_future()
		self.pending[req_id] = future
		frame = {"t": "sub", "id": req_id, "topics": topics}
		if start:
			frame["from"] = start
		await self.send(frame)
		res = await asyncio.wait_for(future, timeout)
		if not res["ok"]:
			raise RpcFailure(res["error"])
		return res["result"]["topics"]

	async def unsub(self, topics):
		await self.send({"t": "unsub", "topics": topics})

	async def next_frame(self, key, timeout=DEFAULT_TIMEOUT):
		return await asyncio.wait_for(self.topic(key).queue.get(), timeout)

	async def wait_for(self, key, predicate, timeout=DEFAULT_TIMEOUT):
		"""Returns the frames of a topic up to and including the first one matching predicate."""
		seen = []
		while True:
			frame = await self.next_frame(key, timeout)
			seen.append(frame)
			if predicate(frame):
				return seen

	def drain(self, key):
		queue, out = self.topic(key).queue, []
		while not queue.empty():
			out.append(queue.get_nowait())
		return out

	def pause_reading(self):
		self.reading.clear()

	def resume_reading(self):
		self.reading.set()

	def abort(self):
		self.sock.abort()
		self._stop_reader()

	async def close(self):
		await self.sock.close()
		self._stop_reader()

	async def wait_closed(self, timeout=DEFAULT_TIMEOUT):
		await asyncio.wait_for(self.closed.wait(), timeout)

	def _stop_reader(self):
		if self.reader is not None:
			self.reader.cancel()
