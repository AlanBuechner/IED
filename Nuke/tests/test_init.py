import os
import unittest

from support import WorkspaceTest, read
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


if __name__ == "__main__":
	unittest.main()
