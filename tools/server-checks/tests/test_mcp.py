"""MCP servers' tools in chats (docs/04 "MCP") against tools/fake-mcp: a chat lists a server's tools as deferred stubs
and calls them through IED's own path (its permission checks, approval cards and tool cards), on stdio and on streamable
HTTP; a server's own tool list changing reaches the next turn; a crash restarts it; a server that never answers fails
with a clear error; its tools stay offered from the cache after a restart; an untrusted workspace has none; Settings'
methods test, stop and restart a server; and a Claude chat gets them through IED's in-process MCP server."""

import json
import os
import shutil
import subprocess
import sys
import time

from test_chat import ChatCase, call_step, text_step, write
from test_claude import ClaudeCase
from server import REPO, local_state

FAKE_MCP = os.path.join(REPO, "tools", "fake-mcp", "fake_mcp.py")
TOOLS = ["echo", "add", "image", "slow", "crash", "fail", "add_tool", "ping_back", "resource", "hang"]


def add_mcp_plugin(home):
	"""The MCP plugin's manifest and settings (its code is compiled into the server, docs/26), without its UI."""
	plugin = os.path.join(home, "plugins", "mcp")
	os.makedirs(plugin, exist_ok=True)
	shutil.copy(os.path.join(REPO, "plugins", "mcp", "settings.schema.json"), os.path.join(plugin, "settings.schema.json"))
	with open(os.path.join(REPO, "plugins", "mcp", "ied-plugin.json"), encoding="utf-8") as f:
		manifest = json.loads("".join(line for line in f if not line.lstrip().startswith("//")))
	manifest["runtime"].pop("ui", None)
	write(os.path.join(plugin, "ied-plugin.json"), json.dumps(manifest))


def stdio_server(log, *extra, **entry):
	return dict({"command": sys.executable, "args": ["-I", FAKE_MCP, "--log", log] + list(extra)}, **entry)


class McpCase(ChatCase):
	def setUp(self):
		self.http = None
		super().setUp()

	def start_server(self):
		add_mcp_plugin(self.home)
		return super().start_server()

	def tearDown(self):
		if self.http:
			self.http.kill()
			self.http.wait(10)
			self.http.stdout.close()
		super().tearDown()

	def log_path(self, name="fake"):
		return os.path.join(self.root, name + "-mcp.jsonl")

	def received(self, name="fake"):
		path = self.log_path(name)
		if not os.path.exists(path):
			return []
		with open(path, encoding="utf-8") as f:
			return [json.loads(line) for line in f if line.strip()]

	def methods(self, name="fake"):
		return [entry["message"].get("method") for entry in self.received(name) if "message" in entry]

	def pids(self, name="fake"):
		return [entry["pid"] for entry in self.received(name) if entry.get("started")]

	async def use_servers(self, servers):
		await self.client.request("settings.set", {"tier": "app", "key": "mcp.servers", "value": servers})

	async def status(self, name="fake"):
		snapshot = await self.client.request("mcp.status", {"wsId": "w1"})
		found = [s for s in snapshot["servers"] if s["name"] == name]
		return found[0] if found else None

	async def wait_status(self, check, name="fake", timeout=20):
		deadline = time.time() + timeout
		while True:
			server = await self.status(name)
			if server and check(server):
				return server
			if time.time() > deadline:
				self.fail("the server never got there: %s" % server)
			time.sleep(0.2)

	def sent_tools(self, request=-1):
		return {t["function"]["name"]: t["function"] for t in self.model.requests()[request]["body"]["tools"]}

	def results(self, request=-1):
		return {m["tool_call_id"]: m["content"] for m in self.model.requests()[request]["body"]["messages"] if m["role"] == "tool"}

	async def output(self):
		"""What the workspace's MCP output source keeps."""
		await self.client.sub(["out:w1:mcp"])
		frames = await self.client.wait_for("out:w1:mcp", lambda f: f["t"] == "snap", 10)
		return frames[-1]["data"].get("tail", "")


