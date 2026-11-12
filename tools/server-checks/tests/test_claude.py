"""Claude through the owner's Claude Code CLI (docs/04 "The Claude Code provider") against tools/fake-claude: a turn streams
and its tool calls run through IED's own path, the session is kept between turns and resumed after a restart, the plan's
usage reaches the aiusage topic, pacing walks the default chain's Claude ladder (over the work week when
ai.claude.paceDays says; Opus alone without ai.claude.stepDown), ai.fallbackModel ends the chain, a window IED reads as
used up leaves no Claude model out - only Claude's own refusal moves a turn down its chain, and the next turn asks Claude
again -, the shared week paces Fable too, a pinned model wins over the default chain, the models the CLI offers are added
to ai.models once each, the old model groups move into ai.agents and ai.defaultModel, and Claude runs on the
subscription only - never an API key."""

import datetime
import importlib.util
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


def plan_usage(session=None, week=None, opus=None, fable=None):
	"""get_usage's answer for a Max plan: each window (utilization, seconds until it resets) or None; `fable` the
	model_scoped week, Fable's own limit on top of the shared one."""
	def window(value):
		return {"utilization": value[0], "resets_at": iso(value[1])} if value else None
	limits = {"five_hour": window(session), "seven_day": window(week), "seven_day_opus": window(opus), "seven_day_sonnet": None}
	if fable:
		limits["model_scoped"] = [dict(window(fable), display_name="Fable")]
	return {"session": {}, "subscription_type": "max", "rate_limits_available": True, "behaviors": None, "rate_limits": limits}


