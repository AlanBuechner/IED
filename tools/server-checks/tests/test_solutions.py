"""Solutions and projects (docs/03): a .ied.sln opened as a solution workspace, its projects found by path and glob and
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
		write(os.path.join(self.repo, "build", "ied", "Game.ied.proj"), project("Game", "../../src/Game"))
		write(os.path.join(self.repo, "tools", "Tool.ied.proj"), project("Tool", "Tool", kind="other"))
		write(os.path.join(self.repo, "Nuke", "tools", "ied", "ied-plugin.json"), {
			"manifestVersion": 1, "id": "c4", "name": "C4", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative", "ui": "ui/main.js"},
			"contributes": {"commands": [{"id": "c4.hello", "title": "Hello"}], "settings": {"schema": "settings.schema.json"}}})
		write(os.path.join(self.repo, "Nuke", "tools", "ied", "settings.schema.json"), SETTINGS_SCHEMA)
		write(os.path.join(self.repo, "Nuke", "tools", "ied", "ui", "main.js"), "export function activate(ied) {}\n")
		self.solution = os.path.join(self.repo, "MyGame.ied.sln")
		write(self.solution, {
			"version": 1, "name": "MyGame",
			"projects": [{"file": "build/ied/*.ied.proj", "optional": True}, {"file": "tools/Tool.ied.proj"}],
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
		write(os.path.join(self.repo, "build", "ied", "Extra.ied.proj"), project("Extra", "../../missing", version="one"))
		frames = await self.client.wait_for("ws:w1", lambda f: f["t"] == "evt" and f["kind"] == "solution.changed", 10)
		projects = {p["name"]: p for p in frames[-1]["data"]["solution"]["projects"]}
		self.assertIn("Extra", projects, "a project appearing under the glob")
		problems = " ".join(projects["Extra"]["problems"])
		self.assertIn("/version", problems)
		self.assertIn("no folder", problems)
		os.remove(os.path.join(self.repo, "tools", "Tool.ied.proj"))
		frames = await self.client.wait_for("ws:w1", lambda f: f["t"] == "evt" and f["kind"] == "solution.changed" and "Tool" not in [p["name"] for p in f["data"]["solution"]["projects"]], 10)
		self.assertIn("no project file matches tools/Tool.ied.proj", frames[-1]["data"]["solution"]["problems"])

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

	async def test_a_local_plugin_files_are_served_per_workspace_once_trusted(self):
		cookie = {"Cookie": self.cookie}
		self.assertEqual(404, self.server.request("GET", "/w/w1/plugins/c4/ui/main.js", headers=cookie).status, "not while untrusted")
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		reply = self.server.request("GET", "/w/w1/plugins/c4/ui/main.js", headers=cookie)
		self.assertEqual((200, "text/javascript; charset=utf-8"), (reply.status, reply.header("Content-Type")))
		self.assertIn(b"export function activate", reply.body)
		for path in ("/plugins/c4/ui/main.js", "/w/w9/plugins/c4/ui/main.js", "/w/w1/plugins/c4/../c4/settings.schema.json/x", "/w/w1/plugins/c4/%2e%2e/MyGame.ied.sln", "/w/w1/plugins/c4/"):
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

	async def test_open_a_solution_over_the_control_pipe(self):
		other = os.path.join(self.root, "Other")
		os.makedirs(other)
		write(os.path.join(other, "Other.ied.sln"), {"version": 1, "name": "Other", "projects": []})
		pipe = ControlPipe(self.server.control)
		try:
			opened = pipe.call("open", {"path": os.path.join(other, "Other.ied.sln")})["result"]
			again = pipe.call("open", {"path": os.path.join(other, "Other.ied.sln")})["result"]
		finally:
			pipe.close()
		self.assertEqual(opened["wsId"], again["wsId"], "one workspace per solution")
		summary = [w for w in (await self.client.request("ws.list"))["workspaces"] if w["wsId"] == opened["wsId"]][0]
		self.assertEqual(("solution", "Other"), (summary["kind"], summary["name"]))
