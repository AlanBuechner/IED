"""Claude through the owner's Claude Code CLI (docs/04 "The Claude Code provider") against tools/fake-claude: a turn streams
and its tool calls run through IED's own path, the session is kept between turns and resumed after a restart, the plan's
usage reaches the aiusage topic, pacing picks the model, a usage limit moves the turn to the fallback, a pinned model
wins over the group's, and Claude runs on the subscription only - never an API key."""

import datetime
import json
import os
import sys
import time

from test_chat import ChatCase, call_step, text_step
from server import REPO

FAKE_CLAUDE = os.path.join(REPO, "tools", "fake-claude", "fake_claude.py")
HOUR = 3600


def iso(seconds_from_now):
	return datetime.datetime.fromtimestamp(time.time() + seconds_from_now, datetime.timezone.utc).isoformat()


def plan_usage(session=None, week=None, opus=None):
	"""get_usage's answer for a Max plan: each window (utilization, seconds until it resets) or None."""
	def window(value):
		return {"utilization": value[0], "resets_at": iso(value[1])} if value else None
	return {"session": {}, "subscription_type": "max", "rate_limits_available": True, "behaviors": None,
		"rate_limits": {"five_hour": window(session), "seven_day": window(week), "seven_day_opus": window(opus), "seven_day_sonnet": None}}


class ClaudeCase(ChatCase):
	def setUp(self):
		super().setUp()
		self.state = os.path.join(self.root, "claude")
		os.makedirs(os.path.join(self.state, "sessions"))

	def put(self, name, value):
		with open(os.path.join(self.state, name), "w", encoding="utf-8") as f:
			json.dump(value, f)

	def script(self, *responses):
		self.put("script.json", {"responses": list(responses)})

	def log(self):
		path = os.path.join(self.state, "log.jsonl")
		if not os.path.exists(path):
			return []
		with open(path, encoding="utf-8") as f:
			return [json.loads(line) for line in f if line.strip()]

	def sessions_started(self):
		return [entry["argv"] for entry in self.log() if "argv" in entry and "-p" in entry["argv"] and "--no-session-persistence" not in entry["argv"]]

	def received(self, kind):
		return [entry["in"] for entry in self.log() if "in" in entry and entry["in"].get("type") == kind]

	def tool_results(self):
		return {entry["toolResult"]["id"]: entry["toolResult"] for entry in self.log() if "toolResult" in entry}

	async def use_claude(self, models=None, groups=None, default_group=None):
		await self.client.request("settings.set", {"tier": "app", "key": "ai.claude.command", "value": [sys.executable, "-I", FAKE_CLAUDE, "--state", self.state]})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.claude.usageRefreshMinutes", "value": 0})
		fake = {"endpoint": self.model.url + "/v1", "watchdog": {"prefillBaseS": 2, "idleS": 1, "prefillRate": 1000000}}
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": dict({"claude-opus": {"provider": "claude-code", "model": "opus"}, "fake": fake}, **(models or {}))})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.groups", "value": groups or {"claude": {"primary": {"model": "claude-opus", "fallback": ["fake"]}}}})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.defaultGroup", "value": default_group or "claude"})

	def notes(self, turn, kind="routing"):
		return [s["text"] for s in turn["segments"] if s["type"] == "harness" and s.get("kind") == kind]


