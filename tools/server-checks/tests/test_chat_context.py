"""The chat's context meter across a restart (docs/04 "Context management"): the count of the last step comes back with
the chat, and a chat with turns but no meter kept - one an older server ran - gets one from its last turn's count,
never nothing. The turns compression left out are kept in chat.json."""

import json
import os

from frames import FrameChecker
from iedclient import Client
from routed import RoutedClient
from server import local_state, read_json_file
from test_chat import ChatCase, call_step, inline, text_step, write


class ContextMeterTests(ChatCase):
	async def snapshot(self, topic):
		other = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
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
		self.assertEqual([], read_json_file(self.header(chat_id))["compressedTurns"], "no turn left out yet")

	async def test_a_chat_without_a_meter_gets_its_last_turns_count(self):
		chat_id, topic = await self.chat()
		self.model.queue(text_step("Counted."))
		await self.turn(chat_id)
		await self.restart()
		self.client.abort()
		self.server.stop(remove_data=False)
		header = read_json_file(self.header(chat_id))
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


SUMMARIZER = "<conversation>"   # what only a compaction's summarizer requests hold (plugins/chat/prompts/compaction.md)


def filler(words, tag):
	"""Distinct words, so the fake model's reply is never caught repeating itself (Repetition.ch)."""
	return " ".join("%s%d" % (tag, i) for i in range(words))


def said_and_called(text, name, arguments, call_id):
	"""A reply that says something and calls a tool in the same step."""
	return inline({"text": text}, {"toolCall": {"name": name, "arguments": arguments, "id": call_id}}, {"finish": "tool_calls"})


def summaries(*texts):
	"""Summarizer replies, each long enough to count as a summary."""
	return [text_step(text + " It keeps the owner's requests, the files touched and the next step.") for text in texts]


class CompactingCase(ChatCase):
	"""A chat whose model's window is sized from what its requests always carry, so a few steps pass the limit."""

	def header(self, chat_id):
		return os.path.join(local_state(self.data, self.repo), "chats", chat_id, "chat.json")

	def context_file(self, chat_id):
		return os.path.join(local_state(self.data, self.repo), "chats", chat_id, "context.json")

	async def sized_window(self, chat_id, room):
		"""A first turn measures what every request carries (the system message and the tools); the window then leaves
		`room` tokens over it before the limit (85%)."""
		self.model.queue(text_step("Ready."))
		self.assertEqual("finished", (await self.turn(chat_id, "Get ready."))["stop"])
		base = self.model.requests()[-1]["promptTokens"]
		window = int((base + room) / 0.85)
		await self.use_model({"contextWindow": window})
		return window

	async def restart(self, chat_id):
		self.client.abort()
		self.server.stop(remove_data=False)
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()
		await self.client.sub(["chat:" + chat_id])
		await self.client.next_frame("chat:" + chat_id)

	def summarizer_requests(self):
		return [r for r in self.model.requests() if SUMMARIZER in json.dumps(r["body"]["messages"])]

	def turn_requests(self):
		return [r for r in self.model.requests() if SUMMARIZER not in json.dumps(r["body"]["messages"])]


class HistoryTests(CompactingCase):
	"""The turns a compaction cut out of a chat's requests stay reachable: history_search says where a word was said,
	history_read gives a turn back (docs/04 "Context management", F-AIR-54)."""

	async def test_a_turn_left_out_is_still_found_and_read(self):
		chat_id, _ = await self.chat()
		await self.sized_window(chat_id, 2500)
		self.model.queue(text_step("The zephyr gate keeps the wells folded. " + filler(1500, "w")))
		self.assertEqual("finished", (await self.turn(chat_id, "Tell me about the gate."))["stop"])
		zephyr = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"][-1]["turnId"]
		self.model.queue_matching(SUMMARIZER, *summaries("SUMMARY: the owner asked about the gate.", "SUMMARY again."))
		self.model.queue(call_step("history_search", {"query": "zephyr"}, "h1"),
			call_step("history_read", {"turn": zephyr}, "h2"), text_step("Found it."))
		self.assertEqual("finished", (await self.turn(chat_id, "What did you say about it?"))["stop"])
		dropped = read_json_file(self.header(chat_id))["compressedTurns"]
		self.assertIn(zephyr, dropped, ("the long answer's turn is out of the requests now: ", dropped))
		last = self.model.requests()[-1]["body"]["messages"]
		self.assertIn("history_search", last[1]["content"], "the summary names the way back")
		results = {m["tool_call_id"]: m["content"] for m in last if m["role"] == "tool"}
		self.assertIn("zephyr gate keeps the wells", results["h1"], "search quotes the line it matched")
		self.assertIn("(left out)", results["h1"], "and says the turn is one the context does not hold")
		self.assertIn("your context does not hold this one", results["h2"], "read says so too")
		self.assertIn("The zephyr gate keeps the wells folded", results["h2"], "and gives the turn's message")


