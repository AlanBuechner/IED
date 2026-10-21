"""Language servers (docs/24): a plugin's profile starts its server when a document it serves opens; documents reach it
from the store; diagnostics land on diag:<wsId> in UTF-16; lsp.request adds the document, converts positions both ways
and maps URIs; a server that exits is started again; lsp.status, lsp.capabilities and lsp.restart."""

import json
import os
import shutil
import sys
import tempfile

from frames import FrameChecker
from iedclient import Client, RpcFailure
from server import REPO, IedServer, ServerTestCase

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def write(path, text):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8", newline="") as f:
		f.write(text if isinstance(text, str) else json.dumps(text, indent="\t"))


class LanguageServerTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-lsp-")
		self.home = os.path.join(self.root, "home")
		self.workspace = os.path.join(self.root, "work")
		self.log = os.path.join(self.root, "fake-lsp.jsonl")
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		plugin = os.path.join(self.home, "plugins", "fake")
		os.makedirs(plugin)
		shutil.copy(os.path.join(HERE, "fake_lsp.py"), plugin)
		write(os.path.join(plugin, "ied-plugin.json"), {
			"manifestVersion": 1, "id": "fake", "name": "Fake", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
			"contributes": {
				"languages": [{"id": "fake", "extensions": [".fk"]}],
				"lspProfiles": [{"id": "fake-lsp", "label": "Fake", "command": sys.executable, "args": ["-I", "${pluginDir}/fake_lsp.py", self.log], "selector": {"languages": ["fake"]}}]}})
		write(os.path.join(self.workspace, "a.fk"), "é bad\nok\n")
		write(os.path.join(self.workspace, "b.fk"), "b\n")
		write(os.path.join(self.workspace, "notes.txt"), "plain\n")
		self.server = IedServer(extra=("--home", self.home, "--workspace", self.workspace))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		await self.client.sub(["diag:w1", "lsp:w1", "out:w1:ied"])
		self.diag = (await self.client.next_frame("diag:w1"))["data"]
		self.status = (await self.client.next_frame("lsp:w1"))["data"]

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	def received(self, method):
		with open(self.log, encoding="utf-8") as f:
			return [m for m in map(json.loads, f) if m.get("method") == method]

	async def diagnostics(self, path):
		frames = await self.client.wait_for("diag:w1", lambda f: f["t"] == "evt" and f["data"]["path"] == path, timeout=20)
		return frames[-1]["data"]

	async def open(self, path):
		return (await self.client.request("doc.open", {"wsId": "w1", "path": path}))["docId"]

	async def test_a_document_starts_its_server_and_gets_diagnostics_in_utf16(self):
		self.assertEqual(({"files": {}}, {"servers": []}), (self.diag, self.status), "nothing runs before a served document opens")
		await self.open("a.fk")
		published = await self.diagnostics("a.fk")
		self.assertEqual("fake-lsp", published["source"])
		self.assertEqual([{"line": 0, "character": 2}, {"line": 0, "character": 5}], [published["items"][0]["range"][k] for k in ("start", "end")], "UTF-8 columns 3..6 are UTF-16 2..5")
		opened = self.received("textDocument/didOpen")[0]["params"]["textDocument"]
		self.assertEqual(("fake", 1, "é bad\nok\n"), (opened["languageId"], opened["version"], opened["text"]))
		self.assertTrue(opened["uri"].startswith("file:///") and opened["uri"].endswith("/a.fk"), opened["uri"])
		snapshot = await self.client.request("lsp.status", {"wsId": "w1"})
		self.assertEqual(("fake-lsp", "running", 0), tuple(snapshot["servers"][0][k] for k in ("profile", "state", "restarts")))
		output = await self.client.wait_for("out:w1:ied", lambda f: "[fake-lsp] fake-lsp started" in json.dumps(f), timeout=20)
		self.assertTrue(output)

	async def test_edits_and_saves_reach_the_server(self):
		doc = await self.open("a.fk")
		await self.diagnostics("a.fk")
		await self.client.request("doc.edit", {"docId": doc, "baseVersion": 1, "changes": [{"range": {"start": {"line": 1, "col": 0}, "end": {"line": 1, "col": 0}}, "text": "bad "}]})
		published = await self.diagnostics("a.fk")
		self.assertEqual([0, 1], [item["range"]["start"]["line"] for item in published["items"]])
		changed = self.received("textDocument/didChange")[-1]["params"]
		self.assertEqual((2, "é bad\nbad ok\n"), (changed["textDocument"]["version"], changed["contentChanges"][0]["text"]))
		await self.client.request("doc.save", {"docId": doc})
		await self.client.request("lsp.status", {"wsId": "w1"})
		self.assertEqual(1, len(self.received("textDocument/didSave")))

	async def test_requests_convert_positions_and_uris(self):
		doc = await self.open("a.fk")
		hover = await self.client.request("lsp.request", {"docId": doc, "method": "textDocument/hover", "params": {"position": {"line": 0, "character": 2}}})
		self.assertEqual("at 0:3", hover["contents"], "the server got UTF-8 column 3")
		self.assertEqual(({"line": 0, "character": 2}, {"line": 0, "character": 5}), (hover["range"]["start"], hover["range"]["end"]))
		self.assertTrue(self.received("textDocument/hover")[0]["params"]["textDocument"]["uri"].endswith("/a.fk"))
		definition = await self.client.request("lsp.request", {"docId": doc, "method": "textDocument/definition", "params": {"position": {"line": 0, "character": 0}}})
		self.assertEqual("ied://w1/b.fk", definition[0]["uri"])
		capabilities = await self.client.request("lsp.capabilities", {"docId": doc})
		self.assertEqual(("fake-lsp", "running", True), (capabilities["profile"], capabilities["state"], capabilities["capabilities"]["hoverProvider"]))

	async def test_documents_no_server_serves(self):
		doc = await self.open("notes.txt")
		with self.assertRaises(RpcFailure) as refused:
			await self.client.request("lsp.request", {"docId": doc, "method": "textDocument/hover", "params": {"position": {"line": 0, "character": 0}}})
		self.assertEqual("not_found", refused.exception.code)
		self.assertEqual({"profile": None}, await self.client.request("lsp.capabilities", {"docId": doc}))

	async def test_a_server_that_exits_starts_again_and_gets_its_documents(self):
		doc = await self.open("a.fk")
		await self.diagnostics("a.fk")
		with self.assertRaises(RpcFailure) as failed:
			await self.client.request("lsp.request", {"docId": doc, "method": "fake/crash"})
		self.assertEqual("plugin_error", failed.exception.code)
		await self.client.wait_for("lsp:w1", lambda f: f["t"] == "evt" and f["data"]["state"] == "running", timeout=20)
		published = await self.diagnostics("a.fk")
		self.assertEqual(1, len(published["items"]), "the restarted server got the open document again")
		self.assertEqual(2, len(self.received("textDocument/didOpen")))
		status = await self.client.request("lsp.status", {"wsId": "w1"})
		self.assertEqual(("running", 1), (status["servers"][0]["state"], status["servers"][0]["restarts"]))

	async def test_a_renamed_document_moves_on_its_server(self):
		await self.open("a.fk")
		await self.diagnostics("a.fk")
		await self.client.request("fs.rename", {"wsId": "w1", "from": "a.fk", "to": "c.fk"})
		cleared = await self.diagnostics("a.fk")
		self.assertEqual([], cleared["items"], "the old path's diagnostics go")
		self.assertEqual(1, len((await self.diagnostics("c.fk"))["items"]), "the server got the document under its new name")
		closed = self.received("textDocument/didClose")[-1]["params"]["textDocument"]["uri"]
		reopened = self.received("textDocument/didOpen")[-1]["params"]["textDocument"]["uri"]
		self.assertEqual((True, True), (closed.endswith("/a.fk"), reopened.endswith("/c.fk")))

	async def test_restart(self):
		await self.open("a.fk")
		await self.diagnostics("a.fk")
		pid = (await self.client.request("lsp.status", {"wsId": "w1"}))["servers"][0]["pid"]
		status = await self.client.request("lsp.restart", {"wsId": "w1", "profile": "fake-lsp"})
		self.assertEqual("starting", status["servers"][0]["state"])
		await self.client.wait_for("lsp:w1", lambda f: f["t"] == "evt" and f["data"]["state"] == "running", timeout=20)
		self.assertNotEqual(pid, (await self.client.request("lsp.status", {"wsId": "w1"}))["servers"][0]["pid"])
		self.assertEqual(1, len(self.received("shutdown")))
		with self.assertRaises(RpcFailure) as refused:
			await self.client.request("lsp.restart", {"wsId": "w1", "profile": "nope"})
		self.assertEqual("not_found", refused.exception.code)


