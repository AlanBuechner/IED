"""Per-client outbound queue: byte budget, coalescing of output floods, gap on overflow (docs/10 backpressure)."""

import asyncio
import collections

from hub import encode, merge_field

MAX_MERGED_BYTES = 1024 * 1024


class Item:
	__slots__ = ("text", "nbytes", "topic", "seq", "first_seq", "kind", "data")

	def __init__(self, text, nbytes, topic=None, seq=None, kind=None, data=None):
		self.text = text
		self.nbytes = nbytes
		self.topic = topic
		self.seq = seq
		self.first_seq = seq
		self.kind = kind
		self.data = data


class Gap:
	def __init__(self, topic, first, last):
		self.topic = topic
		self.first = first
		self.last = last


class Outbox:
	def __init__(self, budget, coalesce_bytes):
		self.budget = budget
		self.coalesce_bytes = coalesce_bytes
		self.items = collections.deque()
		self.bytes = 0
		self.topic_bytes = collections.Counter()
		self.wake = asyncio.Event()

	def push_control(self, text):
		self._append(Item(text, len(text.encode("utf-8"))))

	def push_event(self, topic, event):
		"""Queues an event; returns a Gap when the budget forced this client's heaviest topic to be dropped."""
		if topic.mergeable(event.kind) and self.bytes >= self.coalesce_bytes and self._merge_into_tail(topic.key, event):
			return self._enforce_budget()
		item = Item(event.text, event.nbytes, topic.key, event.seq, event.kind, event.data)
		self._append(item)
		return self._enforce_budget()

	def _append(self, item):
		self.items.append(item)
		self.bytes += item.nbytes
		if item.topic:
			self.topic_bytes[item.topic] += item.nbytes
		self.wake.set()

	def _merge_into_tail(self, key, event):
		tail = self.items[-1] if self.items else None
		if tail is None or tail.topic != key or tail.kind != event.kind or tail.nbytes + event.nbytes > MAX_MERGED_BYTES:
			return False
		field = merge_field(event.kind, event.data)
		if field is None or not _same_except(tail.data, event.data, field):
			return False
		data = dict(tail.data)
		data[field] = tail.data[field] + event.data[field]
		text = encode({"t": "evt", "topic": key, "seq": event.seq, "kind": event.kind, "data": data})
		nbytes = len(text.encode("utf-8"))
		self.bytes += nbytes - tail.nbytes
		self.topic_bytes[key] += nbytes - tail.nbytes
		tail.text, tail.nbytes, tail.seq, tail.data = text, nbytes, event.seq, data
		return True

	def _enforce_budget(self):
		if self.bytes <= self.budget or not self.topic_bytes:
			return None
		heaviest = max(self.topic_bytes, key=self.topic_bytes.get)
		kept, dropped = collections.deque(), []
		for item in self.items:
			(dropped if item.topic == heaviest else kept).append(item)
		self.items = kept
		self.bytes -= sum(i.nbytes for i in dropped)
		del self.topic_bytes[heaviest]
		return Gap(heaviest, dropped[0].first_seq, dropped[-1].seq)

	def drop_topic(self, key):
		if key not in self.topic_bytes:
			return
		self.items = collections.deque(i for i in self.items if i.topic != key)
		self.bytes -= self.topic_bytes.pop(key)

	def pop(self):
		item = self.items.popleft()
		self.bytes -= item.nbytes
		if item.topic:
			self.topic_bytes[item.topic] -= item.nbytes
			if self.topic_bytes[item.topic] <= 0:
				del self.topic_bytes[item.topic]
		if not self.items:
			self.wake.clear()
		return item.text

	async def next(self):
		while not self.items:
			await self.wake.wait()
		return self.pop()


def _same_except(a, b, field):
	if not isinstance(a, dict) or not isinstance(b, dict) or a.keys() != b.keys():
		return False
	if not isinstance(a.get(field), str) or not isinstance(b.get(field), str):
		return False
	return all(a[k] == b[k] for k in a if k != field)
