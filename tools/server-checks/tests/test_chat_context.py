"""The chat's context meter across a restart (docs/04 "Context management"): the count of the last step comes back with
the chat, and a chat with turns but no meter kept - one an older server ran - gets one from its last turn's count,
never nothing. The turns compression left out are kept in chat.json."""

import json
import os

from frames import FrameChecker
from iedclient import Client
from server import local_state
from test_chat import ChatCase, call_step, text_step, write


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
		await self.restart()
		after = (await self.snapshot(topic))["context"]
		self.assertEqual((counted["used"], counted["window"], counted["model"]), (after["used"], after["window"], after["model"]), "the count kept in chat.json")
		with open(self.header(chat_id), encoding="utf-8") as f:
			self.assertEqual([], json.load(f)["compressedTurns"], "no turn left out yet")

	async def test_a_chat_without_a_meter_gets_its_last_turns_count(self):
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
		last = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"][-1]
		self.assertGreater(last["tokensIn"], 1000)
		self.assertEqual(last["tokensIn"], meter["used"], "the last request as the model's server counted it")
		self.assertEqual(meter["used"], sum(p["tokens"] for p in meter["parts"]))
		self.assertNotIn("estimated", meter)
		self.assertEqual(32768, meter["window"], "the window the server learned before the restart")
		self.model.queue(text_step("Counted again."))
		await self.client.sub([topic])
		await self.client.next_frame(topic)
		await self.turn(chat_id)
		recounted = [f["data"] for f in self.frames if f.get("kind") == "context.changed"][-1]
		self.assertGreater(recounted["used"], meter["used"], "the next reply counts it")


class HistoryTests(ChatCase):
	"""The turns compression leaves out of a chat's requests stay reachable: history_search says where a word was
	said, history_read gives a turn back (docs/04 "Context management", F-AIR-54). A model whose window is a few
	hundred tokens is what forces the first turn out."""

	def header(self, chat_id):
		return os.path.join(local_state(self.data, self.repo), "chats", chat_id, "chat.json")

	async def test_a_turn_left_out_is_still_found_and_read(self):
		await self.use_model({"contextWindow": 400})
		chat_id, _ = await self.chat()
		self.model.queue(text_step("The zephyr gate keeps the wells folded."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		self.model.queue(text_step("Now the next turn."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"], "a turn after it is what compression can leave out")
		with open(self.header(chat_id), encoding="utf-8") as f:
			dropped = json.load(f)["compressedTurns"]
		self.assertIn("u1", dropped, ("the first turn is out of the requests now: ", dropped))
		self.model.queue(call_step("history_search", {"query": "zephyr"}, "h1"),
			call_step("history_read", {"turn": "u1"}, "h2"), text_step("Found it."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		last = self.model.requests()[-1]["body"]["messages"]
		results = {m["tool_call_id"]: m["content"] for m in last if m["role"] == "tool"}
		self.assertIn("zephyr gate keeps the wells", results["h1"], "search quotes the line it matched")
		self.assertIn("(left out)", results["h1"], "and says the turn is one the context does not hold")
		self.assertIn("your context does not hold this one", results["h2"], "read says so too")
		self.assertIn("The zephyr gate keeps the wells folded", results["h2"], "and gives the turn's message")

	async def test_the_note_says_the_history_was_left_out_not_summarized(self):
		await self.use_model({"contextWindow": 400})
		chat_id, _ = await self.chat()
		self.model.queue(text_step("First, about the zephyr gate."))
		await self.turn(chat_id)
		self.model.queue(text_step("Second."))
		await self.turn(chat_id)
		turn = await self.last_turn(chat_id)
		notes = [s["text"] for s in self.segments(turn, "harness") if s.get("kind") == "context_compressed"]
		self.assertTrue(notes, "compressing left a note")
		self.assertIn("history_search", notes[0], "and it names the way back")
		self.assertIn("left out of your requests", notes[0], "it says what happened, not that something summarized them")
		self.assertTrue(any(r.startswith("compressed the context at 85%") for r in turn["recoveries"]), turn["recoveries"])

	async def test_trimming_a_turns_own_results_sends_no_note_about_the_history(self):
		"""A turn whose earlier tool results are trimmed left no turn out of the conversation, so it says nothing about the
		history (the owner, 2026-10-26: the note showed up with nothing compressed). One turn, three big reads: its first
		result is cut to the trim note's line, and the one turn there is cannot be dropped."""
		await self.use_model({"contextWindow": 400})
		write(os.path.join(self.repo, "long.txt"), "filler line for the read result\n" * 200)
		chat_id, _ = await self.chat()
		self.model.queue(call_step("fs_read", {"path": "long.txt"}, "r1"), call_step("fs_read", {"path": "long.txt", "offset": 2}, "r2"),
			call_step("fs_read", {"path": "long.txt", "offset": 4}, "r3"), text_step("Read it."))
		end = await self.turn(chat_id)
		self.assertEqual("finished", end["stop"], end)
		turn = await self.last_turn(chat_id)
		self.assertEqual([], [s for s in self.segments(turn, "harness") if s.get("kind") == "context_compressed"],
			"nothing was left out, so nothing says the history was")
		results = [m["content"] for m in self.model.requests()[-1]["body"]["messages"] if m["role"] == "tool"]
		self.assertTrue(any("trimmed to save context" in text for text in results), "a result was trimmed all the same")
