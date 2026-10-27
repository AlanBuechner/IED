"""New projects (docs/03 "New projects"): project.templates lists the plugins' projectTemplates with what the New
Project dialog draws them with, and project.create makes a project from one - a new workspace (the template's steps
run in its folder, then its files are copied in with ${name} filled and none replacing a file there; it opens trusted)
or a project added to a solution whose folder holds one of the template's `into` files. A bad name, a folder that is
there, an untrusted solution and a template that cannot add are refused; a failing step leaves nothing and says why,
and one whose program a setting names says how to give it."""

import json
import os
import shutil
import sys
import tempfile

from frames import FrameChecker
from iedclient import Client, RpcFailure
from routed import RoutedClient
from server import REPO, IedServer, ServerTestCase

PY = sys.executable.replace("\\", "/")


def write(path, value):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8", newline="") as f:
		f.write(value if isinstance(value, str) else json.dumps(value, indent="\t"))


def read(path):
	with open(path, encoding="utf-8") as f:
		return f.read()


def step(code):
	return {"run": [PY, "-I", "-c", code]}


TEMPLATES = [
	{"id": "app", "title": "App", "description": "Makes an app.", "language": "fake", "tags": ["Console"], "files": "templates/app",
		"steps": [step("import os; open('from-step.txt', 'w').write(os.getcwd().replace(chr(92), '/'))"),
			step("open('${name}.txt', 'w').write('the step was first')"),
			step("open('greeting.txt', 'w').write('${config:fake.greeting}')")]},
	{"id": "addable", "title": "Addable", "language": "fake", "into": ["nuke.toml"], "files": "templates/app",
		"steps": [step("import os; open('${name}-made-here.txt', 'w').write(os.getcwd().replace(chr(92), '/'))")]},
	{"id": "failing", "title": "Failing", "steps": [step("import sys; print('it broke here'); sys.exit(3)")]},
	{"id": "missing", "title": "Missing", "steps": [{"run": "\"${config:fake.tool}\" init ${name}"}]},
	{"id": "outside", "title": "Outside", "files": "../elsewhere"}]

PLUGIN = {
	"manifestVersion": 1, "id": "fake.templates", "name": "Fake templates", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
	"contributes": {
		"settings": {"schema": "settings.schema.json"},
		"languages": [{"id": "fake", "aliases": ["Fake Lang"], "extensions": [".fake"]}],
		"projectIcons": [{"languageId": "fake", "label": "FK", "color": "--hue-teal"}],
		"projectTemplates": TEMPLATES}}


class ProjectTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-projects-")
		self.home = os.path.join(self.root, "home")
		self.repo = os.path.join(self.root, "Game")
		self.place = os.path.join(self.root, "place").replace("\\", "/")
		os.makedirs(self.place)
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		plugin = os.path.join(self.home, "plugins", "fake")
		write(os.path.join(plugin, "ied-plugin.json"), PLUGIN)
		write(os.path.join(plugin, "settings.schema.json"), {"properties": {"fake.greeting": {"type": "string", "default": "hi there"},
			"fake.tool": {"type": "string", "default": "no-such-tool-ied"}}})
		write(os.path.join(plugin, "templates", "app", "${name}.txt"), "hello ${name}")
		write(os.path.join(plugin, "templates", "app", "src", "main.fake"), "// ${name}\nmain\n")
		write(os.path.join(self.repo, "nuke.toml"), "[workspace]\n")
		write(os.path.join(self.repo, "Game.iedproj"), {"version": 1, "name": "Game", "root": ".", "kind": "executable"})
		write(os.path.join(self.repo, "Game.iedsln"), {"version": 1, "name": "Game", "projects": [{"file": "Game.iedproj"}]})
		self.server = IedServer(extra=("--home", self.home, "--workspace", os.path.join(self.repo, "Game.iedsln")))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.cookie = self.server.sign_in()
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def create(self, **params):
		return await self.client.request("project.create", dict({"template": "fake.templates/app", "name": "Demo", "location": self.place}, **params))

	async def refused(self, code, text=None, **params):
		with self.assertRaises(RpcFailure) as failure:
			await self.create(**params)
		self.assertEqual(code, failure.exception.code, str(failure.exception))
		if text:
			self.assertIn(text, str(failure.exception))
		return str(failure.exception)

	async def test_the_templates_and_what_draws_them(self):
		listed = await self.client.request("project.templates", {})
		app = next(t for t in listed["templates"] if t["key"] == "fake.templates/app")
		self.assertEqual(("App", "Makes an app.", "fake", "Fake Lang", ["Console"], False), (app["title"], app["description"], app["language"], app["languageName"], app["tags"], app["canAdd"]))
		self.assertEqual([{"plugin": {"id": "fake.templates", "base": "/plugins/fake.templates/"}, "value": {"languageId": "fake", "label": "FK", "color": "--hue-teal"}}],
			[i for i in listed["icons"] if i["plugin"]["id"] == "fake.templates"])
		self.assertTrue(listed["location"], "a folder to put a new project in")
		untrusted = {t["key"]: t["canAdd"] for t in (await self.client.request("project.templates", {"wsId": "w1"}))["templates"]}
		self.assertFalse(untrusted["fake.templates/addable"], "the solution is not trusted yet")
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		trusted = {t["key"]: t["canAdd"] for t in (await self.client.request("project.templates", {"wsId": "w1"}))["templates"]}
		self.assertEqual((True, False), (trusted["fake.templates/addable"], trusted["fake.templates/app"]), "only a template whose `into` the folder holds")

	async def test_a_new_workspace_from_a_template(self):
		made = await self.create()
		folder = self.place + "/Demo"
		self.assertEqual(folder, made["path"])
		self.assertEqual(folder, read(os.path.join(folder, "from-step.txt")), "the steps run in the new folder")
		self.assertEqual("the step was first", read(os.path.join(folder, "Demo.txt")), "a template's file never replaces one there")
		self.assertEqual("// Demo\nmain\n", read(os.path.join(folder, "src", "main.fake")), "${name} filled in the text")
		self.assertEqual("hi there", read(os.path.join(folder, "greeting.txt")), "${config:KEY} from the app's settings")
		workspace = made["workspace"]
		self.assertEqual(("Demo", True), (workspace["name"], workspace["trusted"]), "opened, and trusted: its owner made it")
		listed = (await self.client.request("ws.list", {}))["workspaces"]
		self.assertIn(workspace["wsId"], [w["wsId"] for w in listed])
		await self.refused("conflict", "is there already")

	async def test_a_project_added_to_a_solution(self):
		await self.refused("forbidden", wsId="w1", template="fake.templates/addable", location=None)
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		await self.refused("bad_request", "cannot add a project to this solution", wsId="w1")
		made = await self.client.request("project.create", {"template": "fake.templates/addable", "name": "Tool", "wsId": "w1"})
		root = self.repo.replace("\\", "/")
		self.assertEqual((root + "/Tool", "w1"), (made["path"], made["workspace"]["wsId"]))
		self.assertEqual(root, read(os.path.join(self.repo, "Tool-made-here.txt")), "the steps run in the solution's folder")
		self.assertEqual("hello Tool", read(os.path.join(self.repo, "Tool", "Tool.txt")), "the files go in the project's folder")

	async def test_refusals(self):
		await self.refused("bad_request", "cannot name a project", name="bad name")
		await self.refused("bad_request", "cannot name a project", name="-dash")
		await self.refused("bad_request", "full path", location="relative/folder")
		await self.refused("not_found", "no project template", template="fake.templates/nope")
		await self.refused("bad_request", "outside its plugin", template="fake.templates/outside")
		self.assertFalse(os.path.exists(os.path.join(self.place, "Demo")))

	async def test_a_failing_step_leaves_nothing(self):
		why = await self.refused("unavailable", "failed (exit 3)", template="fake.templates/failing")
		self.assertIn("it broke here", why)
		self.assertFalse(os.path.exists(os.path.join(self.place, "Demo")), "the half-made folder went")
		why = await self.refused("unavailable", "no-such-tool-ied was not found", template="fake.templates/missing")
		self.assertIn("Set fake.tool (Settings) to its full path", why)
