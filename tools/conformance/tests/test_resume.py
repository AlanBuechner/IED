"""Resume after a lost socket and after a server restart (docs/10 'Resume', docs/19 resume-path tests)."""

import asyncio

from harness import ProtocolTest


class ResumeTests(ProtocolTest):
	async def test_resume_replays_missed_events_exactly_once(self):
		client = await self.client()
		topic = await self.new_topic()
		await client.sub([topic])
		await self.emit(topic, 20)
		await client.wait_for(topic, lambda f: f.get("seq") == 20)
		client.abort()
		await self.emit(topic, 30)
		again = await self.reconnect(client)
		self.assertEqual({topic: "ok"}, again.welcome["resume"])
		await self.emit(topic, 5)
		frames = await again.wait_for(topic, lambda f: f.get("seq") == 55)
		self.assertEqual(list(range(21, 56)), [f["seq"] for f in frames])
		self.assertEqual([], again.topic(topic).duplicates)

	async def test_killed_mid_stream_then_resumed_loses_and_repeats_nothing(self):
		client = await self.client()
		topic = await self.new_topic()
		await client.sub([topic])
		await client.next_frame(topic)
		total = 300
		await self.hooks.request("testhooks.stream", {"topic": topic, "count": total, "intervalMs": 2})
		await client.wait_for(topic, lambda f: f.get("seq", 0) >= 40)
		client.abort()
		cut = client.topic(topic).cursor
		again = await self.reconnect(client)
		frames = await again.wait_for(topic, lambda f: f.get("kind") == "stream.end", timeout=20)
		before = [f["data"]["i"] for f in client.topic(topic).applied if f["kind"] == "tick"]
		after = [f["data"]["i"] for f in frames if f.get("kind") == "tick"]
		self.assertEqual(list(range(1, total + 1)), before + after)
		self.assertEqual(cut + 1, frames[0]["seq"])
		self.assertEqual([], again.topic(topic).duplicates)

	async def test_resume_after_ring_expiry_falls_back_to_snapshot(self):
		client = await self.client()
		topic = await self.new_topic(ringEvents=10)
		await client.sub([topic])
		await self.emit(topic, 5)
		await client.wait_for(topic, lambda f: f.get("seq") == 5)
		client.abort()
		await self.emit(topic, 50)
		again = await self.reconnect(client)
		self.assertEqual({topic: "snapshot"}, again.welcome["resume"])
		snap = await again.next_frame(topic)
		self.assertEqual(("snap", 55, 55), (snap["t"], snap["seq"], snap["data"]["events"]))
		await self.emit(topic, 1)
		self.assertEqual(56, (await again.next_frame(topic))["seq"])

	async def test_resume_from_another_epoch_snapshots_every_topic(self):
		client = await self.client()
		topic = await self.new_topic()
		await self.emit(topic, 2)
		again = await self.client(resume={"sessionEpoch": "e-not-this-server", "cursors": {topic: 2}})
		self.assertEqual({topic: "snapshot"}, again.welcome["resume"])
		snap = await again.next_frame(topic)
		self.assertEqual(("snap", 2), (snap["t"], snap["seq"]))

	async def test_resume_omits_topics_that_no_longer_exist(self):
		gone = "testhooks/stream:does-not-exist"
		client = await self.client(resume={"sessionEpoch": self.hooks.welcome["sessionEpoch"], "cursors": {gone: 3}})
		self.assertEqual({}, client.welcome["resume"])

	async def test_server_restart_new_epoch_snapshots_and_durable_state(self):
		client = await self.client()
		topic = await self.new_topic()
		scratch = await client.request("testhooks.scratch", {"lines": 3})
		doc = await client.request("doc.open", {"wsId": scratch["wsId"], "path": scratch["path"]})
		doc_topic = "doc:" + doc["docId"]
		edit = {"range": {"start": {"line": 0, "col": 0}, "end": {"line": 0, "col": 0}}, "text": "kept "}
		await client.request("doc.edit", {"docId": doc["docId"], "baseVersion": doc["version"], "changes": [edit]})
		await client.sub([topic, doc_topic])
		await self.emit(topic, 4)
		await client.wait_for(topic, lambda f: f.get("seq") == 4)
		await client.wait_for(doc_topic, lambda f: f["t"] == "snap")
		old_epoch = client.welcome["sessionEpoch"]
		await self.hooks.request("testhooks.restart", {})
		await client.wait_closed(timeout=10)
		self.assertEqual({"t": "bye", "reason": "server-restart"}, client.bye)
		self.hooks = await self.client()
		again = await self.reconnect(client)
		self.assertNotEqual(old_epoch, again.welcome["sessionEpoch"])
		self.assertEqual({topic: "snapshot", doc_topic: "snapshot"}, again.welcome["resume"])
		snaps = {key: await again.next_frame(key) for key in (topic, doc_topic)}
		self.assertEqual(4, snaps[topic]["data"]["events"])
		self.assertEqual("kept line 0\nline 1\nline 2\n", snaps[doc_topic]["data"]["text"])
		await asyncio.sleep(0)
