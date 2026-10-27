"""Plugin folders (docs/13 "Plugin folders"): plugins:/<plugin id>/<path> names a file of a plugin loaded for the chat's
workspace - its skills, its reference docs, its manifest - which fs_list, fs_read, fs_glob and search_text read even
without developer mode, plugins:/ itself listing the plugins; skill_read and docs_read say where a file lives. Writes
there, and writes into a loaded plugin's folder by its workspace path (a toolchain's plugin a solution pins), are refused
unless a root holds the folder (developer mode, test_developer.py) or the plugin is the workspace's own (.ied/plugins/),
where a plugins:/ write lands."""

import json
import os

from server import IedServer
from test_chat import DEMO_PLUGIN, ChatCase, call_step, text_step, write

SKILL = "---\nname: {0}\ndescription: {1}\nkeywords: [{2}]\n---\n\n# {0}\n\n{3}\n"
GUIDE = "# Guide\n\n## Loops\n\nA demo loop ends with a semicolon.\n"


def declarative(plugin_id, name, contributes):
	return {"manifestVersion": 1, "id": plugin_id, "name": name, "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"}, "contributes": contributes}


class PluginFolderTests(ChatCase):
	"""The demo language plugin of the install with a skill (and a file beside it) and reference docs, in a solution that
	pins a vendored plugin (as a C4 solution pins its toolchain's) and holds one of its own."""

	def start_server(self):
		manifest = json.loads(json.dumps(DEMO_PLUGIN))
		manifest["contributes"]["aiDocs"] = [{"id": "demo", "title": "Demo reference", "folder": "docs"}]
		self.demo = os.path.join(self.home, "plugins", "demo")
		write(os.path.join(self.demo, "ied-plugin.json"), json.dumps(manifest))
		write(os.path.join(self.demo, "skills", "demo-style", "SKILL.md"), SKILL.format("demo-style", "Demo code style.", "semicolon", "Semicolons everywhere; examples.md shows them."))
		write(os.path.join(self.demo, "skills", "demo-style", "examples.md"), "a = 1;\n")
		write(os.path.join(self.demo, "docs", "guide.md"), GUIDE)
		self.kit = os.path.join(self.repo, "vendor", "kit")
		write(os.path.join(self.kit, "ied-plugin.json"), json.dumps(declarative("vendor.kit", "Vendor Kit", {"skills": ["skills"]})))
		write(os.path.join(self.kit, "skills", "kit-use", "SKILL.md"), SKILL.format("kit-use", "Using the kit.", "kit", "Call the kit."))
		self.own = os.path.join(self.repo, ".ied", "plugins", "own")
		write(os.path.join(self.own, "ied-plugin.json"), json.dumps(declarative("own.tools", "Own Tools", {"skills": ["skills"]})))
		solution = os.path.join(self.repo, "Game.iedsln")
		write(solution, json.dumps({"version": 1, "name": "Game", "projects": [{"file": "build/ied/*.iedproj", "optional": True}],
			"localPlugins": [{"id": "vendor.kit", "path": "vendor/kit"}]}))
		return IedServer(data=self.data, extra=("--home", self.home, "--workspace", solution))

	async def results(self, *calls):
		chat_id, _ = await self.chat(permission="auto")
		self.model.queue(*[call_step(name, args, call_id) for call_id, name, args in calls], text_step("Done."))
		end = await self.turn(chat_id)
		self.assertEqual("finished", end["stop"], end)
		sent = self.model.requests()[-1]["body"]["messages"]
		return {m["tool_call_id"]: m["content"] for m in sent if m["role"] == "tool"}

	async def test_a_plugins_files_are_read_where_it_lives(self):
		results = await self.results(
			("l0", "fs_list", {"path": "plugins:/"}),
			("l1", "fs_list", {"path": "plugins:/demo.lang", "depth": 3}),
			("r1", "fs_read", {"path": "plugins:/demo.lang/skills/demo-style/SKILL.md"}),
			("g1", "fs_glob", {"pattern": "**/*.md", "root": "plugins:/demo.lang"}),
			("s1", "search_text", {"pattern": "semicolon", "path": "plugins:/demo.lang/docs"}),
			("k1", "skill_read", {"name": "demo-style"}),
			("k2", "skill_read", {"name": "demo-style", "file": "examples.md"}),
			("d1", "docs_read", {"path": "demo/guide.md"}),
			("d2", "docs_read", {"path": "demo/guide.md", "heading": "Loops"}),
			("r2", "fs_read", {"path": "plugins:/no.such/x.md"}),
			("r3", "fs_read", {"path": "plugins:/demo.lang/nothing.md"}),
			("c1", "code_symbols", {"path": "plugins:/demo.lang/docs/guide.md"}))
		system = self.model.requests()[0]["body"]["messages"][0]["content"]
		self.assertIn("a plugin's are in its folder, plugins:/<plugin id>/", system, "the skills index says where a plugin's skills are")
		self.assertIn("and are read-only", system)
		self.assertIn("plugins:/demo.lang/  Demo Language (builtin)", results["l0"], "plugins:/ lists the plugins")
		self.assertIn("plugins:/vendor.kit/  Vendor Kit (project)", results["l0"])
		self.assertIn("plugins:/demo.lang/skills/demo-style/examples.md", results["l1"], "the paths shown can be passed back")
		self.assertIn("Semicolons everywhere", results["r1"])
		self.assertIn("plugins:/demo.lang/docs/guide.md", results["g1"])
		self.assertIn("plugins:/demo.lang/docs/guide.md\n  5: A demo loop ends with a semicolon.", results["s1"], "search_text searches a plugin's folder")
		self.assertIn("This skill is plugins:/demo.lang/skills/demo-style/SKILL.md, read-only: a plugin's file", results["k1"])
		self.assertIn("Beside it in plugins:/demo.lang/skills/demo-style/: examples.md", results["k1"], "the files beside it, to open")
		self.assertIn("[plugins:/demo.lang/skills/demo-style/examples.md, read-only", results["k2"])
		self.assertIn("demo/guide.md (plugins:/demo.lang/docs/guide.md, read-only): ", results["d1"], "docs_read names where the file lives")
		self.assertIn("Loops (plugins:/demo.lang/docs/guide.md, read-only)", results["d2"])
		self.assertIn("no plugin no.such is loaded for this workspace", results["r2"])
		self.assertIn("demo.lang", results["r2"], "the plugins are named")
		self.assertIn("no file nothing.md in the plugin's folder", results["r3"])
		self.assertIn("does not work there", results["c1"], "a tool that needs a workspace says so")

	async def test_writes_into_plugin_folders_are_refused(self):
		results = await self.results(
			("w1", "fs_write", {"path": "plugins:/demo.lang/skills/demo-style/SKILL.md", "content": "gone\n"}),
			("e1", "fs_edit", {"path": "plugins:/demo.lang/docs/guide.md", "edits": [{"old": "Loops", "new": "Rings"}]}),
			("x1", "shell_run", {"command": "echo hi", "cwd": "plugins:/demo.lang"}),
			("w2", "fs_write", {"path": "vendor/kit/skills/kit-use/SKILL.md", "content": "gone\n"}),
			("w3", "fs_write", {"path": ".ied/plugins/own/skills/mine/SKILL.md", "content": SKILL.format("mine", "Mine.", "mine", "My own.")}),
			("w4", "fs_write", {"path": "plugins:/own.tools/skills/more/SKILL.md", "content": SKILL.format("more", "More.", "more", "More of mine.")}),
			("w5", "fs_write", {"path": "src/notes.txt", "content": "fine\n"}))
		for call in ("w1", "e1", "x1"):
			self.assertIn("plugins:/demo.lang/ is read-only: a plugin's files", results[call], call)
		self.assertIn("write it in the workspace's .ied/skills/<name>/SKILL.md", results["w1"], "where a skill of its own goes")
		self.assertIn("vendor/kit/skills/kit-use/SKILL.md is in the folder of the plugin vendor.kit", results["w2"], "by its workspace path too")
		with open(os.path.join(self.demo, "skills", "demo-style", "SKILL.md"), encoding="utf-8") as f:
			self.assertIn("Semicolons everywhere", f.read())
		with open(os.path.join(self.demo, "docs", "guide.md"), encoding="utf-8") as f:
			self.assertEqual(GUIDE, f.read())
		with open(os.path.join(self.kit, "skills", "kit-use", "SKILL.md"), encoding="utf-8") as f:
			self.assertIn("Call the kit.", f.read())
		self.assertIn("saved", results["w3"], "the workspace's own plugin is the workspace's to change")
		self.assertIn("[plugins:/own.tools/ is .ied/plugins/own/: the change was made there]", results["w4"])
		self.assertTrue(os.path.exists(os.path.join(self.own, "skills", "more", "SKILL.md")))
		self.assertIn("saved", results["w5"], "the rest of the workspace is not touched by it")


class PluginWorkspaceTests(ChatCase):
	"""A workspace that is a loaded plugin's own folder - the owner working on that plugin - is theirs to change."""

	def start_server(self):
		self.plugin = os.path.join(self.home, "plugins", "demo")
		return IedServer(data=self.data, extra=("--home", self.home, "--workspace", self.plugin))

	async def test_the_plugin_being_worked_on_takes_writes(self):
		chat_id, _ = await self.chat(permission="auto")
		self.model.queue(call_step("fs_write", {"path": "skills/new/SKILL.md", "content": SKILL.format("new", "New.", "new", "A new one.")}, "w1"), text_step("Done."))
		await self.turn(chat_id)
		result = {m["tool_call_id"]: m["content"] for m in self.model.requests()[-1]["body"]["messages"] if m["role"] == "tool"}["w1"]
		self.assertIn("skills/new/SKILL.md: +", result)
		self.assertTrue(os.path.exists(os.path.join(self.plugin, "skills", "new", "SKILL.md")))
