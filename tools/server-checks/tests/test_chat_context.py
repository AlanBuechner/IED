"""The chat's context meter across a restart (docs/04 "Context management"): the count of the last step comes back with
the chat, and a chat with turns but no count - one an older server ran - gets an estimate from what its next turn
would send, never nothing. The turns compression left out are kept in chat.json."""

import json
import os

from frames import FrameChecker
from iedclient import Client
from server import local_state
from test_chat import ChatCase, text_step


class ContextMeterTests(ChatCase):
	async def snapshot(self, topic):
		other = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		try:
			await other.sub([topic])
			return (await other.next_frame(topic))["data"]
		finally:
			other.abort()

	async def restart(self):
		self.client.abort()
		self.server.stop(remove_data=False)
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()

	def header(self, chat_id):
		return os.path.join(local_state(self.data, self.repo), "chats", chat_id, "chat.json")

	async def test_the_count_comes_back_after_a_restart(self):
		chat_id, topic = await self.chat()
		self.model.queue(text_step("Counted."))
		await self.turn(chat_id)
		counted = [f["data"] for f in self.frames if f.get("kind") == "context.changed"][-1]
		self.assertGreater(counted["used"], 0)
		self.assertNotIn("estimated", counted)
		await self.restart()
		after = (await self.snapshot(topic))["context"]
		self.assertEqual((counted["used"], counted["window"], counted["model"]), (after["used"], after["window"], after["model"]), "the count kept in chat.json")
		with open(self.header(chat_id), encoding="utf-8") as f:
			self.assertEqual([], json.load(f)["compressedTurns"], "no turn left out yet")

	async def test_a_chat_without_a_count_is_estimated(self):
		chat_id, topic = await self.chat()
		self.model.queue(text_step("Counted."))
		await self.turn(chat_id)
		await self.restart()
		self.client.abort()
		self.server.stop(remove_data=False)
		with open(self.header(chat_id), encoding="utf-8") as f:
			header = json.load(f)
		del header["context"]
		with open(self.header(chat_id), "w", encoding="utf-8") as f:
			json.dump(header, f)
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()
		meter = (await self.snapshot(topic))["context"]
		self.assertIsNotNone(meter, "a chat with turns always has a meter")
		self.assertTrue(meter.get("estimated"), meter)
		self.assertEqual("fake", meter["model"])
		self.assertGreater(meter["used"], 1000, "the system message and tools are in it")
		self.assertEqual(meter["used"], sum(p["tokens"] for p in meter["parts"]))
		self.assertEqual(32768, meter["window"], "the window the server learned before the restart")
		self.model.queue(text_step("Counted again."))
		await self.client.sub([topic])
		await self.client.next_frame(topic)
		await self.turn(chat_id)
		recounted = [f["data"] for f in self.frames if f.get("kind") == "context.changed"][-1]
		self.assertNotIn("estimated", recounted, "the next reply counts it")
