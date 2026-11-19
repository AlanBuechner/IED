"""A chat's privacy (docs/04 "Three settings, each its own"): no cloud models - a cloud model, the chat's own too, is
passed over for a local one, refused to chat_set_model and chat_create, and a turn with no local model left stops and
says so; the solution's setting does it for every chat - hidden from other chats - left out of chat_list, chat_read and
chat_send, its folder refused to the file tools, seen by a chat it started - and no web - no tool of class net or marked
web, no section marked web, and the model told. A chat's own privacy is kept in chat.json across a restart, and a chat
it starts takes it.

The cloud model is a second fake model server, so a request that reached it would show in its log."""

import json
import sys
import os

from iedclient import RpcFailure
from test_chat import ChatCase, FakeModel, call_step, text_step, write

WEB_PLUGIN = {
	"manifestVersion": 1, "id": "demo.web", "name": "Demo Web", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
	"contributes": {
		"systemMessageSections": [{"id": "web", "web": True, "text": "demo_fetch(url) reads a page of the web."}, {"id": "notes", "text": "Demo notes are kept in notes/."}],
		"toolCalls": [
			{"name": "demo_fetch", "description": "Fetch a URL.", "class": "net", "inputSchema": {"type": "object", "properties": {"url": {"type": "string"}}, "required": ["url"]},
				"run": [sys.executable, "-I", "-c", "print('fetched')"]},
			{"name": "demo_page_text", "description": "The text of a page the browser shows.", "class": "read", "web": True, "inputSchema": {"type": "object"},
				"run": [sys.executable, "-I", "-c", "print('page')"]},
			{"name": "demo_notes", "description": "List the notes.", "class": "read", "inputSchema": {"type": "object"},
				"run": [sys.executable, "-I", "-c", "print('notes')"]}]}}

WATCHDOG = {"prefillBaseS": 2, "idleS": 1, "prefillRate": 1000000}


