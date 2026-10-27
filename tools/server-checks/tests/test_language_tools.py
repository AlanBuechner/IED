"""Language tools (docs/26 "Language tools"): the tools a workspace's plugins name, found where their `tools` entries say
- a setting, IED's tools folder, search folders, PATH - with their versions read and checked against the version the
workspace pins; an untrusted workspace runs nothing; one Install downloads (checked against its SHA-256, the pinned one
or the version's checksum file), unpacks and runs commands into IED's tools folder, what a tool requires first; refusals
(a checksum that does not match, an install for the user not allowed, a tool IED cannot install). The tools are fake
batch files served from a loopback HTTP server (fake_tools.py): nothing is downloaded from anywhere. The real Go, Rust and
C/C++ plugins' tools are listed; Go debugs through Delve's DAP mode (here the fake adapter dialing IED's port); a
language server whose tool is missing fails at once, saying so; the AI reads the tools with language_tools."""

import asyncio
import json
import os
import shutil
import sys
import tempfile
import time

from fake_tools import FileServer, broken_script, tool_zip, version_script, write_program
from frames import FrameChecker
from iedclient import RpcFailure
from routed import RoutedClient
from server import IedServer, ServerTestCase
from test_chat import TURN_SECONDS, ChatCase, call_step, text_step

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FAKE_DAP = os.path.join(HERE, "fake_dap.py")


def write(path, value):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8", newline="") as f:
		f.write(value if isinstance(value, str) else json.dumps(value, indent="\t"))


def fake_plugin(url, sha140):
	tools = [
		{"id": "fakec", "title": "Fake C", "kind": "compiler", "when": "workspaceTrusted", "program": "fakec", "setting": "fake.c.path", "onPath": True,
			"searchPaths": ["${workspaceDir}/sdk"], "detect": {"args": ["--version"], "pattern": "fakec version (\\S+)"},
			"versionFrom": [{"file": "fake.toml", "regexp": "^c\\s*=\\s*\"([^\"]+)\"", "match": "minimum"}],
			"install": {"version": "1.4.0", "bin": "bin", "windows": [{"label": "Fake C archive", "download": url("/fakec-${version}.zip"), "sha256": sha140,
				"sha256Url": url("/fakec-${version}.zip.sha256"), "extract": "zip", "strip": 1}]}},
		{"id": "fakels", "title": "Fake LS", "kind": "languageServer", "program": "fakels", "requires": ["fakec"],
			"detect": {"args": ["--version"], "pattern": "fakels version (\\S+)"},
			"install": {"version": "2.0.1", "windows": [{"label": "fakec install", "run": ["${tool:fakec}", "install", "fakels@v${version}"], "env": {"GOBIN": "${installDir}"}}]}},
		{"id": "broken", "title": "Broken", "program": "broken", "searchPaths": ["${workspaceDir}/tools"], "detect": {"args": ["--version"], "pattern": "(\\S+)"}},
		{"id": "manual", "title": "Manual", "program": "nothing-like-this", "homepage": "https://example.invalid/manual"},
		{"id": "badsum", "title": "Bad checksum", "program": "badsum", "install": {"version": "1.0.0", "bin": "bin", "windows": [{"download": url("/fakec-1.4.0.zip"), "sha256": "0" * 64}]}},
		{"id": "foruser", "title": "For the user", "program": "foruser", "install": {"version": "1.0.0", "windows": [{"label": "echo", "run": ["cmd", "/d", "/c", "echo for the user"], "scope": "user"}]}},
		{"id": "elsewhere", "title": "Not here", "program": "x", "when": "workspaceContains.nothing-here"},
	]
	return {"manifestVersion": 1, "id": "test.tools", "name": "Fake language", "version": "1.0.0", "publisher": "checks", "ied": {"api": "^1"},
		"runtime": {"kind": "declarative"}, "contributes": {"settings": {"schema": "settings.schema.json", "page": "Text Editor/Fake"}, "tools": tools,
			"lspProfiles": [{"id": "fake-ls", "label": "Fake LS", "command": "${tool:fakels}", "selector": {"extensions": [".fake"]}}]}}


SETTINGS = {"type": "object", "properties": {"fake.c.path": {"type": "string", "default": "", "description": "Fake C."}}}


class LanguageToolTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-tools-")
		self.folder = os.path.join(self.root, "Proj")
		files = {}
		for version in ("1.4.0", "1.5"):
			data, digest = tool_zip("fakec", version)
			files["/fakec-%s.zip" % version] = data
			files["/fakec-%s.zip.sha256" % version] = ("%s  fakec-%s.zip\n" % (digest, version)).encode("ascii")
		self.sha140 = tool_zip("fakec", "1.4.0")[1]
		self.files = FileServer(files)
		plugin = os.path.join(self.folder, ".ied", "plugins", "test.tools")
		write(os.path.join(plugin, "ied-plugin.json"), fake_plugin(self.files.url, self.sha140))
		write(os.path.join(plugin, "settings.schema.json"), SETTINGS)
		write_program(os.path.join(self.folder, "sdk"), "fakec", version_script("fakec", "1.2.0"))
		write_program(os.path.join(self.folder, "sdk2"), "fakec", version_script("fakec", "1.3.5"))
		write_program(os.path.join(self.folder, "tools"), "broken", broken_script("error: Unknown binary 'broken.exe' in official toolchain"))
		write(os.path.join(self.folder, "fake.toml"), 'c = "1.3"\n')
		write(os.path.join(self.folder, "go.mod"), "module example.com/proj\n\ngo 1.21\n")
		self.server = IedServer(extra=("--workspace", self.folder))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()
		self.files.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		self.ws = (await self.client.request("ws.list"))["workspaces"][0]["wsId"]

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def status(self):
		return await self.client.request("tools.status", {"wsId": self.ws})

	async def settled(self, done=None, timeout=60):
		"""The snapshot once detection and installs are over (and `done(snapshot)` holds)."""
		deadline = time.monotonic() + timeout
		while time.monotonic() < deadline:
			snapshot = await self.status()
			if snapshot["detected"] and not snapshot["detecting"] and not snapshot["install"] and (done is None or done(snapshot)):
				return snapshot
			await asyncio.sleep(0.2)
		self.fail("the tools did not settle: %s" % json.dumps(await self.status())[:2000])

	@staticmethod
	def tool(snapshot, tool_id):
		found = [t for t in snapshot["tools"] if t["id"] == tool_id]
		return found[0] if found else None

	async def trusted(self):
		await self.client.request("ws.setTrust", {"wsId": self.ws, "trusted": True})
		return await self.settled(lambda s: self.tool(s, "fakec") is not None)

	async def test_an_untrusted_workspace_runs_nothing(self):
		snapshot = await self.status()
		self.assertFalse(snapshot["trusted"])
		go = self.tool(snapshot, "go")
		self.assertEqual(("unchecked", "compiler", "ied.go"), (go["state"], go["kind"], go["plugin"]), "the Go plugin's tools are named, not looked for")
		self.assertIsNone(self.tool(snapshot, "fakec"), "a project plugin of an untrusted workspace does not load")
		for method in ("tools.detect", "tools.install"):
			with self.assertRaises(RpcFailure) as refused:
				await self.client.request(method, {"wsId": self.ws})
			self.assertEqual("forbidden", refused.exception.code, method)

	async def test_tools_are_found_with_their_versions_and_pins(self):
		snapshot = await self.trusted()
		fakec = self.tool(snapshot, "fakec")
		self.assertEqual(("wrongVersion", "1.2.0", "searchPath"), (fakec["state"], fakec["version"], fakec["source"]))
		self.assertEqual({"version": "1.3", "match": "minimum", "file": "fake.toml"}, fakec["pin"])
		self.assertTrue(fakec["path"].lower().endswith("\\sdk\\fakec.cmd"), fakec["path"])
		self.assertEqual(("1.4.0", True), (fakec["installs"], fakec["canInstall"]))
		fakels = self.tool(snapshot, "fakels")
		self.assertEqual(("missing", ["fakec"], "2.0.1"), (fakels["state"], fakels["requires"], fakels["installs"]))
		broken = self.tool(snapshot, "broken")
		self.assertEqual("missing", broken["state"])
		self.assertIn("exited with code 1: error: Unknown binary 'broken.exe' in official toolchain", broken["problem"])
		manual = self.tool(snapshot, "manual")
		self.assertEqual(("missing", False, "https://example.invalid/manual"), (manual["state"], manual["canInstall"], manual["homepage"]))
		self.assertIn("nothing-like-this was not found in", manual["problem"])
		self.assertIsNone(self.tool(snapshot, "elsewhere"), "a tool whose when does not hold")
		write(os.path.join(self.folder, "a.fake"), "x\n")
		await self.client.request("doc.open", {"wsId": self.ws, "path": "a.fake"})
		deadline = time.monotonic() + 20
		server = None
		while time.monotonic() < deadline and not (server and server["state"] == "failed"):
			servers = (await self.client.request("lsp.status", {"wsId": self.ws}))["servers"]
			server = next((s for s in servers if s["profile"] == "fake-ls"), None)
			await asyncio.sleep(0.2)
		self.assertEqual(("failed", 0), (server["state"], server["restarts"]), "a language server whose tool is missing is not started")
		self.assertTrue(server["problem"].startswith("Fake LS is not installed: fakels was not found in"), server["problem"])
		self.assertTrue(server["problem"].endswith(". Settings > Languages installs it."), server["problem"])
		self.assertEqual("compiler", self.tool(snapshot, "go")["kind"])
		await self.client.request("settings.set", {"tier": "solution", "wsId": self.ws, "key": "fake.c.path", "value": os.path.join(self.folder, "sdk2", "fakec.cmd")})
		snapshot = await self.settled(lambda s: self.tool(s, "fakec")["source"] == "setting")
		self.assertEqual(("found", "1.3.5"), (self.tool(snapshot, "fakec")["state"], self.tool(snapshot, "fakec")["version"]), "a setting names the program: nothing else is tried")
		await self.client.request("settings.reset", {"tier": "solution", "wsId": self.ws, "key": "fake.c.path"})
		await self.settled(lambda s: self.tool(s, "fakec")["source"] == "searchPath")
		write(os.path.join(self.folder, "fake.toml"), 'c = "1.1"\n')
		snapshot = await self.settled(lambda s: self.tool(s, "fakec")["state"] == "found")
		self.assertEqual("1.1", self.tool(snapshot, "fakec")["pin"]["version"], "a pin file's change is seen")

	async def test_one_install_takes_what_is_needed_first(self):
		await self.trusted()
		await self.client.sub(["out:%s:tools" % self.ws, "server"])
		started = await self.client.request("tools.install", {"wsId": self.ws, "tools": ["fakels"]})
		self.assertEqual({"tools": ["fakec", "fakels"], "output": "tools"}, started, "the compiler it needs first: it is not the pinned version")
		snapshot = await self.settled(lambda s: self.tool(s, "fakels")["state"] == "found")
		fakec, fakels = self.tool(snapshot, "fakec"), self.tool(snapshot, "fakels")
		tools = os.path.join(self.server.data, "tools")
		self.assertEqual(("found", "1.4.0", "installed"), (fakec["state"], fakec["version"], fakec["source"]))
		self.assertEqual(os.path.join(tools, "fakec", "1.4.0", "bin", "fakec.cmd").lower(), fakec["path"].lower())
		self.assertEqual(("found", "2.0.1", "installed"), (fakels["state"], fakels["version"], fakels["source"]))
		self.assertTrue(os.path.isfile(os.path.join(tools, "fakels", "2.0.1", "fakels.cmd")), "go install's GOBIN was the install folder")
		self.assertEqual(["/fakec-1.4.0.zip"], self.files.requests, "the pinned checksum: no checksum file fetched")
		output = "".join(f["data"]["text"] for f in await self.client.wait_for("out:%s:tools" % self.ws, lambda f: "Installed Fake LS" in f.get("data", {}).get("text", ""), timeout=10) if f.get("kind") == "append")
		self.assertIn("SHA-256 checked: " + self.sha140, output)
		self.assertIn("installed fakels 2.0.1", output, "the command's own output")
		note = (await self.client.wait_for("server", lambda f: f.get("kind") == "notification" and "Installed" in f["data"]["text"], timeout=10))[-1]["data"]
		self.assertEqual("Installed Fake C 1.4.0, Fake LS 2.0.1", note["text"])
		term = await self.client.request("term.create", {"wsId": self.ws, "argv": ["cmd", "/d", "/c", "fakec --version"], "cols": 200})
		topic = "term:" + term["termId"]
		await self.client.sub([topic])
		seen = []
		await self.client.wait_for(topic, lambda f: seen.append(f.get("data", {}).get("data", "") or f.get("data", {}).get("scrollback", "")) or "fakec version 1.4.0" in "".join(seen), timeout=20)

	async def test_a_pinned_version_comes_with_its_checksum_file(self):
		write(os.path.join(self.folder, "fake.toml"), 'c = "1.5"\n')
		await self.trusted()
		await self.client.request("tools.install", {"wsId": self.ws, "tools": ["fakec"]})
		snapshot = await self.settled(lambda s: self.tool(s, "fakec")["state"] == "found")
		self.assertEqual(("1.5", "installed"), (self.tool(snapshot, "fakec")["version"], self.tool(snapshot, "fakec")["source"]))
		self.assertEqual(["/fakec-1.5.zip.sha256", "/fakec-1.5.zip"], self.files.requests, "a version the plugin does not pin: its checksum file first")

	async def test_refusals(self):
		await self.trusted()
		await self.client.request("tools.install", {"wsId": self.ws, "tools": ["badsum"]})
		snapshot = await self.settled(lambda s: self.tool(s, "badsum")["state"] == "failed")
		self.assertIn("the download's SHA-256 is %s, not %s" % (self.sha140, "0" * 64), self.tool(snapshot, "badsum")["problem"])
		self.assertFalse(os.path.exists(os.path.join(self.server.data, "tools", "badsum", "1.0.0")), "nothing of it was kept")
		with self.assertRaises(RpcFailure) as for_user:
			await self.client.request("tools.install", {"wsId": self.ws, "tools": ["foruser"]})
		self.assertEqual(("forbidden", "userScope", ["foruser"]), (for_user.exception.code, for_user.exception.data["reason"], for_user.exception.data["tools"]))
		await self.client.request("tools.install", {"wsId": self.ws, "tools": ["foruser"], "allowUserScope": True})
		snapshot = await self.settled(lambda s: self.tool(s, "foruser")["state"] == "failed")
		self.assertIn("it was installed, but it is still not found", self.tool(snapshot, "foruser")["problem"])
		for tools, code in ((["manual"], "bad_request"), (["nope"], "not_found")):
			with self.assertRaises(RpcFailure) as refused:
				await self.client.request("tools.install", {"wsId": self.ws, "tools": tools})
			self.assertEqual(code, refused.exception.code, tools)
		with self.assertRaises(RpcFailure) as idle:
			await self.client.request("tools.cancel", {"wsId": self.ws})
		self.assertEqual("not_found", idle.exception.code)
		self.assertFalse(snapshot["dismissed"])
		await self.client.request("tools.dismiss", {"wsId": self.ws})
		self.assertTrue((await self.status())["dismissed"], "the banner stays away while the same tools lack")


