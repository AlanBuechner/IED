"""Chats (docs/10 "Chats", docs/12): turns run on the server against the fake model server (tools/fake-model), each
stop class recovered as docs/12's policy says - streamed segments, tool calls with permission checks and approvals,
calls written as text, retries, continuations, nudges, pauses - and chats survive a restart."""

import asyncio
import base64
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

from frames import FrameChecker
from iedclient import Client, RpcFailure
from server import REPO, IedServer, ServerTestCase, local_state

FAKE_MODEL = os.path.join(REPO, "tools", "fake-model", "fake_model.py")
MAIN = "int main(void)\n{\n\treturn 0;\n}\n"
TURN_SECONDS = 60
# A Claude CLI that is never signed in: a check's server never finds the owner's own and sets Claude up behind it
# (docs/04 "Setting it up"), which would send a chat of the group it adds to their subscription.
SIGNED_OUT_CLAUDE = [sys.executable, "-I", "-c", "raise SystemExit(1)"]
DAY_MS = 24 * 60 * 60 * 1000


def write(path, value):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8", newline="") as f:
		f.write(value)


def inline(*steps):
	"""A queued script of one response (tools/fake-model/README.md "Script format")."""
	return {"description": "inline", "stopClass": "none", "harness": "-", "responses": [{"steps": [{"role": "assistant"}] + list(steps) + [{"usage": True}, {"done": True}]}]}


def call_step(name, arguments, call_id=None):
	call = {"name": name, "arguments": arguments}
	if call_id:
		call["id"] = call_id
	return inline({"toolCall": call}, {"finish": "tool_calls"})


def text_step(text):
	return inline({"text": text}, {"finish": "stop"})


DEMO_PLUGIN = {
	"manifestVersion": 1, "id": "demo.lang", "name": "Demo Language", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
	"contributes": {
		"projectIcons": [{"languageId": "demo", "label": "DM", "markers": ["demo.toml"]}],
		"skills": ["skills"],
		"systemMessageSections": [{"id": "style", "text": "Demo files end every line with a semicolon.", "when": "workspaceLanguage.demo"}],
		"toolCalls": [{"name": "demo_echo", "description": "Echo a word.", "class": "read", "inputSchema": {"type": "object", "properties": {"word": {"type": "string"}}, "required": ["word"]},
			"run": [sys.executable, "-I", "-c", "import sys; print('echo:', sys.argv[1])", "${args.word}"]}]}}


CODE_PLUGIN = {
	"manifestVersion": 1, "id": "demo.code", "name": "Demo Code", "version": "1.0.0", "ied": {"api": "^1"},
	"runtime": {"kind": "c4", "entry": "DemoCode::Activate"}, "permissions": ["fs.read"],
	"contributes": {
		"bindings": {"provides": [{"name": "demo.count", "version": "1.0.0", "kind": "function"}], "consumes": [{"name": "chat.resource", "version": "1.0.0"}]},
		"systemMessageSections": [{"id": "files", "function": "DemoCode::Section"}],
		"toolCalls": [
			{"name": "demo_shout", "description": "Shout a file's first line.", "class": "read", "function": "DemoCode::Shout",
				"inputSchema": {"type": "object", "properties": {"path": {"type": "string"}}, "required": ["path"]}},
			{"name": "demo_count", "description": "Count calls.", "class": "read", "binding": "demo.count", "inputSchema": {"type": "object"}},
			{"name": "demo_snap", "description": "Show the screen.", "class": "read", "function": "DemoCode::Snap", "inputSchema": {"type": "object"}},
			{"name": "demo_page", "description": "Open the demo page for the chat.", "class": "read", "function": "DemoCode::Page", "inputSchema": {"type": "object"}}]}}

CODE_PLUGIN_SOURCE = r'''include "api/plugin.ch"

namespace DemoCode
{
	int64 calls = 0;

	IED::ToolResult Shout(const IED::ToolCall& call)
	{
		Core::String text;
		Core::String problem;
		if (!IED::ReadFile(call.workspace, call.args.Member("path").AsView(), text, problem)) return IED::ToolFailure(problem.View());
		Core::StringBuilder out;
		Core::StringView first = text.View().Substring(0, text.View().IndexOf('\n', 0));
		for (int64 i = 0; i < first.ByteLength(); ++i) out.AppendByte(first.ByteAt(i) >= 'a' && first.ByteAt(i) <= 'z' ? first.ByteAt(i) - 32 : first.ByteAt(i));
		out.Append("! (in ");
		out.Append(call.workspace.name.View());
		out.Append(")");
		return IED::ToolText(out.View());
	}

	IED::ToolResult Snap(const IED::ToolCall& call)
	{
		IED::ToolResult result = IED::ToolText(Core::StringView("the screen"));
		IED::ToolImage shot;
		shot.mime = Core::String("image/png");
		shot.path = Core::String("shots/screen.png");
		result.images.Push(shot);
		IED::ToolImage elsewhere;
		elsewhere.mime = Core::String("image/png");
		elsewhere.path = Core::String("../outside.png");
		result.images.Push(elsewhere);
		return result;
	}

	IED::ToolResult Page(const IED::ToolCall& call)
	{
		Core::StringBuilder args;
		args.Append("{\"chatId\":\"");
		args.Append(call.chatId.View());
		args.Append("\",\"ref\":\"demo:page1\",\"title\":\"Demo page\",\"kind\":\"page\"}");
		IED::Result told = IED::CallBinding(Core::StringView("chat.resource"), args.View());
		return told.Ok() ? IED::ToolText(Core::StringView("opened")) : IED::ToolFailure(told.error.View());
	}

	Core::String Section(const IED::Workspace& ws)
	{
		Core::StringBuilder out;
		out.Append("Workspace ");
		out.Append(ws.name.View());
		out.Append(" is computed by C4 plugin code.");
		return out.ToString();
	}

	void Activate(IED::PluginContext& context)
	{
		context.Provide(Core::StringView("demo.count"), [](const Serialization::DomNode& args) -> IED::Result {
			calls += 1;
			Core::StringBuilder out;
			out.Append("{\"calls\":");
			out.AppendInt(calls);
			out.Append("}");
			return IED::Answer(out.View());
		});
	}
}
'''


def chat_bindings():
	"""The bindings the chat plugin's manifest declares (its code provides them)."""
	with open(os.path.join(REPO, "plugins", "chat", "ied-plugin.json"), encoding="utf-8") as f:
		manifest = json.loads("".join(line for line in f if not line.lstrip().startswith("//")))
	return manifest["contributes"]["bindings"]


def make_home(home):
	"""An install with the host's schemas, the chat plugin - its server code (compiled into the server, docs/26), its
	prompts, settings, bindings and output source, without its UI - and a demo language plugin with a system-message
	section, a skills folder and a command tool (docs/26)."""
	shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(home, "shared", "host-api", "schemas"))
	plugin = os.path.join(home, "plugins", "chat")
	shutil.copytree(os.path.join(REPO, "plugins", "chat", "prompts"), os.path.join(plugin, "prompts"))
	shutil.copy(os.path.join(REPO, "plugins", "chat", "settings.schema.json"), os.path.join(plugin, "settings.schema.json"))
	write(os.path.join(plugin, "ied-plugin.json"), json.dumps({"manifestVersion": 1, "id": "ied.chat", "name": "Chat", "version": "0.1.0", "ied": {"api": "^1"},
		"runtime": {"kind": "builtin", "entry": "Ai::Activate"}, "permissions": ["ai.invoke", "ai.tools", "ai.context", "settings.write", "fs.read", "fs.write", "process.spawn", "lsp", "workspace.projects"],
		"contributes": {"settings": {"schema": "settings.schema.json", "page": "AI"}, "outputSources": [{"id": "ai", "title": "AI"}], "bindings": chat_bindings()}}))
	write(os.path.join(home, "plugins", "demo", "ied-plugin.json"), json.dumps(DEMO_PLUGIN))
	write(os.path.join(home, "plugins", "democode", "ied-plugin.json"), json.dumps(CODE_PLUGIN))
	write(os.path.join(home, "plugins", "democode", "code", "democode.cf"), CODE_PLUGIN_SOURCE)
	return home


class FakeModel:
	"""tools/fake-model on a free port, its delays scaled down."""

	def __init__(self):
		self.process = subprocess.Popen([sys.executable, "-I", FAKE_MODEL, "--port", "0", "--time-scale", "0.02"], stdout=subprocess.PIPE, text=True)
		self.url = self.process.stdout.readline().split()[1]

	def post(self, path, body):
		request = urllib.request.Request(self.url + path, data=json.dumps(body).encode(), headers={"Content-Type": "application/json"})
		with urllib.request.urlopen(request, timeout=10) as reply:
			return json.loads(reply.read())

	def requests(self):
		with urllib.request.urlopen(self.url + "/_fake/requests", timeout=10) as reply:
			return json.loads(reply.read())

	def queue(self, *scripts):
		self.post("/_fake/queue", {"scripts": list(scripts)})

	def stop(self):
		self.process.kill()
		self.process.wait(10)


