import os
import unittest

from support import WorkspaceTest, read
from nuke import manifest, tomledit


class TomlEditTests(unittest.TestCase):
	TEXT = ("# the workspace\n[workspace]\nname = \"Game\"   # its name\nprojects = [\n\t\"Game\",  # the game\n\t\"tools/*\",\n]\n"
		"default-project = \"Game\"\n\n[configurations.Profile]\noptimize = 2\n")

	def test_a_key_is_replaced_in_place_and_its_comment_kept(self):
		edited = tomledit.set_key(self.TEXT, "workspace", "name", "Other")
		self.assertIn("name = \"Other\"   # its name\n", edited)
		edited = tomledit.set_key(self.TEXT, "workspace", "projects", ["Game", "Engine"])
		self.assertIn("projects = [\"Game\", \"Engine\"]\ndefault-project", edited)
		self.assertTrue(edited.startswith("# the workspace\n") and edited.endswith("[configurations.Profile]\noptimize = 2\n"))

	def test_a_new_key_goes_after_the_tables_last_and_a_new_table_at_the_end(self):
		edited = tomledit.set_key(self.TEXT, "workspace", "exclude", ["old"])
		self.assertIn("default-project = \"Game\"\nexclude = [\"old\"]\n\n[configurations.Profile]", edited)
		edited = tomledit.set_key("[project]\nname = \"A\"\n", "workspace", "name", "A")
		self.assertEqual("[project]\nname = \"A\"\n\n[workspace]\nname = \"A\"\n", edited)
		self.assertEqual("optimize = 3", tomledit.set_key(self.TEXT, "configurations.Profile", "optimize", "3").splitlines()[-1].replace('"', ""))

	def test_a_key_is_removed_with_all_its_lines(self):
		edited = tomledit.remove_key(self.TEXT, "workspace", "projects")
		self.assertIn("name = \"Game\"   # its name\ndefault-project", edited)
		self.assertEqual(self.TEXT, tomledit.remove_key(self.TEXT, "workspace", "nope"))

	def test_a_table_put_first(self):
		edited = tomledit.add_table("# made by hand\n[project]\nname = \"A\"\n", "workspace", {"name": "A"})
		self.assertEqual("# made by hand\n[workspace]\nname = \"A\"\n\n[project]\nname = \"A\"\n", edited)
		self.assertEqual("C:\\\\x \\\"y\\\"", tomledit.render("C:\\x \"y\"")[1:-1])


