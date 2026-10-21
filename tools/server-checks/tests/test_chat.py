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
import urllib.request

from frames import FrameChecker
from iedclient import Client, RpcFailure
from server import REPO, IedServer, ServerTestCase

FAKE_MODEL = os.path.join(REPO, "tools", "fake-model", "fake_model.py")
MAIN = "int main(void)\n{\n\treturn 0;\n}\n"
TURN_SECONDS = 60


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
		"bindings": {"provides": [{"name": "demo.count", "version": "1.0.0", "kind": "function"}]},
		"systemMessageSections": [{"id": "files", "function": "DemoCode::Section"}],
		"toolCalls": [
			{"name": "demo_shout", "description": "Shout a file's first line.", "class": "read", "function": "DemoCode::Shout",
				"inputSchema": {"type": "object", "properties": {"path": {"type": "string"}}, "required": ["path"]}},
			{"name": "demo_count", "description": "Count calls.", "class": "read", "binding": "demo.count", "inputSchema": {"type": "object"}},
			{"name": "demo_snap", "description": "Show the screen.", "class": "read", "function": "DemoCode::Snap", "inputSchema": {"type": "object"}}]}}

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


def make_home(home):
	"""An install with the host's schemas, the chat plugin - its server code (compiled into the server, docs/26), its
	prompts, settings and output source, without its UI - and a demo language plugin with a system-message section, a
	skills folder and a command tool (docs/26)."""
	shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(home, "shared", "host-api", "schemas"))
	plugin = os.path.join(home, "plugins", "chat")
	shutil.copytree(os.path.join(REPO, "plugins", "chat", "prompts"), os.path.join(plugin, "prompts"))
	shutil.copy(os.path.join(REPO, "plugins", "chat", "settings.schema.json"), os.path.join(plugin, "settings.schema.json"))
	write(os.path.join(plugin, "ied-plugin.json"), json.dumps({"manifestVersion": 1, "id": "ied.chat", "name": "Chat", "version": "0.1.0", "ied": {"api": "^1"},
		"runtime": {"kind": "builtin", "entry": "Ai::Activate"}, "permissions": ["ai.invoke", "ai.tools", "ai.context", "settings.write", "fs.read", "fs.write", "process.spawn"],
		"contributes": {"settings": {"schema": "settings.schema.json", "page": "AI"}, "outputSources": [{"id": "ai", "title": "AI"}]}}))
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


class ChatTests(ServerTestCase):
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

	async def chat(self, mode="ask", **extra):
		created = await self.client.request("chat.create", dict({"wsId": "w1", "mode": mode}, **extra))
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

	# --- Steering and stopping ---------------------------------------------------------------------------------------

	async def test_stop_cancels_the_turn(self):
		chat_id, topic = await self.chat()
		await self.use_model({}, script="long-stream")
		await self.client.request("chat.send", {"chatId": chat_id, "text": "talk"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.delta", timeout=20)
		await self.client.request("chat.stop", {"chatId": chat_id})
		end = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=20))[-1]["data"]
		self.assertEqual("user_cancelled", end["stop"])
		self.assertEqual("idle", (await self.summary(chat_id))["state"])

	async def test_queued_messages_run_next_and_injected_ones_steer(self):
		chat_id, topic = await self.chat()
		self.model.queue("long-stream", "ok-text")
		await self.client.request("chat.send", {"chatId": chat_id, "text": "first"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.delta", timeout=20)
		queued = await self.client.request("chat.send", {"chatId": chat_id, "text": "second", "as": "queue"})
		self.assertEqual(1, queued["queued"])
		await self.client.request("chat.stop", {"chatId": chat_id})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=20)
		self.assertEqual(1, (await self.summary(chat_id))["turnCount"], "a stopped turn does not start the queue")
		self.assertEqual({"queued": []}, await self.client.request("chat.unqueue", {"chatId": chat_id, "index": 0}))
		with self.assertRaises(RpcFailure) as gone:
			await self.client.request("chat.unqueue", {"chatId": chat_id, "index": 0})
		self.assertEqual("not_found", gone.exception.code)
		self.model.queue(call_step("fs_list", {"path": "src"}), "ok-text")
		await self.client.request("chat.send", {"chatId": chat_id, "text": "third"})
		await self.client.request("chat.send", {"chatId": chat_id, "text": "use tabs", "as": "inject"})
		self.model.queue("ok-text")
		ends = await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end" and f["data"]["turnId"] != "u1", timeout=30)
		history = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"]
		self.assertEqual(["first", "third"], [t["user"]["text"] for t in history[:2]])
		steering = [s for t in history for s in self.segments(t, "steering")]
		self.assertEqual(["use tabs"], [s["text"] for s in steering])

	async def test_steering_during_the_last_answer_still_reaches_the_model(self):
		chat_id, topic = await self.chat()
		long_answer = inline({"text": "word " * 400, "chunk": 4, "delayMs": 150}, {"finish": "stop"})
		self.model.queue(long_answer, text_step("Using tabs."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "explain"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.delta", timeout=20)
		await self.client.request("chat.send", {"chatId": chat_id, "text": "use tabs", "as": "inject"})
		end = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=30))[-1]["data"]
		self.assertEqual("finished", end["stop"])
		last = self.model.requests()[-1]["body"]["messages"]
		self.assertIn("use tabs", last[-1]["content"], "the answer that would have ended the turn is followed by a step with the steering")

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

	async def test_delete_removes_the_chat_and_its_files(self):
		chat_id, _ = await self.chat()
		self.model.queue("ok-text")
		await self.turn(chat_id)
		folder = os.path.join(self.repo, ".ied", "local", "chats", chat_id)
		self.assertTrue(os.path.exists(os.path.join(folder, "turns.jsonl")))
		await self.client.request("chat.delete", {"chatId": chat_id})
		self.assertFalse(os.path.exists(folder))
		self.assertEqual([], [c for c in (await self.client.request("chat.list", {"wsId": "w1"}))["chats"] if c["chatId"] == chat_id])