class ChatCase(ServerTestCase):
	"""A server with the chat plugin against the fake model, and the helpers the chat checks share."""

	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-chat-")
		self.repo = os.path.join(self.root, "Game")
		self.data = os.path.join(self.root, "data")
		self.home = make_home(os.path.join(self.root, "home"))
		write(os.path.join(self.repo, "src", "main.c"), MAIN)
		self.model = FakeModel()
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)

	def start_server(self):
		return IedServer(data=self.data, extra=("--home", self.home, "--workspace", self.repo))

	def tearDown(self):
		self.server.stop(remove_data=False)
		self.model.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		await self.connect()
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.claude.command", "value": SIGNED_OUT_CLAUDE})
		await self.use_model({})

	async def connect(self):
		self.cookie = self.server.sign_in()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def use_model(self, profile, script=None):
		model = dict({"endpoint": self.model.url + "/v1", "watchdog": {"prefillBaseS": 2, "idleS": 1, "prefillRate": 1000000}}, **profile)
		if script:
			model["model"] = "script:" + script
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": {script or "fake": model}})

	# Permission "ask" (Plan mode's "plan"), so the cases see approval cards; a new chat's default is "auto".
	async def chat(self, mode="ask", permission=None, **extra):
		permission = permission or ("plan" if mode == "plan" else "ask")
		created = await self.client.request("chat.create", dict({"wsId": "w1", "mode": mode, "permission": permission}, **extra))
		topic = "chat:" + created["chatId"]
		await self.client.sub([topic])
		await self.client.next_frame(topic)
		return created["chatId"], topic

	async def turn(self, chat_id, text="Hello", timeout=TURN_SECONDS):
		await self.client.request("chat.send", {"chatId": chat_id, "text": text})
		self.frames = await self.client.wait_for("chat:" + chat_id, lambda f: f.get("kind") == "turn.end", timeout=timeout)
		return self.frames[-1]["data"]

	async def last_turn(self, chat_id):
		return (await self.client.request("chat.history", {"chatId": chat_id}))["turns"][-1]

	async def summary(self, chat_id):
		chats = (await self.client.request("chat.list", {"wsId": "w1"}))["chats"]
		return [c for c in chats if c["chatId"] == chat_id][0]

	def segments(self, turn, kind):
		return [s for s in turn["segments"] if s["type"] == kind]

	# --- Answers -----------------------------------------------------------------------------------------------------