def offered_models():
	"""The models the fake CLI offers unless models.json says others (MODELS in tools/fake-claude/fake_claude.py)."""
	spec = importlib.util.spec_from_file_location("fake_claude", FAKE_CLAUDE)
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module.MODELS


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

	async def use_claude(self, models=None, step_down=False, default_model="", fallback_model=""):
		"""Claude through the fake CLI: ai.models has claude-opus and the local model fake, then `models`. The default chain
		(docs/04 "Which model a turn uses") is ai.defaultModel - empty: Claude's ladder over the Claude profiles there are,
		which without `step_down` (ai.claude.stepDown) is claude-opus alone, with no pace - then ai.fallbackModel: empty,
		the first model that is not Claude's (fake) behind a Claude chain."""
		await self.client.request("settings.set", {"tier": "app", "key": "ai.claude.command", "value": [sys.executable, "-I", FAKE_CLAUDE, "--state", self.state]})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.claude.usageRefreshMinutes", "value": 0})
		fake = {"endpoint": self.model.url + "/v1", "watchdog": {"prefillBaseS": 2, "idleS": 1, "prefillRate": 1000000}}
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": dict({"claude-opus": {"provider": "claude-code", "model": "opus"}, "fake": fake}, **(models or {}))})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.claude.stepDown", "value": step_down})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.defaultModel", "value": default_model})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.fallbackModel", "value": fallback_model})

	async def setting(self, key):
		return (await self.client.request("settings.get", {"wsId": "w1"}))["values"][key]["value"]

	def chain(self, usage):
		"""The default chain in an aiusage snapshot: the one chain it holds."""
		chains = usage["claude"]["chains"]
		self.assertEqual([("default", "primary")], [(c["group"], c["role"]) for c in chains])
		return chains[0]

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
		counted = [f["data"] for f in self.frames if f.get("kind") == "context.changed" and f["data"].get("source") == "claude"]
		if not counted:
			counted = [(await self.client.wait_for("chat:" + chat_id, lambda f: f.get("kind") == "context.changed" and f["data"].get("source") == "claude", timeout=20))[-1]["data"]]
		self.assertEqual((41234, 1000000), (counted[-1]["used"], counted[-1]["window"]), "the turn ends on the CLI's own count (get_context_usage)")
		self.assertEqual(["System prompt", "Tool definitions", "Messages", "Tool results"], [p["name"] for p in counted[-1]["parts"]], "IED's parts")
		self.assertEqual(41234, sum(p["tokens"] for p in counted[-1]["parts"]), "split across the CLI's count")

	def local_state(self):
		from server import local_state
		return local_state(self.data, self.repo)

	async def test_effort_goes_to_the_cli_and_a_new_one_restarts_the_session(self):
		await self.use_claude()
		self.script({"blocks": [{"text": "One."}]}, {"blocks": [{"text": "Two."}]})
		chat_id, _ = await self.chat(effort="xhigh")
		self.assertEqual("finished", (await self.turn(chat_id, "first"))["stop"])
		argv = self.sessions_started()[-1]
		self.assertEqual("xhigh", argv[argv.index("--effort") + 1], "the effort the chat was made with")
		await self.client.request("chat.set", {"chatId": chat_id, "effort": "max"})
		self.assertEqual("finished", (await self.turn(chat_id, "second"))["stop"])
		self.assertEqual(2, len(self.sessions_started()), "a new effort, a new process")
		argv = self.sessions_started()[-1]
		self.assertEqual("max", argv[argv.index("--effort") + 1], "the chat's pick")
		self.assertIn("--resume", argv, "the same conversation")
		self.assertEqual("max", (await self.last_turn(chat_id)).get("effort"))

	async def test_a_shown_chat_without_claudes_count_asks_its_resumed_session(self):
		"""docs/04 "Context management": a Claude chat whose meter is not the CLI's own (its last turn ran on an older
		server) has its session resumed when it is shown, and the meter becomes the CLI's get_context_usage - no query."""
		await self.use_claude()
		self.script({"blocks": [{"text": "One."}]})
		chat_id, _ = await self.chat()
		self.assertEqual("finished", (await self.turn(chat_id, "first"))["stop"])
		header_path = os.path.join(self.local_state(), "chats", chat_id, "chat.json")
		self.server.stop(remove_data=False)
		self.client.abort()
		with open(header_path, encoding="utf-8") as f:
			header = json.load(f)
		del header["context"]
		with open(header_path, "w", encoding="utf-8") as f:
			json.dump(header, f)
		queries = len([entry for entry in self.log() if "query" in entry])
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()
		await self.client.sub(["chat:" + chat_id])
		first = (await self.client.next_frame("chat:" + chat_id))["data"]["context"]
		self.assertNotEqual("claude", first.get("source"), "the last turn's count until Claude answers")
		counted = (await self.client.wait_for("chat:" + chat_id, lambda f: f.get("kind") == "context.changed", timeout=30))[-1]["data"]
		self.assertEqual(("claude", 41234, 1000000), (counted.get("source"), counted["used"], counted["window"]), "Claude's own count")
		self.assertIn("--resume", self.sessions_started()[-1], "the chat's own session")
		self.assertEqual(queries, len([entry for entry in self.log() if "query" in entry]), "no query was made")

	async def test_the_session_is_kept_between_turns_and_resumed_after_a_restart(self):
		await self.use_claude()
		self.script({"blocks": [{"text": "One."}]}, {"blocks": [{"text": "Two."}]}, {"blocks": [{"text": "Three."}]})
		chat_id, _ = await self.chat(title="Kept")
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

	async def test_a_revert_leaves_the_session_and_a_new_one_is_told_what_the_chat_kept(self):
		"""docs/04 "Reverting a chat": the CLI's session holds the dropped turns, so it is not resumed; the next turn starts a
		new session, told the turns kept and none of the dropped."""
		await self.use_claude()
		self.script({"blocks": [{"text": "One."}]}, {"blocks": [{"text": "Two."}]}, {"blocks": [{"text": "Two again."}]})
		chat_id, _ = await self.chat()
		self.assertEqual("finished", (await self.turn(chat_id, "first"))["stop"])
		self.assertEqual("finished", (await self.turn(chat_id, "second"))["stop"])
		second = await self.last_turn(chat_id)
		await self.client.request("chat.revert", {"chatId": chat_id, "turnId": second["turnId"]})
		header = json.load(open(os.path.join(self.local_state(), "chats", chat_id, "chat.json"), encoding="utf-8"))
		self.assertNotIn("claude", header, "the chat no longer names the session")
		self.assertEqual("finished", (await self.turn(chat_id, "second, said again"))["stop"])
		argv = self.sessions_started()
		self.assertEqual(2, len(argv), "a new process for a new session")
		self.assertNotIn("--resume", argv[-1], "the session that holds the dropped turn is not resumed")
		first = [entry["query"] for entry in self.log() if "query" in entry][-1][0]["text"]
		self.assertIn("Earlier in this chat", first)
		self.assertIn("first", first)
		self.assertIn("One.", first)
		self.assertNotIn("Two.", first, "the dropped turn is not told")
		self.assertIn("second, said again\n\nThis chat has no name yet", first, "a chat with no name is asked for one, on Claude too")
		self.assertEqual(1, first.count("no name yet"), "what retells the chat leaves that out")

	async def test_a_chat_that_started_on_another_model_tells_claude_what_happened(self):
		await self.use_claude(default_model="fake")
		chat_id, _ = await self.chat(title="Two models")
		self.model.queue(text_step("Local answer."))
		self.assertEqual("finished", (await self.turn(chat_id, "Hi local"))["stop"])
		self.assertEqual("fake", (await self.last_turn(chat_id))["model"], "ai.defaultModel")
		await self.client.request("chat.set", {"chatId": chat_id, "model": "claude-opus"})
		self.script({"blocks": [{"text": "Claude here."}]})
		self.assertEqual("finished", (await self.turn(chat_id, "Hi Claude"))["stop"])
		first = [entry["query"] for entry in self.log() if "query" in entry][0][0]["text"]
		self.assertIn("Earlier in this chat", first)
		self.assertIn("Hi local", first)
		self.assertIn("Local answer.", first)
		self.assertTrue(first.rstrip().endswith("Hi Claude"), first)

	async def test_a_pinned_model_wins_over_the_default_model(self):
		await self.use_claude(default_model="fake")
		chat_id, _ = await self.chat()
		await self.client.request("chat.set", {"chatId": chat_id, "model": "claude-opus"})
		self.assertEqual("claude-opus", (await self.summary(chat_id))["pinnedModel"])
		self.script({"blocks": [{"text": "Pinned."}]})
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		self.assertEqual("claude-opus", (await self.last_turn(chat_id))["model"])
		with self.assertRaises(Exception):
			await self.client.request("chat.set", {"chatId": chat_id, "model": "no-such-model"})

	async def test_a_message_mid_query_rides_with_the_steps_last_tool_result(self):
		await self.use_claude()
		self.script({"blocks": [{"text": "Writing it."}, {"tool": "fs_write", "input": {"path": "notes.txt", "content": "hi\n"}, "id": "toolu_w"}]},
			{"blocks": [{"tool": "fs_read", "input": {"path": "src/main.c"}, "id": "toolu_r"}]},
			{"blocks": [{"text": "Done, with tabs."}]})
		chat_id, topic = await self.chat()
		await self.client.request("chat.send", {"chatId": chat_id, "text": "Write notes"})
		approval = (await self.client.wait_for(topic, lambda f: f.get("kind") == "approval.request", timeout=30))[-1]["data"]
		await self.client.request("chat.send", {"chatId": chat_id, "text": "use tabs"})
		await self.client.request("chat.approve", {"chatId": chat_id, "approvalId": approval["approvalId"], "decision": "allow"})
		end = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=30))[-1]["data"]
		self.assertEqual("finished", end["stop"], end)
		self.assertEqual(1, len(self.received("user")), "nothing is sent while a query runs")
		results = self.tool_results()
		self.assertIn("[Message from the user, sent while you were working]: use tabs", results["toolu_w"]["result"]["content"][0]["text"],
			"it reached Claude with the result of the round it came in, saying whose it is")
		self.assertNotIn("use tabs", json.dumps(results["toolu_r"]))

	async def test_a_message_during_the_last_answer_is_sent_before_the_turn_ends(self):
		await self.use_claude()
		self.script({"blocks": [{"text": " ".join(["word"] * 200)}], "delayMs": 20}, {"blocks": [{"text": "Using tabs."}]})
		chat_id, topic = await self.chat()
		await self.client.request("chat.send", {"chatId": chat_id, "text": "explain"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.delta", timeout=30)
		await self.client.request("chat.send", {"chatId": chat_id, "text": "use tabs"})
		end = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=60))[-1]["data"]
		self.assertEqual("finished", end["stop"], end)
		queries = [entry["query"][0]["text"] for entry in self.log() if "query" in entry]
		self.assertEqual(2, len(queries), queries)
		self.assertEqual("use tabs", queries[1], "sent as a plain message once the first query ended")

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

	async def test_the_clis_own_model_fallback_is_a_routing_note(self):
		await self.use_claude()
		self.script({"blocks": [{"text": "Hi."}], "system": [{"subtype": "model_fallback", "original_model": "claude-opus-5-5", "fallback_model": "claude-sonnet-5-5", "trigger": "overloaded"}]},
			{"blocks": [{"text": "Again."}], "system": [{"subtype": "model_refusal_fallback", "original_model": "claude-opus-5-5", "fallback_model": "claude-opus-5", "api_refusal_category": "cyber"}]})
		chat_id, _ = await self.chat()
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		self.assertIn("Claude Code moved the request from claude-opus-5-5 to claude-sonnet-5-5 (overloaded)", self.notes(await self.last_turn(chat_id)))
		self.assertEqual("finished", (await self.turn(chat_id, "Once more."))["stop"])
		turn = await self.last_turn(chat_id)
		self.assertIn("Claude Code moved the request from claude-opus-5-5 to claude-opus-5 after a refusal (cyber)", self.notes(turn))
		self.assertEqual("claude-opus", turn["model"], "the CLI's fallback is its own; IED's chain stays where it was")

	# The Claude models setup writes: the default chain is Claude's ladder over them - opus while the plan is on pace,
	# sonnet a quarter faster, haiku half as fast again, then Fable on its own week - and then the local model.
	SONNET = {"claude-sonnet": {"provider": "claude-code", "model": "sonnet"}}
	FABLE = {"claude-fable": {"provider": "claude-code", "model": "fable"}}
	LADDER = dict(SONNET, **{"claude-haiku": {"provider": "claude-code", "model": "haiku"}}, **FABLE)

	async def test_pacing_walks_the_default_ladder(self):
		await self.use_claude(models=self.LADDER, step_down=True)
		self.put("usage.json", plan_usage(session=(10, 4 * HOUR), week=(60, int(3.5 * 24 * HOUR))))
		usage = await self.client.request("ai.usage", {"refresh": True})
		chain = self.chain(usage)
		self.assertEqual("claude-sonnet", chain["now"])
		self.assertEqual([("claude-opus", "over"), ("claude-sonnet", "use"), ("claude-haiku", "use"), ("claude-fable", "over"), ("fake", "use")],
			[(p["model"], p["state"]) for p in chain["places"]], "Fable is paced to 1.00x like Opus")
		self.assertEqual([{"max": 1.0, "target": "both"}, {"max": 1.25, "target": "both"}, {"max": 1.5, "target": "both"}, {"max": 1.0, "target": "both"}, None],
			[p.get("pace") for p in chain["places"]], "the local model always")
		self.script({"blocks": [{"text": "Paced."}]})
		chat_id, _ = await self.chat()
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		turn = await self.last_turn(chat_id)
		self.assertIn("claude-opus skipped for pace: the week is at 1.20x pace, over its 1.00x", self.notes(turn))
		started = [f for f in self.frames if f.get("kind") == "turn.start"][0]["data"]
		self.assertEqual("claude-sonnet", started["model"], "turn.start names the model the pace order picked, not the chain's first")
		self.assertEqual({"claude-sonnet"}, {f["data"]["model"] for f in self.frames if f.get("kind") == "turn.stats"}, "each request's stats name the model")
		argv = self.sessions_started()[-1]
		self.assertEqual("sonnet", argv[argv.index("--model") + 1])
		self.assertEqual(("claude-sonnet", "default"), (turn["model"], turn["group"]))

	async def test_a_plan_ahead_of_pace_falls_to_the_local_model(self):
		await self.use_claude(models=self.LADDER, step_down=True)
		self.put("usage.json", plan_usage(session=(10, 4 * HOUR), week=(90, int(3.5 * 24 * HOUR))))
		await self.client.request("ai.usage", {"refresh": True})
		self.model.queue(text_step("Local, to stretch the week."))
		chat_id, _ = await self.chat()
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		turn = await self.last_turn(chat_id)
		self.assertEqual("fake", turn["model"])
		notes = self.notes(turn)
		self.assertTrue(any(n.startswith("claude-opus skipped for pace") for n in notes) and any(n.startswith("claude-sonnet skipped for pace: the week is at 1.80x pace, over its 1.25x") for n in notes), notes)
		self.assertEqual([], self.sessions_started(), "no Claude session for a turn the chain sent to the local model")
		await self.client.request("settings.set", {"tier": "app", "key": "ai.fallbackModel", "value": "none"})
		self.script({"blocks": [{"text": "Still Claude: nothing else to try."}]})
		self.assertEqual("finished", (await self.turn(chat_id, "again"))["stop"])
		last = await self.last_turn(chat_id)
		self.assertEqual("claude-haiku", last["model"], "ai.fallbackModel none, every place over its pace: the least over (haiku 1.2x past its 1.5x), not opus")
		self.assertEqual(["claude-opus", "claude-sonnet"], [n.split(" ")[0] for n in self.notes(last)], "the places passed over, not the one the turn runs on")
		argv = self.sessions_started()[-1]
		self.assertEqual("haiku", argv[argv.index("--model") + 1])

	async def test_without_step_down_claude_is_opus_alone(self):
		"""ai.claude.stepDown false: the ladder is its first rung, with no pace - a turn stays on Opus however far ahead of
		pace the plan runs - and the fallback after it."""
		await self.use_claude(models=self.LADDER, step_down=True)
		self.put("usage.json", plan_usage(session=(10, 4 * HOUR), week=(90, int(3.5 * 24 * HOUR))))
		chain = self.chain(await self.client.request("ai.usage", {"refresh": True}))
		self.assertEqual(("fake", ["claude-opus", "claude-sonnet", "claude-haiku", "claude-fable", "fake"]), (chain["now"], [p["model"] for p in chain["places"]]))
		await self.client.request("settings.set", {"tier": "app", "key": "ai.claude.stepDown", "value": False})
		chain = self.chain(await self.client.request("ai.usage", {}))
		self.assertEqual([("claude-opus", "use", None), ("fake", "use", None)], [(p["model"], p["state"], p.get("pace")) for p in chain["places"]])
		self.assertEqual("claude-opus", chain["now"])
		self.script({"blocks": [{"text": "Opus, whatever the pace."}]})
		chat_id, _ = await self.chat()
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		turn = await self.last_turn(chat_id)
		self.assertEqual(("claude-opus", []), (turn["model"], self.notes(turn)))

	async def test_the_fallback_model_ends_the_default_chain(self):
		"""ai.fallbackModel: empty, the first model that is not Claude's behind a Claude chain; a name, that model; "none",
		nothing - a turn Claude refuses then has nowhere to go."""
		other = {"endpoint": self.model.url + "/v1", "model": "script:ok-text", "watchdog": {"prefillBaseS": 2, "idleS": 1, "prefillRate": 1000000}}
		await self.use_claude(models={"other": other})

		async def places():
			return [p["model"] for p in self.chain(await self.client.request("ai.usage", {}))["places"]]

		self.assertEqual(["claude-opus", "fake"], await places())
		await self.client.request("settings.set", {"tier": "app", "key": "ai.fallbackModel", "value": "other"})
		self.assertEqual(["claude-opus", "other"], await places())
		self.script({"error": "rate_limit", "text": "Claude AI usage limit reached|%d" % (int(time.time()) + HOUR)})
		chat_id, _ = await self.chat()
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		turn = await self.last_turn(chat_id)
		self.assertEqual("other", turn["model"])
		self.assertTrue(any(n.startswith("switched to other: Claude refused the request") for n in self.notes(turn)), self.notes(turn))
		await self.client.request("settings.set", {"tier": "app", "key": "ai.fallbackModel", "value": "none"})
		self.assertEqual(["claude-opus"], await places())

	async def test_a_pinned_model_is_used_first_and_the_chain_behind_it_is_not_skipped(self):
		await self.use_claude(models=self.LADDER, step_down=True)
		self.put("usage.json", plan_usage(session=(10, 4 * HOUR), week=(90, int(3.5 * 24 * HOUR))))
		await self.client.request("ai.usage", {"refresh": True})
		chat_id, topic = await self.chat()
		await self.client.request("chat.set", {"chatId": chat_id, "model": "fake"})
		changed = (await self.client.wait_for(topic, lambda f: f.get("kind") == "chat.changed" and f["data"]["pinnedModel"] == "fake"))[-1]["data"]
		self.assertEqual(("fake", "default"), (changed["model"], changed["groupInUse"]), "the chat topic says what the next turn starts on")
		self.model.queue(text_step("Pinned locally."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		turn = await self.last_turn(chat_id)
		self.assertEqual(("fake", "default"), (turn["model"], turn["group"]))
		self.assertEqual([], self.notes(turn), "the default chain's Claude models behind the pin were never reached: none is skipped for pace")
		self.assertEqual([], self.sessions_started(), "no Claude session for a turn on the pinned local model")
		await self.client.request("chat.set", {"chatId": chat_id, "model": ""})
		changed = (await self.client.wait_for(topic, lambda f: f.get("kind") == "chat.changed" and f["data"]["pinnedModel"] == ""))[-1]["data"]
		self.assertEqual("fake", changed["model"], "unpinned: the default chain again, as its pace order has it (every Claude model over its pace)")
		listed = (await self.client.request("chat.list", {"wsId": "w1"}))["chats"][0]
		self.assertNotIn("model", listed, "the chats topic keeps the summary")

	async def test_a_refusal_moves_the_turn_to_the_fallback_and_the_next_turn_asks_claude_again(self):
		"""Owner, 2026-10-26: Claude itself rejects a request before a turn moves on, so a plan upgraded mid-week is used at
		once; IED keeps no block of its own."""
		await self.use_claude()
		self.script({"error": "rate_limit", "text": "Claude AI usage limit reached|%d" % (int(time.time()) + 2 * HOUR)}, {"blocks": [{"text": "Claude has room again."}]})
		self.model.queue(text_step("The local model took over."))
		chat_id, _ = await self.chat()
		end = await self.turn(chat_id)
		self.assertEqual("finished", end["stop"], end)
		turn = await self.last_turn(chat_id)
		self.assertEqual("fake", turn["model"])
		self.assertTrue(any(n.startswith("switched to fake: Claude refused the request: its usage limit is reached") and "it frees in " in n for n in self.notes(turn)), self.notes(turn))
		usage = await self.client.request("ai.usage", {})
		self.assertNotIn("blockedUntilMs", usage["claude"], "IED keeps no block of its own")
		self.assertEqual("claude-opus", self.chain(usage)["now"])
		self.assertEqual("finished", (await self.turn(chat_id, "again"))["stop"])
		last = await self.last_turn(chat_id)
		self.assertEqual("claude-opus", last["model"], "the next turn asks Claude again")
		self.assertEqual([], self.notes(last), "and leaves nothing out")
		self.assertEqual("Claude has room again.", self.segments(last, "text")[-1]["text"].strip())

	async def test_a_used_up_week_leaves_no_claude_model_out(self):
		"""The week IED reads as used up (100%) keeps no Claude model out of a turn: Claude says whether it has room - a plan
		upgraded mid-week, or a reading that is off - and the usage is still shown. The chain: Opus, then Fable
		(ai.fallbackModel), neither paced."""
		await self.use_claude(models=self.FABLE, default_model="claude-opus", fallback_model="claude-fable")
		self.put("usage.json", plan_usage(session=(30, 4 * HOUR), week=(100, 2 * 24 * HOUR), fable=(20, 2 * 24 * HOUR)))
		usage = await self.client.request("ai.usage", {"refresh": True})
		windows = {w["id"]: w for w in usage["claude"]["usage"]["windows"]}
		self.assertTrue(windows["seven_day"]["usedUp"] and windows["model:fable"].get("separate") and not windows["seven_day"].get("separate"), windows)
		chain = self.chain(usage)
		self.assertEqual([("claude-opus", "use"), ("claude-fable", "use")], [(p["model"], p["state"]) for p in chain["places"]])
		self.assertEqual("claude-opus", chain["now"])
		self.script({"blocks": [{"text": "Opus, after a plan upgrade."}]})
		chat_id, _ = await self.chat()
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		turn = await self.last_turn(chat_id)
		self.assertEqual(("claude-opus", []), (turn["model"], self.notes(turn)))
		argv = self.sessions_started()[-1]
		self.assertEqual("opus", argv[argv.index("--model") + 1])

	async def test_the_shared_week_paces_fable_too(self):
		"""Owner, 2026-10-26: the shared weekly limit applies to Fable; its own week is a limit on top. In the default ladder
		(opus 1.0, sonnet 1.25, haiku 1.5, fable 1.0, local) Fable's pace is the worse of the two."""
		await self.use_claude(models=self.LADDER, step_down=True)

		async def now(week, fable):
			self.put("usage.json", plan_usage(session=(10, 4 * HOUR), week=week, fable=fable))
			return self.chain(await self.client.request("ai.usage", {"refresh": True}))

		half = int(3.5 * 24 * HOUR)
		chain = await now(week=(90, half), fable=(10, half))
		self.assertEqual("fake", chain["now"], "the week at 1.80x: every Claude place over its pace, Fable's too")
		self.assertEqual(["over", "over", "over", "over", "use"], [p["state"] for p in chain["places"]])
		self.assertIn("the week is at 1.80x pace", chain["places"][3]["why"])
		chain = await now(week=(40, half), fable=(90, half))
		self.assertEqual(("claude-opus", "over"), (chain["now"], chain["places"][3]["state"]), "the week on pace, Fable's own week ahead")
		self.assertIn("Fable week is at 1.80x pace", chain["places"][3]["why"])
		await now(week=(100, half), fable=(10, half))
		self.model.queue(text_step("Local, the week being used up."))
		chat_id, _ = await self.chat()
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		turn = await self.last_turn(chat_id)
		self.assertEqual("fake", turn["model"], "a used-up week runs far ahead of every pace: the chain's paces step aside as before")
		self.assertEqual(["claude-opus", "claude-sonnet", "claude-haiku", "claude-fable"], [n.split(" ")[0] for n in self.notes(turn)])

	async def test_a_refusal_goes_down_the_chain_and_claude_says_for_each_model(self):
		"""A turn Claude refuses goes on to the chain's next model - another Claude model too, which Claude refuses or takes -
		and the next turn starts on the chain's first again. The chain: the chat's pinned Opus, then the default chain -
		Fable (ai.defaultModel), then the local model (ai.fallbackModel)."""
		await self.use_claude(models=self.FABLE, default_model="claude-fable", fallback_model="fake")
		self.put("usage.json", plan_usage(session=(30, 4 * HOUR), week=(100, 2 * 24 * HOUR), fable=(10, 2 * 24 * HOUR)))
		refused = {"rateLimit": {"status": "rejected", "rateLimitType": "seven_day", "resetsAt": int(time.time()) + 2 * 24 * HOUR}, "error": "rate_limit", "text": "You've hit your weekly limit"}
		self.script(refused, refused, {"blocks": [{"text": "Opus again."}]})
		self.model.queue(text_step("Local took over."))
		chat_id, _ = await self.chat()
		await self.client.request("chat.set", {"chatId": chat_id, "model": "claude-opus"})
		end = await self.turn(chat_id)
		self.assertEqual("finished", end["stop"], end)
		turn = await self.last_turn(chat_id)
		self.assertEqual("fake", turn["model"])
		notes = self.notes(turn)
		self.assertTrue(any(n.startswith("switched to claude-fable: Claude refused the request") and "it frees in " in n for n in notes), notes)
		self.assertTrue(any(n.startswith("switched to fake: Claude refused the request") for n in notes), notes)
		asked = [m["request"].get("model") or "" for m in self.received("control_request") if m["request"]["subtype"] == "set_model"]
		self.assertTrue(any("fable" in model for model in asked), "Fable was asked - its newest once the CLI said which: %s" % asked)
		self.assertEqual("Local took over.", self.segments(turn, "text")[-1]["text"].strip())
		usage = await self.client.request("ai.usage", {})
		self.assertTrue({w["id"]: w for w in usage["claude"]["usage"]["windows"]}["seven_day"]["usedUp"], "the refusal shows in the usage")
		self.assertEqual([("claude-fable", "use"), ("fake", "use")], [(p["model"], p["state"]) for p in self.chain(usage)["places"]], "and keeps no model out")
		self.assertEqual("finished", (await self.turn(chat_id, "again"))["stop"])
		last = await self.last_turn(chat_id)
		self.assertEqual(("claude-opus", []), (last["model"], self.notes(last)), "the next turn asks Claude again")
		self.assertEqual("Opus again.", self.segments(last, "text")[-1]["text"].strip())

	async def test_a_usage_limit_without_a_fallback_pauses(self):
		await self.use_claude(fallback_model="none")
		self.script({"error": "rate_limit", "text": "Claude AI usage limit reached|%d" % (int(time.time()) + HOUR)})
		chat_id, _ = await self.chat()
		end = await self.turn(chat_id)
		self.assertEqual("usage_limit", end["stop"], end)
		self.assertEqual("paused", (await self.summary(chat_id))["state"])
		self.assertTrue(any("Claude refused the request" in n for n in self.notes(await self.last_turn(chat_id), "paused")))

	async def test_a_weeks_pace_over_the_work_week(self):
		"""ai.claude.paceDays "workweek": a weekly window's pace counts only the work days of the server's local time zone, and
		the window says where its days off are; the 5-hour session counts every hour."""
		await self.use_claude()
		self.put("usage.json", plan_usage(session=(10, 4 * HOUR), week=(50, int(3.5 * 24 * HOUR))))
		every = {w["id"]: w for w in (await self.client.request("ai.usage", {"refresh": True}))["claude"]["usage"]["windows"]}
		self.assertEqual(None, every["seven_day"].get("workWeek"))
		self.assertAlmostEqual(0.5, every["seven_day"]["through"], places=2)
		await self.client.request("settings.set", {"tier": "app", "key": "ai.claude.paceDays", "value": "workweek"})
		usage = (await self.client.request("ai.usage", {}))["claude"]["usage"]
		self.assertEqual("workweek", usage["paceDays"])
		windows = {w["id"]: w for w in usage["windows"]}
		week = windows["seven_day"]
		self.assertTrue(week["workWeek"] and 1 <= len(week["daysOff"]) <= 2, week)
		off = sum(d["endMs"] - d["startMs"] for d in week["daysOff"])
		self.assertTrue(47 * HOUR * 1000 <= off <= 49 * HOUR * 1000, "two days off in the week: %s" % week["daysOff"])
		self.assertTrue(all(0 <= d["at"] <= 1 for d in week["daysOff"]))
		self.assertAlmostEqual(week["pace"], week["utilization"] / 100 / week["through"], places=2)
		self.assertEqual(None, windows["five_hour"].get("workWeek"))
		self.assertAlmostEqual(0.2, windows["five_hour"]["through"], places=2)
		await self.client.request("settings.set", {"tier": "app", "key": "ai.claude.workDays", "value": ["mon", "tue", "wed", "thu", "fri", "sat", "sun"]})
		week = {w["id"]: w for w in (await self.client.request("ai.usage", {}))["claude"]["usage"]["windows"]}["seven_day"]
		self.assertEqual(([], 0.5), (week["daysOff"], round(week["through"], 2)), "every day a work day: as the whole week")


class ClaudeSetupTests(ClaudeCase):
	"""docs/04 "Setting it up": a CLI signed in to a subscription sets Claude up by itself, once; Sign in opens the CLI's own
	login in a window of its own and watches it; Install is only for a machine without a CLI. docs/04 "The Claude models":
	the models the CLI offers are added once each. The old model groups move to ai.agents and ai.defaultModel."""

	async def local_only(self):
		await self.client.request("settings.set", {"tier": "app", "key": "ai.claude.command", "value": [sys.executable, "-I", FAKE_CLAUDE, "--state", self.state]})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": {"fake": {"endpoint": self.model.url + "/v1"}}})

	async def test_a_cli_signed_in_to_a_subscription_sets_claude_up_by_itself_once(self):
		await self.local_only()
		usage = await self.client.request("ai.usage", {"refresh": True})
		self.assertTrue(usage["claude"]["configured"] and usage["claude"]["offered"] and usage["claude"]["cli"]["asked"], usage["claude"])
		models = await self.setting("ai.models")
		self.assertEqual(["claude-fable", "claude-haiku", "claude-opus", "claude-sonnet", "fake"], sorted(models), "the four Claude families; their versions are not written")
		self.assertEqual(("opus", "sonnet", "haiku", "fable"), tuple(models["claude-" + alias]["model"] for alias in ("opus", "sonnet", "haiku", "fable")))
		self.assertEqual(({}, ""), (await self.setting("ai.groups"), await self.setting("ai.defaultModel")), "no group is written: the default chain is made of the Claude models")
		self.assertEqual([("claude-opus", 1.0), ("claude-sonnet", 1.25), ("claude-haiku", 1.5), ("claude-fable", 1.0), ("fake", None)],
			[(p["model"], p.get("pace", {}).get("max")) for p in self.chain(usage)["places"]], "a chain stepping down as the plan runs ahead of pace, then Fable on its own week, then the local model")
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": {"fake": {"endpoint": self.model.url + "/v1"}}})
		self.assertFalse((await self.client.request("ai.usage", {"refresh": True}))["claude"]["configured"], "removed Claude models stay removed")
		self.assertEqual(["fake"], sorted(await self.setting("ai.models")), "the CLI's models too")
		self.assertTrue((await self.client.request("ai.claude.setup", {}))["claude"]["configured"], "Add Claude models puts them back")

	async def test_the_claude_models_follow_what_the_cli_offers(self):
		"""docs/04 "The Claude models": the master's refresh keeps the CLI's catalog. Each family's profile runs the newest
		model of its family the CLI offers (Fable 5.2 while the fable alias still means 5.1); every version it offers is a
		model a chat can be pinned to, never written to ai.models; what the first sync wrote that the catalog now gives
		goes; a family the CLI brings later gets its profile once, and one taken out stays out."""
		first_sync = {"claude-opus": {"provider": "claude-code", "model": "opus", "label": "Opus 5.5"}, "claude-fable-5-2": {"provider": "claude-code", "model": "claude-fable-5-2", "label": "Fable 5.2"}}
		ladder = {"claude-" + family: {"provider": "claude-code", "model": family} for family in ("sonnet", "haiku", "fable")}
		await self.use_claude(models=dict(ladder, **first_sync))
		usage = await self.client.request("ai.usage", {"refresh": True})
		catalog = {m["value"]: m for m in usage["claude"]["models"]}
		self.assertEqual(13, len(catalog), "every model the CLI offers but default")
		self.assertEqual(("claude-fable-5-2", False, "Fable 5.1"), (catalog["fable"]["runs"], catalog["fable"]["newest"], catalog["fable"]["label"]), "the fable alias lags")
		self.assertEqual(("opus", True), (catalog["opus"]["runs"], catalog["opus"]["newest"]))
		self.assertEqual((False, "haiku"), ("efforts" in catalog["claude-haiku-4-5-20251001"], catalog["claude-haiku-4-5-20251001"]["family"]), "Haiku 4.5 takes no effort level")
		models = await self.setting("ai.models")
		self.assertEqual(["claude-fable", "claude-haiku", "claude-opus", "claude-sonnet", "fake"], sorted(models), "the version the first sync wrote: the catalog gives it now")
		self.assertEqual({"provider": "claude-code", "model": "opus"}, models["claude-opus"], "its name comes from the catalog now")

		self.script({"blocks": [{"text": "On Fable."}]}, {"blocks": [{"text": "On Opus 4.6."}]})
		chat_id, _ = await self.chat()
		await self.client.request("chat.set", {"chatId": chat_id, "model": "claude-fable"})
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		argv = self.sessions_started()[-1]
		self.assertEqual("claude-fable-5-2", argv[argv.index("--model") + 1], "the fable family runs its newest")
		await self.client.request("chat.set", {"chatId": chat_id, "model": "claude-opus-4-6"})
		self.assertEqual("finished", (await self.turn(chat_id, "Again."))["stop"])
		self.assertEqual("claude-opus-4-6", (await self.last_turn(chat_id))["model"], "a version not in ai.models")
		asked = [a[a.index("--model") + 1] for a in self.sessions_started()] + [m["request"].get("model") for m in self.received("control_request") if m["request"]["subtype"] == "set_model"]
		self.assertIn("claude-opus-4-6", asked)
		self.assertNotIn("claude-opus-4-6", await self.setting("ai.models"), "pinned without being written")

		# A later CLI: Opus 6 behind the alias, Opus 5.5 among the versions, and a family with no alias.
		later = [dict(m, resolvedModel="claude-opus-6", displayName="Opus 6") if m["value"] == "opus" else m for m in offered_models()]
		self.put("models.json", later + [{"value": "claude-opus-5-5", "resolvedModel": "claude-opus-5-5", "displayName": "Opus 5.5"},
			{"value": "claude-mythos-1", "resolvedModel": "claude-mythos-1", "displayName": "Mythos 1"}])
		usage = await self.client.request("ai.usage", {"refresh": True})
		catalog = {m["value"]: m for m in usage["claude"]["models"]}
		self.assertEqual(("Opus 6", "opus"), (catalog["opus"]["label"], catalog["opus"]["runs"]), "Opus 6 the day the CLI offers it")
		self.assertFalse(catalog["claude-opus-5-5"]["alias"], "Opus 5.5 still there, as a version")
		models = await self.setting("ai.models")
		self.assertEqual({"provider": "claude-code", "model": "mythos"}, models.get("claude-mythos"), "a new family gets its profile")
		self.assertEqual(["claude-fable", "claude-haiku", "claude-mythos", "claude-opus", "claude-sonnet", "fake"], sorted(models), "and nothing else")
		del models["claude-mythos"]
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": models})
		await self.client.request("ai.usage", {"refresh": True})
		self.assertNotIn("claude-mythos", await self.setting("ai.models"), "a family taken out stays out")

	async def test_model_groups_move_into_ai_agents_and_the_default_model(self):
		"""Model groups are gone (owner, 2026-11-11): the master's refresh moves the app settings' ai.groups once - their
		sub-agents into ai.agents unless it has its own, a default group that is not Claude's its primary model into an
		empty ai.defaultModel - and empties ai.groups and ai.defaultGroup."""
		await self.local_only()
		helper = {"model": "fake", "description": "Answers small questions", "mode": "ask"}
		await self.client.request("settings.set", {"tier": "app", "key": "ai.groups", "value": {"claude": {"primary": {"model": "claude-opus"}},
			"local": {"primary": {"model": "fake"}, "agents": {"helper": helper}}}})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.defaultGroup", "value": "local"})
		await self.client.request("ai.usage", {"refresh": True})

		async def moved():
			return tuple([await self.setting(key) for key in ("ai.groups", "ai.defaultGroup", "ai.defaultModel", "ai.agents")])

		self.assertEqual(({}, "", "fake", {"helper": helper}), await moved())
		self.model.queue(text_step("Local, as before."))
		chat_id, _ = await self.chat()
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		self.assertEqual("fake", (await self.last_turn(chat_id))["model"], "the default group's model: Claude, set up beside it, waits")
		self.assertIn("- helper (fake", self.model.requests()[-1]["body"]["messages"][0]["content"], "the group's sub-agent")
		await self.client.request("settings.set", {"tier": "app", "key": "ai.defaultModel", "value": ""})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.groups", "value": {"claude": {"primary": {"model": "claude-opus"},
			"agents": {"other": {"model": "claude-sonnet"}}}}})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.defaultGroup", "value": "claude"})
		await self.client.request("ai.usage", {"refresh": True})
		self.assertEqual(({}, "", "", {"helper": helper}), await moved(), "Claude's group names no default model, and ai.agents keeps its own")

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
		self.assertIn("claude-opus", await self.setting("ai.models"))

	async def test_install_is_refused_when_a_cli_is_there(self):
		await self.local_only()
		with self.assertRaises(Exception) as refused:
			await self.client.request("ai.claude.install", {})
		self.assertIn("installed already", str(refused.exception))


class ClaudeToolTests(ClaudeCase):
	async def test_the_ai_reads_the_usage_and_pins_a_model_for_another_chat(self):
		await self.use_claude(default_model="fake")
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
		await self.use_claude(fallback_model="none")
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
