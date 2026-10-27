import os
import unittest

import support  # noqa: F401  (puts Nuke on sys.path)
from nuke import manifest
from nuke.errors import NukeError


def parse(data, path="C:/ws/nuke.toml"):
	return manifest.parse(data, path)


class ProjectManifestTests(unittest.TestCase):
	def test_defaults_name_from_folder_and_kind_executable(self):
		project = parse({"project": {}}, "C:/ws/Game/nuke.toml").project
		self.assertEqual(project.name, "Game")
		self.assertEqual(project.kind, "executable")
		self.assertEqual(project.sources, ("src",))
		self.assertEqual(project.tests, "auto")

	def test_reads_every_field(self):
		project = parse({"project": {
			"name": "Game", "kind": "module", "depends": ["Engine"], "sources": ["src", "gen"], "includes": ["third"],
			"defines": {"A": "1", "B": 2, "C": True}, "links": ["user32"], "compiler-args": ["-march=x86-64-v3"],
			"tests": False, "working-dir": "assets", "copy": [{"from": "data", "to": "data2"}, {"from": "a/b", "exclude": ["nuke.toml", "*.tmp"]}],
			"config": {"Dist": {"defines": {"SHIP": "1"}, "links": ["x"], "compiler-args": ["-q"]}},
			"steps": [{"phase": "pre-build", "run": ["cook"], "inputs": ["a/**"], "outputs": ["b"], "configs": ["Dist"]}],
		}}).project
		self.assertEqual(project.defines, {"A": "1", "B": "2", "C": "1"})
		self.assertEqual(project.copies[0], manifest.CopySpec("data", "data2"))
		self.assertEqual(project.copies[1], manifest.CopySpec("a/b", "b", ("nuke.toml", "*.tmp")))
		self.assertEqual(project.config_overrides["Dist"].defines, {"SHIP": "1"})
		self.assertEqual(project.steps[0].run, ("cook",))
		self.assertEqual(project.steps[0].name, "cook")
		self.assertTrue(project.steps[0].applies_to("dist"))
		self.assertFalse(project.steps[0].applies_to("Debug"))

	def test_unknown_key_is_an_error_with_a_suggestion(self):
		with self.assertRaisesRegex(NukeError, r"unknown key project\.dependz \(did you mean 'depends'\?\)"):
			parse({"project": {"dependz": []}})

	def test_unknown_root_table_is_an_error(self):
		with self.assertRaisesRegex(NukeError, r"unknown key \(root\)\.projects"):
			parse({"projects": {}})

	def test_wrong_types_are_errors(self):
		for bad in ({"depends": "Engine"}, {"defines": {"A": [1]}}, {"kind": "library"}, {"tests": "yes"}, {"name": "bad name"}):
			with self.subTest(bad=bad), self.assertRaises(NukeError):
				parse({"project": bad})

	def test_step_needs_exactly_one_action_and_a_phase(self):
		for step in ({"phase": "build"}, {"phase": "build", "run": ["a"], "python": "x.py:f"}, {"run": ["a"]}):
			with self.subTest(step=step), self.assertRaises(NukeError):
				parse({"project": {"steps": [step]}})

	def test_output_only_for_custom_projects(self):
		with self.assertRaisesRegex(NukeError, "only for kind"):
			parse({"project": {"output": "x.exe"}})
		self.assertEqual(parse({"project": {"kind": "custom", "output": "x.exe"}}).project.output, "x.exe")

	def test_subsystem_is_console_or_windows_for_c4_executables(self):
		self.assertEqual(parse({"project": {}}).project.subsystem, "console")
		self.assertEqual(parse({"project": {"subsystem": "windows"}}).project.subsystem, "windows")
		with self.assertRaisesRegex(NukeError, "subsystem must be one of console, windows"):
			parse({"project": {"subsystem": "gui"}})
		with self.assertRaisesRegex(NukeError, "subsystem is for C4 executables"):
			parse({"project": {"kind": "module", "subsystem": "windows"}})
		with self.assertRaisesRegex(NukeError, "subsystem is for C4 executables"):
			parse({"project": {"kind": "custom", "subsystem": "windows"}})

	def test_needs_a_table(self):
		with self.assertRaisesRegex(NukeError, r"needs a \[workspace\] or a \[project\]"):
			parse({})


class WorkspaceManifestTests(unittest.TestCase):
	def test_workspace_fields_commands_and_configurations(self):
		parsed = parse({
			"workspace": {"name": "W", "toolchain": "1.2.3", "default-project": "Game", "projects": ["a", "b/*"],
				"exclude": ["x/**"], "default-configuration": "Debug", "scripts": ["ext.py"], "ide": False,
				"steps": [{"phase": "post-build", "run": "echo hi"}]},
			"configurations": {"Profile": {"optimize": 2, "debug-info": True, "aliases": ["prof"], "defines": {"P": "1"}}},
			"commands": {"kit": {"description": "Package", "requires": ["build"], "projects": ["Game"], "steps": [{"run": ["zip"]}]}},
		})
		workspace = parsed.workspace
		self.assertEqual(workspace.projects, ("a", "b/*"))
		self.assertFalse(workspace.ide)
		self.assertEqual(workspace.configurations["Profile"]["optimize"], 2)
		kit = workspace.commands[0]
		self.assertEqual((kit.name, kit.requires, kit.projects), ("kit", ("build",), ("Game",)))
		self.assertEqual(kit.steps[0].phase, "kit")
		self.assertEqual(workspace.steps[0].run, "echo hi")

	def test_projects_default_to_discovery(self):
		self.assertIsNone(parse({"workspace": {}}).workspace.projects)

	def test_command_steps_take_the_command_phase(self):
		with self.assertRaisesRegex(NukeError, "remove 'phase'"):
			parse({"workspace": {}, "commands": {"kit": {"steps": [{"phase": "x", "run": ["a"]}]}}})

	def test_bad_command_and_configuration_names(self):
		with self.assertRaises(NukeError):
			parse({"workspace": {}, "commands": {"Kit": {}}})
		with self.assertRaises(NukeError):
			parse({"workspace": {}, "configurations": {"Pro file": {}}})
		with self.assertRaises(NukeError):
			parse({"workspace": {}, "configurations": {"Profile": {"optimize": 7}}})

	def test_commands_only_in_the_workspace_file(self):
		with self.assertRaisesRegex(NukeError, "belong to the workspace file"):
			parse({"project": {}, "commands": {"kit": {}}})

	def test_toml_syntax_error_names_the_file(self):
		import tempfile
		with tempfile.TemporaryDirectory() as folder:
			path = os.path.join(folder, "nuke.toml")
			with open(path, "w") as f:
				f.write("[project\n")
			with self.assertRaisesRegex(NukeError, "nuke.toml"):
				manifest.load(path)


if __name__ == "__main__":
	unittest.main()
