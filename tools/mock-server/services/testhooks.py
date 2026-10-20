"""testhooks.*: the deterministic control surface the conformance suite needs (see tools/conformance/README.md).

A real server exposes the same methods only when started with test hooks enabled.
"""

import asyncio

from rpc import ERROR_CODES, RpcError
from services import textedit

PREFIX = "testhooks/stream:"
EMIT_YIELD_EVERY = 1000


class TestTopic:
	def __init__(self, key):
		self.key = key
		self.events = 0
		self.text_length = 0

	def snapshot(self):
		return {"events": self.events, "lastIndex": self.events, "textLength": self.text_length}


class TestHooks:
	def __init__(self, server):
		self.server = server
		self.topics = {}
		self.counters = {}
		self.streams = set()
		self.scratch_count = 0
		self.delays = 0
		names = ("info", "configure", "topic", "emit", "stream", "delay", "counter", "fail", "restart", "scratch", "serverEdit")
		for name in names:
			server.rpc.register("testhooks." + name, getattr(self, "m_" + name))

	async def m_info(self, call, p):
		return {"version": 1, "methods": sorted(m for m in self.server.rpc.methods if m.startswith("testhooks."))}

	async def m_configure(self, call, p):
		session = call.session
		session.outbox.budget = p.int("outboundBudget", session.outbox.budget, required=False)
		session.outbox.coalesce_bytes = p.int("coalesceBytes", session.outbox.coalesce_bytes, required=False)
		idle_ms = p.int("idleTimeoutMs", None, required=False)
		if idle_ms is not None:
			session.idle_timeout = idle_ms / 1000.0
		return {"outboundBudget": session.outbox.budget, "coalesceBytes": session.outbox.coalesce_bytes, "idleTimeoutMs": int(session.idle_timeout * 1000)}

	async def m_topic(self, call, p):
		key = "%s%d" % (PREFIX, len(self.topics) + 1)
		topic = TestTopic(key)
		self.topics[key] = topic
		self.server.hub.add(
			key, topic.snapshot,
			ring_events=p.int("ringEvents", None, required=False),
			ring_bytes=p.int("ringBytes", None, required=False),
			coalescible=p.bool("coalescible", False, required=False),
		)
		return {"topic": key}

	def _topic(self, p):
		key = p.str("topic")
		topic = self.topics.get(key)
		if topic is None:
			raise RpcError("not_found", "no test topic %r (create one with testhooks.topic)" % key)
		return topic

	def _publish(self, topic, kind, size):
		topic.events += 1
		pad = "x" * size
		if kind == "output":
			data = {"text": "%d|%s\n" % (topic.events, pad)}
		else:
			data = {"i": topic.events, "text": pad}
		topic.text_length += len(data["text"])
		self.server.hub.publish(topic.key, kind, data)

	async def m_emit(self, call, p):
		topic = self._topic(p)
		count, kind, size = p.int("count", 1, required=False), p.str("kind", "tick", required=False), p.int("size", 0, required=False)
		first = topic.events + 1
		for i in range(count):
			self._publish(topic, kind, size)
			if (i + 1) % EMIT_YIELD_EVERY == 0:
				await asyncio.sleep(0)
		return {"first": first, "last": topic.events, "seq": self.server.hub.get(topic.key).seq}

	async def m_stream(self, call, p):
		topic = self._topic(p)
		count, interval = p.int("count"), p.int("intervalMs", 10, required=False) / 1000.0
		task = asyncio.ensure_future(self._stream(topic, count, interval, p.int("size", 0, required=False)))
		self.streams.add(task)
		task.add_done_callback(self.streams.discard)
		call.open_stream(task.cancel)
		return {"topic": topic.key}

	async def _stream(self, topic, count, interval, size):
		emitted = 0
		try:
			for _ in range(count):
				await asyncio.sleep(interval)
				self._publish(topic, "tick", size)
				emitted += 1
			reason = "done"
		except asyncio.CancelledError:
			reason = "cancelled"
		self.server.hub.publish(topic.key, "stream.end", {"reason": reason, "emitted": emitted})

	def on_restart(self):
		for task in list(self.streams):
			task.cancel()

	async def m_delay(self, call, p):
		ms = p.int("ms")
		self.delays += 1
		execution = self.delays
		await asyncio.sleep(ms / 1000.0)
		return {"sleptMs": ms, "result": p.any("result"), "execution": execution}

	async def m_counter(self, call, p):
		name = p.str("name", "default", required=False)
		self.counters[name] = self.counters.get(name, 0) + 1
		return {"name": name, "value": self.counters[name]}

	async def m_fail(self, call, p):
		code = p.str("code")
		if code not in ERROR_CODES:
			raise RpcError("bad_request", "unknown error code %r" % code)
		raise RpcError(code, p.str("message", "requested failure", required=False), p.any("data"))

	async def m_restart(self, call, p):
		asyncio.ensure_future(self.server.restart())
		return {"previousEpoch": self.server.epoch}

	async def m_scratch(self, call, p):
		ws_id = next(iter(self.server.workspace.items))
		ws = self.server.workspace.get(ws_id)
		self.scratch_count += 1
		path = "scratch/conformance-%d.txt" % self.scratch_count
		text = "".join("line %d\n" % i for i in range(p.int("lines", 5, required=False)))
		revision = self.server.workspace.write(ws, path, text)
		return {"wsId": ws_id, "path": path, "text": text, "revision": revision}

	async def m_serverEdit(self, call, p):
		doc = self.server.docs.get(p.str("docId"))
		try:
			changes = textedit.parse_changes(p.list("changes"))
		except textedit.BadEdit as e:
			raise RpcError("bad_request", str(e))
		origin = p.str("origin", "ai:testhooks", required=False)
		return self.server.docs.apply_edit(doc, p.int("baseVersion", doc.version, required=False), changes, origin)