class LanguagePluginToolTests(ServerTestCase):
	"""The real Go, Rust and C/C++ plugins' tools, and Go's debugger: Delve's DAP mode dialing IED's port (`dlv dap
	--client-addr`), here go.delve.path naming this Python, which runs the workspace's `dap` script (the fake adapter)
	and `version` script (Delve's version line)."""

	MARKERS = {"Cargo.toml": '[package]\nname = "mixed"\nversion = "0.1.0"\n', "CMakeLists.txt": "project(mixed)\n"}

	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-langtools-")
		self.folder = os.path.join(self.root, "Mixed")
		self.log = os.path.join(self.root, "dap.jsonl")
		write(os.path.join(self.folder, "go.mod"), "module example.com/mixed\n\ngo 1.21\n")
		write(os.path.join(self.folder, "main.go"), "package main\n\nfunc main() {\n}\n")
		if "only_go" not in self._testMethodName:
			for name, text in self.MARKERS.items():
				write(os.path.join(self.folder, name), text)
		write(os.path.join(self.folder, "dap"), "import runpy, sys\nsys.argv = [%r, %r] + sys.argv\nrunpy.run_path(%r, run_name='__main__')\n" % (FAKE_DAP, self.log, FAKE_DAP))
		write(os.path.join(self.folder, "version"), "print('Delve Debugger')\nprint('Version: 1.27.2')\n")
		self.server = IedServer(extra=("--workspace", self.folder))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		await self.client.request("settings.set", {"tier": "app", "key": "go.delve.path", "value": sys.executable})
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def settled(self):
		deadline = time.monotonic() + 60
		while time.monotonic() < deadline:
			snapshot = await self.client.request("tools.status", {"wsId": "w1"})
			if snapshot["detected"] and not snapshot["detecting"] and any(t["id"] == "dlv" and t["source"] == "setting" for t in snapshot["tools"]):
				return snapshot
			await asyncio.sleep(0.2)
		self.fail("the tools did not settle")

	async def test_the_language_plugins_name_their_tools(self):
		snapshot = await self.settled()
		kinds = {t["id"]: (t["plugin"], t["kind"]) for t in snapshot["tools"]}
		for tool_id, plugin, kind in (("go", "ied.go", "compiler"), ("gopls", "ied.go", "languageServer"), ("dlv", "ied.go", "debugAdapter"),
				("rustup", "ied.rust", "buildTool"), ("cargo", "ied.rust", "compiler"), ("rust-analyzer", "ied.rust", "languageServer"),
				("clangd", "ied.cpp", "languageServer"), ("codelldb", "ied.cpp", "debugAdapter")):
			self.assertEqual((plugin, kind), kinds.get(tool_id), tool_id)
		dlv = [t for t in snapshot["tools"] if t["id"] == "dlv"][0]
		self.assertEqual(("found", "1.27.2"), (dlv["state"], dlv["version"]), "the setting's program, asked its version in the workspace")
		cargo = [t for t in snapshot["tools"] if t["id"] == "cargo"][0]
		self.assertTrue(cargo["installsForUser"], "rustup's installs write for the user: asked first")
		vendored = os.path.join(os.path.dirname(os.path.dirname(HERE)), "vendor", "llvm", "bin", "clangd.exe")
		if os.path.isfile(vendored):
			clangd = [t for t in snapshot["tools"] if t["id"] == "clangd"][0]
			self.assertEqual(("found", "bundled"), (clangd["state"], clangd["source"]), "the clangd IED comes with")

	async def test_only_go_debugs_through_delves_dap_mode(self):
		await self.settled()
		await self.client.sub(["dbg:w1"])
		await self.client.next_frame("dbg:w1")
		started = await self.client.request("dbg.start", {"wsId": "w1", "configuration": "Launch"})
		topic = started["topic"]
		await self.client.sub([topic])
		ended = (await self.client.wait_for("dbg:w1", lambda f: f.get("kind") == "dbg.session" and f["data"]["state"] in ("ended", "failed"), timeout=30))[-1]["data"]
		self.assertEqual(("ended", "go"), (ended["state"], ended["type"]), ended)
		with open(self.log, encoding="utf-8") as f:
			logged = [json.loads(line) for line in f if line.strip()]
		self.assertEqual("dap", logged[0]["argv"][0])
		self.assertTrue(logged[0]["argv"][1].startswith("--client-addr=127.0.0.1:"), logged[0]["argv"])
		launch = [m for m in logged if m.get("command") == "launch"][0]["arguments"]
		self.assertEqual(("launch", "debug"), (launch["request"], launch["mode"]))
		self.assertEqual(os.path.realpath(self.folder).lower(), os.path.realpath(launch["program"]).lower(), "the package: the project's folder")