class ClaudeTurnTests(ClaudeCase):
	async def test_a_turn_streams_and_its_tools_run_through_ied(self):
		await self.use_claude()
		self.script({"blocks": [{"thinking": "Look at main first."}, {"text": "Reading it."}, {"tool": "fs_read", "input": {"path": "src/main.c"}, "id": "toolu_a"}]},
			{"blocks": [{"text": "It returns 0."}]})
		chat_id, _ = await self.chat()
		end = await self.turn(chat_id, "What does main return?")
		self.assertEqual("finished", end["stop"], end)
		turn = await self.last_turn(chat_id)
		self.assertEqual("claude-opus", turn["model"])
		self.assertEqual(["Look at main first."], [s["text"] for s in self.segments(turn, "thinking")])
		self.assertEqual(["Reading it.", "It returns 0."], [s["text"].strip() for s in self.segments(turn, "text")])
		tool = self.segments(turn, "tool")[0]
		self.assertEqual(("fs_read", "ok"), (tool["tool"], tool["state"]), tool)
		self.assertIn("return 0", self.tool_results()["toolu_a"]["result"]["content"][0]["text"], "IED's fs_read answered the CLI's tools/call")
		argv = self.sessions_started()
		self.assertEqual(1, len(argv), argv)
		for flag in ("-p", "--include-partial-messages", "--strict-mcp-config", "--system-prompt-file", "--permission-prompt-tool"):
			self.assertIn(flag, argv[0])
		self.assertEqual("", argv[0][argv[0].index("--tools") + 1], "no built-in tools")
		self.assertEqual("opus", argv[0][argv[0].index("--model") + 1])
		system = open(argv[0][argv[0].index("--system-prompt-file") + 1], encoding="utf-8").read()
		self.assertIn("fs_read", system, "IED's system message")
		tools = [entry["tools"] for entry in self.log() if "tools" in entry][-1]
		self.assertIn("fs_read", tools)
		self.assertNotIn("tool_search", tools, "every tool is loaded; the CLI defers MCP tools itself")
		started = [entry for entry in self.log() if "argv" in entry and "-p" in entry["argv"]][0]
		self.assertEqual("1", started["env"].get("CLAUDE_CODE_DISABLE_CLAUDE_MDS"), "the CLI reads no CLAUDE.md or AGENTS.md of its own")
		header = json.load(open(os.path.join(self.local_state(), "chats", chat_id, "chat.json"), encoding="utf-8"))
		self.assertTrue(header["claude"]["session"], header)
		self.assertEqual([turn["turnId"]], header["claude"]["turns"])
		context = [f for f in self.frames if f.get("kind") == "context.changed"][-1]["data"]
		self.assertEqual(1000000, context["window"], "the window the CLI reports")

	def local_state(self):
		from server import local_state
		return local_state(self.data, self.repo)

	async def test_the_session_is_kept_between_turns_and_resumed_after_a_restart(self):
		await self.use_claude()
		self.script({"blocks": [{"text": "One."}]}, {"blocks": [{"text": "Two."}]}, {"blocks": [{"text": "Three."}]})
		chat_id, _ = await self.chat()
		self.assertEqual("finished", (await self.turn(chat_id, "first"))["stop"])
		self.assertEqual("finished", (await self.turn(chat_id, "second"))["stop"])
		self.assertEqual(1, len(self.sessions_started()), "one process for both turns")
		queries = [entry["query"] for entry in self.log() if "query" in entry]
		self.assertEqual("second", queries[1][0]["text"], "the session has the first turn: no history is retold")
		session = json.load(open(os.path.join(self.local_state(), "chats", chat_id, "chat.json"), encoding="utf-8"))["claude"]["session"]
		self.server.stop(remove_data=False)
		self.client.abort()
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()
		await self.client.sub(["chat:" + chat_id])
		self.assertEqual("finished", (await self.turn(chat_id, "third"))["stop"])
		argv = self.sessions_started()[-1]
		self.assertEqual(session, argv[argv.index("--resume") + 1], "the new server resumes the chat's session")

	async def test_a_chat_that_started_on_another_model_tells_claude_what_happened(self):
		await self.use_claude(groups={"local": {"primary": {"model": "fake"}}, "claude": {"primary": {"model": "claude-opus"}}}, default_group="local")
		chat_id, _ = await self.chat()
		self.model.queue(text_step("Local answer."))
		self.assertEqual("finished", (await self.turn(chat_id, "Hi local"))["stop"])
		await self.client.request("chat.set", {"chatId": chat_id, "group": "claude"})
		self.script({"blocks": [{"text": "Claude here."}]})
		self.assertEqual("finished", (await self.turn(chat_id, "Hi Claude"))["stop"])
		first = [entry["query"] for entry in self.log() if "query" in entry][0][0]["text"]
		self.assertIn("Earlier in this chat", first)
		self.assertIn("Hi local", first)
		self.assertIn("Local answer.", first)
		self.assertTrue(first.rstrip().endswith("Hi Claude"), first)

	async def test_a_pinned_model_wins_over_the_groups(self):
		await self.use_claude(groups={"local": {"primary": {"model": "fake"}}}, default_group="local")
		chat_id, _ = await self.chat()
		await self.client.request("chat.set", {"chatId": chat_id, "model": "claude-opus"})
		self.assertEqual("claude-opus", (await self.summary(chat_id))["pinnedModel"])
		self.script({"blocks": [{"text": "Pinned."}]})
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		self.assertEqual("claude-opus", (await self.last_turn(chat_id))["model"])
		with self.assertRaises(Exception):
			await self.client.request("chat.set", {"chatId": chat_id, "model": "no-such-model"})

	async def test_steering_mid_query_rides_with_the_next_tool_result(self):
		await self.use_claude()
		self.script({"blocks": [{"text": "Writing it."}, {"tool": "fs_write", "input": {"path": "notes.txt", "content": "hi\n"}, "id": "toolu_w"}]},
			{"blocks": [{"tool": "fs_read", "input": {"path": "src/main.c"}, "id": "toolu_r"}]},
			{"blocks": [{"text": "Done, with tabs."}]})
		chat_id, topic = await self.chat()
		await self.client.request("chat.send", {"chatId": chat_id, "text": "Write notes"})
		approval = (await self.client.wait_for(topic, lambda f: f.get("kind") == "approval.request", timeout=30))[-1]["data"]
		await self.client.request("chat.send", {"chatId": chat_id, "text": "use tabs", "as": "inject"})
		await self.client.request("chat.approve", {"chatId": chat_id, "approvalId": approval["approvalId"], "decision": "allow"})
		end = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=30))[-1]["data"]
		self.assertEqual("finished", end["stop"], end)
		self.assertEqual(1, len(self.received("user")), "nothing is sent while a query runs")
		results = self.tool_results()
		self.assertNotIn("use tabs", json.dumps(results["toolu_w"]))
		self.assertIn("use tabs", results["toolu_r"]["result"]["content"][0]["text"], "the steering reached Claude with the next tool result")

	async def test_steering_during_the_last_answer_is_sent_before_the_turn_ends(self):
		await self.use_claude()
		self.script({"blocks": [{"text": " ".join(["word"] * 200)}], "delayMs": 20}, {"blocks": [{"text": "Using tabs."}]})
		chat_id, topic = await self.chat()
		await self.client.request("chat.send", {"chatId": chat_id, "text": "explain"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.delta", timeout=30)
		await self.client.request("chat.send", {"chatId": chat_id, "text": "use tabs", "as": "inject"})
		end = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=60))[-1]["data"]
		self.assertEqual("finished", end["stop"], end)
		queries = [entry["query"][0]["text"] for entry in self.log() if "query" in entry]
		self.assertEqual(2, len(queries), queries)
		self.assertIn("use tabs", queries[1], "sent as a message once the first query ended")

	async def test_stop_interrupts_the_cli(self):
		await self.use_claude()
		self.script({"blocks": [{"text": " ".join(["word"] * 400)}], "delayMs": 50})
		chat_id, topic = await self.chat()
		await self.client.request("chat.send", {"chatId": chat_id, "text": "Talk"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.delta", timeout=30)
		await self.client.request("chat.stop", {"chatId": chat_id})
		frames = await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=30)
		self.assertEqual("user_cancelled", frames[-1]["data"]["stop"])
		self.assertIn("interrupt", [m["request"]["subtype"] for m in self.received("control_request")])


class ClaudeUsageTests(ClaudeCase):
	async def test_the_plans_usage_reaches_the_clients(self):
		await self.use_claude()
		self.put("usage.json", plan_usage(session=(42.5, 2 * HOUR), week=(61, 3 * 24 * HOUR)))
		usage = await self.client.request("ai.usage", {"refresh": True})
		claude = usage["claude"]
		self.assertTrue(claude["configured"])
		self.assertEqual(("2.1.304", "signed_in", "claude.ai", True), (claude["cli"]["version"], claude["cli"]["signIn"], claude["cli"]["authMethod"], claude["cli"]["subscription"]))
		windows = {w["id"]: w for w in claude["usage"]["windows"]}
		self.assertEqual("max", claude["usage"]["plan"])
		self.assertAlmostEqual(42.5, windows["five_hour"]["utilization"])
		self.assertAlmostEqual(61, windows["seven_day"]["utilization"])
		self.assertAlmostEqual(0.6, windows["five_hour"]["through"], places=2)
		self.assertAlmostEqual(42.5 / 60, windows["five_hour"]["pace"], places=2)
		await self.client.sub(["aiusage"])
		snapshot = await self.client.next_frame("aiusage")
		self.assertAlmostEqual(61, {w["id"]: w for w in snapshot["data"]["claude"]["usage"]["windows"]}["seven_day"]["utilization"])

	async def test_a_turn_updates_the_usage_from_its_events_and_its_end(self):
		await self.use_claude()
		await self.client.sub(["aiusage"])
		await self.client.next_frame("aiusage")
		self.put("usage.json", plan_usage(session=(30, 4 * HOUR), week=(20, 6 * 24 * HOUR)))
		self.script({"blocks": [{"text": "Hi."}], "rateLimit": {"status": "allowed_warning", "rateLimitType": "seven_day", "utilization": 0.8, "resetsAt": int(time.time()) + 24 * HOUR}})
		chat_id, _ = await self.chat()
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		frames = await self.client.wait_for("aiusage", lambda f: f.get("kind") == "usage.changed" and f["data"]["claude"]["usage"]["status"] == "allowed_warning", timeout=10)
		self.assertTrue(frames)
		final = await self.client.request("ai.usage", {})
		self.assertAlmostEqual(30, {w["id"]: w for w in final["claude"]["usage"]["windows"]}["five_hour"]["utilization"], msg="the session's own get_usage after the turn")

	async def test_pacing_picks_the_model_for_the_turn(self):
		await self.use_claude(models={"claude-auto": {"provider": "claude-code", "model": "auto", "pacing": {"ladder": ["opus", "sonnet", "haiku"], "target": "both"}}},
			groups={"claude": {"primary": {"model": "claude-auto", "fallback": ["fake"]}}})
		self.put("usage.json", plan_usage(session=(10, 4 * HOUR), week=(60, int(3.5 * 24 * HOUR))))
		await self.client.request("ai.usage", {"refresh": True})
		self.script({"blocks": [{"text": "Paced."}]})
		chat_id, _ = await self.chat()
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		turn = await self.last_turn(chat_id)
		self.assertTrue(any(n.startswith("claude-auto: sonnet (the week is at 1.20x pace") for n in self.notes(turn)), turn["segments"])
		argv = self.sessions_started()[-1]
		self.assertEqual("sonnet", argv[argv.index("--model") + 1])

	async def test_a_usage_limit_moves_the_turn_to_the_fallback(self):
		await self.use_claude()
		self.script({"error": "rate_limit", "text": "Claude AI usage limit reached|%d" % (int(time.time()) + 2 * HOUR)})
		self.model.queue(text_step("The local model took over."))
		chat_id, _ = await self.chat()
		end = await self.turn(chat_id)
		self.assertEqual("finished", end["stop"], end)
		turn = await self.last_turn(chat_id)
		self.assertTrue(any("switched to fake" in n and "usage limit" in n for n in self.notes(turn)), turn["segments"])
		usage = await self.client.request("ai.usage", {})
		self.assertGreater(usage["claude"]["blockedUntilMs"], time.time() * 1000 + HOUR * 1000)
		self.model.queue(text_step("Still local."))
		self.assertEqual("finished", (await self.turn(chat_id, "again"))["stop"])
		self.assertTrue(any(n.startswith("claude-opus left out") for n in self.notes(await self.last_turn(chat_id))), "the next turn skips Claude until it frees")

	async def test_a_usage_limit_without_a_fallback_pauses(self):
		await self.use_claude(groups={"claude": {"primary": {"model": "claude-opus"}}})
		self.script({"error": "rate_limit", "text": "Claude AI usage limit reached|%d" % (int(time.time()) + HOUR)})
		chat_id, _ = await self.chat()
		end = await self.turn(chat_id)
		self.assertEqual("usage_limit", end["stop"], end)
		self.assertEqual("paused", (await self.summary(chat_id))["state"])


class ClaudeSetupTests(ClaudeCase):
	"""docs/04 "Setting it up": a CLI signed in to a subscription sets Claude up by itself, once; Sign in opens the CLI's own
	login in a window of its own and watches it; Install is only for a machine without a CLI."""

	async def local_only(self):
		await self.client.request("settings.set", {"tier": "app", "key": "ai.claude.command", "value": [sys.executable, "-I", FAKE_CLAUDE, "--state", self.state]})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": {"fake": {"endpoint": self.model.url + "/v1"}}})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.groups", "value": {"local": {"primary": {"model": "fake"}}}})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.defaultGroup", "value": "local"})

	async def setting(self, key):
		return (await self.client.request("settings.get", {"wsId": "w1"}))["values"][key]["value"]

	async def test_a_cli_signed_in_to_a_subscription_sets_claude_up_by_itself_once(self):
		await self.local_only()
		usage = await self.client.request("ai.usage", {"refresh": True})
		self.assertTrue(usage["claude"]["configured"] and usage["claude"]["offered"], usage["claude"])
		models = await self.setting("ai.models")
		self.assertEqual(["claude-auto", "claude-fable", "claude-haiku", "claude-opus", "claude-sonnet", "fake"], sorted(models))
		self.assertEqual("auto", models["claude-auto"]["model"])
		groups = await self.setting("ai.groups")
		self.assertEqual({"primary": {"model": "claude-auto", "fallback": ["fake"]}}, groups["claude"], "falling back to the local model")
		self.assertEqual("local", await self.setting("ai.defaultGroup"), "the owner's default group is left as it is")
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": {"fake": {"endpoint": self.model.url + "/v1"}}})
		self.assertFalse((await self.client.request("ai.usage", {"refresh": True}))["claude"]["configured"], "removed Claude models stay removed")
		self.assertTrue((await self.client.request("ai.claude.setup", {}))["claude"]["configured"], "Add Claude models puts them back")

	async def test_sign_in_opens_the_cli_login_and_sets_claude_up(self):
		await self.local_only()
		self.put("auth.json", {"loggedIn": False, "authMethod": "none", "apiProvider": "firstParty"})
		await self.client.sub(["aiusage"])
		await self.client.next_frame("aiusage")
		self.assertFalse((await self.client.request("ai.usage", {"refresh": True}))["claude"]["configured"], "not signed in: nothing set up")
		started = await self.client.request("ai.claude.signIn", {})
		self.assertTrue(started["claude"]["cli"]["signingIn"], started["claude"]["cli"])
		again = await self.client.request("ai.claude.signIn", {})
		self.assertTrue(again["claude"]["cli"]["signingIn"], "a second click while the window is open opens no other")
		frames = await self.client.wait_for("aiusage", lambda f: f.get("kind") == "usage.changed" and f["data"]["claude"]["configured"] and not f["data"]["claude"]["cli"]["signingIn"], timeout=40)
		self.assertEqual(("signed_in", "claude.ai", True), tuple(frames[-1]["data"]["claude"]["cli"][k] for k in ("signIn", "authMethod", "subscription")), "done means signed in, on the page at once")
		logins = [entry for entry in self.log() if "argv" in entry and entry["argv"][:2] == ["auth", "login"]]
		self.assertEqual(1, len(logins), logins)
		self.assertEqual(["auth", "login", "--claudeai"], logins[0]["argv"], "the subscription login, not the Console's")
		self.assertEqual(([], []), (logins[0]["billed"], logins[0]["inherited"]), "the login window gets IED's clean environment")
		self.assertIn("claude-auto", await self.setting("ai.models"))

	async def test_install_is_refused_when_a_cli_is_there(self):
		await self.local_only()
		with self.assertRaises(Exception) as refused:
			await self.client.request("ai.claude.install", {})
		self.assertIn("installed already", str(refused.exception))