class ChatTests(ChatCase):
	async def test_an_answer_streams_into_a_text_segment(self):
		chat_id, topic = await self.chat()
		self.model.queue("ok-text")
		end = await self.turn(chat_id)
		self.assertEqual("finished", end["stop"])
		turn = await self.last_turn(chat_id)
		text = self.segments(turn, "text")
		self.assertEqual(1, len(text), turn)
		self.assertTrue(text[0]["text"].strip(), text)
		self.assertEqual("fake", turn["model"])
		deltas = [f for f in self.frames if f.get("kind") == "segment.delta"]
		self.assertTrue(deltas, "the text streamed")
		summary = await self.summary(chat_id)
		self.assertEqual(("idle", 1, "Hello"), (summary["state"], summary["turnCount"], summary["title"]), "the first message names a new chat")
		sent = self.model.requests()[-1]["body"]
		self.assertEqual(("system", "user"), (sent["messages"][0]["role"], sent["messages"][-1]["role"]))
		self.assertTrue(sent["stream"] and sent["tools"], "a streamed request with the tools")
		self.assertNotIn("finish", [t["function"]["name"] for t in sent["tools"]], "finish is offered in Agent mode only")
		context = [f for f in self.frames if f.get("kind") == "context.changed"][-1]["data"]
		self.assertEqual(32768, context["window"], "a profile without a window gets the server's (its slot context)")

	async def test_reasoning_is_a_thinking_segment(self):
		for script in ("ok-reasoning-then-answer", "reasoning-field-variants"):
			chat_id, _ = await self.chat()
			self.model.queue(script)
			self.assertEqual("finished", (await self.turn(chat_id))["stop"])
			turn = await self.last_turn(chat_id)
			self.assertTrue(self.segments(turn, "thinking") and self.segments(turn, "text"), (script, turn))

	async def test_unknown_finish_reasons_and_bad_chunks_still_end_the_turn(self):
		for script in ("unknown-finish-eos", "unknown-finish-end-turn", "malformed-chunk", "done-without-finish"):
			chat_id, _ = await self.chat()
			self.model.queue(script)
			end = await self.turn(chat_id)
			self.assertEqual(("finished", []), (end["stop"], end["recoveries"]), script)

	# --- What plugins give the AI (docs/26 section 8) -----------------------------------------------------------------

	async def test_plugin_sections_skills_and_command_tools(self):
		skill = "---\nname: {0}\ndescription: {1}\nkeywords: [{2}]\n---\n\n# {0}\n\n{3}\n"
		write(os.path.join(self.repo, ".ied", "skills", "greet", "SKILL.md"), skill.format("greet", "How this project greets people.", "hello, welcome", "Say hello first."))
		write(os.path.join(self.home, "plugins", "demo", "skills", "demo-style", "SKILL.md"), skill.format("demo-style", "Demo code style.", "semicolon", "Semicolons everywhere."))
		write(os.path.join(self.home, "plugins", "demo", "skills", "greet", "SKILL.md"), skill.format("greet", "The plugin's greet, which the project's hides.", "hello", "Wave."))
		chat_id, _ = await self.chat()
		self.model.queue(call_step("skill_search", {"query": "semicolon"}, "c1"), text_step("No demo here."))
		await self.turn(chat_id)
		system = self.model.requests()[0]["body"]["messages"][0]["content"]
		self.assertNotIn("Demo files end every line", system, "the section waits for a workspace that uses the language")
		self.assertIn("- demo-style: Demo code style.", system)
		self.assertIn("- greet: How this project greets people.", system, "the project's skill of a name wins over the plugin's")
		write(os.path.join(self.repo, "demo.toml"), "")
		chat_id, _ = await self.chat()
		self.model.queue(call_step("skill_read", {"name": "greet"}, "c2"), call_step("demo_echo", {"word": "hi"}, "c3"), text_step("Done."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		sent = self.model.requests()[-1]["body"]
		self.assertIn("## Demo Language\n\nDemo files end every line with a semicolon.", sent["messages"][0]["content"], "the workspace uses demo now")
		self.assertIn("demo_echo", [t["function"]["name"] for t in sent["tools"]])
		results = {m["tool_call_id"]: m["content"] for m in sent["messages"] if m["role"] == "tool"}
		self.assertIn("Say hello first.", results["c2"], "skill_read gives the body")
		self.assertNotIn("name: greet", results["c2"], "without its frontmatter")
		self.assertIn("echo: hi", results["c3"], "the command ran with the call's argument")

	async def test_plugin_code_tools_and_sections(self):
		chat_id, _ = await self.chat()
		self.model.queue(call_step("demo_shout", {"path": "src/main.c"}, "c1"), call_step("demo_count", {}, "c2"), call_step("demo_count", {}, "c3"), text_step("Done."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=TURN_SECONDS * 2))["stop"])
		sent = self.model.requests()[-1]["body"]
		self.assertIn("## Demo Code\n\nWorkspace Game is computed by C4 plugin code.", sent["messages"][0]["content"], "a section a C4 function computed")
		results = {m["tool_call_id"]: m["content"] for m in sent["messages"] if m["role"] == "tool"}
		self.assertEqual("INT MAIN(VOID)! (in Game)", results["c1"], "a C4 function tool read the workspace's file")
		self.assertEqual({"calls": 2}, json.loads(results["c3"]), "a binding tool keeps its plugin's state")
		tool = self.segments(await self.last_turn(chat_id), "tool")[0]
		self.assertEqual(("demo_shout", "ok", "plugin demo.code"), (tool["tool"], tool["state"], tool.get("source")))

	async def test_the_prompt_read_so_far_keeps_the_watchdog_off_and_reaches_the_clients(self):
		chat_id, topic = await self.chat()
		# five reports a second apart (scaled), past the 2 s prefill watchdog: each one extends it, as llama.cpp's do
		self.model.queue(inline({"progress": 5, "delayMs": 50000}, {"text": "Read it all."}, {"finish": "stop"}))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "Hello"})
		seen = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.progress" and (f["data"]["prefill"] or {}).get("processed", 0) > 0, timeout=20))[-1]["data"]
		self.assertTrue(0 < seen["prefill"]["processed"] <= seen["prefill"]["total"], seen)
		turn = await self.last_turn(chat_id)
		self.assertIn("prefill", turn, "a client that joins now sees how far the prompt is read")
		frames = await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=TURN_SECONDS)
		self.assertEqual("finished", frames[-1]["data"]["stop"], "no stalled_prefill while the server reports progress")
		progress = [f["data"]["prefill"] for f in frames if f.get("kind") == "turn.progress"]
		self.assertIsNone(progress[-1], "cleared when the reply starts")
		self.assertNotIn("prefill", await self.last_turn(chat_id))
		self.assertIs(True, self.model.requests()[-1]["body"].get("return_progress"), "a llama.cpp server is asked for it")

	async def test_a_request_sends_the_newest_images_and_names_the_rest(self):
		os.makedirs(os.path.join(self.repo, "shots"))
		with open(os.path.join(self.repo, "shots", "screen.png"), "wb") as out:
			out.write(b"\x89PNG\r\n\x1a\n")
		await self.use_model({"vision": True})
		chat_id, _ = await self.chat()
		self.model.queue(*[call_step("demo_snap", {}, "c%d" % i) for i in range(1, 6)], text_step("Five screens."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=TURN_SECONDS * 3))["stop"])
		sent = self.model.requests()[-1]["body"]["messages"]
		parts = [p for m in sent if m["role"] == "user" and isinstance(m["content"], list) for p in m["content"] if p["type"] == "image_url"]
		self.assertEqual(4, len(parts), "the newest four")
		first = [m for m in sent if m["role"] == "tool" and m["tool_call_id"] == "c1"][0]
		self.assertIn("[image shots/screen.png (image/png): an earlier image: a request sends the newest 4]", first["content"], "the oldest is named")

	async def test_a_server_that_takes_images_gets_them_unless_the_profile_says(self):
		os.makedirs(os.path.join(self.repo, "shots"))
		with open(os.path.join(self.repo, "shots", "screen.png"), "wb") as out:
			out.write(b"\x89PNG\r\n\x1a\n")
		self.model.post("/_fake/config", {"vision": True})
		chat_id, _ = await self.chat()
		self.model.queue(call_step("demo_snap", {}, "c1"), text_step("A screen."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=TURN_SECONDS * 2))["stop"])
		self.assertEqual("user", self.model.requests()[-1]["body"]["messages"][-1]["role"], "its /props says it takes images: they are sent")
		await self.use_model({"vision": False})
		chat_id, _ = await self.chat()
		self.model.queue(call_step("demo_snap", {}, "c2"), text_step("A screen."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=TURN_SECONDS * 2))["stop"])
		sent = self.model.requests()[-1]["body"]["messages"]
		self.assertEqual("tool", sent[-1]["role"], "the profile's false wins")
		self.assertIn("this model is not sent images", sent[-1]["content"])

	async def test_a_server_that_does_not_report_its_reading_gets_the_estimate_shown(self):
		self.model.post("/_fake/config", {"props": False})
		await self.use_model({"watchdog": {"prefillBaseS": 2, "idleS": 1, "prefillRate": 50}})
		chat_id, topic = await self.chat()
		self.model.queue(text_step("Read."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "Hello"})
		frames = await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=TURN_SECONDS)
		progress = [f["data"]["prefill"] for f in frames if f.get("kind") == "turn.progress"]
		self.assertTrue(progress and progress[0]["estimated"], progress)
		self.assertGreaterEqual(progress[0]["estimatedMs"], 3000, "shown only for a read that takes a while")
		self.assertGreater(progress[0]["total"], 0)
		self.assertIsNone(progress[-1], "cleared when the reply starts")
		self.assertNotIn("return_progress", self.model.requests()[-1]["body"], "only llama.cpp is asked for progress")

	async def test_a_llamacpp_server_gets_the_turns_earlier_thinking_back(self):
		chat_id, _ = await self.chat()
		thought = inline({"reasoning": "Echo it first, then answer."}, {"toolCall": {"id": "c1", "name": "demo_echo", "arguments": {"word": "hi"}}}, {"finish": "tool_calls"})
		self.model.queue(thought, text_step("Echoed."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=TURN_SECONDS * 2))["stop"])
		step = [m for m in self.model.requests()[-1]["body"]["messages"] if m.get("tool_calls")][0]
		self.assertEqual("Echo it first, then answer.", step.get("reasoning_content"), "its template shows a step's reasoning with the step")
		self.model.queue(text_step("Again."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		self.assertFalse(any("reasoning_content" in m for m in self.model.requests()[-1]["body"]["messages"]), "an earlier turn's thinking stays out")
		await self.use_model({"keepThinking": False})
		chat_id, _ = await self.chat()
		self.model.queue(thought, text_step("Echoed."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=TURN_SECONDS * 2))["stop"])
		self.assertFalse(any("reasoning_content" in m for m in self.model.requests()[-1]["body"]["messages"]), "the profile turns it off")

	async def test_a_turn_sends_its_effort_as_a_llamacpp_template_reads_it(self):
		await self.client.request("settings.set", {"tier": "app", "key": "ai.groups", "value": {"g": {"primary": {"model": "fake", "effort": "medium"}}}})
		chat_id, topic = await self.chat(group="g")
		self.model.queue(text_step("Thought some."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		body = self.model.requests()[-1]["body"]
		self.assertEqual({"reasoning_effort": "medium"}, body.get("chat_template_kwargs"), "the role's effort, as the chat template reads it")
		self.assertNotIn("reasoning_effort", body, "llama.cpp ignores OpenAI's field")
		self.assertEqual("medium", (await self.last_turn(chat_id)).get("effort"), "the turn says what it used")
		with self.assertRaises(RpcFailure) as refused:
			await self.client.request("chat.set", {"chatId": chat_id, "effort": "extreme"})
		self.assertEqual("bad_request", refused.exception.code)
		await self.client.request("chat.set", {"chatId": chat_id, "effort": "xhigh"})
		self.model.queue(text_step("Thought hard."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		self.assertEqual({"reasoning_effort": "xhigh"}, self.model.requests()[-1]["body"].get("chat_template_kwargs"), "xhigh, as it is")
		await self.client.request("chat.set", {"chatId": chat_id, "effort": "low"})
		self.assertEqual("low", (await self.summary(chat_id))["effort"])
		self.model.queue(text_step("Thought less."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		self.assertEqual({"reasoning_effort": "low"}, self.model.requests()[-1]["body"].get("chat_template_kwargs"), "the chat's pick over its role's")
		self.client.abort()
		self.server.stop(remove_data=False)
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()
		self.assertEqual("low", (await self.summary(chat_id))["effort"], "kept across a restart")
		await self.client.sub([topic])
		snapshot = (await self.client.next_frame(topic))["data"]
		self.assertEqual(("low", "medium"), (snapshot["effort"], snapshot["roleEffort"]), "the snapshot has both, for the composer")
		await self.client.request("chat.set", {"chatId": chat_id, "effort": ""})
		self.model.queue(text_step("As the role says."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		self.assertEqual({"reasoning_effort": "medium"}, self.model.requests()[-1]["body"].get("chat_template_kwargs"), "empty is the role's again")

	async def test_a_profile_maps_effort_and_other_servers_get_openais_field(self):
		self.model.post("/_fake/config", {"props": False})
		await self.use_model({"effort": {"low": {"chat_template_kwargs": {"enable_thinking": False}}}})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.groups", "value": {"g": {"primary": {"model": "fake", "effort": "low"}}}})
		chat_id, _ = await self.chat(group="g")
		self.model.queue(text_step("No thinking."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		body = self.model.requests()[-1]["body"]
		self.assertEqual({"enable_thinking": False}, body.get("chat_template_kwargs"), "the profile's fields for the level")
		self.assertNotIn("reasoning_effort", body)
		await self.client.request("chat.set", {"chatId": chat_id, "effort": "high"})
		self.model.queue(text_step("Mapped to nothing."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		body = self.model.requests()[-1]["body"]
		self.assertFalse("chat_template_kwargs" in body or "reasoning_effort" in body, "a level the mapping leaves out adds nothing: %s" % sorted(body))
		await self.use_model({})
		self.model.queue(text_step("OpenAI's way."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		body = self.model.requests()[-1]["body"]
		self.assertEqual("high", body.get("reasoning_effort"), "a server that is not llama.cpp, without a mapping")
		self.assertNotIn("chat_template_kwargs", body)

	async def test_image_info_reads_a_texture_header(self):
		header = b"DDS " + bytes(8) + (512).to_bytes(4, "little") + (256).to_bytes(4, "little") + bytes(8) + (10).to_bytes(4, "little") + bytes(48)
		header += (4).to_bytes(4, "little") + b"DXT1" + bytes(40)
		with open(os.path.join(self.repo, "rock.dds"), "wb") as out:
			out.write(header)
		chat_id, _ = await self.chat()
		self.model.queue(call_step("image_info", {"path": "rock.dds"}, "i1"), call_step("image_info", {"path": "src/main.c"}, "i2"), text_step("A texture."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=TURN_SECONDS * 2))["stop"])
		results = {m["tool_call_id"]: m["content"] for m in self.model.requests()[-1]["body"]["messages"] if m["role"] == "tool"}
		self.assertIn("rock.dds: DDS, 256x512, 10 mips, DXT1 (BC1)", results["i1"])
		self.assertIn("it is not a PNG, JPEG, GIF, WebP, BMP or DDS file", results["i2"])

	async def test_image_view_shows_a_workspace_image(self):
		os.makedirs(os.path.join(self.repo, "shots"))
		with open(os.path.join(self.repo, "shots", "menu.png"), "wb") as out:
			out.write(b"\x89PNG\r\n\x1a\n")
		await self.use_model({"vision": True})
		chat_id, _ = await self.chat()
		self.model.queue(call_step("image_view", {"path": "shots/menu.png"}, "v1"), call_step("image_view", {"path": "src/main.c"}, "v2"), text_step("Seen."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=TURN_SECONDS * 2))["stop"])
		sent = self.model.requests()[-1]["body"]["messages"]
		parts = [p for m in sent if m["role"] == "user" and isinstance(m["content"], list) for p in m["content"]]
		self.assertIn("call v1, shots/menu.png", parts[0]["text"])
		self.assertTrue(parts[1]["image_url"]["url"].startswith("data:image/png;base64,"))
		wrong = [m for m in sent if m["role"] == "tool" and m["tool_call_id"] == "v2"][0]
		self.assertIn("is not a PNG, JPEG, WebP or GIF file", wrong["content"])
		tool = self.segments(await self.last_turn(chat_id), "tool")[0]
		self.assertEqual([{"mime": "image/png", "path": "shots/menu.png"}], tool["result"]["images"], "the card shows it")

	async def test_a_tools_images_reach_a_model_that_sees_them(self):
		png = b"\x89PNG\r\n\x1a\n" + bytes(range(24))
		os.makedirs(os.path.join(self.repo, "shots"))
		with open(os.path.join(self.repo, "shots", "screen.png"), "wb") as out:
			out.write(png)
		chat_id, _ = await self.chat()
		self.model.queue(call_step("demo_snap", {}, "c1"), text_step("A screen."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=TURN_SECONDS * 2))["stop"])
		sent = self.model.requests()[-1]["body"]["messages"]
		result = [m for m in sent if m["role"] == "tool"][0]
		self.assertIn("[image shots/screen.png (image/png): this model is not sent images]", result["content"], "a model without vision is told the path")
		self.assertIn("[image ../outside.png left out:", result["content"], "an image outside the workspace is never sent")
		self.assertEqual("tool", sent[-1]["role"], "no image message for it")
		tool = self.segments(await self.last_turn(chat_id), "tool")[0]
		self.assertIn("[image shots/screen.png]", tool["result"]["preview"], "the card names the image")
		self.assertEqual([{"mime": "image/png", "path": "shots/screen.png"}], tool["result"]["images"], "and shows it")
		shown = self.server.request("GET", "/w/w1/files/shots/screen.png", headers={"Cookie": self.cookie})
		self.assertEqual((200, png), (shown.status, shown.body), "from the workspace's files")

		await self.use_model({"vision": True})
		chat_id, _ = await self.chat()
		self.model.queue(call_step("demo_snap", {}, "c2"), text_step("A screen."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=TURN_SECONDS * 2))["stop"])
		sent = self.model.requests()[-1]["body"]["messages"]
		self.assertEqual(["system", "user", "assistant", "tool", "user"], [m["role"] for m in sent], "the images follow the step's results")
		self.assertTrue(sent[3]["content"].startswith("the screen\n[image ../outside.png left out:"), sent[3]["content"])
		self.assertNotIn("this model is not sent images", sent[3]["content"], "a model that sees images is not told it does not")
		parts = sent[4]["content"]
		self.assertEqual(["text", "image_url"], [p["type"] for p in parts])
		self.assertIn("call c2, shots/screen.png", parts[0]["text"])
		self.assertEqual("data:image/png;base64," + base64.b64encode(png).decode(), parts[1]["image_url"]["url"], "the file's bytes")

	async def test_chats_talk_to_each_other(self):
		model = {"endpoint": self.model.url + "/v1", "watchdog": {"prefillBaseS": 2, "idleS": 1, "prefillRate": 1000000}}
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": {"fake": model, "fixed": dict(model, model="script:ok-text")}})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.groups", "value": {"lead": {"primary": {"model": "fake"}}, "helper": {"primary": {"model": "fixed"}}}})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.permissions", "value": {"allow": ["chat.send(*)"]}})
		helper = (await self.client.request("chat.create", {"wsId": "w1", "mode": "ask", "group": "helper", "title": "Helper"}))["chatId"]
		chat_id, _ = await self.chat(mode="ask", group="lead")
		self.model.queue(call_step("chat_send", {"chat": helper, "text": "ping"}, "s1"), call_step("chat_wait", {"chat": helper, "seconds": 30}, "w1"),
			call_step("chat_list", {}, "l1"), text_step("Done."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=TURN_SECONDS * 2))["stop"])
		mine = [r for r in self.model.requests() if r["body"].get("model") != "script:ok-text"][-1]
		results = {m["tool_call_id"]: m["content"] for m in mine["body"]["messages"] if m["role"] == "tool"}
		helper_turns = (await self.client.request("chat.history", {"chatId": helper}))["turns"]
		self.assertTrue(helper_turns, "chat_send started a turn in the helper: %s" % results)
		helper_turn = helper_turns[-1]
		self.assertEqual(("ping", chat_id), (helper_turn["user"]["text"], helper_turn["user"]["from"]["chatId"]), "the helper's turn says who sent it")
		self.assertIn("it started a turn for it", results["s1"])
		self.assertIn("its last turn finished", results["w1"], "chat_wait returned the helper's answer")
		self.assertIn('"Helper"', results["l1"])
		self.assertIn("- this chat", results["l1"])

	async def test_a_plugin_tells_the_chat_what_it_has_open(self):
		chat_id, topic = await self.chat()
		self.model.queue(call_step("demo_page", {}, "p1"), text_step("Opened."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=TURN_SECONDS * 2))["stop"])
		changed = [f["data"] for f in self.frames if f.get("kind") == "resources.changed"]
		results = [m["content"] for m in self.model.requests()[-1]["body"]["messages"] if m["role"] == "tool"]
		self.assertTrue(changed, "resources.changed published: %s" % results)
		self.assertEqual([{"ref": "demo:page1", "title": "Demo page", "kind": "page"}], changed[-1]["resources"])
		other = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		try:
			await other.sub([topic])
			snapshot = (await other.next_frame(topic))["data"]
			self.assertEqual("demo:page1", snapshot["resources"][0]["ref"], "the snapshot has it")
		finally:
			other.abort()
		self.client.abort()
		self.server.stop(remove_data=False)
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()
		await self.client.sub([topic])
		self.assertEqual([], (await self.client.next_frame(topic))["data"]["resources"], "after a restart nothing is open until a plugin says so again")

	async def test_plugin_tools_load_on_demand(self):
		names = lambda body: [t["function"]["name"] for t in body["tools"]]
		chat_id, _ = await self.chat()
		self.model.queue(call_step("tool_search", {"query": "shout"}, "s1"), text_step("Loaded."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		first, last = self.model.requests()[-2]["body"], self.model.requests()[-1]["body"]
		self.assertNotIn("demo_shout", names(first), "a plugin tool is not sent at first")
		self.assertIn("- demo_shout: Shout a file's first line. (demo.code)", first["messages"][0]["content"], "it is listed by name")
		self.assertIn("demo_shout", names(last), "tool_search loaded it")
		self.assertIn("## demo_shout", [m for m in last["messages"] if m["role"] == "tool"][0]["content"], "its definition told the model")
		chat_id, _ = await self.chat()
		self.model.queue(text_step("Hi."))
		await self.turn(chat_id)
		self.assertNotIn("demo_shout", names(self.model.requests()[-1]["body"]), "loaded per chat")
		await self.client.request("settings.set", {"tier": "app", "key": "ai.tools.loadAll", "value": True})
		chat_id, _ = await self.chat()
		self.model.queue(text_step("Hi."))
		await self.turn(chat_id)
		sent = self.model.requests()[-1]["body"]
		self.assertIn("demo_shout", names(sent), "ai.tools.loadAll sends them all")
		self.assertNotIn("# Tools you can load", sent["messages"][0]["content"])

	# --- Tools -------------------------------------------------------------------------------------------------------

	async def test_a_tool_call_runs_and_the_model_gets_its_result(self):
		chat_id, topic = await self.chat()
		self.model.queue(call_step("fs_read", {"path": "src/main.c"}, "call_1"), text_step("It returns 0."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		turn = await self.last_turn(chat_id)
		tool = self.segments(turn, "tool")[0]
		self.assertEqual(("fs_read", "ok", False), (tool["tool"], tool["state"], tool["result"]["isError"]))
		sent = self.model.requests()[-1]["body"]["messages"]
		result = [m for m in sent if m["role"] == "tool"][0]
		self.assertEqual("call_1", result["tool_call_id"])
		self.assertIn("1\tint main(void)", result["content"], "line-numbered")
		assistant = [m for m in sent if m.get("tool_calls")][0]
		self.assertEqual("fs_read", assistant["tool_calls"][0]["function"]["name"])

	async def test_calls_written_as_text_run_like_native_ones(self):
		for dialect in ("hermes", "qwen", "mistral", "mistral-args", "fenced-json", "harmony"):
			chat_id, _ = await self.chat()
			script = inline({"textToolCall": {"dialect": dialect, "name": "fs_read", "arguments": {"path": "src/main.c"}}}, {"finish": "stop"})
			self.model.queue(script, text_step("Done."))
			self.assertEqual("finished", (await self.turn(chat_id))["stop"], dialect)
			turn = await self.last_turn(chat_id)
			tools = self.segments(turn, "tool")
			self.assertEqual([("fs_read", "ok")], [(t["tool"], t["state"]) for t in tools], (dialect, turn))
			for text in self.segments(turn, "text"):
				self.assertNotIn("fs_read", text["text"], "the call's markup never shows as prose")

	async def test_merged_call_deltas(self):
		for script in ("tool-calls-same-index-different-ids", "tool-calls-without-ids", "tool-call-id-on-every-delta"):
			chat_id, _ = await self.chat()
			self.model.queue(script, text_step("Done."))
			await self.turn(chat_id)
			turn = await self.last_turn(chat_id)
			expected = 1 if script == "tool-call-id-on-every-delta" else 2
			self.assertEqual(expected, len(self.segments(turn, "tool")), (script, turn))

	async def test_writes_ask_and_the_owner_decides(self):
		chat_id, topic = await self.chat()
		self.model.queue(call_step("fs_write", {"path": "src/new.c", "content": "int x;\n"}), text_step("Written."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "write it"})
		request = (await self.client.wait_for(topic, lambda f: f.get("kind") == "approval.request", timeout=20))[-1]["data"]
		self.assertEqual(("fs_write", "fs.write(src/**)"), (request["tool"], request["subject"]))
		self.assertIn("+int x;", request["diff"])
		self.assertEqual("awaiting_approval", (await self.summary(chat_id))["state"])
		self.assertFalse(os.path.exists(os.path.join(self.repo, "src", "new.c")), "nothing is written before the decision")
		await self.client.request("chat.approve", {"chatId": chat_id, "approvalId": request["approvalId"], "decision": "allow"})
		with self.assertRaises(RpcFailure) as twice:
			await self.client.request("chat.approve", {"chatId": chat_id, "approvalId": request["approvalId"], "decision": "deny"})
		self.assertIn(twice.exception.code, ("conflict", "not_found"))
		end = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=20))[-1]["data"]
		self.assertEqual("finished", end["stop"])
		with open(os.path.join(self.repo, "src", "new.c"), encoding="utf-8") as f:
			self.assertEqual("int x;\n", f.read())
		snapshot = await self.client.request("chat.history", {"chatId": chat_id})
		self.assertEqual("ok", self.segments(snapshot["turns"][-1], "tool")[0]["state"])

	async def test_a_denied_write_tells_the_model(self):
		chat_id, topic = await self.chat()
		self.model.queue(call_step("fs_write", {"path": "src/new.c", "content": "x"}), text_step("Understood."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "write it"})
		request = (await self.client.wait_for(topic, lambda f: f.get("kind") == "approval.request", timeout=20))[-1]["data"]
		await self.client.request("chat.approve", {"chatId": chat_id, "approvalId": request["approvalId"], "decision": "deny", "message": "not now"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=20)
		turn = await self.last_turn(chat_id)
		self.assertEqual("denied", self.segments(turn, "tool")[0]["state"])
		result = [m for m in self.model.requests()[-1]["body"]["messages"] if m["role"] == "tool"][0]
		self.assertIn("not now", result["content"])
		self.assertFalse(os.path.exists(os.path.join(self.repo, "src", "new.c")))

	async def test_accept_edits_writes_without_asking_and_tracks_the_file(self):
		chat_id, topic = await self.chat(permission="acceptEdits")
		edit = {"path": "src/main.c", "edits": [{"old": "\treturn 0;", "new": "\treturn 1;"}]}
		self.model.queue(call_step("fs_edit", edit), text_step("Edited."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		with open(os.path.join(self.repo, "src", "main.c"), encoding="utf-8") as f:
			self.assertIn("return 1;", f.read())
		turn = await self.last_turn(chat_id)
		tool = self.segments(turn, "tool")[0]
		self.assertEqual(("ok", 1, 1), (tool["state"], tool["plus"], tool["minus"]))
		files = [f for f in self.frames if f.get("kind") == "files.changed"][-1]["data"]["files"]
		self.assertEqual([{"path": "src/main.c", "plus": 1, "minus": 1}], files)

	async def test_the_clipboard_copies_from_results_and_files_and_pastes_into_calls(self):
		chat_id, topic = await self.chat(permission="acceptEdits")
		paste = {"path": "src/main.c", "edits": [{"old": "{{clip:ret}}", "new": "\tputs(\"{{!clip:sig}}\");\n{{clip:ret}}"}]}
		self.model.queue(call_step("fs_read", {"path": "src/main.c"}, "r1"), call_step("clipboard_copy", {"name": "ret", "result": 1, "start_line": 3, "end_line": 3}, "c1"),
			call_step("clipboard_copy", {"name": "sig", "path": "src/main.c", "from": "int", "to": ")"}, "c2"), call_step("fs_edit", paste, "e1"),
			call_step("fs_write", {"path": "src/decl.h", "content": "{{clip:sig}};\n"}, "w1"), call_step("fs_read", {"path": "{{clip:nope}}"}, "x1"),
			call_step("clipboard_list", {}, "l1"), text_step("Done."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		sent = self.model.requests()[-1]["body"]["messages"]
		results = {m["tool_call_id"]: m["content"] for m in sent if m["role"] == "tool"}
		self.assertIn("Copied 1 line (11 bytes) of the fs_read result line 3 to the clip ret", results["c1"], "the read's line numbers are left out")
		self.assertIn("{{clip:ret}}", results["c1"], "the result says how to paste it")
		self.assertIn("Line: \treturn 0;", results["c1"])
		self.assertIn("of src/main.c to the clip sig", results["c2"])
		with open(os.path.join(self.repo, "src", "main.c"), encoding="utf-8") as f:
			self.assertEqual("int main(void)\n{\n\tputs(\"{{clip:sig}}\");\n\treturn 0;\n}\n", f.read(), "pasted into old and new; the escaped one written as text")
		with open(os.path.join(self.repo, "src", "decl.h"), encoding="utf-8") as f:
			self.assertEqual("int main(void);\n", f.read())
		self.assertIn("there is no clip nope (the clips are ret, sig)", results["x1"])
		self.assertIn("ret: 1 line (11 bytes), from the fs_read result line 3", results["l1"])
		calls = [c["function"]["arguments"] for m in sent if m.get("tool_calls") for c in m["tool_calls"]]
		self.assertTrue(any("{{clip:ret}}" in c for c in calls), "the model's own calls keep the tokens, not the pasted text")
		tools = {t["callId"]: t for t in self.segments(await self.last_turn(chat_id), "tool")}
		self.assertEqual(["ok", "ok", "ok", "ok", "ok", "error", "ok"], [t["state"] for t in tools.values()])
		clips = [f["data"]["clips"] for f in self.frames if f.get("kind") == "clips.changed"][-1]
		self.assertEqual([("ret", "the fs_read result line 3", 1, 11, "\treturn 0;\n"), ("sig", "src/main.c", 1, 14, "int main(void)")],
			[(c["name"], c["source"], c["lines"], c["bytes"], c["text"]) for c in clips])
		self.client.abort()
		self.server.stop(remove_data=False)
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()
		await self.client.sub([topic])
		self.assertEqual(["ret", "sig"], [c["name"] for c in (await self.client.next_frame(topic))["data"]["clips"]], "the clipboard is kept with the chat")
		other, _ = await self.chat(permission="acceptEdits")
		self.model.queue(call_step("fs_write", {"path": "src/other.c", "content": "{{clip:sig}}"}, "o1"), text_step("No."))
		await self.turn(other)
		result = [m for m in self.model.requests()[-1]["body"]["messages"] if m["role"] == "tool"][0]["content"]
		self.assertIn("there is no clip sig (the clipboard is empty)", result, "each chat has its own clipboard")
		self.assertFalse(os.path.exists(os.path.join(self.repo, "src", "other.c")))

	async def test_a_plan_mode_chat_cannot_write(self):
		chat_id, _ = await self.chat(mode="plan")
		self.model.queue(call_step("fs_write", {"path": "src/new.c", "content": "x"}), text_step("Planned."))
		await self.turn(chat_id)
		tool = self.segments(await self.last_turn(chat_id), "tool")[0]
		self.assertEqual("error", tool["state"], "fs_write is not offered in Plan mode")
		self.assertIn("no tool named fs_write", tool["result"]["preview"])

	async def test_unknown_tools_and_bad_arguments_teach(self):
		chat_id, _ = await self.chat()
		self.model.queue(call_step("read_file", {"path": "src/main.c"}), call_step("fs_read", {"file": "src/main.c"}),
			call_step("fs_read", '{"path": "src/main.c", "line": }'), call_step("fs_read", {"path": "src/main.c"}), text_step("Ok."))
		self.assertEqual("finished", (await self.turn(chat_id))["stop"])
		unknown, missing, broken, fixed = self.segments(await self.last_turn(chat_id), "tool")
		self.assertIn("fs_read", unknown["result"]["preview"], "suggests the tool it meant")
		self.assertIn("path", missing["result"]["preview"], "names the missing argument")
		self.assertIn("not a JSON object", broken["result"]["preview"], "arguments that are not JSON are sent back, never run")
		self.assertEqual("ok", fixed["state"])

	async def test_ask_user_waits_for_the_answer(self):
		chat_id, topic = await self.chat()
		self.model.queue(call_step("ask_user", {"question": "Which one?", "options": ["A", "B"]}), text_step("B it is."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "choose"})
		question = (await self.client.wait_for(topic, lambda f: f.get("kind") == "question.request", timeout=20))[-1]["data"]
		self.assertEqual(("Which one?", ["A", "B"]), (question["question"], question["options"]))
		self.assertEqual("awaiting_answer", (await self.summary(chat_id))["state"])
		await self.client.request("chat.answer", {"chatId": chat_id, "questionId": question["questionId"], "answer": "B"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=20)
		result = [m for m in self.model.requests()[-1]["body"]["messages"] if m["role"] == "tool"][0]
		self.assertIn("B", result["content"])

	async def test_agent_mode_ends_with_finish_and_keeps_tasks(self):
		chat_id, topic = await self.chat(mode="agent")
		self.model.queue(
			call_step("tasks_set", {"items": [{"title": "Read main", "status": "in_progress"}, {"title": "Report"}]}),
			call_step("finish", {"summary": "s", "verification": "v"}),
			call_step("task_update", {"id": "t1", "status": "done"}),
			call_step("task_update", {"id": "t2", "status": "skipped", "note": "nothing to report"}),
			call_step("finish", {"summary": "Read it", "verification": "looked"}))
		end = await self.turn(chat_id)
		self.assertEqual("finished", end["stop"])
		turn = await self.last_turn(chat_id)
		finishes = [t for t in self.segments(turn, "tool") if t["tool"] == "finish"]
		self.assertEqual(["error", "ok"], [t["state"] for t in finishes], "the first finish is sent back: tasks are open")
		summary = await self.summary(chat_id)
		self.assertEqual((2, 2), (summary["tasksDone"], summary["tasksTotal"]))

	async def test_agent_mode_nudges_a_reply_without_finish_then_pauses(self):
		chat_id, _ = await self.chat(mode="agent")
		await self.use_model({}, script="no-progress")
		end = await self.turn(chat_id)
		self.assertEqual("no_progress", end["stop"])
		summary = await self.summary(chat_id)
		self.assertEqual(("paused", "no_progress"), (summary["state"], summary["stateReason"]))
		turn = await self.last_turn(chat_id)
		notes = [s for s in self.segments(turn, "harness") if s["kind"] == "agent_no_finish"]
		self.assertEqual(3, len(notes))

	async def test_the_same_failing_call_three_times_is_a_loop(self):
		chat_id, _ = await self.chat(mode="agent")
		await self.use_model({}, script="loop-same-call")
		self.assertEqual("loop_detected", (await self.turn(chat_id))["stop"])

	async def test_steps_are_not_capped_unless_ai_max_steps_says_so(self):
		chat_id, topic = await self.chat(mode="agent")
		await self.use_model({}, script="budget-runaway")
		await self.client.request("chat.send", {"chatId": chat_id, "text": "read them all"})
		calls = []

		def past_the_old_cap(frame):
			if frame.get("kind") == "segment.start" and frame["data"].get("type") == "tool":
				calls.append(frame)
			return len(calls) > 120 or frame.get("kind") == "turn.end"
		last = (await self.client.wait_for(topic, past_the_old_cap, timeout=20))[-1]
		self.assertNotEqual("turn.end", last.get("kind"), "ai.maxSteps 0 (the default) never pauses for steps: %s" % last)
		await self.client.request("chat.stop", {"chatId": chat_id})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=20)
		await self.client.request("settings.set", {"tier": "app", "key": "ai.maxSteps", "value": 5})
		self.assertEqual("budget", (await self.turn(chat_id, "again"))["stop"])
		turn = await self.last_turn(chat_id)
		self.assertEqual(5, len(self.segments(turn, "tool")), "five model requests, each a call")
		self.assertTrue(any("80%" in s["text"] for s in self.segments(turn, "harness")), "the 80% note before the pause")

	# --- Recovery ----------------------------------------------------------------------------------------------------

	async def test_each_stop_class_is_recovered(self):
		cases = [
			("http-error-503", "retried after http_error 503"),
			("http-error-429-retry-after", "retried after http_error 429"),
			("connect-failed", "retried after connect_failed"),
			("max-tokens", "continued after max_tokens"),
			("max-tokens-mid-tool-args", "continued after max_tokens"),
			("disconnect-mid-stream", "continued after disconnected_mid_stream"),
			("eof-without-done", "continued after disconnected_mid_stream"),
			("stall-mid-stream", "continued after stalled_stream"),
			("stall-prefill", "retried after stalled_prefill"),
			("stall-prefill-after-headers", "retried after stalled_prefill"),
			("announced-no-action", "nudged after announced_no_action"),
			("reasoning-only", "nudged after reasoning_only"),
			("reasoning-only-think-tag", "nudged after reasoning_only"),
			("malformed-tool-args", None),
			("context-overflow-llamacpp", "retried after context_overflow"),
		]
		for script, recovery in cases:
			chat_id, _ = await self.chat()
			await self.use_model({}, script=script)
			self.model.post("/_fake/reset", {})
			end = await self.turn(chat_id)
			if script in ("announced-no-action", "reasoning-only", "reasoning-only-think-tag"):
				self.assertIn(end["stop"], ("finished", "announced_no_action", "reasoning_only"), (script, end))
			elif script in ("max-tokens-mid-tool-args", "malformed-tool-args"):
				self.assertEqual("loop_detected", end["stop"], "the script's later call names a tool IED does not have (write_file, read_file) on every request")
			else:
				self.assertEqual("finished", end["stop"], (script, end))
			if recovery:
				self.assertTrue(any(r.startswith(recovery) for r in end["recoveries"]), (script, end["recoveries"]))

	async def test_a_stop_class_past_its_bound_pauses(self):
		chat_id, _ = await self.chat()
		await self.use_model({}, script="empty")
		end = await self.turn(chat_id)
		self.assertEqual("empty", end["stop"])
		self.assertEqual("paused", (await self.summary(chat_id))["state"])
		turn = await self.last_turn(chat_id)
		self.assertEqual(["empty", "empty", "paused"], [s["kind"] for s in self.segments(turn, "harness")])

	async def test_a_retry_after_a_stalled_prefill_waits_longer(self):
		# The owner's report: a server still reading a cold prompt when the watchdog fires. The retry must not be handed
		# the deadline that just failed, or the turn pauses after two tries while the server was never broken.
		chat_id, _ = await self.chat()
		await self.use_model({}, script="stall-prefill-twice")
		self.model.post("/_fake/reset", {})
		end = await self.turn(chat_id, timeout=TURN_SECONDS * 3)
		self.assertEqual("finished", end["stop"], (end, "the second prefill finished inside the longer deadline"))
		self.assertEqual(2, len(self.model.requests()), "one stall, one retry")
		self.assertTrue(any(r.startswith("retried after stalled_prefill") for r in end["recoveries"]), end["recoveries"])

	async def test_a_server_still_reading_its_prompt_is_not_called_stalled(self):
		# The same silence, on a server whose gauges say the work is still in its engine (the owner's vLLM: a 250k prompt
		# read for 87 s with nothing on the wire). The watchdog asks, is told it is working, and waits - so the request is
		# never cancelled and the answer lands in one go. Cancelling is what turned a healthy long prefill into a stall:
		# the retry starts the read over and runs into the same short deadline twice, then pauses.
		chat_id, _ = await self.chat()
		self.model.post("/_fake/config", {"metrics": "busy"})
		await self.use_model({"watchdog": {"prefillBaseS": 1, "idleS": 1, "prefillRate": 1000000}}, script="stall-prefill-after-headers")
		self.model.post("/_fake/reset", {})
		end = await self.turn(chat_id, timeout=TURN_SECONDS * 2)
		frames = self.frames
		self.model.post("/_fake/config", {"metrics": "off"})
		self.assertEqual("finished", end["stop"], end)
		self.assertEqual([], end["recoveries"], "nothing was retried: the wait was never called a stall")
		self.assertEqual(1, len(self.model.requests()), "the one request was left alone until it answered")
		progress = [f["data"]["prefill"] for f in frames if f.get("kind") == "turn.progress" and f["data"]["prefill"]]
		self.assertTrue(any(p.get("working") for p in progress), "the client was told the server is still working")

	async def test_a_server_whose_engine_is_idle_still_counts_as_stalled(self):
		# The check must not hide a real stall: a server that answers /metrics with nothing in flight is a server that
		# lost the request, and that is still retried like one.
		chat_id, _ = await self.chat()
		self.model.post("/_fake/config", {"metrics": "idle"})
		await self.use_model({"watchdog": {"prefillBaseS": 1, "idleS": 1, "prefillRate": 1000000}}, script="stall-prefill-after-headers")
		self.model.post("/_fake/reset", {})
		end = await self.turn(chat_id, timeout=TURN_SECONDS * 2)
		self.model.post("/_fake/config", {"metrics": "off"})
		self.assertEqual("finished", end["stop"], end)
		self.assertTrue(any(r.startswith("retried after stalled_prefill") for r in end["recoveries"]), end["recoveries"])

	async def test_a_status_page_that_times_the_ask_out_is_not_a_stall(self):
		# The ask itself can be what fails: a vLLM under load answers /metrics in milliseconds, but a status page that
		# takes seconds times IED's 2 s ask out. That must not be read as "the server says it has nothing" - one timed-out
		# ask used to end a healthy prefill. The deadline moves by the asking and the request is left alone until it answers.
		chat_id, _ = await self.chat()
		self.model.post("/_fake/config", {"metrics": "slow"})
		await self.use_model({"watchdog": {"prefillBaseS": 1, "idleS": 1, "prefillRate": 1000000}}, script="stall-prefill-after-headers")
		self.model.post("/_fake/reset", {})
		end = await self.turn(chat_id, timeout=TURN_SECONDS * 2)
		self.model.post("/_fake/config", {"metrics": "off"})
		self.assertEqual("finished", end["stop"], end)
		self.assertEqual(1, len(self.model.requests()), "waited through the timed-out ask instead of retrying")

	async def test_a_looping_reply_is_cut_and_dropped(self):

		chat_id, _ = await self.chat()
		await self.use_model({}, script="repetition-loop")
		self.model.post("/_fake/reset", {})
		end = await self.turn(chat_id)
		self.assertEqual("finished", end["stop"], end)
		self.assertTrue(any(r.startswith("nudged after repetition") for r in end["recoveries"]), end["recoveries"])
		requests = self.model.requests()
		self.assertEqual(2, len(requests), "the loop was cut and asked again once")
		sent = json.dumps(requests[-1]["body"]["messages"])
		self.assertNotIn("check the header again", sent, "the looping reply is not sent back")
		self.assertIn("kept repeating the same text", sent, "the note says why")

	async def test_a_refused_request_pauses_with_the_servers_words(self):
		chat_id, _ = await self.chat()
		await self.use_model({}, script="context-overflow-openai")
		end = await self.turn(chat_id)
		self.assertIn(end["stop"], ("finished", "context_overflow"), end)

	async def test_a_model_that_is_not_there_falls_back(self):
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": {
			"gone": {"endpoint": "http://127.0.0.1:9/v1", "watchdog": {"connectS": 1}},
			"fake": {"endpoint": self.model.url + "/v1", "watchdog": {"prefillRate": 1000000}}}})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.groups", "value": {"g": {"primary": {"model": "gone", "fallback": ["fake"]}}}})
		chat_id, _ = await self.chat()
		self.model.queue("ok-text")
		end = await self.turn(chat_id)
		self.assertEqual("finished", end["stop"])
		self.assertTrue(any(r.startswith("switched to fake") for r in end["recoveries"]), end["recoveries"])

	async def test_no_model_says_so(self):
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": {}})
		chat_id, _ = await self.chat()
		end = await self.turn(chat_id)
		self.assertEqual("no_model", end["stop"])
		self.assertIn("ai.models", self.segments(await self.last_turn(chat_id), "harness")[0]["text"])

	async def test_test_connection_reports_the_server(self):
		probe = await self.client.request("ai.probe", {"wsId": "w1", "model": "fake"})
		self.assertEqual((True, "llama.cpp", ["fake-model"], 32768, 1, False), (probe["ok"], probe["server"], probe["models"], probe["contextWindow"], probe["slots"], probe["modelServed"]))
		unsaved = await self.client.request("ai.probe", {"wsId": "w1", "profile": {"endpoint": self.model.url + "/v1", "model": "fake-model"}})
		self.assertTrue(unsaved["ok"] and unsaved["modelServed"], unsaved)
		gone = await self.client.request("ai.probe", {"wsId": "w1", "profile": {"endpoint": "http://127.0.0.1:9/v1", "watchdog": {"connectS": 1}}})
		self.assertFalse(gone["ok"])
		self.assertTrue(gone["error"], gone)
		with self.assertRaises(RpcFailure) as missing:
			await self.client.request("ai.probe", {"wsId": "w1", "model": "nope"})
		self.assertEqual("bad_request", missing.exception.code)

	# --- Messages while a turn runs, and stopping ---------------------------------------------------------------------

	async def test_stop_cancels_the_turn(self):
		chat_id, topic = await self.chat()
		await self.use_model({}, script="long-stream")
		await self.client.request("chat.send", {"chatId": chat_id, "text": "talk"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.delta", timeout=20)
		await self.client.request("chat.stop", {"chatId": chat_id})
		end = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=20))[-1]["data"]
		self.assertEqual("user_cancelled", end["stop"])
		self.assertEqual("idle", (await self.summary(chat_id))["state"])

	async def test_queued_messages_run_next_and_sent_ones_join_the_running_turn(self):
		chat_id, topic = await self.chat()
		self.model.queue("long-stream", "ok-text")
		await self.client.request("chat.send", {"chatId": chat_id, "text": "first"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.delta", timeout=20)
		queued = await self.client.request("chat.send", {"chatId": chat_id, "text": "second", "as": "queue"})
		self.assertEqual(1, queued["queued"])
		await self.client.request("chat.stop", {"chatId": chat_id})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=20)
		self.assertEqual(1, (await self.summary(chat_id))["turnCount"], "a stopped turn does not start the queue")
		self.assertEqual({"queued": [], "message": {"text": "second"}}, await self.client.request("chat.unqueue", {"chatId": chat_id, "index": 0}), "the message taken off, whole")
		with self.assertRaises(RpcFailure) as gone:
			await self.client.request("chat.unqueue", {"chatId": chat_id, "index": 0})
		self.assertEqual("not_found", gone.exception.code)
		self.model.post("/_fake/reset", {})
		self.model.queue(inline({"text": "word " * 200, "chunk": 4, "delayMs": 150}, {"toolCall": {"name": "fs_list", "arguments": {"path": "src"}}}, {"finish": "tool_calls"}), "ok-text")
		await self.client.request("chat.send", {"chatId": chat_id, "text": "third"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.delta" and f["data"].get("text"), timeout=20)
		self.assertEqual({"pending": 1}, await self.client.request("chat.send", {"chatId": chat_id, "text": "use tabs"}), "a message to a running turn waits for its next step")
		told = (await self.client.wait_for(topic, lambda f: f.get("kind") == "queue.changed" and f["data"]["pending"], timeout=20))[-1]["data"]
		self.assertEqual({"queued": [], "pending": ["use tabs"]}, told, "every device sees it waiting")
		frames = await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end" and f["data"]["turnId"] != "u1", timeout=30)
		self.assertIn({"queued": [], "pending": []}, [f["data"] for f in frames if f.get("kind") == "queue.changed"], "it leaves the list when the turn takes it")
		history = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"]
		self.assertEqual(["first", "third"], [t["user"]["text"] for t in history[:2]])
		joined = [s for t in history for s in self.segments(t, "message")]
		self.assertEqual(["use tabs"], [s["text"] for s in joined])
		self.assertGreater(joined[0]["atMs"], 0)
		third = history[1]["segments"]
		self.assertEqual("tool", third[third.index(joined[0]) - 1]["type"], "shown after the tool round it came in")
		last = self.model.requests()[-1]["body"]["messages"]
		self.assertEqual(("tool", "user", "use tabs"), (last[-2]["role"], last[-1]["role"], last[-1]["content"]), "the model gets it as a plain message after the results")

	async def test_a_message_during_the_last_answer_still_reaches_the_model(self):
		chat_id, topic = await self.chat()
		long_answer = inline({"text": "word " * 400, "chunk": 4, "delayMs": 150}, {"finish": "stop"})
		self.model.queue(long_answer, text_step("Using tabs."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "explain"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.delta", timeout=20)
		await self.client.request("chat.send", {"chatId": chat_id, "text": "use tabs"})
		end = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=30))[-1]["data"]
		self.assertEqual("finished", end["stop"])
		last = self.model.requests()[-1]["body"]["messages"]
		self.assertEqual("use tabs", last[-1]["content"], "the answer that would have ended the turn is followed by a step with the message")

	async def test_a_message_the_turn_did_not_take_waits_for_the_next(self):
		chat_id, topic = await self.chat()
		await self.use_model({}, script="long-stream")
		await self.client.request("chat.send", {"chatId": chat_id, "text": "talk"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.delta", timeout=20)
		await self.client.request("chat.send", {"chatId": chat_id, "text": "and then this"})
		await self.client.request("chat.stop", {"chatId": chat_id})
		frames = await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=20)
		told = [f["data"] for f in frames if f.get("kind") == "queue.changed"][-1]
		self.assertEqual({"queued": ["and then this"], "pending": []}, told, "a stopped turn leaves it queued, not lost")
		with self.assertRaises(RpcFailure) as wrong:
			await self.client.request("chat.send", {"chatId": chat_id, "text": "x", "as": "inject"})
		self.assertEqual("bad_request", wrong.exception.code)

	# --- Persistence -------------------------------------------------------------------------------------------------

	async def test_chats_survive_a_restart_and_an_interrupted_turn_pauses(self):
		chat_id, topic = await self.chat()
		self.model.queue("ok-text")
		await self.turn(chat_id)
		await self.use_model({}, script="long-stream")
		await self.client.request("chat.send", {"chatId": chat_id, "text": "talk"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.delta", timeout=20)
		self.client.abort()
		self.server.process.kill()
		self.server.process.wait(10)
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()
		summary = await self.summary(chat_id)
		self.assertEqual((2, "paused", "interrupted"), (summary["turnCount"], summary["state"], summary["stateReason"]))
		history = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"]
		self.assertEqual(["finished", "interrupted"], [t["stop"] for t in history])
		self.model.queue("ok-text")
		await self.use_model({})
		await self.client.sub([topic])
		await self.client.next_frame(topic)
		self.assertEqual("finished", (await self.turn(chat_id, "continue"))["stop"], "a paused chat continues with the next message")

	async def test_an_interrupted_agent_turn_goes_on_by_itself(self):
		"""ai.resumeInterrupted is "agent" by default: an Agent turn a crash cut off goes on by itself once the server is
		back, its message IED's (docs/12 "Restart behavior"); an Ask chat waits, as the check above shows."""
		chat_id, topic = await self.chat(mode="agent")
		await self.use_model({}, script="long-stream")
		await self.client.request("chat.send", {"chatId": chat_id, "text": "work"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.delta", timeout=20)
		self.client.abort()
		self.server.process.kill()
		self.server.process.wait(10)
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()
		deadline = time.time() + 30
		while True:
			history = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"]
			if len(history) == 2:
				break
			self.assertLess(time.time(), deadline, "the cut-off turn did not go on: %r" % [t["stop"] for t in history])
			await asyncio.sleep(0.25)
		self.assertEqual("interrupted", history[0]["stop"])
		self.assertEqual("interrupted", history[1]["user"].get("resumed"), "IED started it, not the owner")
		self.assertIn("cut it off", history[1]["user"]["text"])
		await self.client.request("chat.stop", {"chatId": chat_id})

	async def test_a_chat_file_that_is_not_utf8_still_loads(self):
		"""A running.json written before C4 BUG-384 held a console's code-page bytes as they were, and the JSON reader
		refused it: the chat came back without its turn. Its stray bytes now read as U+FFFD."""
		chat_id, topic = await self.chat()
		await self.use_model({}, script="long-stream")
		await self.client.request("chat.send", {"chatId": chat_id, "text": "talk"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.delta", timeout=20)
		self.client.abort()
		self.server.process.kill()
		self.server.process.wait(10)
		running = os.path.join(local_state(self.data, self.repo), "chats", chat_id, "running.json")
		with open(running, "rb") as f:
			data = f.read()
		self.assertIn(b'"talk"', data)
		with open(running, "wb") as f:
			f.write(data.replace(b'"talk"', b'"talk \x83 roadmap"', 1))
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()
		history = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"]
		self.assertEqual(["interrupted"], [t["stop"] for t in history], "the turn the restart interrupted is there")
		self.assertIn("talk " + chr(0xFFFD) + " roadmap", json.dumps(history, ensure_ascii=False))
		self.assertFalse(os.path.exists(running), "and taken in, as any interrupted turn")

	async def test_delete_keeps_the_chat_restorable_until_it_is_purged(self):
		chat_id, topic = await self.chat()
		self.model.queue("ok-text")
		await self.turn(chat_id)
		state = local_state(self.data, self.repo)
		folder = os.path.join(state, "chats", chat_id)
		kept = os.path.join(state, "chats-deleted", chat_id)
		self.assertTrue(os.path.exists(os.path.join(folder, "turns.jsonl")))
		self.assertFalse(os.path.exists(os.path.join(self.repo, ".ied", "local")), "a folder's chats live in the app data, never in the folder")
		await self.client.sub(["chats:w1"])
		await self.client.next_frame("chats:w1")
		reply = await self.client.request("chat.delete", {"chatId": chat_id})
		self.assertFalse(os.path.exists(folder))
		self.assertTrue(os.path.exists(os.path.join(kept, "turns.jsonl")), "kept whole among the deleted chats")
		listed = await self.client.request("chat.list", {"wsId": "w1"})
		self.assertEqual([], [c for c in listed["chats"] if c["chatId"] == chat_id])
		entry = listed["deleted"][0]
		self.assertEqual((chat_id, 1, 30 * DAY_MS), (entry["chatId"], entry["turnCount"], entry["purgeMs"] - entry["deletedMs"]), "kept 30 days by default")
		self.assertEqual(entry, reply["deleted"])
		restored = await self.client.request("chat.restore", {"chatId": chat_id})
		self.assertEqual((chat_id, 1), (restored["chatId"], restored["turnCount"]))
		self.assertFalse(os.path.exists(kept))
		listed = await self.client.request("chat.list", {"wsId": "w1"})
		self.assertEqual(([chat_id], []), ([c["chatId"] for c in listed["chats"]], listed["deleted"]))
		await self.client.sub([topic])
		await self.client.next_frame(topic)
		self.model.queue("ok-text")
		self.assertEqual("finished", (await self.turn(chat_id, "again"))["stop"], "a restored chat goes on")
		await self.client.request("chat.delete", {"chatId": chat_id})
		await self.client.request("chat.purge", {"chatId": chat_id})
		self.assertFalse(os.path.exists(kept))
		listed = await self.client.request("chat.list", {"wsId": "w1"})
		self.assertEqual(([], []), (listed["chats"], listed["deleted"]))
		frames = await self.client.wait_for("chats:w1", lambda f: f.get("kind") == "chat.purged")
		kinds = [f["kind"] for f in frames if f.get("kind") in ("chat.deleted", "chat.restored", "chat.purged")]
		self.assertEqual(["chat.deleted", "chat.restored", "chat.deleted", "chat.purged"], kinds)
		with self.assertRaises(RpcFailure) as gone:
			await self.client.request("chat.restore", {"chatId": chat_id})
		self.assertEqual("not_found", gone.exception.code)

	async def test_a_deleted_chat_is_purged_once_kept_its_days(self):
		old_id, _ = await self.chat()
		new_id, _ = await self.chat()
		await self.client.request("settings.set", {"tier": "app", "key": "ai.chats.keepDeletedDays", "value": 2})
		await self.client.request("chat.delete", {"chatId": old_id})
		await self.client.request("chat.delete", {"chatId": new_id})
		self.client.abort()
		self.server.stop(remove_data=False)
		state = local_state(self.data, self.repo)
		marker = os.path.join(state, "chats-deleted", old_id, "deleted.json")
		with open(marker, encoding="utf-8") as f:
			deleted_ms = json.load(f)["deletedMs"]
		write(marker, json.dumps({"deletedMs": deleted_ms - 3 * DAY_MS}))
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()
		deleted = (await self.client.request("chat.list", {"wsId": "w1"}))["deleted"]
		self.assertEqual([new_id], [d["chatId"] for d in deleted], "the one kept past ai.chats.keepDeletedDays went at load")
		self.assertEqual(2 * DAY_MS, deleted[0]["purgeMs"] - deleted[0]["deletedMs"])
		self.assertFalse(os.path.exists(os.path.join(state, "chats-deleted", old_id)))

	async def test_chats_an_older_server_left_in_the_folder_move_to_the_app_data(self):
		chat_id, _ = await self.chat()
		self.model.queue("ok-text")
		await self.turn(chat_id)
		self.client.abort()
		self.server.stop(remove_data=False)
		state = local_state(self.data, self.repo)
		os.makedirs(os.path.join(self.repo, ".ied", "local"))
		shutil.move(os.path.join(state, "chats"), os.path.join(self.repo, ".ied", "local", "chats"))
		write(os.path.join(self.repo, ".ied", "local", "lsp-options", "Game.json"), "{}")
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()
		chats = (await self.client.request("chat.list", {"wsId": "w1"}))["chats"]
		self.assertIn(chat_id, [c["chatId"] for c in chats], "the chat is still there")
		self.assertTrue(os.path.exists(os.path.join(state, "chats", chat_id, "turns.jsonl")), "moved to the app data")
		self.assertEqual(["lsp-options"], os.listdir(os.path.join(self.repo, ".ied", "local")), "what is not IED's own stays")
