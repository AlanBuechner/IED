"""Managing projects (docs/03 "Managing projects"): project.import makes a folder an IED solution - an open folder
workspace becomes it in place, a folder elsewhere opens as one - and project.add, project.remove and project.move
change a solution's projects. A solution written by hand is edited by IED (its comments kept; the language and the
task preset come from the plugins' markers); one a plugin's project system owns goes through its steps and then its
generate. Every change reaches the open workspace at once: solution.changed (with the kind and name) on its topic and
workspace.changed on the server's, documents open in a moved folder following it. The chat's project_* tools do the
same for the AI."""

import json
import os
import shutil
import sys
import tempfile
import unittest

from frames import FrameChecker
from iedclient import Client, RpcFailure
from routed import RoutedClient
from server import REPO, IedServer, ServerTestCase
from test_chat import TURN_SECONDS, ChatCase, call_step, text_step

PY = sys.executable.replace("\\", "/")
NUKE = os.path.join(REPO, "nuke.bat")

LANGUAGE_PLUGIN = {
	"manifestVersion": 1, "id": "fake.lang", "name": "Fake language", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
	"contributes": {
		"languages": [{"id": "fakelang", "aliases": ["Fake"], "extensions": [".fk"]}, {"id": "notes", "extensions": [".note"]}],
		"projectIcons": [{"languageId": "fakelang", "label": "FK", "color": "--hue-teal", "markers": ["fake.lock"]}, {"languageId": "otherlang", "label": "OT", "color": "--hue-blue", "markers": ["other.lock"]}],
		"taskPresets": [{"id": "fake.build", "label": "Fake build", "detect": "fake.lock", "tasks": {"build": {"label": "Build", "slot": "build", "steps": [{"run": "echo built"}]}}}]}}

# A build system of its own: fake.json maps project names to folders; generate writes the IED files from it.
SYSTEM_SCRIPT = r'''
import json, os, sys
verb, args = sys.argv[1], sys.argv[2:]
manifest = json.load(open("fake.json")) if os.path.exists("fake.json") else {"projects": {}}
projects = manifest["projects"]
if verb == "add":
	folder, name, language = args
	projects[name or os.path.basename(folder)] = folder
	print("added", folder, "language", language)
elif verb == "remove":
	projects.pop(args[0])
elif verb == "move":
	if args[1].startswith("fail"):
		print("cannot move there"); sys.exit(3)
	for name, folder in list(projects.items()):
		if folder == args[0]:
			projects[name] = args[1]
elif verb == "generate":
	os.makedirs("build/ied", exist_ok=True)
	for old in os.listdir("build/ied"):
		os.remove(os.path.join("build/ied", old))
	for name, folder in projects.items():
		json.dump({"version": 1, "name": name, "root": "../../" + folder, "kind": "executable"}, open("build/ied/%s.iedproj" % name, "w"))
	json.dump({"version": 1, "name": "Sys", "projects": [{"file": "build/ied/*.iedproj", "optional": True}], "generator": {"run": ["fake", "generate"]}}, open("Sys.iedsln", "w"))
	print("generated", len(projects))
json.dump(manifest, open("fake.json", "w"))
'''


def system_plugin(script):
	run = lambda *words: [{"run": [PY, "-I", script] + list(words)}]
	return {
		"manifestVersion": 1, "id": "fake.system", "name": "Fake build system", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
		"contributes": {"projectSystems": [{"id": "fake", "label": "Fake", "detect": "fake.json", "generate": run("generate"),
			"add": run("add", "${folder}", "${name}", "${language}"), "remove": run("remove", "${projectName}"), "delete": run("remove", "${projectName}"),
			"move": run("move", "${from}", "${to}")}]}}


def write(path, value):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8", newline="") as f:
		f.write(value if isinstance(value, str) else json.dumps(value, indent="\t"))


def read(path):
	with open(path, encoding="utf-8") as f:
		return f.read()