class LanguageToolsChatTests(ChatCase):
	"""The AI asks what the workspace's languages need (language_tools, docs/13): each tool found or missing, why, its pin
	and what IED would install."""

	def setUp(self):
		super().setUp()
		self.server.stop(remove_data=False)
		plugin = os.path.join(self.repo, ".ied", "plugins", "test.tools")
		write(os.path.join(plugin, "ied-plugin.json"), {"manifestVersion": 1, "id": "test.tools", "name": "Fake language", "version": "1.0.0", "publisher": "checks",
			"ied": {"api": "^1"}, "runtime": {"kind": "declarative"}, "contributes": {"tools": [
				{"id": "fakec", "title": "Fake C", "kind": "compiler", "program": "fakec", "searchPaths": ["${workspaceDir}/sdk"],
					"detect": {"args": ["--version"], "pattern": "fakec version (\\S+)"}, "versionFrom": [{"file": "fake.toml", "regexp": "^c\\s*=\\s*\"([^\"]+)\"", "match": "minimum"}]},
				{"id": "fakels", "title": "Fake LS", "kind": "languageServer", "program": "fakels",
					"install": {"version": "2.0.1", "windows": [{"run": ["cmd", "/d", "/c", "echo"]}]}}]}})
		write_program(os.path.join(self.repo, "sdk"), "fakec", version_script("fakec", "1.2.0"))
		write(os.path.join(self.repo, "fake.toml"), 'c = "1.1"\n')
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)

	async def test_the_ai_reads_the_language_tools(self):
		deadline = time.monotonic() + 60
		while time.monotonic() < deadline:
			snapshot = await self.client.request("tools.status", {"wsId": "w1"})
			if snapshot["detected"] and not snapshot["detecting"] and any(t["id"] == "fakec" for t in snapshot["tools"]):
				break
			await asyncio.sleep(0.2)
		chat_id, _ = await self.chat(permission="auto")
		self.model.queue(call_step("language_tools", {}, "t1"), text_step("Done."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=TURN_SECONDS))["stop"])
		sent = self.model.requests()[-1]["body"]["messages"]
		result = [m["content"] for m in sent if m["role"] == "tool"][0]
		self.assertIn("- Fake C (compiler, Fake language): found 1.2.0 at ", result)
		self.assertIn("; the workspace pins 1.1 or newer (fake.toml)", result)
		self.assertIn("- Fake LS (languageServer, Fake language): missing - fakels was not found in", result)
		self.assertIn("; IED installs 2.0.1", result)
