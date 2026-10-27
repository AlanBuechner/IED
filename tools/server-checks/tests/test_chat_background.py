"""Background tasks and sub-agents (docs/04 "Background tasks", "A group is a topology"): a command started in the
background answers at once and its chat is told when it ends - an idle chat by a turn of its own - background_wait and
chat.stopTask, and a group's agent run with agent_run as a chat of its own whose answer comes back to the chat that ran
it, with the system section and agent_list's record."""

import asyncio
import sys

from frames import FrameChecker
from iedclient import Client
from routed import RoutedClient
from test_chat import TURN_SECONDS, ChatCase, call_step, text_step

SLEEP = sys.executable + ' -c "import time; time.sleep(1.5); print(\'slept well\')"'
WAITED_SLEEP = sys.executable + ' -c "import time; time.sleep(4); print(\'slept well\')"'
LONG_SLEEP = sys.executable + ' -c "import time; time.sleep(60)"'


class BackgroundTests(ChatCase):
	async def results(self):
		"""The tool results of the parent chat's last model request, by call id."""
		mine = [r for r in self.model.requests() if r["body"].get("model") != "script:ok-text"][-1]
		return {m["tool_call_id"]: m["content"] for m in mine["body"]["messages"] if m["role"] == "tool"}

	async def wake(self, topic):
		"""The turn IED starts by itself to tell of ended tasks: its turn.start, then its end."""
		started = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.start", timeout=TURN_SECONDS))[-1]["data"]
		ended = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=TURN_SECONDS))[-1]["data"]
		return started, ended

	async def test_a_background_command_tells_its_idle_chat_when_it_ends(self):
		chat_id, topic = await self.chat(permission="auto")
		self.model.queue(call_step("shell_run", {"command": SLEEP, "background": True}, "b1"), text_step("Started it."), text_step("It slept well."))
		self.assertEqual("finished", (await self.turn(chat_id, "Run it in the background"))["stop"])
		results = await self.results()
		self.assertIn("Started background task bg-", results["b1"])
		started, ended = await self.wake(topic)
		self.assertEqual("background", started["user"].get("origin"), started)
		self.assertIn("exit 0", started["user"]["text"])
		self.assertIn("slept well", started["user"]["text"], "the end of its output")
		self.assertEqual("finished", ended["stop"])
		told = self.model.requests()[-1]["body"]["messages"]
		self.assertIn("Background tasks you started have ended", told[-1]["content"], "the model reads it as the turn's message")
		snapshot = await self.snapshot(topic)
		self.assertEqual([("done", True)], [(t["state"], t.get("told", False)) for t in snapshot["background"]], snapshot["background"])
		self.assertEqual("exit 0", snapshot["background"][0]["summary"])
		self.assertEqual(0, snapshot["backgroundRunning"])

	async def snapshot(self, topic):
		"""The chat's snapshot as a client that subscribes now gets it."""
		other = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		try:
			await other.sub([topic])
			return (await other.next_frame(topic))["data"]
		finally:
			other.abort()

	async def test_the_ai_waits_for_a_background_task_and_is_not_told_twice(self):
		chat_id, topic = await self.chat(permission="auto")
		self.model.queue(call_step("shell_run", {"command": WAITED_SLEEP, "background": True}, "b1"), call_step("background_status", {}, "s1"),
			call_step("background_wait", {"seconds": 30}, "w1"), text_step("Waited."))
		self.assertEqual("finished", (await self.turn(chat_id, "Run it and wait"))["stop"])
		results = await self.results()
		self.assertIn("running", results["s1"])
		self.assertIn("exit 0", results["w1"])
		self.assertIn("slept well", results["w1"])
		await asyncio.sleep(2)
		history = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"]
		self.assertEqual(1, len(history), "a task the AI waited for wakes nothing")

	async def test_the_owner_stops_a_background_task(self):
		chat_id, topic = await self.chat(permission="auto")
		self.model.queue(call_step("shell_run", {"command": LONG_SLEEP, "background": True}, "b1"), text_step("Started."), text_step("It was stopped."))
		self.assertEqual("finished", (await self.turn(chat_id, "Start it"))["stop"])
		snapshot = await self.snapshot(topic)
		self.assertEqual(["running"], [t["state"] for t in snapshot["background"]])
		self.assertEqual(1, snapshot["backgroundRunning"])
		await self.client.request("chat.stopTask", {"chatId": chat_id, "taskId": snapshot["background"][0]["taskId"]})
		started, ended = await self.wake(topic)
		self.assertIn("stopped", started["user"]["text"])
		self.assertEqual("stopped", (await self.snapshot(topic))["background"][0]["state"])

	async def test_an_agent_runs_as_a_chat_and_its_answer_comes_back(self):
		model = {"endpoint": self.model.url + "/v1", "watchdog": {"prefillBaseS": 2, "idleS": 1, "prefillRate": 1000000}}
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": {"fake": model, "fixed": dict(model, model="script:ok-text")}})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.groups", "value": {"team": {"primary": {"model": "fake"},
			"agents": {"helper": {"model": "fixed", "mode": "ask", "description": "Answers small questions"}}}}})
		chat_id, topic = await self.chat(permission="auto", group="team")
		self.model.queue(call_step("agent_list", {}, "a0"), call_step("agent_run", {"agent": "helper", "task": "Say hello"}, "a1"), text_step("Asked the helper."),
			text_step("The helper said hello."))
		self.assertEqual("finished", (await self.turn(chat_id, "Ask the helper"))["stop"])
		first = [r for r in self.model.requests() if r["body"].get("model") != "script:ok-text"][0]
		system = first["body"]["messages"][0]["content"]
		self.assertIn("# Sub-agents", system)
		self.assertIn("- helper (fixed, local, ask mode): Answers small questions", system)
		results = await self.results()
		self.assertIn("No runs in this workspace yet", results["a0"])
		self.assertIn("Started agent helper (fixed) in chat ", results["a1"])
		self.assertIn("The brief is short", results["a1"], "a short brief gets the reminder")
		started, ended = await self.wake(topic)
		self.assertEqual("background", started["user"].get("origin"))
		self.assertIn("Hello from the fake model.", started["user"]["text"], "the agent's answer")
		self.assertIn("finished on fixed", started["user"]["text"])
		chats = (await self.client.request("chat.list", {"wsId": "w1"}))["chats"]
		agent = [c for c in chats if c.get("agent") == "helper"]
		self.assertEqual(1, len(agent), chats)
		self.assertEqual((chat_id, "ask", "auto", "team"), (agent[0]["parentChatId"], agent[0]["mode"], agent[0]["permission"], agent[0]["group"]))
		helper_turn = (await self.client.request("chat.history", {"chatId": agent[0]["chatId"]}))["turns"][-1]
		self.assertEqual(chat_id, helper_turn["user"]["from"]["chatId"])
		self.model.queue(call_step("agent_list", {}, "a2"), text_step("Listed."))
		self.assertEqual("finished", (await self.turn(chat_id, "How did it do?"))["stop"])
		self.assertIn("1 run - 1 finished, 0 failed, 0 stopped", (await self.results())["a2"])

	async def test_a_chat_on_a_single_model_runs_no_agents(self):
		await self.client.request("settings.set", {"tier": "app", "key": "ai.groups", "value": {"team": {"primary": {"model": "fake"},
			"agents": {"helper": {"model": "fake", "description": "Answers"}}}}})
		chat_id, topic = await self.chat(permission="auto", group="team")
		summary = await self.client.request("chat.set", {"chatId": chat_id, "model": "fake"})
		self.assertEqual(("", "fake"), (summary["group"], summary["pinnedModel"]), "a model alone clears the group")
		self.model.queue(text_step("Alone."))
		await self.turn(chat_id)
		system = self.model.requests()[-1]["body"]["messages"][0]["content"]
		self.assertNotIn("# Sub-agents", system)
		self.assertNotIn("agent_run", [t["function"]["name"] for t in self.model.requests()[-1]["body"].get("tools", [])])
		summary = await self.client.request("chat.set", {"chatId": chat_id, "group": "team"})
		self.assertEqual(("team", ""), (summary["group"], summary["pinnedModel"]), "a group alone clears the model")
