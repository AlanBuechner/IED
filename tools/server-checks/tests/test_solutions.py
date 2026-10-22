"""Solutions and projects (docs/03): a .iedsln opened as a solution workspace, its projects found by path and glob and
validated, roots resolved, problems reported, the model read again when its files change; the project settings tier;
project-local plugins only in a trusted workspace (docs/02, docs/11)."""

import asyncio
import json
import os
import shutil
import tempfile

from frames import FrameChecker
from iedclient import Client
from server import REPO, ControlPipe, IedServer, ServerTestCase

SETTINGS_SCHEMA = {"properties": {"local.indent": {"type": "integer", "default": 2}}}


def write(path, value):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8") as f:
		f.write(value if isinstance(value, str) else json.dumps(value, indent="\t"))


def project(name, root, **fields):
	body = {"version": 1, "name": name, "root": root, "kind": "executable"}
	body.update(fields)
	return body


class SolutionTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-solution-")
		self.repo = os.path.join(self.root, "MyGame")
		self.home = os.path.join(self.root, "home")
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		os.makedirs(os.path.join(self.repo, "src", "Game"))
		os.makedirs(os.path.join(self.repo, "tools", "Tool"))
		write(os.path.join(self.repo, "build", "ied", "Game.iedproj"), project("Game", "../../src/Game"))
		write(os.path.join(self.repo, "tools", "Tool.iedproj"), project("Tool", "Tool", kind="other"))
		write(os.path.join(self.repo, "Nuke", "tools", "ied", "ied-plugin.json"), {
			"manifestVersion": 1, "id": "c4", "name": "C4", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative", "ui": "ui/main.js"},
			"contributes": {"commands": [{"id": "c4.hello", "title": "Hello"}], "settings": {"schema": "settings.schema.json"}, "languages": [{"id": "c4", "extensions": [".cf"]}]}})
		write(os.path.join(self.repo, "Nuke", "tools", "ied", "settings.schema.json"), SETTINGS_SCHEMA)
		write(os.path.join(self.repo, "Nuke", "tools", "ied", "ui", "main.js"), "export function activate(ied) {}\n")
		self.solution = os.path.join(self.repo, "MyGame.iedsln")
		write(self.solution, {
			"version": 1, "name": "MyGame",
			"projects": [{"file": "build/ied/*.iedproj", "optional": True}, {"file": "tools/Tool.iedproj"}],
			"groups": [{"name": "Game", "projects": ["Game"]}], "items": ["README.md"],
			"localPlugins": [{"id": "c4", "path": "Nuke/tools/ied"}]})
		self.server = IedServer(extra=("--home", self.home, "--workspace", self.solution))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.cookie = self.server.sign_in()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		await self.client.sub(["ws:w1"])
		self.snapshot = (await self.client.next_frame("ws:w1"))["data"]

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def test_the_solution_and_its_projects(self):
		self.assertEqual(("solution", "MyGame"), (self.snapshot["kind"], self.snapshot["name"]))
		solution = self.snapshot["solution"]
		self.assertEqual([], solution["problems"])
		self.assertEqual(["README.md"], solution["items"])
		projects = {p["name"]: p for p in solution["projects"]}
		self.assertEqual({"Game", "Tool"}, set(projects))
		self.assertTrue(projects["Game"]["root"].lower().endswith(os.path.join("mygame", "src", "game")), projects["Game"]["root"])
		self.assertEqual(("other", []), (projects["Tool"]["kind"], projects["Tool"]["problems"]))
		self.assertEqual("executable", projects["Game"]["data"]["kind"])

	async def test_problems_and_live_changes(self):
		write(os.path.join(self.repo, "build", "ied", "Extra.iedproj"), project("Extra", "../../missing", version="one"))
		frames = await self.client.wait_for("ws:w1", lambda f: f["t"] == "evt" and f["kind"] == "solution.changed", 10)
		projects = {p["name"]: p for p in frames[-1]["data"]["solution"]["projects"]}
		self.assertIn("Extra", projects, "a project appearing under the glob")
		problems = " ".join(projects["Extra"]["problems"])
		self.assertIn("/version", problems)
		self.assertIn("no folder", problems)
		os.remove(os.path.join(self.repo, "tools", "Tool.iedproj"))
		frames = await self.client.wait_for("ws:w1", lambda f: f["t"] == "evt" and f["kind"] == "solution.changed" and "Tool" not in [p["name"] for p in f["data"]["solution"]["projects"]], 10)
		self.assertIn("no project file matches tools/Tool.iedproj", frames[-1]["data"]["solution"]["problems"])

	async def test_project_tier(self):
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		await self.client.request("settings.set", {"tier": "solution", "wsId": "w1", "key": "local.indent", "value": 4})
		await self.client.request("settings.set", {"tier": "project", "wsId": "w1", "project": "Game", "key": "local.indent", "value": 8})
		self.assertTrue(os.path.exists(os.path.join(self.repo, ".ied", "projects", "Game.json")))
		game = await self.client.request("settings.get", {"wsId": "w1", "project": "Game"})
		self.assertEqual({"value": 8, "source": "project"}, game["values"]["local.indent"])
		tool = await self.client.request("settings.get", {"wsId": "w1", "project": "Tool"})
		self.assertEqual({"value": 4, "source": "solution"}, tool["values"]["local.indent"])
		self.assertIn("Game", game["projects"])

	async def test_local_plugins_need_trust(self):
		listed = {(p["id"], p["source"]): p for p in (await self.client.request("plugins.list", {"wsId": "w1"}))["plugins"]}
		self.assertEqual("untrusted", listed[("c4", "project")]["state"])
		self.assertEqual([], (await self.client.request("plugins.contributions", {"wsId": "w1"}))["plugins"])
		self.assertNotIn("local.indent", (await self.client.request("settings.schema", {"wsId": "w1"}))["settings"])
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		contributed = (await self.client.request("plugins.contributions", {"wsId": "w1"}))["plugins"]
		self.assertEqual([("c4", "project", "/w/w1/plugins/c4/ui/main.js")], [(p["id"], p["source"], p["ui"]) for p in contributed])
		self.assertIn("local.indent", (await self.client.request("settings.schema", {"wsId": "w1"}))["settings"])
		self.assertNotIn("local.indent", (await self.client.request("settings.schema"))["settings"], "not for the app tier")

	async def test_an_open_document_is_told_the_language_trust_brings(self):
		write(os.path.join(self.repo, "src", "Game", "main.cf"), "x\n")
		doc = await self.client.request("doc.open", {"wsId": "w1", "path": "src/Game/main.cf"})
		topic = "doc:" + doc["docId"]
		await self.client.sub([topic])
		self.assertEqual("plaintext", (await self.client.wait_for(topic, lambda f: f["t"] == "snap"))[-1]["data"]["languageId"], "no plugin knows .cf yet")
		told = lambda language: lambda f: f["t"] == "evt" and f["kind"] == "doc.language" and f["data"]["languageId"] == language
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		self.assertEqual({"docId": doc["docId"], "languageId": "c4"}, (await self.client.wait_for(topic, told("c4"), 10))[-1]["data"], "the local plugin's language")
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": False})
		await self.client.wait_for(topic, told("plaintext"), 10)
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": False})
		await self.client.request("plugins.list", {"wsId": "w1"})
		self.assertEqual([], [f for f in self.client.drain(topic) if f.get("kind") == "doc.language"], "only a change is told")
		again = await self.client.request("doc.open", {"wsId": "w1", "path": "src/Game/main.cf"})
		self.assertEqual(doc["docId"], again["docId"])

	async def test_a_local_plugin_files_are_served_per_workspace_once_trusted(self):
		cookie = {"Cookie": self.cookie}
		self.assertEqual(404, self.server.request("GET", "/w/w1/plugins/c4/ui/main.js", headers=cookie).status, "not while untrusted")
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		reply = self.server.request("GET", "/w/w1/plugins/c4/ui/main.js", headers=cookie)
		self.assertEqual((200, "text/javascript; charset=utf-8"), (reply.status, reply.header("Content-Type")))
		self.assertIn(b"export function activate", reply.body)
		for path in ("/plugins/c4/ui/main.js", "/w/w9/plugins/c4/ui/main.js", "/w/w1/plugins/c4/../c4/settings.schema.json/x", "/w/w1/plugins/c4/%2e%2e/MyGame.iedsln", "/w/w1/plugins/c4/"):
			self.assertEqual(404, self.server.request("GET", path, headers=cookie).status, path)
		self.assertEqual(401, self.server.request("GET", "/w/w1/plugins/c4/ui/main.js").status, "files need a session")

	async def test_a_local_plugin_must_have_the_expected_id(self):
		with open(self.solution) as f:
			solution = json.load(f)
		solution["localPlugins"][0]["id"] = "not-c4"
		write(self.solution, solution)
		await self.client.wait_for("ws:w1", lambda f: f["t"] == "evt" and f["kind"] == "solution.changed", 10)
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		listed = {(p["id"], p["source"]): p for p in (await self.client.request("plugins.list", {"wsId": "w1"}))["plugins"]}
		self.assertEqual("failed", listed[("c4", "project")]["state"])
		self.assertIn("expects id not-c4", " ".join(listed[("c4", "project")]["problems"]))

	async def test_the_workspace_s_own_plugins(self):
		"""docs/26: each folder of .ied/plugins/ is a project plugin - it needs trust, the solution's pin of its id wins over
		it, and an edit reaches the clients as plugins.changed."""
		markup = {"manifestVersion": 1, "id": "my.markup", "name": "Markup", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
			"contributes": {"languages": [{"id": "mymarkup", "extensions": [".mym"]}]}}
		own = os.path.join(self.repo, ".ied", "plugins")
		write(os.path.join(own, "markup", "ied-plugin.json"), markup)
		write(os.path.join(own, "c4", "ied-plugin.json"), {"manifestVersion": 1, "id": "c4", "name": "Other C4", "version": "2.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"}})
		states = lambda listed: sorted((p["id"], p["source"], p["state"]) for p in listed["plugins"] if p["source"] == "project")
		listed = await self.client.request("plugins.list", {"wsId": "w1"})
		self.assertEqual([("c4", "project", "untrusted"), ("c4", "project", "untrusted"), ("my.markup", "project", "untrusted")], states(listed))
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		listed = await self.client.request("plugins.list", {"wsId": "w1"})
		self.assertEqual([("c4", "project", "loaded"), ("c4", "project", "overridden"), ("my.markup", "project", "loaded")], states(listed))
		pinned = [p for p in listed["plugins"] if p["id"] == "c4" and p["state"] == "loaded"][0]
		self.assertEqual("1.0.0", pinned["version"], "the solution's pin wins over .ied/plugins/")
		contributed = {p["id"]: p for p in (await self.client.request("plugins.contributions", {"wsId": "w1"}))["plugins"]}
		self.assertEqual(["c4", "my.markup"], sorted(contributed))
		markup["contributes"]["fileIcons"] = [{"extensions": [".mym"], "label": "MY", "color": "--hue-pink"}]
		write(os.path.join(own, "markup", "ied-plugin.json"), markup)
		await self.client.wait_for("ws:w1", lambda f: f["t"] == "evt" and f["kind"] == "plugins.changed", 10)
		contributed = {p["id"]: p for p in (await self.client.request("plugins.contributions", {"wsId": "w1"}))["plugins"]}
		self.assertEqual("MY", contributed["my.markup"]["contributes"]["fileIcons"][0]["label"], "read again after the edit")

	async def test_a_folder_workspace_has_its_own_plugins_too(self):
		folder = os.path.join(self.root, "Notes")
		write(os.path.join(folder, ".ied", "plugins", "markup", "ied-plugin.json"), {"manifestVersion": 1, "id": "my.markup", "name": "Markup", "version": "1.0.0",
			"ied": {"api": "^1"}, "runtime": {"kind": "declarative"}, "contributes": {"languages": [{"id": "mymarkup", "extensions": [".mym"]}]}})
		pipe = ControlPipe(self.server.control)
		try:
			opened = pipe.call("open", {"path": folder})["result"]
		finally:
			pipe.close()
		await self.client.request("ws.setTrust", {"wsId": opened["wsId"], "trusted": True})
		contributed = (await self.client.request("plugins.contributions", {"wsId": opened["wsId"]}))["plugins"]
		self.assertEqual([("my.markup", "project")], [(p["id"], p["source"]) for p in contributed])

	async def test_open_a_solution_over_the_control_pipe(self):
		other = os.path.join(self.root, "Other")
		os.makedirs(other)
		write(os.path.join(other, "Other.iedsln"), {"version": 1, "name": "Other", "projects": []})
		pipe = ControlPipe(self.server.control)
		try:
			opened = pipe.call("open", {"path": os.path.join(other, "Other.iedsln")})["result"]
			again = pipe.call("open", {"path": os.path.join(other, "Other.iedsln")})["result"]
		finally:
			pipe.close()
		self.assertEqual(opened["wsId"], again["wsId"], "one workspace per solution")
		self.assertEqual("", opened["path"], "the solution opens; its file is not shown as a document")
		summary = [w for w in (await self.client.request("ws.list"))["workspaces"] if w["wsId"] == opened["wsId"]][0]
		self.assertEqual(("solution", "Other"), (summary["kind"], summary["name"]))

	async def test_a_folder_with_one_solution_opens_as_it(self):
		"""docs/03: opening a folder that holds one solution file opens the solution (its launch URL names no file); a
		folder with two opens as the folder."""
		one = os.path.join(self.root, "One")
		write(os.path.join(one, "One.iedsln"), {"version": 1, "name": "OneSolution", "projects": []})
		two = os.path.join(self.root, "Two")
		write(os.path.join(two, "A.iedsln"), {"version": 1, "name": "A", "projects": []})
		write(os.path.join(two, "B.iedsln"), {"version": 1, "name": "B", "projects": []})
		pipe = ControlPipe(self.server.control)
		try:
			solution = pipe.call("open", {"path": one})["result"]
			folder = pipe.call("open", {"path": two})["result"]
		finally:
			pipe.close()
		self.assertEqual("", solution["path"], "nothing to open inside it")
		listed = {w["wsId"]: w for w in (await self.client.request("ws.list"))["workspaces"]}
		self.assertEqual(("solution", "OneSolution"), (listed[solution["wsId"]]["kind"], listed[solution["wsId"]]["name"]))
		self.assertEqual("folder", listed[folder["wsId"]]["kind"], "two solutions: no choice made for the owner")

	async def test_the_names_before_still_open(self):
		"""docs/03 "Files": a .ied.sln and its .ied.proj, from before the rename, open as a solution with its project."""
		old = os.path.join(self.root, "Old")
		write(os.path.join(old, "build", "ied", "Game.ied.proj"), project("Game", "../.."))
		write(os.path.join(old, "Old.ied.sln"), {"version": 1, "name": "Old", "projects": [{"file": "build/ied/*.ied.proj"}]})
		pipe = ControlPipe(self.server.control)
		try:
			opened = pipe.call("open", {"path": old})["result"]
		finally:
			pipe.close()
		await self.client.sub(["ws:" + opened["wsId"]])
		frames = await self.client.wait_for("ws:" + opened["wsId"], lambda f: f["t"] == "snap")
		solution = frames[-1]["data"]["solution"]
		self.assertEqual(("Old", ["Game"], []), (solution["name"], [p["name"] for p in solution["projects"]], solution["problems"]))