class ClaudeToolTests(ClaudeCase):
	async def test_the_ai_reads_the_usage_and_pins_a_model_for_another_chat(self):
		await self.use_claude(groups={"local": {"primary": {"model": "fake"}}}, default_group="local")
		self.put("usage.json", plan_usage(session=(50, int(2.5 * HOUR)), week=(20, 6 * 24 * HOUR)))
		await self.client.request("ai.usage", {"refresh": True})
		lead, _ = await self.chat(permission="auto")
		helper, _ = await self.chat()
		self.model.queue(call_step("ai_usage", {}, "c1"), call_step("chat_set_model", {"model": "claude-opus", "chat": helper}, "c2"), text_step("Done."))
		self.assertEqual("finished", (await self.turn(lead))["stop"])
		sent = self.model.requests()[-1]["body"]
		results = {m["tool_call_id"]: m["content"] for m in sent["messages"] if m["role"] == "tool"}
		self.assertIn("Session (5 h): 50% used, resets in 2 h 30 min; 50% through; pace 1.00x", results["c1"])
		self.assertIn("on the Claude subscription; plan max", results["c1"])
		self.assertIn("- claude-opus: Claude opus", results["c1"])
		self.assertIn("next turns use claude-opus", results["c2"])
		self.assertEqual("claude-opus", (await self.summary(helper))["pinnedModel"])
		self.model.queue(call_step("chat_set_model", {"model": "gpt-nothing"}, "c3"), text_step("Done."))
		await self.turn(lead, "again")
		results = {m["tool_call_id"]: m["content"] for m in self.model.requests()[-1]["body"]["messages"] if m["role"] == "tool"}
		self.assertIn("there is no model gpt-nothing", results["c3"])
		self.script({"blocks": [{"text": "Helper on Claude."}]})
		self.model.queue(call_step("chat_create", {"text": "Rename the variables", "title": "Helper", "model": "claude-opus"}, "c4"), text_step("Started."))
		await self.turn(lead, "hand it out")
		made = [c for c in (await self.client.request("chat.list", {"wsId": "w1"}))["chats"] if c["title"] == "Helper"][0]
		self.assertEqual("claude-opus", made["pinnedModel"], "a helper started on the model the lead named")


