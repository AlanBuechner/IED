"""Plugin code (docs/26 section 2): a plugin whose runtime is c4 has its code compiled by the server's script host against
the plugin API (src/api/), started through its entry, and reached through server bindings (plugins.call), its computed
contributions and the protocol events it listens to; a build that does not compile, code that faults, code that
includes the server beyond the API, a new build that fails while one runs, and a project's own plugin in a trusted or
untrusted workspace, reloaded as its code is edited."""

import asyncio
import json
import os
import shutil
import tempfile
import time

from frames import FrameChecker
from iedclient import Client, RpcFailure
from server import EXECUTABLE_ENV, REPO, IedServer, ServerTestCase

COMPILE_SECONDS = 120
BROKEN = "\nnamespace Probe { int32 Broken() { return missing_name; } }\n"

PROBE = r'''include "api/plugin.ch"

namespace Probe
{
	int64 heard = 0;
	IED::PluginContext* plugin = nullptr;

	IED::Result Echo(const Serialization::DomNode& args)
	{
		Core::StringBuilder out;
		out.Append("{\"said\":");
		Serialization::WriteJsonString(args.Member("text").AsView(), &out);
		out.Append(",\"heard\":");
		out.AppendInt(heard);
		out.Append(",\"version\":VERSION}");
		return IED::Answer(out.View());
	}

	IED::Result Crash(const Serialization::DomNode& args)
	{
		int32* nothing = (int32*)(void*)args.Member("address").AsInt();
		*nothing = 7;
		return IED::Answer(Core::StringView("{}"));
	}

	void Opened(const IED::Workspace& ws)
	{
		Core::StringBuilder entries;
		entries.Append("[{\"id\":\"probe.hello\",\"title\":\"Hello ");
		entries.Append(ws.name.View());
		entries.Append("\"}]");
		Core::String problem;
		plugin->Contribute(Core::StringView("commands"), ws.id.View(), entries.View(), problem);
	}

	void Activate(IED::PluginContext& context)
	{
		plugin = &context;
		context.Log(Core::StringView("probe started"));
		context.Provide(Core::StringView("probe.echo"), Echo);
		context.Provide(Core::StringView("probe.crash"), Crash);
		context.Listen(Core::StringView("tree.changed"), [](const IED::Event& event) { heard += 1; });
		context.OnWorkspace(Opened);
	}
}
'''

SERVER = r'''include "api/plugin.ch"

namespace Serve
{
	int64 changes = 0;
	int64 trusts = 0;
	IED::PluginContext* plugin = nullptr;

	Core::String Counts()
	{
		Core::StringBuilder out;
		out.Append("{\"changes\":");
		out.AppendInt(changes);
		out.Append(",\"trusts\":");
		out.AppendInt(trusts);
		out.Append("}");
		return out.ToString();
	}

	Core::String Ping(const IED::Request& request)
	{
		if (request.params.Str("say", Core::StringView("")).View() == "boom") throw IED::RpcError(IED::ErrorCode::Conflict, "boom refused");
		plugin->Publish(Core::StringView("test.serve/news"), "said", Core::StringView("{\"n\":1}"));
		plugin->Output(request.params.Str("wsId").View(), Core::StringView("serve"), Core::StringView("pinged\n"));
		return Counts();
	}

	void Activate(IED::PluginContext& context)
	{
		plugin = &context;
		Core::String problem;
		context.Method(Core::StringView("test.serve/ping"), Ping, problem);
		if (!context.Method(Core::StringView("git.repos"), Ping, problem)) context.Log(problem.View());
		context.Topic(Core::StringView("test.serve/news"), Core::StringView("{\"pushed\":true}"));
		context.TopicFrom(Core::StringView("test.serve/counts"), Counts);
		context.OnFilesChanged([](const IED::FileChanges& files) { changes += files.changes.Count(); });
		context.OnTrustChanged([](const IED::Workspace& ws) { trusts += 1; });
	}
}
'''