class McpChatTests(McpCase):
	async def test_a_chat_lists_and_calls_a_servers_tools(self):
		await self.use_servers({"fake": stdio_server(self.log_path(), "--page-size", "4", permission="auto")})
		chat_id, _ = await self.chat(permission="auto")
		self.model.queue(call_step("mcp_fake_echo", {"text": "hi"}, "c1"), call_step("mcp_fake_add", {"a": 2, "b": 3}, "c2"),
			call_step("mcp_fake_image", {}, "c3"), call_step("mcp_fake_resource", {}, "c4"), call_step("mcp_fake_fail", {}, "c5"),
			call_step("mcp_fake_ping_back", {}, "c6"), text_step("Done."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=90))["stop"])
		first = self.sent_tools(0)
		self.assertEqual(["mcp_fake_" + t for t in TOOLS], [n for n in first if n.startswith("mcp_")], "every page of tools/list, in order")
		self.assertEqual({"name": "mcp_fake_echo", "description": "fake (MCP): Say the text back. [deferred]", "parameters": {"type": "object"}}, first["mcp_fake_echo"], "a deferred stub")
		self.assertEqual(self.sent_tools(0), self.sent_tools(-1), "the list stays as it was through the turn")
		system = self.model.requests()[0]["body"]["messages"][0]["content"]
		self.assertIn("- fake (fake-mcp): 10 tools. Fake tools for IED's checks", system, "the servers and their instructions in the system message")
		results = self.results()
		self.assertEqual("echo: hi", results["c1"])
		self.assertEqual("5", results["c2"])
		self.assertIn("a pixel", results["c3"])
		self.assertIn("[resource fake://notes/1]\na note", results["c4"])
		self.assertIn("[resource link fake://notes/2 - second note: not read]", results["c4"])
		self.assertIn("it went wrong", results["c5"])
		self.assertEqual("pinged: {}", results["c6"], "IED answered the server's ping")
		segments = {s["callId"]: s for s in self.segments(await self.last_turn(chat_id), "tool")}
		tools = [s for s in self.segments(await self.last_turn(chat_id), "tool")]
		self.assertEqual(("mcp_fake_echo", "ok", "MCP fake"), (tools[0]["tool"], tools[0]["state"], tools[0].get("source")), "the card says where it ran")
		self.assertTrue(tools[4]["result"]["isError"], "isError makes an error result")
		image = tools[2]["result"]["images"][0]
		self.assertTrue(image["path"].startswith(".ied/local/mcp/media/fake-") and image["path"].endswith(".png"), image)
		self.assertTrue(os.path.exists(os.path.join(local_state(self.data, self.repo), image["path"][len(".ied/local/"):])), "the picture is a file of the workspace's local state")
		methods = self.methods()
		self.assertEqual(["initialize", "notifications/initialized", "tools/list", "tools/list", "tools/list"], methods[:5], "the handshake, then three pages")
		self.assertEqual(6, methods.count("tools/call"))
		hello = [e["message"] for e in self.received() if e.get("message", {}).get("method") == "initialize"][0]
		self.assertEqual(("2025-06-18", "IED"), (hello["params"]["protocolVersion"], hello["params"]["clientInfo"]["name"]))
		server = await self.status()
		self.assertEqual(("running", 10, "fake-mcp", "1.2.3"), (server["state"], len(server["tools"]), server["serverName"], server["serverVersion"]))
		tail = await self.output()
		self.assertIn("[fake] running: fake-mcp 1.2.3, MCP 2025-06-18, 10 tools", tail)
		self.assertIn("[fake] fake_mcp fake-mcp on stdio", tail, "its standard error")
		self.assertIn("[fake] info: echoing hi", tail, "its log notifications")
		cache = json.load(open(os.path.join(local_state(self.data, self.repo), "mcp", "tools.json"), encoding="utf-8"))
		self.assertEqual(10, len(cache["servers"]["fake"]["tools"]))

	async def test_a_call_asks_until_the_owner_allows_it_for_the_chat(self):
		await self.use_servers({"fake": stdio_server(self.log_path())})
		chat_id, topic = await self.chat(permission="auto")
		self.model.queue(call_step("mcp_fake_echo", {"text": "one"}, "c1"), call_step("mcp_fake_echo", {"text": "two"}, "c2"), text_step("Done."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "echo"})
		request = (await self.client.wait_for(topic, lambda f: f.get("kind") == "approval.request", timeout=30))[-1]["data"]
		self.assertEqual(("mcp_fake_echo", "plugin(ied.mcp/mcp_fake_echo)"), (request["tool"], request["subject"]), "asks in auto mode too")
		self.assertEqual("Call echo (Echo) on the MCP server fake", request["summary"])
		self.assertNotIn("tools/call", self.methods(), "nothing is called before the decision")
		await self.client.request("chat.approve", {"chatId": chat_id, "approvalId": request["approvalId"], "decision": "chat"})
		frames = await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=30)
		self.assertEqual(0, len([f for f in frames if f.get("kind") == "approval.request"]), "allowed for the chat: the second call does not ask")
		self.assertEqual({"c1": "echo: one", "c2": "echo: two"}, self.results())
		await self.use_servers({"fake": stdio_server(self.log_path(), permission="read")})
		chat_id, _ = await self.chat(mode="plan")
		self.model.queue(call_step("mcp_fake_echo", {"text": "plan"}, "c3"), text_step("Read."))
		frames_end = await self.turn(chat_id, timeout=60)
		self.assertEqual("finished", frames_end["stop"])
		self.assertEqual("echo: plan", self.results()["c3"], "a read-only server runs in Plan mode without asking")

	async def test_a_changed_tool_list_reaches_the_next_turn(self):
		await self.use_servers({"fake": stdio_server(self.log_path(), permission="auto")})
		chat_id, _ = await self.chat(permission="auto")
		self.model.queue(call_step("mcp_fake_add_tool", {}, "c1"), text_step("Added."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=60))["stop"])
		self.assertNotIn("mcp_fake_extra_11", self.sent_tools())
		await self.wait_status(lambda s: len(s["tools"]) == 11)
		self.model.queue(call_step("mcp_fake_extra_11", {}, "c2"), text_step("Called."))
		self.assertEqual("finished", (await self.turn(chat_id, "again", timeout=60))["stop"])
		self.assertIn("mcp_fake_extra_11", self.sent_tools(), "notifications/tools/list_changed listed them again")
		self.assertIn("[fake] its tools changed: 11 tools", await self.output())

	async def test_a_crash_restarts_the_server(self):
		await self.use_servers({"fake": stdio_server(self.log_path(), permission="auto")})
		chat_id, _ = await self.chat(permission="auto")
		self.model.queue(call_step("mcp_fake_crash", {}, "c1"), text_step("It crashed."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=60))["stop"])
		self.assertIn("fake: crash failed: the server exited before it answered", self.results()["c1"])
		server = await self.wait_status(lambda s: s["state"] == "running" and s["restarts"] == 1)
		self.assertEqual(2, len(set(self.pids())), "started again: another process")
		self.model.queue(call_step("mcp_fake_echo", {"text": "back"}, "c2"), text_step("Back."))
		self.assertEqual("finished", (await self.turn(chat_id, "again", timeout=60))["stop"])
		self.assertEqual("echo: back", self.results()["c2"])
		tail = await self.output()
		self.assertIn("[fake] exited with code 3; it printed: ", tail)
		self.assertIn("fake_mcp: crashing as asked; starting it again in 1 s", tail)

	async def test_a_server_that_never_answers_fails_with_a_clear_error(self):
		await self.use_servers({"fake": stdio_server(self.log_path(), "--hang-initialize", startSeconds=2)})
		chat_id, _ = await self.chat(permission="auto")
		self.model.queue(text_step("No tools."))
		started = time.time()
		self.assertEqual("finished", (await self.turn(chat_id, timeout=60))["stop"])
		self.assertLess(time.time() - started, 20, "the turn waited for the start at most its startSeconds")
		self.assertFalse([n for n in self.sent_tools() if n.startswith("mcp_")])
		server = await self.status()
		self.assertEqual("failed", server["state"])
		self.assertIn("initialize: the server did not answer within 2 s; the request was cancelled (mcp.servers.fake.startSeconds)", server["error"])
		await self.use_servers({"slow": stdio_server(self.log_path("slow"), permission="auto", timeoutSeconds=2)})
		chat_id, _ = await self.chat(permission="auto")
		self.model.queue(call_step("mcp_slow_hang", {}, "c1"), text_step("It hung."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=60))["stop"])
		self.assertIn("slow: hang failed: the server did not answer within 2 s; the request was cancelled (mcp.servers.slow.timeoutSeconds)", self.results()["c1"])
		cancelled = [e["message"] for e in self.received("slow") if e.get("message", {}).get("method") == "notifications/cancelled"]
		self.assertTrue(cancelled and cancelled[0]["params"]["reason"] == "no answer in time", cancelled)

	async def test_stop_cancels_a_call_at_the_server(self):
		await self.use_servers({"fake": stdio_server(self.log_path(), permission="auto")})
		chat_id, topic = await self.chat(permission="auto")
		self.model.queue(call_step("mcp_fake_slow", {"seconds": 30}, "c1"), text_step("Stopped."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "wait"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "tool.state" and f["data"].get("state") == "running", timeout=30)
		deadline = time.time() + 10
		while "tools/call" not in self.methods() and time.time() < deadline:
			time.sleep(0.1)
		await self.client.request("chat.stop", {"chatId": chat_id})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=30)
		deadline = time.time() + 10
		while not [e for e in self.received() if "cancelled" in e] and time.time() < deadline:
			time.sleep(0.1)
		self.assertTrue([e for e in self.received() if "cancelled" in e], "the server was told: notifications/cancelled")

	async def test_tools_are_offered_from_the_cache_after_a_restart(self):
		await self.use_servers({"fake": stdio_server(self.log_path(), permission="auto")})
		chat_id, _ = await self.chat(permission="auto")
		self.model.queue(text_step("Hi."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=60))["stop"])
		before = self.sent_tools()
		self.client.abort()
		self.server.stop(remove_data=False)
		self.server = self.start_server()
		await self.connect()
		await self.client.sub(["chat:" + chat_id])
		await self.client.next_frame("chat:" + chat_id)
		self.model.queue(text_step("Hi again."))
		self.assertEqual("finished", (await self.turn(chat_id, "again", timeout=60))["stop"])
		self.assertEqual(before, self.sent_tools(), "the same tools, byte for byte")
		self.assertEqual(1, len(self.pids()), "without starting the server")
		self.model.queue(call_step("mcp_fake_echo", {"text": "now"}, "c1"), text_step("Started."))
		self.assertEqual("finished", (await self.turn(chat_id, "call", timeout=60))["stop"])
		self.assertEqual("echo: now", self.results()["c1"], "the call started it")
		self.assertEqual(2, len(self.pids()))

	async def test_an_untrusted_workspace_has_no_mcp_tools(self):
		await self.use_servers({"fake": stdio_server(self.log_path(), permission="auto")})
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": False})
		chat_id, _ = await self.chat(permission="auto")
		self.model.queue(text_step("Nothing."))
		await self.turn(chat_id, timeout=60)
		self.assertFalse([n for n in self.sent_tools() if n.startswith("mcp_")])
		self.assertEqual([], self.pids(), "nothing started")
		snapshot = await self.client.request("mcp.status", {"wsId": "w1"})
		self.assertFalse(snapshot["trusted"])

	async def test_settings_test_stop_and_restart(self):
		tested = await self.client.request("mcp.test", {"wsId": "w1", "name": "draft", "server": stdio_server(self.log_path("draft"))})
		self.assertTrue(tested["ok"], tested)
		self.assertEqual(10, len(tested["tools"]))
		self.assertEqual("mcp_draft_echo", tested["tools"][0]["aiName"])
		self.assertIn("fake_mcp fake-mcp on stdio", tested["log"])
		self.assertIsNone(await self.status("draft"), "a test of a server not saved adds none")
		bad = await self.client.request("mcp.test", {"wsId": "w1", "name": "bad", "server": {"command": "no-such-program-here"}})
		self.assertFalse(bad["ok"])
		self.assertIn("there is no program no-such-program-here on PATH", bad["error"])
		crashing = await self.client.request("mcp.test", {"wsId": "w1", "name": "dead", "server": stdio_server(self.log_path("dead"), "--exit-at-start", "4")})
		self.assertFalse(crashing["ok"])
		self.assertIn("exited with code 4", crashing["error"])
		self.assertIn("exiting at start as asked", crashing["error"])
		await self.use_servers({"fake": stdio_server(self.log_path())})
		tested = await self.client.request("mcp.test", {"wsId": "w1", "name": "fake"})
		self.assertTrue(tested["ok"], tested)
		self.assertEqual("running", (await self.status())["state"])
		await self.client.request("mcp.stop", {"wsId": "w1", "name": "fake"})
		self.assertEqual("stopped", (await self.status())["state"])
		self.assertEqual(10, len((await self.status())["tools"]), "its tools stay known")
		await self.client.request("mcp.restart", {"wsId": "w1", "name": "fake"})
		self.assertEqual("running", (await self.status())["state"])
		self.assertEqual(2, len(self.pids()))
		await self.use_servers({"fake": stdio_server(self.log_path(), enabled=False)})
		server = await self.wait_status(lambda s: s["state"] == "stopped")
		self.assertFalse(server["enabled"])