class PrivacyTests(ChatCase):
	def setUp(self):
		self.cloud = FakeModel()
		super().setUp()

	def start_server(self):
		write(os.path.join(self.home, "plugins", "demoweb", "ied-plugin.json"), json.dumps(WEB_PLUGIN))
		return super().start_server()

	def tearDown(self):
		super().tearDown()
		self.cloud.stop()

	async def asyncSetUp(self):
		await super().asyncSetUp()
		await self.models(local=True)

	# The local model first: the default chain is the first model.
	async def models(self, local):
		value = {"fake": {"endpoint": self.model.url + "/v1", "watchdog": WATCHDOG}} if local else {}
		value["cloudy"] = {"endpoint": self.cloud.url + "/v1", "privacy": "cloud", "watchdog": WATCHDOG}
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": value})

	def results(self, model):
		"""Every tool result `model` was sent, by call id."""
		found = {}
		for request in model.requests():
			for message in request["body"]["messages"]:
				if message["role"] == "tool":
					found[message["tool_call_id"]] = message["content"]
		return found

	def tool_names(self, request):
		return [t["function"]["name"] for t in request["body"].get("tools", [])]

	def system(self, request):
		return request["body"]["messages"][0]["content"]

	# --- No cloud models -------------------------------------------------------------------------------------------------

	async def test_a_chat_with_no_cloud_models_passes_its_own_cloud_model_over(self):
		chat_id, topic = await self.chat(mode="ask", permission="auto", model="cloudy", privacy={"noCloudModels": True})
		self.assertEqual({"noCloudModels": True}, (await self.summary(chat_id))["privacy"])
		self.model.queue(call_step("chat_set_model", {"model": "cloudy"}, "m1"), text_step("Done."))
		end = await self.turn(chat_id, "Hello")
		self.assertEqual("finished", end["stop"])
		start = [f for f in self.frames if f.get("kind") == "turn.start"][-1]["data"]
		self.assertEqual("fake", start["model"])
		self.assertEqual([], self.cloud.requests(), "nothing reached the cloud model")
		self.assertIn("cloudy is a cloud model", self.results(self.model)["m1"])
		self.assertIn("# Privacy", self.system(self.model.requests()[0]))
		# The owner turns it off: the chat runs on its own model again.
		await self.client.request("chat.set", {"chatId": chat_id, "privacy": {}})
		self.cloud.queue(text_step("From the cloud."))
		end = await self.turn(chat_id, "Again")
		self.assertEqual(1, len(self.cloud.requests()))
		self.assertEqual({}, (await self.summary(chat_id))["privacy"])

	async def test_the_solutions_setting_stops_a_chat_with_no_local_model(self):
		await self.models(local=False)
		await self.client.request("settings.set", {"tier": "solution", "wsId": "w1", "key": "ai.privacy.noCloudModels", "value": True})
		chat_id, _ = await self.chat(mode="ask", permission="auto")
		end = await self.turn(chat_id)
		self.assertEqual("no_model", end["stop"])
		note = self.segments(await self.last_turn(chat_id), "harness")[0]["text"]
		self.assertIn("runs on local models only", note)
		self.assertIn("(cloudy)", note)
		self.assertEqual([], self.cloud.requests())
		self.assertEqual({}, (await self.summary(chat_id))["privacy"], "the solution's is not the chat's own")
		with self.assertRaises(RpcFailure):
			await self.client.request("settings.set", {"tier": "app", "key": "ai.privacy.noCloudModels", "value": True})

	# --- Hidden from other chats -----------------------------------------------------------------------------------------

	async def test_a_hidden_chat_is_left_out_of_other_chats_tools(self):
		hidden_id, _ = await self.chat(privacy={"hidden": True})
		lead_id, _ = await self.chat(mode="ask", permission="auto")
		self.model.queue(call_step("chat_list", {}, "l1"), call_step("chat_read", {"chat": hidden_id}, "r1"), call_step("chat_send", {"chat": hidden_id, "text": "hi"}, "s1"),
			call_step("fs_read", {"path": ".ied/local/chats/" + hidden_id + "/chat.json"}, "f1"), call_step("fs_read", {"path": ".ied/local/chats/" + lead_id + "/chat.json"}, "f2"),
			text_step("Done."))
		end = await self.turn(lead_id, "Look around")
		self.assertEqual("finished", end["stop"])
		results = self.results(self.model)
		self.assertIn(lead_id, results["l1"])
		self.assertNotIn(hidden_id, results["l1"])
		self.assertIn("there is no chat " + hidden_id, results["r1"])
		self.assertIn("there is no chat " + hidden_id, results["s1"])
		self.assertIn("hidden from this chat", results["f1"])
		self.assertIn('"chatId"', results["f2"], "its own folder is not refused")
		self.assertEqual(0, (await self.summary(hidden_id))["turnCount"], "nothing was sent to it")

	async def test_a_chat_a_private_chat_starts_takes_its_privacy_and_sees_it(self):
		helper_model = FakeModel()
		try:
			await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": {
				"fake": {"endpoint": self.model.url + "/v1", "watchdog": WATCHDOG}, "helper": {"endpoint": helper_model.url + "/v1", "watchdog": WATCHDOG},
				"cloudy": {"endpoint": self.cloud.url + "/v1", "privacy": "cloud", "watchdog": WATCHDOG}}})
			private_id, _ = await self.chat(mode="ask", permission="auto", privacy={"hidden": True, "noWeb": True, "noCloudModels": True})
			self.model.queue(call_step("chat_create", {"title": "Helper", "text": "Sum the column.", "model": "cloudy", "effort": "default"}, "c1"),
				call_step("chat_create", {"title": "Helper", "text": "Sum the column.", "model": "helper", "effort": "default"}, "c2"), text_step("Asked."))
			helper_model.queue(call_step("chat_list", {}, "h1"), text_step("42"))
			end = await self.turn(private_id, "Get it summed")
			self.assertEqual("finished", end["stop"])
			results = self.results(self.model)
			self.assertIn("cloudy is a cloud model", results["c1"])
			chats = (await self.client.request("chat.list", {"wsId": "w1"}))["chats"]
			helper = [c for c in chats if c["chatId"] != private_id][0]
			self.assertEqual(({"noCloudModels": True, "hidden": True, "noWeb": True}, private_id), (helper["privacy"], helper.get("startedBy")))
			await self.client.sub(["chat:" + helper["chatId"]])
			await self.client.wait_for("chat:" + helper["chatId"], lambda f: f.get("kind") == "turn.end" or (f.get("data") or {}).get("state") == "idle", timeout=30)
			self.assertIn(private_id, self.results(helper_model).get("h1", ""), "the chat that started it sees it, hidden as it is")
		finally:
			helper_model.stop()

	# --- No web ----------------------------------------------------------------------------------------------------------

	async def test_a_chat_with_no_web_is_offered_no_web_tool(self):
		open_id, _ = await self.chat(mode="ask", permission="auto")
		closed_id, _ = await self.chat(mode="ask", permission="auto", privacy={"noWeb": True})
		self.model.queue(text_step("Open."))
		await self.turn(open_id)
		opened = self.model.requests()[-1]
		self.assertTrue({"demo_fetch", "demo_page_text", "demo_notes"} <= set(self.tool_names(opened)), self.tool_names(opened))
		self.assertIn("demo_fetch(url) reads a page", self.system(opened))
		self.model.queue(call_step("demo_fetch", {"url": "https://example.com"}, "w1"), text_step("Closed."))
		await self.turn(closed_id)
		closed = self.model.requests()[-2]
		names = self.tool_names(closed)
		self.assertIn("demo_notes", names)
		self.assertNotIn("demo_fetch", names)
		self.assertNotIn("demo_page_text", names)
		system = self.system(closed)
		self.assertNotIn("demo_fetch(url) reads a page", system)
		self.assertIn("Demo notes are kept in notes/.", system)
		self.assertIn("- No web:", system)
		self.assertNotIn("fetched", self.results(self.model)["w1"], "the call did not run")

	async def test_a_chats_privacy_survives_a_restart_and_chat_set_checks_it(self):
		chat_id, _ = await self.chat(privacy={"noWeb": True, "hidden": True})
		for bad in ({"noWeb": "yes"}, {"private": True}, ["noWeb"]):
			with self.assertRaises(RpcFailure):
				await self.client.request("chat.set", {"chatId": chat_id, "privacy": bad})
		self.client.abort()
		self.server.stop(remove_data=False)
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()
		self.assertEqual({"hidden": True, "noWeb": True}, (await self.summary(chat_id))["privacy"])