class ClaudeSubscriptionTests(ClaudeCase):
	# The server is started with an API key and a parent Claude Code session's variables (bare mode) in its environment.
	PARENT = {"ANTHROPIC_API_KEY": "sk-ant-not-a-real-key", "CLAUDE_CODE_SIMPLE": "1", "CLAUDECODE": "1", "CLAUDE_CODE_SESSION_ID": "parent"}

	def setUp(self):
		saved = {k: os.environ.get(k) for k in self.PARENT}
		os.environ.update(self.PARENT)
		try:
			super().setUp()
		finally:
			for key, value in saved.items():
				if value is None:
					os.environ.pop(key, None)
				else:
					os.environ[key] = value

	async def test_an_api_key_or_a_parent_session_in_the_environment_never_reaches_the_cli(self):
		await self.use_claude()
		self.script({"blocks": [{"text": "On the subscription."}]})
		chat_id, _ = await self.chat()
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		for entry in self.log():
			if "billed" in entry:
				self.assertEqual(([], []), (entry["billed"], entry["inherited"]), entry["argv"])
		self.assertTrue(self.sessions_started())

	async def test_a_cli_signed_in_with_an_api_key_is_refused(self):
		await self.use_claude(groups={"claude": {"primary": {"model": "claude-opus"}}})
		self.put("auth.json", {"loggedIn": True, "authMethod": "api_key", "apiProvider": "firstParty"})
		chat_id, _ = await self.chat()
		end = await self.turn(chat_id)
		self.assertEqual("not_subscription", end["stop"], end)
		self.assertEqual([], self.sessions_started(), "no session was started")
		self.assertEqual([], self.received("user"), "nothing was sent")
		usage = await self.client.request("ai.usage", {})
		self.assertFalse(usage["claude"]["cli"]["subscription"])

	async def test_a_session_that_reports_an_api_key_is_stopped_and_the_turn_falls_back(self):
		await self.use_claude()
		self.put("auth.json", {"loggedIn": True, "authMethod": "claude.ai", "apiKeySource": "apiKeyHelper"})
		self.script({"blocks": [{"text": "Should never stream."}]})
		self.model.queue(text_step("Local."))
		chat_id, _ = await self.chat()
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		turn = await self.last_turn(chat_id)
		self.assertTrue(any("API key" in n and "switched to fake" in n for n in self.notes(turn)), turn["segments"])
		self.assertNotIn("Should never stream.", " ".join(s["text"] for s in self.segments(turn, "text")))
