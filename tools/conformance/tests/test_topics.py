"""sub/unsub, snapshots, per-topic ordering, replay from a cursor, fan-out (docs/10 'Topics')."""

import asyncio

from harness import ProtocolTest


class TopicTests(ProtocolTest):
	async def test_unknown_topic_is_reported_not_found(self):
		client = await self.client()
		self.assertEqual({"testhooks/stream:nope": "not_found"}, await client.sub(["testhooks/stream:nope"]))

	async def test_fresh_sub_gets_snapshot_then_contiguous_events(self):
		client = await self.client()
		topic = await self.new_topic()
		await self.emit(topic, 5)
		self.assertEqual({topic: "snapshot"}, await client.sub([topic]))
		snap = await client.next_frame(topic)
		self.assertEqual(("snap", 5, 5), (snap["t"], snap["seq"], snap["data"]["events"]))
		await self.emit(topic, 200)
		frames = await client.wait_for(topic, lambda f: f.get("seq") == 205)
		self.assertEqual(list(range(6, 206)), self.seqs(frames))
		self.assertEqual(list(range(6, 206)), [f["data"]["i"] for f in frames])

	async def test_untouched_topic_snapshots_at_seq_zero(self):
		client = await self.client()
		topic = await self.new_topic()
		await client.sub([topic])
		snap = await client.next_frame(topic)
		self.assertEqual((0, 0), (snap["seq"], snap["data"]["events"]))

	async def test_sub_from_cursor_replays_only_what_was_missed(self):
		client = await self.client()
		topic = await self.new_topic()
		await self.emit(topic, 10)
		self.assertEqual({topic: "ok"}, await client.sub([topic], {topic: 4}))
		frames = await client.wait_for(topic, lambda f: f.get("seq") == 10)
		self.assertEqual(list(range(5, 11)), [f["seq"] for f in frames])
		self.assertTrue(all(f["t"] == "evt" for f in frames))

	async def test_sub_from_head_is_ok_with_nothing_replayed(self):
		client = await self.client()
		topic = await self.new_topic()
		await self.emit(topic, 3)
		self.assertEqual({topic: "ok"}, await client.sub([topic], {topic: 3}))
		await self.barrier(client)
		self.assertEqual([], client.drain(topic))
		await self.emit(topic, 1)
		self.assertEqual(4, (await client.next_frame(topic))["seq"])

	async def test_unsub_stops_delivery(self):
		client = await self.client()
		topic = await self.new_topic()
		await client.sub([topic])
		await client.next_frame(topic)
		await client.unsub([topic])
		await self.emit(topic, 5)
		await self.barrier(client)
		self.assertEqual([], client.drain(topic))

	async def test_all_subscribers_see_the_same_sequence(self):
		first, second = await self.client(), await self.client(device_class="phone")
		topic = await self.new_topic()
		for client in (first, second):
			await client.sub([topic])
			await client.next_frame(topic)
		await self.emit(topic, 50)
		got = [await client.wait_for(topic, lambda f: f.get("seq") == 50) for client in (first, second)]
		self.assertEqual(got[0], got[1])
		self.assertEqual(list(range(1, 51)), self.seqs(got[0]))

	async def test_resubscribe_without_cursor_resyncs_with_a_snapshot(self):
		client = await self.client()
		topic = await self.new_topic()
		await client.sub([topic])
		await client.next_frame(topic)
		await self.emit(topic, 3)
		await client.wait_for(topic, lambda f: f.get("seq") == 3)
		self.assertEqual({topic: "snapshot"}, await client.sub([topic]))
		snap = await client.next_frame(topic)
		self.assertEqual(("snap", 3), (snap["t"], snap["seq"]))

	async def test_topics_are_independent(self):
		client = await self.client()
		a, b = await self.new_topic(), await self.new_topic()
		await client.sub([a, b])
		await client.next_frame(a)
		await client.next_frame(b)
		await self.emit(a, 3)
		await self.emit(b, 2)
		await self.emit(a, 1)
		await asyncio.gather(client.wait_for(a, lambda f: f.get("seq") == 4), client.wait_for(b, lambda f: f.get("seq") == 2))
		self.assertEqual([1, 2, 3, 4], self.seqs(client.topic(a).frames))
		self.assertEqual([1, 2], self.seqs(client.topic(b).frames))