class CompactionTests(CompactingCase):
	"""A local model's context compacted (docs/04 "Context management"; the owner, 2026-11-22: "the context for local
	models fills up to 100%"): past the limit, the model summarizes what comes before a cut - which falls inside the
	running turn when that turn is what fills the window - and requests send the summary right after the system message,
	then the newest messages as they were. A summarizer that fails leaves IED's digest, so it always makes room; a
	restart sends the same summary; a revert of what it covered drops it; a prompt the server refuses is compacted."""

	def long_turn(self, steps):
		return [said_and_called("Step %d. " % i + filler(400, "s%d_" % i), "fs_read", {"path": "src/main.c"}, "r%d" % i) for i in range(1, steps + 1)]

	async def test_a_long_turn_is_compacted_with_the_models_summary(self):
		chat_id, _ = await self.chat()
		window = await self.sized_window(chat_id, 1500)
		self.model.queue_matching(SUMMARIZER, *summaries("SUMMARY ONE: the owner asked for the long thing; steps 1-2 read src/main.c.",
			*["SUMMARY %d." % n for n in range(2, 12)]))
		self.model.queue(*self.long_turn(8), text_step("All done."))
		end = await self.turn(chat_id, "Do the long thing.")
		self.assertEqual("finished", end["stop"], end)
		turn = await self.last_turn(chat_id)
		self.assertIn("compacted the context at 85%", turn["recoveries"])
		summarizer = self.summarizer_requests()
		self.assertGreaterEqual(len(summarizer), 2, "a long turn compacts as often as it fills the window")
		first = summarizer[0]["body"]
		self.assertNotIn("tools", first, "the summarizer is asked for words, not calls")
		self.assertTrue(first["stream"])
		transcript = first["messages"][-1]["content"]
		for said in ("### The owner's message", "Do the long thing.", "Step 1.", "-> fs_read(", "### Tool result"):
			self.assertIn(said, transcript)
		self.assertIn("SUMMARY ONE", json.dumps(summarizer[1]["body"]["messages"]), "the next compaction folds the summary so far in")
		requests = self.model.requests()
		at = requests.index(summarizer[0])
		after = requests[at + 1]
		messages = after["body"]["messages"]
		self.assertEqual(["system", "user", "assistant", "tool"], [m["role"] for m in messages][:4], "the summary, then the newest step from its call")
		self.assertIn("[Context summary 1]", messages[1]["content"])
		self.assertIn("SUMMARY ONE", messages[1]["content"])
		self.assertIn("Do the long thing.", messages[1]["content"], "the turn's own message, cut out of it, kept word for word")
		self.assertIn("history_search", messages[1]["content"], "and the way back to what was cut")
		self.assertNotIn("Step 1.", json.dumps(messages[2:]), "what was summarized is not sent")
		self.assertLess(after["promptTokens"], 0.85 * window, "the request after it is under the limit again")
		self.assertLess(max(r["promptTokens"] for r in self.turn_requests()), window, "no request of the turn filled the window")
		notes = self.segments(turn, "harness")
		self.assertTrue([s for s in notes if s.get("kind") == "context_compacting"], "the chat says it is compacting")
		compacted = [s["text"] for s in notes if s.get("kind") == "context_compacted"]
		self.assertTrue(compacted[0].startswith("Compacted the context from "), compacted[0])
		self.assertIn("SUMMARY ONE", compacted[0], "and shows the owner what the model was given")
		self.assertEqual([], [s for s in notes if s.get("kind") == "context_compressed"], "no note says turns were left out unsummarized")
		context = read_json_file(self.context_file(chat_id))
		self.assertEqual(len(summarizer), context["summary"]["count"])
		self.assertEqual([turn["turnId"]], [cut["turn"] for cut in context["sentFrom"]], "the turn itself was cut")
		history = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"][-1]
		self.assertEqual(9, len([s for s in history["segments"] if s["type"] == "text"]), "the chat keeps every reply")

	async def test_a_summarizer_that_fails_leaves_a_digest(self):
		chat_id, _ = await self.chat()
		await self.sized_window(chat_id, 1500)
		self.model.queue_matching(SUMMARIZER, *[call_step("fs_read", {"path": "src/main.c"}, "x%d" % i) for i in range(6)])
		self.model.queue(*self.long_turn(5), text_step("All done."))
		end = await self.turn(chat_id, "Do the long thing.")
		self.assertEqual("finished", end["stop"], end)
		turn = await self.last_turn(chat_id)
		compacted = [s["text"] for s in self.segments(turn, "harness") if s.get("kind") == "context_compacted"]
		self.assertTrue(compacted, "it compacted all the same")
		self.assertIn("could not summarize", compacted[0])
		self.assertIn("no summary in its reply", compacted[0], "saying why")
		requests = self.model.requests()
		after = requests[requests.index(self.summarizer_requests()[0]) + 1]["body"]["messages"]
		self.assertIn("IED's digest", after[1]["content"])
		self.assertIn('which started with: "Do the long thing."', after[1]["content"])
		self.assertIn("calls: fs_read x", after[1]["content"])

	async def test_a_restart_sends_the_summary_it_had_and_a_revert_drops_it(self):
		chat_id, _ = await self.chat()
		await self.sized_window(chat_id, 1500)
		self.model.queue_matching(SUMMARIZER, *summaries(*["SUMMARY %d." % n for n in range(1, 9)]))
		self.model.queue(*self.long_turn(4), text_step("All done."))
		self.assertEqual("finished", (await self.turn(chat_id, "Do the long thing."))["stop"])
		self.assertTrue(self.summarizer_requests(), "it compacted")
		last = self.model.requests()[-1]["body"]["messages"]
		self.assertIn("[Context summary", last[1]["content"])
		await self.restart(chat_id)
		self.model.queue(text_step("Still here."))
		await self.turn(chat_id, "Still there?")
		again = self.model.requests()[-1]["body"]["messages"]
		self.assertEqual(last, again[:len(last)], "a restart sends what was sent, the summary in front")
		long_turn = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"][1]["turnId"]
		await self.client.request("chat.revert", {"chatId": chat_id, "turnId": long_turn})
		context = read_json_file(self.context_file(chat_id))
		self.assertNotIn("summary", context, "the summary told of turns that are gone")
		self.assertEqual([], context["sentFrom"])
		self.model.queue(text_step("Back."))
		await self.turn(chat_id, "Once more.")
		sent = self.model.requests()[-1]["body"]["messages"]
		self.assertFalse(any("[Context summary" in (m.get("content") or "") for m in sent), "no summary is sent")
		self.assertIn("Get ready.", json.dumps(sent), "the turn kept is sent whole again")

	async def test_a_prompt_the_server_refuses_is_compacted(self):
		"""The server's word wins over IED's estimate: a context error compacts whatever the limit said."""
		await self.use_model({"contextWindow": 1000000})
		chat_id, _ = await self.chat()
		self.model.queue(text_step("A long answer. " + filler(3000, "a")))
		self.assertEqual("finished", (await self.turn(chat_id, "Say a lot."))["stop"])
		first = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"][-1]["turnId"]
		self.model.queue_matching(SUMMARIZER, *summaries("SUMMARY AFTER A REFUSAL."))
		self.model.queue(said_and_called("Looking.", "fs_read", {"path": "src/main.c"}, "r1"), "context-overflow-llamacpp", text_step("Fits now."))
		end = await self.turn(chat_id, "Go on.")
		self.assertEqual("finished", end["stop"], end)
		turn = await self.last_turn(chat_id)
		self.assertIn("compacted after context_overflow (1x)", turn["recoveries"])
		self.assertIn(first, read_json_file(self.header(chat_id))["compressedTurns"])
		self.assertIn("SUMMARY AFTER A REFUSAL.", self.model.requests()[-1]["body"]["messages"][1]["content"])