def make_home(home):
	shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(home, "shared", "host-api", "schemas"))
	write(os.path.join(home, "plugins", "fakelang", "ied-plugin.json"), LANGUAGE_PLUGIN)
	script = os.path.join(home, "plugins", "fakesys", "system.py").replace("\\", "/")
	write(script, SYSTEM_SCRIPT)
	write(os.path.join(home, "plugins", "fakesys", "ied-plugin.json"), system_plugin(script))
	return home


class ProjectManagementCase(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-manage-")
		self.home = make_home(os.path.join(self.root, "home"))
		self.folder = os.path.join(self.root, "Game")
		self.prepare()
		self.server = IedServer(data=os.path.join(self.root, "data"), extra=("--home", self.home, "--workspace", self.folder))
		self.assertTrue(self.server.started, self.server.error)

	def prepare(self):
		write(os.path.join(self.folder, "fake.lock"), "")
		write(os.path.join(self.folder, "src", "main.fk"), "main\n")
		write(os.path.join(self.folder, "tools", "packer", "pack.fk"), "pack\n")
		write(os.path.join(self.folder, "tools", "packer", "readme.note"), "notes\n")
		write(os.path.join(self.folder, "tools", "packer", "more.note"), "notes\n")
		write(os.path.join(self.folder, "docs", "a.note"), "a\n")

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		await self.client.sub(["ws:w1", "server"])
		self.client.drain("ws:w1")
		self.client.drain("server")

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def restart_on_solution(self):
		self.client.abort()
		self.server.stop()
		self.server = IedServer(data=os.path.join(self.root, "data2"), extra=("--home", self.home, "--workspace", os.path.join(self.folder, "Game.iedsln")))
		self.assertTrue(self.server.started, self.server.error)
		await self.asyncSetUp()

	def path(self, *parts):
		return os.path.join(self.folder, *parts)

	async def solution_changed(self):
		frames = await self.client.wait_for("ws:w1", lambda f: f.get("kind") == "solution.changed")
		return frames[-1]["data"]

	async def refused(self, code, text, method, params):
		with self.assertRaises(RpcFailure) as failure:
			await self.client.request(method, params)
		self.assertEqual(code, failure.exception.code, str(failure.exception))
		self.assertIn(text, str(failure.exception))


class HandWrittenSolutionTests(ProjectManagementCase):
	async def test_an_open_folder_becomes_a_solution_in_place(self):
		root = os.path.realpath(self.folder).replace("\\", "/")
		made = await self.client.request("project.import", {"path": self.folder})
		self.assertEqual(("w1", "solution", "Game"), (made["workspace"]["wsId"], made["workspace"]["kind"], made["workspace"]["name"]), "the same workspace, now a solution")
		self.assertTrue(made["path"].endswith("/Game.iedsln"), made)
		changed = await self.solution_changed()
		self.assertEqual(("solution", "Game", ["Game"]), (changed["kind"], changed["name"], [p["name"] for p in changed["solution"]["projects"]]))
		summary = (await self.client.wait_for("server", lambda f: f.get("kind") == "workspace.changed"))[-1]["data"]
		self.assertEqual(("w1", "solution"), (summary["wsId"], summary["kind"]))
		project = json.loads(read(self.path("Game.iedproj")))
		self.assertEqual({"version": 1, "name": "Game", "root": ".", "kind": "executable", "language": "fakelang", "presets": ["fake.build"]}, project,
			"the language from its marker file, the preset whose detect file is there")
		self.assertIn("language fakelang, from the folder's files", made["notes"])
		recent = (await self.client.wait_for("server", lambda f: f.get("kind") == "recent.changed"))[-1]["data"]["recent"]
		self.assertEqual(root + "/Game.iedsln", recent[0]["path"].replace("\\", "/"), "the recent projects name the solution now")
		self.assertNotIn(root, [r["path"].replace("\\", "/") for r in recent], "and not the folder")
		listed = (await self.client.request("ws.list", {}))["workspaces"]
		self.assertEqual(["solution"], [w["kind"] for w in listed if w["wsId"] == "w1"])
		await self.refused("conflict", "is an IED solution already", "project.import", {"path": self.folder})

	async def test_a_solution_a_generator_wrote_into_the_open_folder_is_opened_as_it_is(self):
		write(self.path("build", "ied", "Gen.iedproj"), {"version": 1, "name": "Gen", "root": "../..", "kind": "executable"})
		solution = {"version": 1, "name": "Generated", "projects": [{"file": "build/ied/*.iedproj", "optional": True}]}
		write(self.path("Generated.iedsln"), solution)
		made = await self.client.request("project.import", {"path": self.folder})
		self.assertEqual(("w1", "solution", "Generated"), (made["workspace"]["wsId"], made["workspace"]["kind"], made["workspace"]["name"]), "the same workspace, now the generated solution")
		self.assertTrue(made["path"].endswith("/Generated.iedsln"), made)
		self.assertTrue(any("was there already" in note for note in made["notes"]), made["notes"])
		changed = await self.solution_changed()
		self.assertEqual(["Gen"], [p["name"] for p in changed["solution"]["projects"]], "the generated project files, as written")
		self.assertEqual(solution, json.loads(read(self.path("Generated.iedsln"))), "the solution file is left as it was")
		self.assertFalse(os.path.exists(self.path("Game.iedsln")), "no second solution is written")
		await self.refused("conflict", "is an IED solution already", "project.import", {"path": self.folder})

	async def test_a_folder_joins_the_solution_and_the_comments_stay(self):
		await self.client.request("project.import", {"path": self.folder})
		await self.solution_changed()
		text = read(self.path("Game.iedsln")).replace("{\n", "{\n\t// written by hand\n", 1)
		write(self.path("Game.iedsln"), text)
		added = await self.client.request("project.add", {"wsId": "w1", "folder": "tools/packer"})
		self.assertEqual("packer", added["project"])
		changed = await self.solution_changed()
		self.assertEqual(["Game", "packer"], sorted(p["name"] for p in changed["solution"]["projects"]))
		solution = read(self.path("Game.iedsln"))
		self.assertIn("// written by hand", solution)
		self.assertIn("tools/packer/packer.iedproj", solution)
		packer = json.loads(read(self.path("tools", "packer", "packer.iedproj")))
		self.assertEqual(("fakelang", "other"), (packer["language"], packer["kind"]), "most of its files are notes, which no projectIcons entry draws; no preset: kind other")
		self.assertTrue(any("no build or run tasks" in note for note in added["notes"]), added["notes"])
		await self.refused("conflict", "is project packer already", "project.add", {"wsId": "w1", "folder": "tools/packer"})
		await self.refused("not_found", "no folder", "project.add", {"wsId": "w1", "folder": "nowhere"})
		await self.refused("bad_request", "own folder", "project.add", {"wsId": "w1", "folder": ""})

	async def test_adding_to_a_folder_makes_it_a_solution_first(self):
		added = await self.client.request("project.add", {"wsId": "w1", "folder": "docs", "name": "Docs", "language": "notes", "kind": "other"})
		self.assertEqual(("solution", "Docs"), (added["workspace"]["kind"], added["project"]))
		changed = await self.solution_changed()
		self.assertEqual(["Docs"], [p["name"] for p in changed["solution"]["projects"]], "no project for the folder itself")
		self.assertEqual("notes", json.loads(read(self.path("docs", "Docs.iedproj")))["language"])

	async def test_moving_a_project_takes_its_open_documents(self):
		await self.client.request("project.import", {"path": self.folder})
		await self.client.request("project.add", {"wsId": "w1", "folder": "tools/packer"})
		opened = await self.client.request("doc.open", {"wsId": "w1", "path": "tools/packer/pack.fk"})
		await self.client.sub(["doc:" + opened["docId"]])
		self.client.drain("ws:w1")
		moved = await self.client.request("project.move", {"wsId": "w1", "project": "packer", "to": "libs/packer"})
		self.assertTrue(moved["path"].endswith("/libs/packer"), moved)
		renamed = (await self.client.wait_for("doc:" + opened["docId"], lambda f: f.get("kind") == "doc.renamed"))[-1]["data"]
		self.assertEqual("libs/packer/pack.fk", renamed["path"])
		changed = await self.solution_changed()
		packer = next(p for p in changed["solution"]["projects"] if p["name"] == "packer")
		self.assertTrue(packer["root"].replace("\\", "/").endswith("/libs/packer"), packer)
		self.assertIn("libs/packer/packer.iedproj", read(self.path("Game.iedsln")))
		self.assertNotIn("tools/packer", read(self.path("Game.iedsln")))
		await self.refused("bad_request", "into its own folder", "project.move", {"wsId": "w1", "project": "packer", "to": "libs/packer/inner"})
		await self.refused("bad_request", "workspace's own", "project.move", {"wsId": "w1", "project": "Game", "to": "elsewhere"})
		await self.refused("conflict", "is there already", "project.move", {"wsId": "w1", "project": "packer", "to": "src"})

	async def test_a_project_file_elsewhere_gets_its_root_rewritten(self):
		write(self.path("Game.iedsln"), {"version": 1, "name": "Game", "projects": [{"file": "ide/Engine.iedproj"}]})
		write(self.path("ide", "Engine.iedproj"), {"version": 1, "name": "Engine", "root": "../engine", "kind": "library"})
		write(self.path("engine", "e.fk"), "e\n")
		await self.restart_on_solution()
		await self.client.request("project.move", {"wsId": "w1", "project": "Engine", "to": "libs/engine"})
		self.assertEqual("../libs/engine", json.loads(read(self.path("ide", "Engine.iedproj")))["root"])
		changed = await self.solution_changed()
		self.assertEqual([], changed["solution"]["projects"][0]["problems"])

	async def test_removing_keeps_or_trashes_and_waits_for_dependents(self):
		await self.client.request("project.import", {"path": self.folder})
		await self.client.request("project.add", {"wsId": "w1", "folder": "tools/packer"})
		await self.client.request("project.add", {"wsId": "w1", "folder": "docs", "name": "Docs"})
		game = json.loads(read(self.path("Game.iedproj")))
		game["references"] = ["packer"]
		write(self.path("Game.iedproj"), game)
		await self.client.wait_for("ws:w1", lambda f: f.get("kind") == "solution.changed" and any(p["data"].get("references") for p in f["data"]["solution"]["projects"]))
		await self.refused("conflict", "Game depend on packer", "project.remove", {"wsId": "w1", "project": "packer"})
		await self.refused("not_found", "its projects: Game", "project.remove", {"wsId": "w1", "project": "Nope"})
		await self.refused("bad_request", "workspace's own", "project.remove", {"wsId": "w1", "project": "Game", "deleteFiles": True})
		self.client.drain("ws:w1")
		kept = await self.client.request("project.remove", {"wsId": "w1", "project": "Docs"})
		self.assertNotIn("trashId", kept)
		changed = await self.solution_changed()
		self.assertNotIn("Docs", [p["name"] for p in changed["solution"]["projects"]])
		self.assertTrue(os.path.exists(self.path("docs", "a.note")) and os.path.exists(self.path("docs", "Docs.iedproj")), "its files stay")
		game.pop("references")
		write(self.path("Game.iedproj"), game)
		await self.client.wait_for("ws:w1", lambda f: f.get("kind") == "solution.changed" and not any(p["data"].get("references") for p in f["data"]["solution"]["projects"]))
		deleted = await self.client.request("project.remove", {"wsId": "w1", "project": "packer", "deleteFiles": True})
		self.assertTrue(deleted["trashId"])
		self.assertFalse(os.path.exists(self.path("tools", "packer")), "in the trash")
		trash = (await self.client.request("trash.list", {}))["entries"]
		self.assertIn("tools/packer", [e["path"] for e in trash])

	async def test_a_globbed_project_file_goes_to_the_trash(self):
		write(self.path("Game.iedsln"), {"version": 1, "name": "Game", "projects": [{"file": "ide/*.iedproj"}]})
		write(self.path("ide", "Engine.iedproj"), {"version": 1, "name": "Engine", "root": "../src", "kind": "library"})
		await self.restart_on_solution()
		removed = await self.client.request("project.remove", {"wsId": "w1", "project": "Engine"})
		self.assertIn("a glob of the solution's projects lists it", " ".join(removed["notes"]))
		self.assertFalse(os.path.exists(self.path("ide", "Engine.iedproj")))
		self.assertTrue(os.path.exists(self.path("src", "main.fk")))


class ProjectSystemTests(ProjectManagementCase):
	def prepare(self):
		super().prepare()
		write(os.path.join(self.folder, "fake.json"), {"projects": {"Game": "src"}})

	async def test_a_build_system_owns_the_projects(self):
		await self.refused("forbidden", "trust the workspace first", "project.import", {"path": self.folder})
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		made = await self.client.request("project.import", {"path": self.folder})
		self.assertTrue(made["path"].endswith("/Sys.iedsln") and "generated 1" in made["log"], made)
		changed = await self.solution_changed()
		self.assertEqual(("solution", ["Game"]), (changed["kind"], [p["name"] for p in changed["solution"]["projects"]]))
		added = await self.client.request("project.add", {"wsId": "w1", "folder": "tools/packer", "name": "Packer"})
		self.assertIn("added tools/packer language fakelang", added["log"], "the build system gets the folder and its language")
		self.assertEqual("Packer", added["project"])
		moved = await self.client.request("project.move", {"wsId": "w1", "project": "Packer", "to": "libs/packer"})
		self.assertEqual("libs/packer", json.loads(read(self.path("fake.json")))["projects"]["Packer"])
		self.assertEqual([], moved["notes"])
		await self.refused("unavailable", "cannot move there", "project.move", {"wsId": "w1", "project": "Packer", "to": "fail/here"})
		self.assertTrue(os.path.exists(self.path("libs", "packer", "pack.fk")), "a failed move puts the folder back")
		removed = await self.client.request("project.remove", {"wsId": "w1", "project": "Packer", "deleteFiles": True})
		self.assertTrue(removed["trashId"])
		self.assertEqual({"Game": "src"}, json.loads(read(self.path("fake.json")))["projects"])
		changed = await self.client.wait_for("ws:w1", lambda f: f.get("kind") == "solution.changed" and [p["name"] for p in f["data"]["solution"]["projects"]] == ["Game"])
		self.assertTrue(changed)

	async def test_a_generated_solution_no_system_knows_is_refused(self):
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		await self.client.request("project.import", {"path": self.folder})
		os.remove(self.path("fake.json"))
		await self.refused("bad_request", "made by `fake generate`", "project.add", {"wsId": "w1", "folder": "docs"})


@unittest.skipUnless(os.path.exists(NUKE), "no nuke.bat")
class NukeSolutionTests(ProjectManagementCase):
	"""The nuke plugin's project system: `nuke project add/remove/move` change the manifests, `nuke init --ied` writes the
	solution again."""

	def prepare(self):
		super().prepare()
		shutil.copytree(os.path.join(REPO, "plugins", "nuke"), os.path.join(self.home, "plugins", "nuke"))
		write(os.path.join(self.folder, "nuke.toml"), "# the demo\n[workspace]\nname = \"Demo\"\n")
		write(os.path.join(self.folder, "Game", "nuke.toml"), "[project]\nkind = \"custom\"\n")

	async def asyncSetUp(self):
		await super().asyncSetUp()
		await self.client.request("settings.set", {"tier": "app", "key": "nuke.command", "value": NUKE})
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})

	async def shows(self, names):
		"""Waits for the solution.changed that lists `names` (the watcher's own echoes of earlier changes come first)."""
		frames = await self.client.wait_for("ws:w1", lambda f: f.get("kind") == "solution.changed" and sorted(p["name"] for p in f["data"]["solution"]["projects"]) == names)
		return frames[-1]["data"]

	async def test_projects_of_a_nuke_workspace(self):
		made = await self.client.request("project.import", {"path": self.folder})
		self.assertTrue(made["path"].endswith("/Demo.iedsln"), made)
		await self.shows(["Game"])
		added = await self.client.request("project.add", {"wsId": "w1", "folder": "tools/packer"})
		await self.shows(["Game", "packer"])
		self.assertIn("language = \"fakelang\"", read(self.path("tools", "packer", "nuke.toml")), added["log"])
		self.assertIn("kind = \"custom\"", read(self.path("tools", "packer", "nuke.toml")), "no Nuke driver for the language: an IDE-only project")
		moved = await self.client.request("project.move", {"wsId": "w1", "project": "packer", "to": "libs/packer"})
		self.assertEqual([], moved["notes"], "the solution shows it in its new folder")
		await self.client.request("project.remove", {"wsId": "w1", "project": "packer", "deleteFiles": True})
		await self.shows(["Game"])
		self.assertFalse(os.path.exists(self.path("libs", "packer")))
		await self.client.request("project.remove", {"wsId": "w1", "project": "Game"})
		await self.shows([])
		self.assertIn("exclude = [\"Game\"]", read(self.path("nuke.toml")))
		self.assertTrue(read(self.path("nuke.toml")).startswith("# the demo\n"), "the manifest's comments stay")
		self.assertTrue(os.path.exists(self.path("Game", "nuke.toml")), "its files stay")