class ProjectScopeTests(ServerTestCase):
	"""A project-scoped profile in a solution: one server per project, named by ${projectName} and ${projectDir}, and a
	workspace-wide one for documents in no project."""

	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-lsp-projects-")
		self.home = os.path.join(self.root, "home")
		self.repo = os.path.join(self.root, "Repo")
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		plugin = os.path.join(self.home, "plugins", "fake")
		os.makedirs(plugin)
		shutil.copy(os.path.join(HERE, "fake_lsp.py"), plugin)
		self.logs = os.path.join(self.root, "logs")
		os.makedirs(self.logs)
		write(os.path.join(plugin, "ied-plugin.json"), {
			"manifestVersion": 1, "id": "fake", "name": "Fake", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
			"contributes": {
				"languages": [{"id": "fake", "extensions": [".fk"]}],
				"lspProfiles": [{"id": "fake-lsp", "command": sys.executable, "scope": "project", "selector": {"languages": ["fake"]},
					"args": ["-I", "${pluginDir}/fake_lsp.py", os.path.join(self.logs, "log-${projectName}.jsonl"), "${projectName}", "${projectDir}"]}]}})
		for name in ("Game", "Tool"):
			write(os.path.join(self.repo, "build", "ied", name + ".ied.proj"), {"version": 1, "name": name, "root": "../../src/" + name.lower(), "kind": "executable"})
			write(os.path.join(self.repo, "src", name.lower(), "main.fk"), "bad\n")
		write(os.path.join(self.repo, "loose.fk"), "fine\n")
		write(os.path.join(self.repo, "Repo.ied.sln"), {"version": 1, "name": "Repo", "projects": [{"file": "build/ied/*.ied.proj"}]})
		self.server = IedServer(extra=("--home", self.home, "--workspace", os.path.join(self.repo, "Repo.ied.sln")))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		await self.client.sub(["diag:w1"])
		await self.client.next_frame("diag:w1")

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def test_one_server_per_project_and_one_for_the_rest(self):
		for path in ("src/game/main.fk", "src/tool/main.fk", "loose.fk"):
			await self.client.request("doc.open", {"wsId": "w1", "path": path})
		sources = {}
		while len(sources) < 3:
			frame = (await self.client.wait_for("diag:w1", lambda f: f["t"] == "evt", timeout=20))[-1]["data"]
			sources[frame["path"]] = frame["source"]
		self.assertEqual({"src/game/main.fk": "fake-lsp/Game", "src/tool/main.fk": "fake-lsp/Tool", "loose.fk": "fake-lsp"}, sources)
		status = await self.client.request("lsp.status", {"wsId": "w1"})
		self.assertEqual(["", "Game", "Tool"], sorted(s["project"] for s in status["servers"]))
		for name, folder in (("Game", "game"), ("Tool", "tool")):
			with open(os.path.join(self.logs, "log-%s.jsonl" % name), encoding="utf-8") as f:
				argv = json.loads(f.readline())["argv"]
			self.assertEqual(name, argv[0])
			self.assertTrue(os.path.normcase(argv[1]).endswith(os.path.normcase(os.path.join("src", folder))), argv[1])
		with open(os.path.join(self.logs, "log-.jsonl"), encoding="utf-8") as f:
			self.assertEqual(["", ""], json.loads(f.readline())["argv"], "the workspace-wide server has no project")
		restarted = await self.client.request("lsp.restart", {"wsId": "w1", "profile": "fake-lsp", "project": "Tool"})
		self.assertEqual("starting", [s for s in restarted["servers"] if s["project"] == "Tool"][0]["state"])
		with self.assertRaises(RpcFailure) as refused:
			await self.client.request("lsp.restart", {"wsId": "w1", "profile": "fake-lsp", "project": "Nope"})
		self.assertEqual("not_found", refused.exception.code)
