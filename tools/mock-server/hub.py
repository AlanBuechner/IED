"""Topics: per-topic seq, bounded ring buffers, snapshots, fan-out to subscribed sessions (docs/10)."""

import collections
import json

MB = 1024 * 1024
RING_LIMITS = {
	"chat": (20000, 8 * MB),
	"run": (50000, 16 * MB),
	"term": (1000000, 5 * MB),
}
DEFAULT_RING = (2000, 4 * MB)
COALESCIBLE_KINDS = ("out", "run", "term", "chat")
MERGE_FIELDS = {"output": ("text",), "append": ("text",), "data": ("data",), "segment.delta": ("text", "argsText")}


def merge_field(kind, data):
	"""The growing string member of an event that merges (docs/10 "Backpressure"), or None."""
	for field in MERGE_FIELDS.get(kind, ()):
		if isinstance(data, dict) and field in data:
			return field
	return None


def encode(frame):
	return json.dumps(frame, ensure_ascii=False, separators=(",", ":"))


def topic_kind(key):
	return key.rpartition("/")[2].partition(":")[0]


class Event:
	__slots__ = ("seq", "kind", "data", "text", "nbytes")

	def __init__(self, frame):
		self.seq = frame["seq"]
		self.kind = frame["kind"]
		self.data = frame["data"]
		self.text = encode(frame)
		self.nbytes = len(self.text.encode("utf-8"))


class Topic:
	def __init__(self, key, snapshot_fn, ring_events, ring_bytes, coalescible):
		self.key = key
		self.snapshot_fn = snapshot_fn
		self.ring_events = ring_events
		self.ring_bytes = ring_bytes
		self.coalescible = coalescible
		self.seq = 0
		self.ring = collections.deque()
		self.held_bytes = 0
		self.subscribers = set()

	def first_seq(self):
		return self.ring[0].seq if self.ring else self.seq + 1

	def can_replay_from(self, cursor):
		return cursor <= self.seq and cursor + 1 >= self.first_seq()

	def events_after(self, cursor):
		return [e for e in self.ring if e.seq > cursor]

	def replay_bytes(self, cursor):
		return sum(e.nbytes for e in self.ring if e.seq > cursor)

	def snapshot_frame(self):
		return {"t": "snap", "topic": self.key, "seq": self.seq, "data": self.snapshot_fn()}

	def append(self, event):
		self.seq = event.seq
		self.ring.append(event)
		self.held_bytes += event.nbytes
		while self.ring and (len(self.ring) > self.ring_events or self.held_bytes > self.ring_bytes):
			self.held_bytes -= self.ring.popleft().nbytes

	def clear_ring(self):
		self.ring.clear()
		self.held_bytes = 0

	def mergeable(self, kind):
		return self.coalescible and kind in MERGE_FIELDS


class Hub:
	def __init__(self, check_frame):
		self.topics = {}
		self.check_frame = check_frame

	def add(self, key, snapshot_fn, ring_events=None, ring_bytes=None, coalescible=None):
		if key in self.topics:
			raise KeyError("topic exists: %s" % key)
		default_events, default_bytes = RING_LIMITS.get(topic_kind(key), DEFAULT_RING)
		if coalescible is None:
			coalescible = topic_kind(key) in COALESCIBLE_KINDS
		topic = Topic(key, snapshot_fn, ring_events or default_events, ring_bytes or default_bytes, coalescible)
		self.topics[key] = topic
		return topic

	def get(self, key):
		return self.topics.get(key)

	def publish(self, key, kind, data):
		topic = self.topics[key]
		frame = {"t": "evt", "topic": key, "seq": topic.seq + 1, "kind": kind, "data": data}
		self.check_frame(frame)
		event = Event(frame)
		topic.append(event)
		for session in list(topic.subscribers):
			session.deliver(topic, event)
		return event.seq

	def remove(self, key):
		topic = self.topics.pop(key, None)
		if topic is not None:
			for session in list(topic.subscribers):
				session.subs.pop(key, None)

	def clear_rings(self):
		for topic in self.topics.values():
			topic.clear_ring()