class ProjectToolTests(ChatCase):
	"""The chat's project tools on the same server operations."""

	def setUp(self):
		super().setUp()
		self.server.stop(remove_data=False)
		write(os.path.join(self.home, "plugins", "fakelang", "ied-plugin.json"), LANGUAGE_PLUGIN)
		write(os.path.join(self.repo, "fake.lock"), "")
		write(os.path.join(self.repo, "tools", "packer", "pack.fk"), "pack\n")
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)

	async def results(self, *calls):
		chat_id, _ = await self.chat(permission="auto")
		self.model.queue(*[call_step(name, args, call_id) for call_id, name, args in calls], text_step("Done."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=TURN_SECONDS))["stop"])
		sent = self.model.requests()[-1]["body"]["messages"]
		return {m["tool_call_id"]: m["content"] for m in sent if m["role"] == "tool"}

	async def test_the_ai_makes_and_changes_projects(self):
		await self.client.sub(["ws:w1"])
		place = os.path.join(self.root, "place").replace("\\", "/")
		os.makedirs(place)
		results = await self.results(
			("l1", "project_list", {}),
			("i1", "project_import", {"folder": "."}),
			("i2", "project_import", {"folder": "tools/packer", "name": "Packer"}),
			("m1", "project_move", {"project": "Packer", "to": "libs/packer"}),
			("t1", "project_templates", {}),
			("r1", "project_remove", {"project": "Packer"}),
			("r2", "project_remove", {"project": "Nope"}))
		self.assertIn("No projects: it is a folder", results["l1"])
		self.assertIn("is now the IED solution", results["i1"])
		self.assertIn("1 project:\n- Game (executable, fakelang) in the workspace's folder, file Game.iedproj", results["i1"])
		self.assertIn("as project Packer", results["i2"])
		self.assertIn("- Packer (", results["i2"])
		self.assertIn("in libs/packer", results["m1"])
		self.assertIn("A new workspace goes in", results["t1"])
		self.assertIn("Removed project Packer", results["r1"])
		self.assertNotIn("- Packer", results["r1"])
		self.assertIn("project_remove: no project Nope in the solution; its projects: Game", results["r2"])
		frames = self.client.drain("ws:w1")
		kinds = [f["data"]["kind"] for f in frames if f.get("kind") == "solution.changed"]
		self.assertIn("solution", kinds, "the open workspace changed under the client")
