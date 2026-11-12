"""Chats waiting on each other (docs/04 "Chats waiting on each other"): a lead hands work to a helper with
require_reply and suspends; the helper cannot finish or suspend before it replies, and its reply wakes the lead; a helper
whose turn ends without a reply is answered for - its answer is the reply, a stop by its owner drops the debt - and a
suspended chat and its debts survive a restart; a message stops a wait on a background task.

The lead runs on the fake model's queue, the helper on a second fake model server of its own, so each chat's script is
its own whatever order their requests come in. A helper's first reply stalls a few seconds, so the lead has suspended
before the helper's reply comes."""

import asyncio
import sys

from test_chat import TURN_SECONDS, ChatCase, FakeModel, call_step, inline, text_step

LONG_SLEEP = sys.executable + ' -c "import time; time.sleep(60)"'
LEAD_REASON = "Waiting for the helper's port"


def stalled(script, seconds):
	"""`script` with a stall of `seconds` (at the checks' time scale of 0.02) before its first chunk."""
	steps = script["responses"][0]["steps"]
	return dict(script, responses=[{"steps": [{"stall": seconds / 0.02}] + steps}])


class RepliesTests(ChatCase):
	def setUp(self):
		super().setUp()
		self.helper_model = FakeModel()

	def tearDown(self):
		self.helper_model.stop()
		super().tearDown()

	async def asyncSetUp(self):
		await super().asyncSetUp()
		lead = {"endpoint": self.model.url + "/v1", "watchdog": {"prefillBaseS": 2, "idleS": 1, "prefillRate": 1000000}}
		# The helper's first reply stalls on purpose: a watchdog that waits for it.
		helper = {"endpoint": self.helper_model.url + "/v1", "watchdog": {"prefillBaseS": 60, "idleS": 30, "prefillRate": 1000000}}
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": {"fake": lead, "helper": helper}})

	def results(self, model):
		"""The tool results of `model`'s last request, by call id."""
		return {m["tool_call_id"]: m["content"] for m in model.requests()[-1]["body"]["messages"] if m["role"] == "tool"}

	def all_results(self, model):
		"""Every tool result `model` was sent, by call id."""
		found = {}
		for request in model.requests():
			for message in request["body"]["messages"]:
				if message["role"] == "tool":
					found[message["tool_call_id"]] = message["content"]
		return found

	async def helper_id(self, lead_id):
		chats = (await self.client.request("chat.list", {"wsId": "w1"}))["chats"]
		return [c for c in chats if c["chatId"] != lead_id][0]["chatId"]

	async def next_turn(self, topic):
		"""A turn IED starts by itself on `topic`: its turn.start, then its turn.end."""
		started = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.start", timeout=TURN_SECONDS))[-1]["data"]
		ended = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=TURN_SECONDS))[-1]["data"]
		return started, ended

	def create_helper(self, mode, call_id="c1"):
		return call_step("chat_create", {"title": "Helper", "text": "Port Ring.h and tell me what changed.", "mode": mode, "model": "helper", "effort": "default", "require_reply": True}, call_id)

	async def test_a_suspended_lead_wakes_with_the_reply_it_required(self):
		lead_id, topic = await self.chat(mode="agent", permission="auto")
		self.model.queue(self.create_helper("agent"), call_step("suspend", {"reason": LEAD_REASON}, "s1"),
			call_step("finish", {"summary": "The helper ported it", "verification": "its reply"}, "f1"))
		self.helper_model.queue(stalled(call_step("suspend", {"reason": "nothing"}, "hs"), 3), call_step("finish", {"summary": "Ported", "verification": "none"}, "hf"),
			call_step("chat_send", {"chat": lead_id, "text": "Ported Ring.h; the tests pass."}, "hr"), call_step("finish", {"summary": "Ported", "verification": "tests"}, "hf2"))
		ended = await self.turn(lead_id, "Get Ring.h ported")
		self.assertEqual("suspended", ended["stop"])
		results = self.all_results(self.model)
		self.assertIn("It owes this chat a reply now", results["c1"])
		helper_id = await self.helper_id(lead_id)
		lead = await self.summary(lead_id)
		self.assertEqual(("suspended", LEAD_REASON), (lead["state"], lead.get("stateReason")))
		self.assertEqual([helper_id], [a["chatId"] for a in lead["awaits"]])
		self.assertEqual([lead_id], [o["chatId"] for o in (await self.summary(helper_id))["owes"]])
		started, woken = await self.next_turn(topic)
		self.assertEqual((helper_id, "answer"), (started["user"]["from"]["chatId"], started["user"]["from"].get("reply")), started["user"])
		self.assertEqual("Ported Ring.h; the tests pass.", started["user"]["text"])
		self.assertEqual("finished", woken["stop"])
		told = [m for m in self.model.requests()[-1]["body"]["messages"] if m["role"] == "user"][-1]["content"]
		self.assertTrue(told.startswith('Reply from chat "Helper"'), told)
		suspended = self.all_results(self.model)["s1"]
		self.assertIn("Suspended, with the tasks as they are", suspended)
		self.assertIn("- the reply chat %s" % helper_id, suspended, "the result says what wakes it")
		helper = self.all_results(self.helper_model)
		self.assertIn("Not suspended: this chat owes a reply", helper["hs"])
		self.assertIn("Not finished: this chat owes a reply", helper["hf"])
		self.assertIn('chat_send({"chat": "%s"' % lead_id, helper["hf"], "the note shows the call that replies")
		self.assertIn("Sent your reply to", helper["hr"])
		self.assertEqual("finished", (await self.last_turn(helper_id))["stop"], "once it replied, finish ended its turn")
		first = [m for m in self.helper_model.requests()[0]["body"]["messages"] if m["role"] == "user"][0]["content"]
		self.assertIn("it requires your reply", first)
		self.assertEqual(([], []), ((await self.summary(lead_id))["awaits"], (await self.summary(helper_id))["owes"]), "the debt is paid")

	async def test_an_ask_helpers_answer_is_its_reply(self):
		lead_id, topic = await self.chat(mode="agent", permission="auto")
		self.model.queue(self.create_helper("ask"), call_step("suspend", {"reason": LEAD_REASON}, "s1"),
			call_step("finish", {"summary": "Done", "verification": "the helper's answer"}, "f1"))
		self.helper_model.queue(stalled(text_step("Ring.h is ported."), 3))
		self.assertEqual("suspended", (await self.turn(lead_id))["stop"])
		started, woken = await self.next_turn(topic)
		self.assertEqual("answer", started["user"]["from"].get("reply"))
		self.assertIn("Its turn ended with this answer", started["user"]["text"])
		self.assertIn("Ring.h is ported.", started["user"]["text"])
		self.assertEqual("finished", woken["stop"])
		self.assertEqual([], (await self.summary(lead_id))["awaits"])

	async def test_a_helper_stopped_by_its_owner_owes_nothing(self):
		lead_id, topic = await self.chat(mode="agent", permission="auto")
		self.model.queue(self.create_helper("agent"), call_step("suspend", {"reason": LEAD_REASON}, "s1"),
			call_step("finish", {"summary": "Gave up", "verification": "the helper was stopped"}, "f1"))
		self.helper_model.queue(stalled(text_step("never"), 40))
		self.assertEqual("suspended", (await self.turn(lead_id))["stop"])
		helper_id = await self.helper_id(lead_id)
		await self.client.request("chat.stop", {"chatId": helper_id})
		started, woken = await self.next_turn(topic)
		self.assertEqual(("notice", helper_id), (started["user"]["from"].get("reply"), started["user"]["from"]["chatId"]))
		self.assertIn("Its owner stopped its turn before it replied", started["user"]["text"])
		told = [m for m in self.model.requests()[-1]["body"]["messages"] if m["role"] == "user"][-1]["content"]
		self.assertTrue(told.startswith('IED, for chat "Helper"'), told)
		self.assertEqual(([], []), ((await self.summary(lead_id))["awaits"], (await self.summary(helper_id))["owes"]))

	async def test_a_suspended_chat_and_its_debts_survive_a_restart(self):
		lead_id, topic = await self.chat(mode="agent", permission="auto")
		self.model.queue(self.create_helper("ask"), call_step("suspend", {"reason": LEAD_REASON}, "s1"),
			call_step("finish", {"summary": "Heard", "verification": "the notice"}, "f1"))
		self.helper_model.queue(stalled(text_step("never"), 40))
		self.assertEqual("suspended", (await self.turn(lead_id))["stop"])
		helper_id = await self.helper_id(lead_id)
		self.client.abort()
		self.server.stop(remove_data=False)
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()
		lead = await self.summary(lead_id)
		self.assertEqual(("suspended", LEAD_REASON, [helper_id]), (lead["state"], lead.get("stateReason"), [a["chatId"] for a in lead["awaits"]]))
		self.assertEqual([lead_id], [o["chatId"] for o in (await self.summary(helper_id))["owes"]])
		# The helper is an Ask chat the restart cut off: it waits for its owner (ai.resumeInterrupted "agent"), so the
		# lead hears it stopped and that the reply is still owed.
		await self.client.sub([topic])
		await self.client.next_frame(topic)
		started, woken = await self.next_turn(topic)
		self.assertEqual("notice", started["user"]["from"].get("reply"))
		self.assertIn("Its turn stopped (interrupted) before it replied. It still owes this chat the reply", started["user"]["text"])
		self.assertEqual([lead_id], [o["chatId"] for o in (await self.summary(helper_id))["owes"]], "a pause keeps the debt")

	async def test_a_question_from_the_chat_waited_on_stops_the_wait(self):
		lead_id, topic = await self.chat(mode="ask", permission="auto")
		helper_id = (await self.client.request("chat.create", {"wsId": "w1", "mode": "agent", "permission": "auto", "title": "Helper", "model": "helper"}))["chatId"]
		self.model.queue(call_step("chat_send", {"chat": helper_id, "text": "Port Ring.h."}, "c1"),
			call_step("chat_wait", {"chat": helper_id, "seconds": 60}, "w1"), text_step("It asked; I will answer."))
		self.helper_model.queue(stalled(call_step("chat_send", {"chat": lead_id, "text": "Which branch?"}, "q1"), 2),
			stalled(call_step("finish", {"summary": "Asked", "verification": "none"}, "f1"), 30))
		await self.client.request("chat.send", {"chatId": lead_id, "text": "Get it ported"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.start" and f["data"].get("tool") == "chat_wait", timeout=TURN_SECONDS)
		ended = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=TURN_SECONDS))[-1]["data"]
		self.assertEqual("finished", ended["stop"])
		messages = self.model.requests()[-1]["body"]["messages"]
		results = {m["tool_call_id"]: m["content"] for m in messages if m["role"] == "tool"}
		self.assertIn("a message came for this turn", results["w1"])
		self.assertIn('Message from chat "Helper"', messages[-1]["content"])
		self.assertIn("Which branch?", messages[-1]["content"])
		await self.client.request("chat.stop", {"chatId": helper_id})

	async def test_a_message_stops_a_wait(self):
		chat_id, topic = await self.chat(mode="ask", permission="auto")
		self.model.queue(call_step("shell_run", {"command": LONG_SLEEP, "background": True}, "b1"), call_step("background_wait", {"seconds": 60}, "w1"),
			text_step("Read your message."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "Run it and wait"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.start" and f["data"].get("tool") == "background_wait", timeout=TURN_SECONDS)
		await asyncio.sleep(1.5)
		started = asyncio.get_running_loop().time()
		await self.client.request("chat.send", {"chatId": chat_id, "text": "Stop waiting and look at this"})
		ended = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=TURN_SECONDS))[-1]["data"]
		self.assertEqual("finished", ended["stop"])
		self.assertLess(asyncio.get_running_loop().time() - started, 20, "the wait stopped long before its 60 s")
		messages = self.model.requests()[-1]["body"]["messages"]
		results = {m["tool_call_id"]: m["content"] for m in messages if m["role"] == "tool"}
		self.assertIn("Stopped waiting for bg-", results["w1"])
		self.assertIn("The task goes on", results["w1"])
		self.assertEqual("Stop waiting and look at this", messages[-1]["content"], "the message follows the result")
		self.assertEqual(1, (await self.summary(chat_id))["backgroundRunning"], "the task it waited on goes on")
		task_id = results["b1"].split("Started background task ")[1].split(":")[0]
		await self.client.request("chat.stopTask", {"chatId": chat_id, "taskId": task_id})
