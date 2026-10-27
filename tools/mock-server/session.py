"""One WebSocket client: hello/welcome, resume, sub/unsub, req/cancel, ack, ping, liveness (docs/10)."""

import asyncio
import json
import time

import ws
from hub import encode
from outbox import Outbox

SUPPORTED_PROTOS = (1,)
CLIENT_FRAMES = ("hello", "req", "cancel", "sub", "unsub", "ack", "ping")
PING_INTERVAL = 20.0


class Session:
	def __init__(self, server, sock, peer):
		self.server = server
		self.sock = sock
		self.peer = peer
		# A client on the server's machine (docs/10 `welcome.local`): its socket comes from a loopback address.
		self.local = str(peer).rsplit(":", 1)[0].strip("[]") in ("127.0.0.1", "::1", "localhost")
		self.client_id = None
		self.device = {}
		self.subs = {}
		self.inflight = {}
		self.streams = {}
		self.acked = {}
		self.closed = False
		self.outbox = Outbox(server.config.outbound_budget, server.config.coalesce_bytes)
		self.idle_timeout = server.config.idle_timeout

	async def run(self):
		try:
			hello = await self._await_hello()
			if hello is not None:
				self._start(hello)
				await self._serve_frames()
		except (ws.ConnectionClosed, asyncio.TimeoutError):
			pass
		finally:
			self._cleanup()
			if not self.sock.closed:
				await self.sock.close(1001, "session ended")

	async def _serve_frames(self):
		writer = asyncio.create_task(self._write_loop())
		watchdog = asyncio.create_task(self._liveness())
		try:
			while True:
				frame = await self._next_frame()
				if frame is not None:
					self._dispatch(frame)
		finally:
			writer.cancel()
			watchdog.cancel()

	async def _await_hello(self):
		frame = await asyncio.wait_for(self._next_frame(), self.idle_timeout)
		if frame is None or frame["t"] != "hello":
			await self._fail(1002, "first frame must be hello")
		if frame["proto"] not in SUPPORTED_PROTOS:
			await self.sock.send_text(encode({"t": "bye", "reason": "protocol-mismatch"}))
			await self.sock.close(1002, "protocol-mismatch")
			return None
		return frame

	def _start(self, hello):
		self.client_id = hello["clientId"]
		self.device = {k: hello[k] for k in ("deviceId", "deviceClass", "deviceName") if k in hello}
		plans, statuses = self._resume_plans(hello.get("resume"))
		welcome = {
			"t": "welcome", "proto": hello["proto"], "serverVersion": self.server.version,
			"sessionEpoch": self.server.epoch, "clientId": self.client_id, "local": self.local,
		}
		if hello.get("resume") is not None:
			welcome["resume"] = statuses
		self.push(welcome)
		for topic, status, cursor in plans:
			self._attach(topic, status, cursor)
		self.server.sessions.add(self)
		self.server.presence.connected(self)

	def _resume_plans(self, resume):
		if resume is None:
			return [], {}
		same_epoch = resume["sessionEpoch"] == self.server.epoch
		plans, statuses = [], {}
		for key, cursor in resume["cursors"].items():
			topic = self.server.hub.get(key)
			if topic is None:
				continue
			status = self._resume_status(topic, cursor) if same_epoch else "snapshot"
			statuses[key] = status
			plans.append((topic, status, cursor))
		return plans, statuses

	def _resume_status(self, topic, cursor):
		"""Replays from the ring when it still holds cursor+1 and the replay fits this client's budget; else snapshots."""
		if cursor is None or not topic.can_replay_from(cursor):
			return "snapshot"
		fits = topic.replay_bytes(cursor) <= self.outbox.budget - self.outbox.bytes
		return "ok" if fits else "snapshot"

	def _attach(self, topic, status, cursor):
		self.outbox.drop_topic(topic.key)
		self.subs[topic.key] = False
		topic.subscribers.add(self)
		if status != "ok":
			self.push(topic.snapshot_frame())
			return
		for event in topic.events_after(cursor):
			if self.subs.get(topic.key) is not False:
				break
			self._push_event(topic, event)

	async def _next_frame(self):
		message = await self.sock.recv()
		if isinstance(message, bytes):
			await self._fail(1003, "binary frames are reserved for protocol v1.1")
		try:
			frame = json.loads(message)
		except ValueError:
			await self._fail(1007, "frame is not valid JSON")
		self.server.frame_log.write("in", message)
		problems = self._frame_problems(frame)
		if not problems:
			return frame
		if isinstance(frame, dict) and frame.get("t") == "req" and type(frame.get("id")) is int:
			self.push_error(frame["id"], "bad_request", "invalid req frame: %s" % problems[0])
			return None
		await self._fail(1002, "invalid frame: %s" % problems[0])

	def _frame_problems(self, frame):
		if not isinstance(frame, dict) or frame.get("t") not in CLIENT_FRAMES:
			return ["unknown client frame type %r" % (frame.get("t") if isinstance(frame, dict) else frame)]
		return self.server.frames.problems(frame)

	async def _fail(self, code, reason):
		self.server.log("closing %s: %s" % (self.peer, reason))
		await self.sock.close(code, reason)
		raise ws.ConnectionClosed(code, reason)

	def _dispatch(self, frame):
		kind = frame["t"]
		if kind == "req":
			self._on_req(frame)
		elif kind == "cancel":
			self._on_cancel(frame["id"])
		elif kind == "sub":
			self._on_sub(frame)
		elif kind == "unsub":
			self._on_unsub(frame["topics"])
		elif kind == "ack":
			self.acked.update(frame["cursors"])
		elif kind == "ping":
			self.push({"t": "pong", "ts": frame["ts"]} if "ts" in frame else {"t": "pong"})
		else:
			self.server.log("closing %s: duplicate hello" % self.peer)
			asyncio.ensure_future(self.sock.close(1002, "duplicate hello"))

	def _on_req(self, frame):
		rid = frame["id"]
		if rid in self.inflight:
			self.push_error(rid, "bad_request", "request id %d is already in flight" % rid)
			return
		self.inflight[rid] = asyncio.ensure_future(self._serve_request(frame))

	async def _serve_request(self, frame):
		rid = frame["id"]
		try:
			kind, value = await self.server.rpc.call(self, frame)
		except asyncio.CancelledError:
			kind, value = "err", {"code": "cancelled", "message": "request cancelled"}
		finally:
			self.inflight.pop(rid, None)
		if kind == "ok":
			self.push({"t": "res", "id": rid, "ok": True, "result": value})
		else:
			self.push({"t": "res", "id": rid, "ok": False, "error": value})

	def _on_cancel(self, rid):
		task = self.inflight.get(rid)
		if task is not None:
			task.cancel()
		elif rid in self.streams:
			self.streams.pop(rid)()

	def _on_sub(self, frame):
		start = frame.get("from", {})
		results, plans = {}, []
		for key in frame["topics"]:
			topic = self.server.hub.get(key)
			if topic is None:
				results[key] = "not_found"
				continue
			cursor = start.get(key)
			status = self._resume_status(topic, cursor)
			results[key] = status
			plans.append((topic, status, cursor))
		self.push({"t": "res", "id": frame["id"], "ok": True, "result": {"topics": results}})
		for topic, status, cursor in plans:
			self._attach(topic, status, cursor)

	def _on_unsub(self, keys):
		for key in keys:
			self.subs.pop(key, None)
			topic = self.server.hub.get(key)
			if topic is not None:
				topic.subscribers.discard(self)
			self.outbox.drop_topic(key)

	def deliver(self, topic, event):
		if self.subs.get(topic.key) is False:
			self._push_event(topic, event)

	def _push_event(self, topic, event):
		if self.closed:
			return
		gap = self.outbox.push_event(topic, event)
		if gap is not None:
			self.subs[gap.topic] = True
			self.push({"t": "gap", "topic": gap.topic, "from": gap.first, "to": gap.last})

	def push(self, frame):
		if not self.closed:
			self.server.check_outgoing(frame)
			self.outbox.push_control(encode(frame))

	def push_error(self, rid, code, message, data=None):
		error = {"code": code, "message": message}
		if data is not None:
			error["data"] = data
		self.push({"t": "res", "id": rid, "ok": False, "error": error})

	async def _write_loop(self):
		try:
			while True:
				text = await self.outbox.next()
				await self.sock.send_text(text)
				self.server.frame_log.write("out", text)
		except ws.ConnectionClosed:
			pass

	async def _liveness(self):
		last_ping = time.monotonic()
		while not self.sock.closed:
			await asyncio.sleep(min(1.0, self.idle_timeout / 4))
			now = time.monotonic()
			if now - self.sock.last_frame_at > self.idle_timeout:
				self.server.log("closing %s: silent for %.1f s" % (self.peer, now - self.sock.last_frame_at))
				await self.sock.close(1001, "idle timeout")
				return
			if now - last_ping >= PING_INTERVAL:
				self.sock.ping(b"ied")
				last_ping = now

	async def shutdown(self, reason):
		"""Sends bye, flushes what is queued (bounded), then closes the socket."""
		self.push({"t": "bye", "reason": reason})
		deadline = time.monotonic() + 2.0
		while self.outbox.items and time.monotonic() < deadline and not self.sock.closed:
			await asyncio.sleep(0.01)
		self.closed = True
		await self.sock.close(1001, reason)

	def _cleanup(self):
		self.closed = True
		for key in list(self.subs):
			topic = self.server.hub.get(key)
			if topic is not None:
				topic.subscribers.discard(self)
		self.subs.clear()
		self.streams.clear()
		if self in self.server.sessions:
			self.server.sessions.discard(self)
			self.server.presence.disconnected(self)