class ProjectCommandTests(WorkspaceTest):
	def manifest_of(self, *parts):
		return manifest.load(self.path(*parts, "nuke.toml"))

	def listed(self):
		code, out = self.nuke("list")
		self.assertEqual(0, code, out)
		return out

	def test_a_folder_becomes_a_project(self):
		self.files({"nuke.toml": "[workspace]\ntoolchain = \"1.0.0\"\n", "Game/nuke.toml": "[project]\n", "Engine/src/engine.ch": "", "Tool/main.cf": ""})
		code, out = self.nuke("project", "add", "Engine")
		self.assertEqual(0, code, out)
		engine = self.manifest_of("Engine").project
		self.assertEqual(("Engine", "module", "c4"), (engine.name, engine.kind, engine.language), "C4 without a main.cf: a module")
		self.assertEqual(0, self.nuke("project", "add", "Tool", "--name", "Cli")[0])
		tool = self.manifest_of("Tool").project
		self.assertEqual(("Cli", "executable", (".",)), (tool.name, tool.kind, tool.sources), "a main.cf: an executable; no src: its own folder")
		self.assertIn("Cli", self.listed())
		code, out = self.nuke("project", "add", "Tool")
		self.assertEqual(0, code, "a project already: nothing to do")
		self.assertIn("is project Cli already", out)
		self.assertEqual("[project]\nname = \"Cli\"\nkind = \"executable\"\nsources = [\".\"]\n", read(self.path("Tool", "nuke.toml")), "left as it was")
		self.assertEqual(0, self.nuke("project", "add", "Tool", "--name", "Cli")[0], "its own name")
		self.assertEqual(1, self.nuke("project", "add", "Tool", "--name", "Other")[0], "add does not rename")
		self.assertEqual(1, self.nuke("project", "add", "Missing")[0])
		self.assertEqual(1, self.nuke("project", "add", ".")[0], "the workspace's own folder")

	def test_a_language_without_a_driver_is_custom(self):
		self.files({"nuke.toml": "[workspace]\n", "scripts/tool.py": ""})
		code, out = self.nuke("project", "add", "scripts", "--language", "python")
		self.assertEqual(0, code, out)
		self.assertEqual(("custom", "python"), (self.manifest_of("scripts").project.kind, self.manifest_of("scripts").project.language))
		self.assertEqual(1, self.nuke("project", "add", "scripts", "--kind", "nope")[0])

	def test_a_listing_workspace_lists_the_new_project_and_keeps_its_comments(self):
		self.files({"nuke.toml": "# mine\n[workspace]\nprojects = [\"Game\"]  # by hand\n", "Game/nuke.toml": "[project]\n", "libs/Net/x.cf": ""})
		code, out = self.nuke("project", "add", "libs/Net")
		self.assertEqual(0, code, out)
		self.assertEqual("# mine\n[workspace]\nprojects = [\"Game\", \"libs/Net\"]  # by hand\n", read(self.path("nuke.toml")))
		self.assertIn("Net", self.listed())

	def test_adding_takes_a_folder_out_of_exclude(self):
		self.files({"nuke.toml": "[workspace]\nexclude = [\"Old\"]\n", "Old/nuke.toml": "[project]\nname = \"Old\"\n"})
		self.assertNotIn("Old", self.listed())
		self.assertEqual(0, self.nuke("project", "add", "Old")[0])
		self.assertEqual("[workspace]\n", read(self.path("nuke.toml")), "an exclude left empty goes")
		self.assertIn("Old", self.listed())

	def test_a_one_project_workspace_gets_a_workspace_table(self):
		self.files({"nuke.toml": "[project]\nname = \"Game\"\n", "Lib/a.cf": ""})
		code, out = self.nuke("project", "add", "Lib")
		self.assertEqual(0, code, out)
		parsed = manifest.load(self.path("nuke.toml"))
		self.assertEqual(("Game", "Game"), (parsed.workspace.name, parsed.workspace.default_project))
		self.assertIn("Lib", self.listed())

	def test_removing_keeps_the_folder_and_excludes_it(self):
		self.files({"nuke.toml": "[workspace]\ndefault-project = \"Tool\"\n", "Game/nuke.toml": "[project]\ndepends = [\"Engine\"]\n",
			"Engine/nuke.toml": "[project]\nkind = \"module\"\n", "Tool/nuke.toml": "[project]\n"})
		code, out = self.nuke("project", "remove", "Engine")
		self.assertEqual(1, code)
		self.assertIn("Game depends on Engine", out)
		code, out = self.nuke("project", "remove", "Tool")
		self.assertEqual(0, code, out)
		self.assertIn("default project", out)
		self.assertTrue(os.path.isfile(self.path("Tool", "nuke.toml")), "the folder is kept")
		self.assertEqual("[workspace]\nexclude = [\"Tool\"]\n", read(self.path("nuke.toml")))
		self.assertNotIn("Tool", self.listed())

	def test_removing_from_a_list_and_from_a_glob(self):
		self.files({"nuke.toml": "[workspace]\nprojects = [\"Game\", \"tools/*\"]\n", "Game/nuke.toml": "[project]\n",
			"tools/A/nuke.toml": "[project]\n", "tools/B/nuke.toml": "[project]\n"})
		self.assertEqual(0, self.nuke("project", "remove", "A")[0])
		self.assertIn("exclude = [\"tools/A\"]", read(self.path("nuke.toml")), "a glob still finds it: excluded")
		self.assertEqual(0, self.nuke("project", "remove", "B", "--deleting")[0])
		self.assertNotIn("tools/B\"", read(self.path("nuke.toml")), "the caller deletes it: nothing to exclude")
		self.files({"Game2/nuke.toml": "[project]\n"})
		self.assertEqual(0, self.nuke("project", "add", "Game2")[0])
		self.assertEqual(0, self.nuke("project", "remove", "Game2")[0])
		self.assertIn("projects = [\"Game\", \"tools/*\"]", read(self.path("nuke.toml")), "a listed folder's entry goes")
		out = self.listed()
		self.assertNotIn("Game2", out)
		self.assertNotIn(" A ", out)

	def test_the_workspaces_own_project_stays(self):
		self.files({"nuke.toml": "[workspace]\n[project]\nname = \"Game\"\n"})
		code, out = self.nuke("project", "remove", "Game")
		self.assertEqual(1, code)
		self.assertIn("own project", out)

	def test_moving_a_project(self):
		self.files({"nuke.toml": "[workspace]\nprojects = [\"Game\", \"Engine\"]\nexclude = [\"Engine/old\"]\n", "Game/nuke.toml": "[project]\ndepends = [\"Engine\"]\n",
			"Engine/nuke.toml": "[project]\nkind = \"module\"\nincludes = [\"../third_party/include\"]\n", "Engine/src/e.ch": ""})
		code, out = self.nuke("project", "move", "Engine", "libs/Engine")
		self.assertEqual(0, code, out)
		self.assertTrue(os.path.isfile(self.path("libs", "Engine", "src", "e.ch")) and not os.path.exists(self.path("Engine")))
		self.assertIn("projects = [\"Game\", \"libs/Engine\"]\nexclude = [\"libs/Engine/old\"]", read(self.path("nuke.toml")))
		self.assertIn("../third_party/include", out, "a path leaving the project is pointed out")
		self.assertIn("Engine", self.listed())
		self.assertEqual(1, self.nuke("project", "move", "Game", "libs/Engine")[0], "the target is there")
		self.assertEqual(1, self.nuke("project", "move", "Game", "Game/inner")[0], "into itself")
		self.assertEqual(1, self.nuke("project", "move", "Game", "../outside")[0])

	def test_a_folder_moved_already_has_only_its_entries_fixed(self):
		self.files({"nuke.toml": "[workspace]\nprojects = [\"Game\", \"Engine\"]\n", "Game/nuke.toml": "[project]\n", "Engine/nuke.toml": "[project]\nkind = \"module\"\n"})
		os.makedirs(self.path("libs"))
		os.rename(self.path("Engine"), self.path("libs", "Engine"))
		code, out = self.nuke("project", "move", "Engine", "libs/Engine")
		self.assertEqual(0, code, out)
		self.assertIn("projects = [\"Game\", \"libs/Engine\"]", read(self.path("nuke.toml")))
		self.assertIn("Engine", self.listed())

	def test_a_discovering_workspace_moved_already(self):
		self.files({"nuke.toml": "[workspace]\n", "Engine/nuke.toml": "[project]\nkind = \"module\"\n"})
		os.makedirs(self.path("libs"))
		os.rename(self.path("Engine"), self.path("libs", "Engine"))
		code, out = self.nuke("project", "move", "Engine", "libs/Engine")
		self.assertEqual(0, code, out)
		self.assertEqual("[workspace]\n", read(self.path("nuke.toml")))

	def test_a_discovering_workspace_needs_no_entry(self):
		self.files({"nuke.toml": "[workspace]\n", "Game/nuke.toml": "[project]\n"})
		self.assertEqual(0, self.nuke("project", "move", "Game", "games/Game")[0])
		self.assertEqual("[workspace]\n", read(self.path("nuke.toml")))
		self.assertIn("Game", self.listed())

	def test_help_and_bad_verbs(self):
		code, out = self.nuke("project", "--help")
		self.assertEqual(0, code)
		self.assertIn("nuke project add FOLDER", out)
		self.assertEqual(1, self.nuke("project", "rename", "A")[0])


if __name__ == "__main__":
	unittest.main()
