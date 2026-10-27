"""What a chat sends stays what it sent (docs/04 "Context management", docs/12 "Watchdogs"): a model server reads only
the part of a prompt its prefix cache does not hold, so anything that rewrites the front of a chat's requests makes it
read the whole conversation again - the slow, silent read the prefill watchdog calls a stall. A new turn, a tool
loaded mid-turn and a restart of IED leave the system message, the tool list and every earlier message as the last
request had them; compression is the one thing that may change them, only near a full window, and what it changed
comes back the same after a restart. Nothing a request leaves out is gone from the chat."""

import json
import os

from server import local_state
from test_chat import ChatCase, call_step, text_step, write


def body(request):
	return request["body"]


def system_of(request):
	return body(request)["messages"][0]


def tool_names(request):
	return [t["function"]["name"] for t in body(request).get("tools", [])]


class PrefixTests(ChatCase):
	async def restart(self):
		self.client.abort()
		self.server.stop(remove_data=False)
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()
		await self.client.sub(["chat:" + self.chat_id])
		await self.client.next_frame("chat:" + self.chat_id)

	def assertPrefix(self, before, after, why):
		self.assertEqual(system_of(before), system_of(after), why + ": the system message")
		self.assertEqual(body(before).get("tools"), body(after).get("tools"), why + ": the tool list")
		old = body(before)["messages"]
		new = body(after)["messages"]
		self.assertGreaterEqual(len(new), len(old), why)
		for i, message in enumerate(old):
			self.assertEqual(message, new[i], "%s: message %d changed (%s)" % (why, i, message.get("role")))

	async def test_a_new_turn_sends_the_last_request_again_and_adds_to_it(self):
		write(os.path.join(self.repo, "notes.txt"), "a short note\n")
		self.chat_id, _ = await self.chat()
		self.model.queue(call_step("fs_read", {"path": "notes.txt"}, "r1"), text_step("Read."))
		await self.turn(self.chat_id)
		last = self.model.requests()[-1]
		self.model.queue(text_step("Again."))
		await self.turn(self.chat_id, "And now?")
		self.assertPrefix(last, self.model.requests()[-1], "the next turn")

	async def test_loading_a_tool_appends_it_and_leaves_the_front_alone(self):
		"""tool_search's answer carries the tool's description and parameters at the end of the conversation; the system
		message's list and the request's tools stay as they were, so the server reads only what was added."""
		self.chat_id, _ = await self.chat()
		self.model.queue(call_step("tool_search", {"query": "select:history_search"}, "t1"),
			call_step("history_search", {"query": "zephyr"}, "t2"), text_step("Searched."))
		end = await self.turn(self.chat_id)
		self.assertEqual("finished", end["stop"], end)
		first, loaded, last = self.model.requests()[-3:]
		self.assertIn("history_search", tool_names(first), "a deferred tool is in the list from the start")
		self.assertPrefix(first, loaded, "loading a tool")
		self.assertPrefix(loaded, last, "calling the tool it loaded")
		results = {m["tool_call_id"]: m["content"] for m in body(last)["messages"] if m["role"] == "tool"}
		self.assertIn("Parameters:", results["t1"], "the load answers with the tool's parameters")
		self.assertNotIn("Unknown tool", results["t2"])

	async def test_a_deferred_tool_called_without_loading_runs(self):
		self.chat_id, _ = await self.chat()
		self.model.queue(call_step("history_search", {"query": "zephyr"}, "t1"), text_step("Searched."))
		end = await self.turn(self.chat_id)
		self.assertEqual("finished", end["stop"], end)
		results = {m["tool_call_id"]: m for m in body(self.model.requests()[-1])["messages"] if m["role"] == "tool"}
		self.assertNotIn("not loaded", results["t1"]["content"])
		self.assertNotIn("Parameters:", results["t1"]["content"], "it ran rather than being shown")

	async def test_arguments_a_deferred_tool_refuses_come_back_with_its_parameters(self):
		self.chat_id, _ = await self.chat()
		self.model.queue(call_step("history_search", {"words": 3}, "t1"), text_step("Hm."))
		await self.turn(self.chat_id)
		results = {m["tool_call_id"]: m["content"] for m in body(self.model.requests()[-1])["messages"] if m["role"] == "tool"}
		self.assertIn("Expected schema", results["t1"], "the model learns the real parameters from the refusal")
		self.assertIn('"query"', results["t1"])

	async def test_a_restart_sends_the_context_it_had(self):
		"""Turn 1 reads a big file; turn 2 runs near the window, which trims that result; after a restart turn 3 sends
		the trimmed result as turn 2 did - it does not read the whole file back in and compress the chat again - and the
		chat still holds the whole result."""
		write(os.path.join(self.repo, "big.txt"), "".join("line %05d of the big file, read once\n" % i for i in range(1000)))
		await self.use_model({"contextWindow": 1000000})
		self.chat_id, _ = await self.chat()
		self.model.queue(call_step("fs_read", {"path": "big.txt"}, "r1"), text_step("Read the big file."))
		self.assertEqual("finished", (await self.turn(self.chat_id))["stop"])
		first = self.model.requests()[-1]
		whole = [m["content"] for m in body(first)["messages"] if m["role"] == "tool"][0]
		# A window the read pushes past 85% (the meter follows the server's own count) and the trimmed chat - IED's
		# estimate, 3.15 bytes a token, of what is left - fits in half of.
		counted = first["promptTokens"]
		trimmed_estimate = (len(json.dumps(body(first)["messages"])) - len(whole) + 600) / 3.15
		low, high = 2.2 * trimmed_estimate, counted / 0.85 * 0.95
		self.assertLess(low, high, "the read is big enough to tell the two apart")
		window = int((low + high) / 2)
		await self.use_model({"contextWindow": window})
		self.model.queue(text_step("Second."))
		await self.turn(self.chat_id, "Go on.")
		turn2 = await self.last_turn(self.chat_id)
		self.assertTrue(any(r.startswith("compressed") for r in turn2["recoveries"]), turn2["recoveries"])
		before = self.model.requests()[-1]
		trimmed = [m["content"] for m in body(before)["messages"] if m["role"] == "tool"][0]
		self.assertIn("trimmed to save context", trimmed)
		self.assertEqual([], [s for s in self.segments(turn2, "harness") if s.get("kind") == "context_compressed"], "no turn was left out")

		await self.restart()
		self.model.queue(text_step("Third."))
		await self.turn(self.chat_id, "Still there?")
		turn3 = await self.last_turn(self.chat_id)
		self.assertEqual([], [r for r in turn3["recoveries"] if r.startswith("compressed")], "nothing to compress after the restart")
		self.assertPrefix(before, self.model.requests()[-1], "after a restart")

		with open(os.path.join(local_state(self.data, self.repo), "chats", self.chat_id, "turns.jsonl"), encoding="utf-8") as f:
			stored = [json.loads(line) for line in f if line.strip()]
		results = [m["content"] for m in stored[0]["messages"] if m["role"] == "tool"]
		self.assertEqual(whole, results[0], "the chat keeps the whole result")

	async def test_a_turns_own_trimmed_results_are_kept_whole(self):
		write(os.path.join(self.repo, "long.txt"), "filler line for the read result\n" * 200)
		await self.use_model({"contextWindow": 400})
		self.chat_id, _ = await self.chat()
		self.model.queue(call_step("fs_read", {"path": "long.txt"}, "r1"), call_step("fs_read", {"path": "long.txt", "offset": 2}, "r2"),
			call_step("fs_read", {"path": "long.txt", "offset": 4}, "r3"), text_step("Read it."))
		await self.turn(self.chat_id)
		sent = [m["content"] for m in body(self.model.requests()[-1])["messages"] if m["role"] == "tool"]
		self.assertTrue(any("trimmed to save context" in text for text in sent), "a result was trimmed in the request")
		with open(os.path.join(local_state(self.data, self.repo), "chats", self.chat_id, "turns.jsonl"), encoding="utf-8") as f:
			stored = [json.loads(line) for line in f if line.strip()]
		kept = [m["content"] for m in stored[-1]["messages"] if m["role"] == "tool"]
		self.assertFalse(any("trimmed to save context" in text for text in kept), "the chat keeps every result whole")
		self.assertTrue(all("filler line" in text for text in kept))

	async def test_the_estimate_counts_what_the_server_has_not_cached(self):
		"""A server that reports no progress gets its read estimated (docs/12 "What makes a server read the whole prompt
		again"): the first request is read whole; the next turn's request only adds to it, so the estimate is of what was
		added - the owner's "inaccurate prompt processing info" was the whole prompt estimated every time."""
		self.model.post("/_fake/config", {"props": False})
		await self.use_model({"watchdog": {"prefillBaseS": 2, "idleS": 1, "prefillRate": 50}})
		self.chat_id, topic = await self.chat()
		self.model.queue(text_step("First."))
		await self.client.request("chat.send", {"chatId": self.chat_id, "text": "Hello"})
		frames = await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=60)
		first = [f["data"]["prefill"] for f in frames if f.get("kind") == "turn.progress" and f["data"]["prefill"]]
		self.assertTrue(first and first[0]["read"] == first[0]["total"], ("a first request is read whole", first))
		self.model.queue(text_step("Second."))
		await self.client.request("chat.send", {"chatId": self.chat_id, "text": "And again"})
		frames = await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=60)
		second = [f["data"]["prefill"] for f in frames if f.get("kind") == "turn.progress" and f["data"]["prefill"]]
		self.assertTrue(all(p["read"] < p["total"] / 4 for p in second), ("the next only what it added", second))
