import json
import os
import unittest

from support import StubStore, WorkspaceTest, make_install, read, write_files
from nuke import manifest


class InitTests(WorkspaceTest):
	def test_new_workspace_uses_the_latest_toolchain_and_copies_skills(self):
		code, out = self.nuke("init", "Game")
		self.assertEqual(code, 0, out)
		parsed = manifest.load(self.path("nuke.toml"))
		self.assertEqual((parsed.workspace.toolchain, parsed.project.name, parsed.project.kind), ("1.0.0", "Game", "executable"))
		self.assertIn("int32 main()", read(self.path("src", "main.cf")))
		self.assertTrue(os.path.isfile(self.path(".ied", "skills", "c4-language", "SKILL.md")))
		self.assertTrue(os.path.isfile(self.path(".ied", "skills", "nuke", "SKILL.md")))
		self.assertEqual(read(self.path(".gitignore")), "build/\n.ied/local/\n")

	def test_gitignore_is_extended_not_replaced(self):
		self.files({".gitignore": "*.log"})
		self.nuke("init", "Game")
		self.assertEqual(read(self.path(".gitignore")), "*.log\nbuild/\n.ied/local/\n")

	def test_project_inside_a_workspace(self):
		self.files({"nuke.toml": "[workspace]\ntoolchain = \"1.0.0\"\n"})
		code, out = self.nuke("init", "Engine", "--kind", "module")
		self.assertEqual(code, 0, out)
		self.assertEqual(manifest.load(self.path("Engine", "nuke.toml")).project.kind, "module")
		self.assertTrue(os.path.isfile(self.path("Engine", "src", "Engine.ch")))
		code, out = self.nuke("list")
		self.assertIn("Engine", out)

	def test_project_in_a_listing_workspace_prints_what_to_add(self):
		self.files({"nuke.toml": "[workspace]\nprojects = [\"Game\"]\n", "Game/nuke.toml": "[project]\n"})
		code, out = self.nuke("init", "Tools", "--kind", "custom")
		self.assertEqual(code, 0, out)
		self.assertIn('add "Tools" to [workspace].projects', out)
		self.assertFalse(os.path.exists(self.path("Tools", "src")))

	def test_refuses_to_overwrite_and_duplicate(self):
		self.files({"nuke.toml": "[workspace]\n", "Game/nuke.toml": "[project]\n"})
		self.assertEqual(self.nuke("init", "Game")[0], 1)
		self.assertEqual(self.nuke("init")[0], 1)
		self.files({"Other/x.txt": ""})
		self.assertEqual(self.nuke("init", "--here", cwd=self.path("Other"))[0], 0)
		self.assertEqual(self.nuke("init", "--here", cwd=self.path("Other"))[0], 1)

	def test_bad_name_and_help(self):
		self.assertEqual(self.nuke("init", "bad name")[0], 1)
		code, out = self.nuke("init", "--help")
		self.assertEqual(code, 0)
		self.assertIn("nuke init [NAME]", out)



class IedSolutionTests(WorkspaceTest):
	"""nuke init --ied: the IED solution, a project file per project, and the toolchain's IED plugin pinned."""

	def setUp(self):
		super().setUp()
		install = make_install(os.path.join(self.root, "toolchains", "c4", "1.0.0"))
		write_files(install, {"ied/ied-plugin.json": "{}"})
		self.store = StubStore(os.path.join(self.root, "toolchains"))

	def solution(self):
		return json.loads(read(self.path("Game.ied.sln")))

	def test_a_new_workspace_with_its_solution(self):
		code, out = self.nuke("init", "Game", "--ied")
		self.assertEqual(code, 0, out)
		solution = self.solution()
		self.assertEqual(["build/ied/*.ied.proj"], [p["file"] for p in solution["projects"]])
		self.assertEqual([{"id": "c4", "path": "toolchains/c4/1.0.0/ied"}], solution["localPlugins"])
		self.assertEqual(["nuke", "init", "--ied"], solution["generator"]["run"])
		self.assertEqual(["nuke.toml"], solution["items"])
		project = json.loads(read(self.path("build", "ied", "Game.ied.proj")))
		self.assertEqual({"version": 1, "name": "Game", "root": "../..", "kind": "executable", "buildSystem": {"id": "nuke", "projectName": "Game"}}, project)
		self.assertIn("1 project(s)", out)

	def test_projects_follow_the_workspace_and_hand_edits_are_kept(self):
		self.files({"nuke.toml": "[workspace]\nname = \"Game\"\ntoolchain = \"1.0.0\"\n", "Engine/nuke.toml": "[project]\nkind = \"module\"\n",
			"Tools/nuke.toml": "[project]\nkind = \"custom\"\ndepends = [\"Engine\"]\n"})
		code, out = self.nuke("init", "--ied")
		self.assertEqual(code, 0, out)
		tools = json.loads(read(self.path("build", "ied", "Tools.ied.proj")))
		self.assertEqual(("../../Tools", "other", ["Engine"]), (tools["root"], tools["kind"], tools["references"]))
		self.assertEqual("library", json.loads(read(self.path("build", "ied", "Engine.ied.proj")))["kind"])
		code, out = self.nuke("init", "--ied")
		self.assertIn("0 file(s) written", out, "nothing changed")
		os.remove(self.path("Tools", "nuke.toml"))
		with open(self.path("Game.ied.sln"), "a", encoding="utf-8") as f:
			f.write(" ")
		code, out = self.nuke("init", "--ied")
		self.assertFalse(os.path.exists(self.path("build", "ied", "Tools.ied.proj")), "a project gone: its file too")
		self.assertIn("Game.ied.sln was not written by `nuke init --ied` or was edited since", out)
		self.assertTrue(read(self.path("Game.ied.sln")).endswith(" "), "the hand edit stays")

	def test_a_solution_it_did_not_write_is_left_alone(self):
		self.files({"nuke.toml": "[workspace]\nname = \"Game\"\ntoolchain = \"1.0.0\"\n\n[project]\nname = \"Game\"\n", "Game.ied.sln": "{\"mine\": true}"})
		code, out = self.nuke("init", "--ied")
		self.assertEqual(code, 0, out)
		self.assertEqual("{\"mine\": true}", read(self.path("Game.ied.sln")))
		self.assertTrue(os.path.isfile(self.path("build", "ied", "Game.ied.proj")))

	def test_a_toolchain_without_a_plugin(self):
		make_install(os.path.join(self.root, "toolchains", "c4", "0.9.0"))
		self.files({"nuke.toml": "[workspace]\nname = \"Game\"\ntoolchain = \"0.9.0\"\n\n[project]\n"})
		code, out = self.nuke("init", "--ied")
		self.assertEqual(code, 0, out)
		self.assertNotIn("localPlugins", self.solution())
		self.assertIn("ships no IED plugin", out)


if __name__ == "__main__":
	unittest.main()
