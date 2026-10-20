"""Slow consumers: gap then resync, coalescing of floods, responses never dropped (docs/10 'Backpressure and floods')."""

import asyncio

from harness import ProtocolTest

MB = 1024 * 1024


class BackpressureTests(ProtocolTest):
	async def overflow(self, topic, count, size):
		"""Floods a paused, tiny-budget subscriber; returns (client, frames seen up to and including the gap, counter future)."""
		client = await self.client()
		await client.request("testhooks.configure", {"outboundBudget": 64 * 1024, "coalesceBytes": 64 * MB})
		await client.sub([topic])
		await client.next_frame(topic)
		client.pause_reading()
		_, counter, _ = client.start_request("testhooks.counter", {"name": "while-paused"})
		await self.emit(topic, count, size=size)
		client.resume_reading()
		frames = await client.wait_for(topic, lambda f: f["t"] == "gap", timeout=30)
		return client, frames, counter

	async def resync_and_check(self, client, topic, gap, total):
		await self.barrier(client)
		self.assertEqual([], [f for f in client.drain(topic) if f["t"] == "evt"], "no events for a topic after its gap until resync")
		cursor = client.topic(topic).cursor
		status = (await client.sub([topic], {topic: cursor}))[topic]
		frames = await client.wait_for(topic, lambda f: f.get("seq") == total)
		if status == "ok":
			self.assertEqual(list(range(cursor + 1, total + 1)), self.seqs(frames))
		else:
			self.assertEqual(("snap", total, total), (frames[0]["t"], frames[0]["seq"], frames[0]["data"]["events"]))
		await self.emit(topic, 1)
		self.assertEqual(total + 1, (await client.next_frame(topic))["seq"])
		return status

	async def test_overflow_sends_gap_and_snapshot_resync_restores_state(self):
		topic = await self.new_topic(ringEvents=100)
		client, frames, counter = await self.overflow(topic, 4000, 4096)
		gap = frames[-1]
		delivered = self.seqs(frames)
		self.assertEqual(delivered, list(range(1, len(delivered) + 1)), "events before the gap arrive in order")
		self.assertEqual(len(delivered) + 1, gap["from"], "the gap starts right after the last delivered event")
		self.assertLessEqual(gap["to"], 4000)
		self.assertTrue((await asyncio.wait_for(counter, 10))["ok"], "responses are never dropped")
		self.assertEqual("snapshot", await self.resync_and_check(client, topic, gap, 4000))

	async def test_overflow_resync_can_replay_from_the_ring(self):
		topic = await self.new_topic(ringEvents=100000, ringBytes=256 * MB)
		client, frames, counter = await self.overflow(topic, 4000, 4096)
		self.assertTrue((await asyncio.wait_for(counter, 10))["ok"])
		await client.request("testhooks.configure", {"outboundBudget": 256 * MB})
		self.assertEqual("ok", await self.resync_and_check(client, topic, frames[-1], 4000))

	async def test_replay_larger_than_the_budget_is_sent_as_a_snapshot(self):
		topic = await self.new_topic(ringEvents=100000, ringBytes=256 * MB)
		client = await self.client()
		await client.request("testhooks.configure", {"outboundBudget": 64 * 1024})
		await self.emit(topic, 100, size=4096)
		self.assertEqual({topic: "snapshot"}, await client.sub([topic], {topic: 0}))
		self.assertEqual({topic: "ok"}, await client.sub([topic], {topic: 95}))

	async def test_output_floods_are_coalesced_without_loss(self):
		topic = await self.new_topic(coalescible=True)
		client = await self.client()
		await client.request("testhooks.configure", {"outboundBudget": 256 * MB, "coalesceBytes": 1})
		await client.sub([topic])
		await client.next_frame(topic)
		client.pause_reading()
		count = 20000
		await self.emit(topic, count, kind="output", size=16)
		client.resume_reading()
		frames = await client.wait_for(topic, lambda f: f.get("seq") == count, timeout=30)
		events = [f for f in frames if f["t"] == "evt"]
		self.assertLess(len(events), count, "a flood to a slow client is merged into fewer frames")
		seqs = [f["seq"] for f in events]
		self.assertEqual(sorted(set(seqs)), seqs, "merged events keep increasing seqs")
		expected = "".join("%d|%s\n" % (i, "x" * 16) for i in range(1, count + 1))
		self.assertEqual(expected, "".join(f["data"]["text"] for f in events))