class McpHttpTests(McpCase):
	def start_http(self, *extra):
		self.http = subprocess.Popen([sys.executable, "-I", FAKE_MCP, "--http", "0", "--log", self.log_path()] + list(extra), stdout=subprocess.PIPE, text=True)
		return self.http.stdout.readline().split()[1]

	async def test_a_streamable_http_server_answers_with_events_or_json(self):
		for mode in (["--sse"], []):
			url = self.start_http(*mode)
			await self.use_servers({"web": {"url": url, "permission": "auto"}})
			chat_id, _ = await self.chat(permission="auto")
			self.model.queue(call_step("mcp_web_echo", {"text": "over http"}, "c1"), call_step("mcp_web_add_tool", {}, "c2"), text_step("Done."))
			self.assertEqual("finished", (await self.turn(chat_id, timeout=60))["stop"], mode)
			self.assertEqual("echo: over http", self.results()["c1"], mode)
			sessions = {e["message"].get("method"): e for e in self.received() if "message" in e}
			self.assertIn("tools/call", sessions)
			server = await self.status("web")
			self.assertEqual(("running", "http"), (server["state"], server["transport"]))
			if mode:
				await self.wait_status(lambda s: len(s["tools"]) == 11, name="web")
			self.http.kill()
			self.http.wait(10)
			self.http.stdout.close()
			self.http = None
			os.remove(self.log_path())

	async def test_a_lost_session_starts_a_new_one(self):
		url = self.start_http("--forget-session-after", "4")
		await self.use_servers({"web": {"url": url, "permission": "auto"}})
		chat_id, _ = await self.chat(permission="auto")
		self.model.queue(*[call_step("mcp_web_echo", {"text": str(i)}, "c%d" % i) for i in range(4)], text_step("Done."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=60))["stop"])
		self.assertEqual({"c%d" % i: "echo: %d" % i for i in range(4)}, self.results())
		self.assertGreaterEqual(self.methods().count("initialize"), 2, "initialized again after the 404")
		self.assertIn("the server ended the session: starting a new one", await self.output())

	async def test_https_is_refused_with_why(self):
		tested = await self.client.request("mcp.test", {"wsId": "w1", "name": "far", "server": {"url": "https://example.com/mcp"}})
		self.assertFalse(tested["ok"])
		self.assertIn("https is not supported", tested["error"])


