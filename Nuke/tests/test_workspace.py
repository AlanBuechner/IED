import unittest

from support import WorkspaceTest
from nuke import graph, workspace as workspaces
from nuke.errors import NukeError


class WorkspaceLoadingTests(WorkspaceTest):
	def load(self):
		return workspaces.load_workspace(workspaces.find_root(self.root))

	def test_discovers_projects_and_skips_build_hidden_and_nested_workspaces(self):
		self.files({
			"nuke.toml": "[workspace]\nname = \"W\"\n",
			"Game/nuke.toml": "[project]\ndepends = [\"Engine\"]\n",
			"libs/Engine/nuke.toml": "[project]\nkind = \"module\"\n",
			"build/Stale/nuke.toml": "[project]\n",
			".hidden/X/nuke.toml": "[project]\n",
			"third_party/Y/nuke.toml": "[project]\n",
			"Other/nuke.toml": "[workspace]\n",
			"Other/Inner/nuke.toml": "[project]\n",
			"notes/nuke.toml.txt": "",
		})
		self.assertEqual(sorted(self.load().projects), ["Engine", "Game"])

	def test_exclude_globs(self):
		self.files({"nuke.toml": "[workspace]\nexclude = [\"samples/*\"]\n", "Game/nuke.toml": "[project]\n",
			"samples/Demo/nuke.toml": "[project]\n"})
		self.assertEqual(list(self.load().projects), ["Game"])

	def test_explicit_project_list_and_globs(self):
		self.files({"nuke.toml": "[workspace]\nprojects = [\"Game\", \"tools/*\"]\n", "Game/nuke.toml": "[project]\n",
			"tools/A/nuke.toml": "[project]\nkind = \"custom\"\n", "tools/B/nuke.toml": "[project]\nkind = \"custom\"\n",
			"Unlisted/nuke.toml": "[project]\n"})
		self.assertEqual(sorted(self.load().projects), ["A", "B", "Game"])

	def test_listed_entry_without_manifest_is_an_error(self):
		self.files({"nuke.toml": "[workspace]\nprojects = [\"Missing\"]\n"})
		with self.assertRaisesRegex(NukeError, "matches no folder"):
			self.load()

	def test_combined_workspace_and_project_file(self):
		self.files({"nuke.toml": "[workspace]\nname = \"Solo\"\n[project]\nname = \"Solo\"\n"})
		ws = self.load()
		self.assertEqual(list(ws.projects), ["Solo"])
		self.assertEqual(ws.default_project().name, "Solo")

	def test_project_only_file_is_an_implicit_workspace(self):
		self.files({"nuke.toml": "[project]\nname = \"Tool\"\n", "src/main.cf": ""})
		ws = self.load()
		self.assertEqual((ws.name, ws.default_project().name), ("Tool", "Tool"))

	def test_find_root_walks_up(self):
		self.files({"nuke.toml": "[workspace]\n", "Game/nuke.toml": "[project]\n", "Game/src/deep/x.cf": ""})
		self.assertEqual(workspaces.find_root(self.path("Game", "src", "deep")).replace("\\", "/"), self.root.replace("\\", "/"))

	def test_duplicate_project_names(self):
		self.files({"nuke.toml": "[workspace]\n", "a/nuke.toml": "[project]\nname = \"X\"\n", "b/nuke.toml": "[project]\nname = \"x\"\n"})
		with self.assertRaisesRegex(NukeError, "two projects are named"):
			self.load()

	def test_default_project(self):
		self.files({"nuke.toml": "[workspace]\n", "A/nuke.toml": "[project]\n", "B/nuke.toml": "[project]\nkind = \"module\"\n"})
		self.assertEqual(self.load().default_project().name, "A")
		self.files({"C/nuke.toml": "[project]\n"})
		self.assertIsNone(self.load().default_project())

	def test_project_lookup_suggests(self):
		self.files({"nuke.toml": "[workspace]\n", "Game/nuke.toml": "[project]\n"})
		with self.assertRaisesRegex(NukeError, "did you mean 'Game'"):
			self.load().project("Gmae")

	def test_extension_scripts_register_commands_and_steps(self):
		self.files({
			"nuke.toml": "[workspace]\nscripts = [\"ext.py\"]\n",
			"Game/nuke.toml": "[project]\nscripts = [\"game_ext.py\"]\n",
			"ext.py": "def register(nuke):\n\t@nuke.command('kit', requires=['build'])\n\tdef kit(ctx):\n\t\t'Package it'\n",
			"Game/game_ext.py": "def register(nuke):\n\tnuke.step('post-build', lambda ctx: 0, name='stamp')\n",
		})
		ws = self.load()
		self.assertEqual(ws.commands["kit"].description, "Package it")
		self.assertEqual([s.name for s in ws.projects["Game"].steps], ["stamp"])

	def test_extension_errors_name_the_script(self):
		self.files({"nuke.toml": "[workspace]\nscripts = [\"ext.py\"]\n", "ext.py": "x = 1\n"})
		with self.assertRaisesRegex(NukeError, "must define register"):
			self.load()
		self.files({"ext.py": "def register(nuke):\n\traise ValueError('boom')\n"})
		with self.assertRaisesRegex(NukeError, "boom"):
			self.load()


class GraphTests(WorkspaceTest):
	def load(self, files):
		self.files(files)
		return workspaces.load_workspace(self.root)

	def test_build_order_puts_dependencies_first_once(self):
		ws = self.load({"nuke.toml": "[workspace]\n", "Game/nuke.toml": "[project]\ndepends = [\"Engine\", \"Core\"]\n",
			"Engine/nuke.toml": "[project]\nkind = \"module\"\ndepends = [\"Core\"]\n", "Core/nuke.toml": "[project]\nkind = \"module\"\n"})
		graph.validate(ws)
		self.assertEqual([p.name for p in graph.build_order(ws, [ws.project("Game")])], ["Core", "Engine", "Game"])
		self.assertEqual([p.name for p in graph.c4_modules(ws, ws.project("Game"))], ["Game", "Engine", "Core"])

	def test_cycles_are_reported_with_the_chain(self):
		ws = self.load({"nuke.toml": "[workspace]\n", "A/nuke.toml": "[project]\nkind = \"module\"\ndepends = [\"B\"]\n",
			"B/nuke.toml": "[project]\nkind = \"module\"\ndepends = [\"A\"]\n"})
		with self.assertRaisesRegex(NukeError, "dependency cycle: (A -> B -> A|B -> A -> B)"):
			graph.validate(ws)

	def test_a_c4_program_cannot_depend_on_an_executable(self):
		ws = self.load({"nuke.toml": "[workspace]\n", "A/nuke.toml": "[project]\ndepends = [\"B\"]\n", "B/nuke.toml": "[project]\n"})
		with self.assertRaisesRegex(NukeError, "must be kind = \"module\""):
			graph.validate(ws)

	def test_unknown_dependency(self):
		ws = self.load({"nuke.toml": "[workspace]\n", "A/nuke.toml": "[project]\ndepends = [\"Nope\"]\n"})
		with self.assertRaisesRegex(NukeError, "no project named 'Nope'"):
			graph.validate(ws)


if __name__ == "__main__":
	unittest.main()