# A binding that calls the plugin's own binding (which runs at once, on its thread), which calls another plugin's: the
# web search tool's shape (its tool calls websearch.search, which calls tor.proxy).
RELAY = r'''include "api/plugin.ch"

namespace Relay
{
	IED::Result Inner(const Serialization::DomNode& args)
	{
		return IED::CallBinding(Core::StringView("probe.echo"), Core::StringView("{\"text\":\"relayed\"}"));
	}

	IED::Result Outer(const Serialization::DomNode& args)
	{
		return IED::CallBinding(Core::StringView("relay.inner"), Core::StringView("{}"));
	}

	void Activate(IED::PluginContext& context)
	{
		context.Provide(Core::StringView("relay.inner"), Inner);
		context.Provide(Core::StringView("relay.outer"), Outer);
	}
}
'''


def probe_manifest(plugin_id="test.probe", **fields):
	body = {
		"manifestVersion": 1, "id": plugin_id, "name": "Probe", "version": "1.0.0", "ied": {"api": "^1"},
		"runtime": {"kind": "c4", "entry": "Probe::Activate"},
		"contributes": {"bindings": {"provides": [
			{"name": "probe.echo", "version": "1.0.0", "kind": "function"},
			{"name": "probe.crash", "version": "1.0.0", "kind": "function"}]}},
	}
	body.update(fields)
	return body


def write(path, text):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8", newline="") as f:
		f.write(text)


class PluginCodeTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-code-")
		self.home = os.path.join(self.root, "home")
		self.repo = os.path.join(self.root, "Game")
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		os.makedirs(os.path.join(self.home, "plugins"))
		write(os.path.join(self.repo, "main.cf"), "int32 main() { return 0; }\n")
		self.server = None

	def tearDown(self):
		if self.server:
			self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	def add(self, folder, body, code):
		write(os.path.join(folder, "ied-plugin.json"), json.dumps(body, indent="\t"))
		for name, text in code.items():
			write(os.path.join(folder, *name.split("/")), text)
		return folder

	def builtin(self, body, code):
		return self.add(os.path.join(self.home, "plugins", body["id"]), body, code)

	def start(self, data=None, executable=None, options=()):
		self.server = IedServer(data=data, extra=("--home", self.home, "--workspace", self.repo) + tuple(options), executable=executable)
		self.assertTrue(self.server.started, self.server.error)
		self.cookie = self.server.sign_in()

	async def asyncTearDown(self):
		if getattr(self, "client", None):
			self.assertEqual([], self.client.violations)
			self.client.abort()

	async def connect(self):
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})

	async def code(self, plugin_id, ws=None):
		params = {"wsId": ws} if ws else {}
		for plugin in (await self.client.request("plugins.list", params))["plugins"]:
			if plugin["id"] == plugin_id and "code" in plugin:
				return plugin["code"]
		return None

	async def settled(self, plugin_id, ws=None, after=None):
		"""The plugin's code once it is no longer compiling, and has compiled again since `after` (a code state)."""
		deadline = time.monotonic() + COMPILE_SECONDS
		while time.monotonic() < deadline:
			code = await self.code(plugin_id, ws)
			if code and code["state"] not in ("idle", "compiling") and code["builds"] > (after["builds"] if after else 0):
				return code
			await asyncio.sleep(0.25)
		self.fail("the code of %s did not settle: %s" % (plugin_id, await self.code(plugin_id, ws)))

	async def echo(self, text, ws=None):
		params = {"name": "probe.echo", "args": {"text": text}}
		if ws:
			params["wsId"] = ws
		return (await self.client.request("plugins.call", params))["result"]

	async def test_code_starts_provides_bindings_and_contributes(self):
		self.builtin(probe_manifest(), {"code/probe.cf": PROBE.replace("VERSION", "1")})
		self.start()
		await self.connect()
		code = await self.settled("test.probe")
		self.assertEqual("active", code["state"], code)
		self.assertEqual({"said": "hi", "heard": 0, "version": 1}, await self.echo("hi"))
		contributions = await self.client.request("plugins.contributions", {"wsId": "w1"})
		probe = [p for p in contributions["plugins"] if p["id"] == "test.probe"][0]
		self.assertEqual([{"id": "probe.hello", "title": "Hello Game"}], probe["contributes"]["commands"], "computed for the open workspace")
		with self.assertRaises(RpcFailure) as missing:
			await self.client.request("plugins.call", {"name": "probe.nothing"})
		self.assertEqual("not_found", missing.exception.code)

	async def test_methods_topics_outputs_and_workspace_events(self):
		self.builtin(probe_manifest("test.serve", runtime={"kind": "c4", "entry": "Serve::Activate"}, permissions=["fs.read"],
			contributes={"outputSources": [{"id": "serve", "title": "Serve"}]}), {"code/serve.cf": SERVER})
		self.start()
		await self.connect()
		self.assertEqual("active", (await self.settled("test.serve"))["state"])
		await self.client.sub(["test.serve/news", "test.serve/counts", "out:w1:serve"])
		news = await self.client.next_frame("test.serve/news")
		self.assertEqual({"pushed": True}, news["data"], "a pushed snapshot")
		counts = await self.client.next_frame("test.serve/counts")
		self.assertEqual({"changes": 0, "trusts": 0}, counts["data"], "a pulled snapshot")
		await self.client.next_frame("out:w1:serve")
		self.assertEqual({"changes": 0, "trusts": 0}, await self.client.request("test.serve/ping", {"wsId": "w1"}))
		said = await self.client.next_frame("test.serve/news")
		self.assertEqual(("said", {"n": 1}), (said["kind"], said["data"]))
		appended = await self.client.next_frame("out:w1:serve")
		self.assertEqual("pinged\n", appended["data"]["text"])
		with self.assertRaises(RpcFailure) as taken:
			await self.client.request("git.repos", {"wsId": "w1"})
		self.assertEqual("not_found", taken.exception.code, "a plugin that is not built in names its methods after itself")
		write(os.path.join(self.repo, "changed.txt"), "x")
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		deadline = time.monotonic() + 10
		while time.monotonic() < deadline:
			counts = await self.client.request("test.serve/ping", {"wsId": "w1"})
			if counts["changes"] > 0 and counts["trusts"] > 0:
				break
			await asyncio.sleep(0.2)
		self.assertGreater(counts["changes"], 0, "file changes reach OnFilesChanged")
		self.assertEqual(1, counts["trusts"], "trusting the workspace reaches OnTrustChanged")

	async def test_a_script_method_refuses_with_a_protocol_error(self):
		# Fails until C4Script keeps a throw inside a guarded call working (reported to the C4 session, docs/26 open items).
		self.builtin(probe_manifest("test.serve", runtime={"kind": "c4", "entry": "Serve::Activate"}, permissions=["fs.read"],
			contributes={"outputSources": [{"id": "serve", "title": "Serve"}]}), {"code/serve.cf": SERVER})
		self.start()
		await self.connect()
		self.assertEqual("active", (await self.settled("test.serve"))["state"])
		with self.assertRaises(RpcFailure) as refused:
			await self.client.request("test.serve/ping", {"wsId": "w1", "say": "boom"})
		self.assertEqual(("conflict", "boom refused"), (refused.exception.code, refused.exception.message), "a method's RpcError reaches the client")
		self.assertEqual("active", (await self.code("test.serve"))["state"], "refusing is not a fault")

	async def test_events_reach_the_code_that_listens(self):
		self.builtin(probe_manifest(), {"code/probe.cf": PROBE.replace("VERSION", "1")})
		self.start()
		await self.connect()
		await self.settled("test.probe")
		write(os.path.join(self.repo, "new.txt"), "x")
		deadline = time.monotonic() + 10
		while (await self.echo("x"))["heard"] == 0 and time.monotonic() < deadline:
			await asyncio.sleep(0.2)
		self.assertGreater((await self.echo("x"))["heard"], 0, "tree.changed reached the plugin")

	async def test_a_binding_through_its_own_binding_reaches_another_plugin(self):
		self.builtin(probe_manifest(), {"code/probe.cf": PROBE.replace("VERSION", "1")})
		self.builtin(probe_manifest("test.relay", runtime={"kind": "c4", "entry": "Relay::Activate"}, contributes={"bindings": {
			"provides": [{"name": "relay.inner", "version": "1.0.0", "kind": "function"}, {"name": "relay.outer", "version": "1.0.0", "kind": "function"}],
			"consumes": [{"name": "relay.inner", "version": "1.0.0"}, {"name": "probe.echo", "version": "1.0.0"}]}}), {"code/relay.cf": RELAY})
		self.start()
		await self.connect()
		await self.settled("test.probe")
		self.assertEqual("active", (await self.settled("test.relay"))["state"])
		# Its own binding runs with the server lock released; the nested call must take it again, not think it holds it.
		answer = await asyncio.wait_for(self.client.request("plugins.call", {"name": "relay.outer"}), 30)
		self.assertEqual("relayed", answer["result"]["said"])
		self.assertTrue((await self.client.request("ws.list"))["workspaces"], "and the server still answers")

	async def test_a_fault_fails_the_plugin_not_the_server(self):
		self.builtin(probe_manifest(), {"code/probe.cf": PROBE.replace("VERSION", "1")})
		self.start()
		await self.connect()
		await self.settled("test.probe")
		with self.assertRaises(RpcFailure) as crashed:
			await self.client.request("plugins.call", {"name": "probe.crash", "args": {"address": 16}})
		self.assertEqual("plugin_error", crashed.exception.code)
		self.assertIn("faulted", crashed.exception.message)
		code = await self.code("test.probe")
		self.assertEqual("failed", code["state"])
		with self.assertRaises(RpcFailure):
			await self.echo("after")
		self.assertTrue((await self.client.request("ws.list"))["workspaces"], "the server still answers")
		await self.client.request("plugins.reload")
		await self.settled("test.probe", after=code)
		self.assertEqual("after", (await self.echo("after"))["said"], "a reload builds it again")

	async def test_diagnostics_the_allow_list_and_a_failed_rebuild(self):
		folder = self.builtin(probe_manifest(), {"code/probe.cf": "include \"api/plugin.ch\"\nnamespace Probe { void Activate(IED::PluginContext& context) { undefined_thing(); } }\n"})
		self.add(os.path.join(self.home, "plugins", "test.sneaky"), probe_manifest("test.sneaky", contributes={}),
			{"code/sneaky.cf": "include \"api/plugin.ch\"\ninclude \"host/server.ch\"\nnamespace Probe { void Activate(IED::PluginContext& context) {} }\n"})
		self.start()
		await self.connect()
		broken = await self.settled("test.probe")
		self.assertEqual("failed", broken["state"])
		self.assertIn("did not compile", broken["problem"])
		self.assertIn("undefined_thing", broken["problem"])
		sneaky = await self.settled("test.sneaky")
		self.assertIn("not on the allow-list", sneaky["problem"])
		write(os.path.join(folder, "code", "probe.cf"), PROBE.replace("VERSION", "2"))
		await self.client.request("plugins.reload")
		rebuilt = await self.settled("test.probe", after=broken)
		self.assertEqual("active", rebuilt["state"], rebuilt)
		self.assertEqual(2, (await self.echo("v"))["version"])
		write(os.path.join(folder, "code", "probe.cf"), PROBE.replace("VERSION", "3") + BROKEN)
		await self.client.request("plugins.reload")
		deadline = time.monotonic() + COMPILE_SECONDS
		while "previous one runs" not in ((await self.code("test.probe")).get("problem") or "") and time.monotonic() < deadline:
			await asyncio.sleep(0.25)
		code = await self.code("test.probe")
		self.assertEqual("active", code["state"], code)
		self.assertIn("previous one runs", code.get("problem", ""))
		self.assertIn("missing_name", code["problem"])
		self.assertEqual(2, (await self.echo("v"))["version"], "the build that compiled still answers")

	async def test_a_restart_links_the_kept_build(self):
		# Debug scripts are never kept (C4/docs/runtime/04 section 6.4), and a -g server debugs its scripts by default.
		self.builtin(probe_manifest(), {"code/probe.cf": PROBE.replace("VERSION", "5")})
		data = os.path.join(self.root, "data")
		self.start(data, options=("--debug-scripts", "off"))
		await self.connect()
		code = await self.settled("test.probe")
		self.assertEqual("active", code["state"], code)
		self.assertNotIn("cached", code, "the first start compiles")
		self.assertNotIn("debug", code, "not as a debug script")
		self.assertTrue(os.listdir(os.path.join(data, "script-cache")), "and keeps the build")
		self.client.abort()
		self.server.stop(remove_data=False)
		self.start(data, options=("--debug-scripts", "off"))
		await self.connect()
		code = await self.settled("test.probe")
		self.assertEqual(("active", True), (code["state"], code.get("cached")), code)
		self.assertEqual(5, (await self.echo("x"))["version"], "the kept build runs")

	async def test_debug_scripts_compile_at_every_start(self):
		"""--debug-scripts on (a -g server's default): plugin code is linked as a DLL with a PDB a debugger reads, never
		kept in the script cache, so a restart compiles it again."""
		self.builtin(probe_manifest(), {"code/probe.cf": PROBE.replace("VERSION", "7")})
		data = os.path.join(self.root, "data")
		self.start(data, options=("--debug-scripts", "on"))
		await self.connect()
		code = await self.settled("test.probe")
		self.assertEqual(("active", True), (code["state"], code.get("debug")), code)
		self.client.abort()
		self.server.stop(remove_data=False)
		self.start(data, options=("--debug-scripts", "on"))
		await self.connect()
		code = await self.settled("test.probe")
		self.assertEqual(("active", True, None), (code["state"], code.get("debug"), code.get("cached")), code)
		self.assertEqual(7, (await self.echo("x"))["version"], "compiled again, it runs")

	async def test_a_server_without_its_script_library_runs_and_says_why_code_cannot(self):
		self.builtin(probe_manifest(), {"code/probe.cf": PROBE.replace("VERSION", "6")})
		built = os.environ[EXECUTABLE_ENV]
		copy = os.path.join(self.root, "nodll")
		os.makedirs(copy)
		for name in (os.path.basename(built), os.path.splitext(os.path.basename(built))[0] + ".c4i"):
			shutil.copy(os.path.join(os.path.dirname(built), name), copy)
		self.start(executable=os.path.join(copy, os.path.basename(built)))
		await self.connect()
		code = await self.settled("test.probe")
		self.assertEqual("failed", code["state"], code)
		self.assertIn("C4Script.dll", code["problem"], "the reason names the library")
		self.assertTrue((await self.client.request("ws.list"))["workspaces"], "the server itself runs")

	async def test_the_example_plugin_compiles(self):
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "examples", "markup-plugin"), os.path.join(self.home, "plugins", "dialogue"))
		write(os.path.join(self.repo, "dialogue.toml"), "")
		write(os.path.join(self.repo, "intro.dlg"), "[Ana] Hello.\n[Bo] Hi.\n[Ana] Bye.\n")
		self.start()
		await self.connect()
		code = await self.settled("mygame.dialogue")
		self.assertEqual("active", code["state"], code)

	async def test_a_project_plugin_runs_only_in_a_trusted_workspace(self):
		self.add(os.path.join(self.repo, ".ied", "plugins", "probe"), probe_manifest("game.probe"), {"code/probe.cf": PROBE.replace("VERSION", "3")})
		self.start()
		await self.connect()
		self.assertIsNone(await self.code("game.probe", "w1"), "untrusted: no code runs")
		with self.assertRaises(RpcFailure):
			await self.echo("x", "w1")
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		code = await self.settled("game.probe", "w1")
		self.assertEqual("active", code["state"], code)
		self.assertEqual(3, (await self.echo("x", "w1"))["version"])
		write(os.path.join(self.repo, ".ied", "plugins", "probe", "code", "probe.cf"), PROBE.replace("VERSION", "4"))
		await self.settled("game.probe", "w1", after=code)
		self.assertEqual(4, (await self.echo("x", "w1"))["version"], "an edit of its code reloads it")
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": False})
		self.assertIsNone(await self.code("game.probe", "w1"), "untrusted again: it stopped")