class McpClaudeTests(ClaudeCase):
	def start_server(self):
		add_mcp_plugin(self.home)
		return super().start_server()

	async def test_a_claude_chat_calls_mcp_tools_through_ieds_server(self):
		log = os.path.join(self.root, "fake-mcp.jsonl")
		await self.client.request("settings.set", {"tier": "app", "key": "mcp.servers", "value": {"fake": stdio_server(log, permission="auto")}})
		await self.use_claude()
		self.script({"blocks": [{"tool": "mcp_fake_echo", "input": {"text": "from claude"}, "id": "toolu_m"}]}, {"blocks": [{"text": "It echoed."}]})
		chat_id, _ = await self.chat(permission="auto")
		end = await self.turn(chat_id, "Echo something.", timeout=90)
		self.assertEqual("finished", end["stop"], end)
		tools = [entry["tools"] for entry in self.log() if "tools" in entry][-1]
		self.assertIn("mcp_fake_echo", tools, "in IED's tools/list for the CLI")
		self.assertEqual("echo: from claude", self.tool_results()["toolu_m"]["result"]["content"][0]["text"], "IED ran it and answered the CLI")
		argv = self.sessions_started()[-1]
		config = argv[argv.index("--mcp-config") + 1] if "--mcp-config" in argv else ""
		self.assertNotIn("fake_mcp", config, "the CLI is never given the MCP server itself")
