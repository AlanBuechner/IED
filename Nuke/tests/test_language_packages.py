"""What a language package can add to Nuke (IED docs/26 section 6): its workspace settings in [languages.<id>], a
driver split into a Python package, dependencies its driver adds to a project, the library projects it brings (pulled
into a workspace that depends on one), ${<language>.<name>} variables, a driver context that sees the project's
dependencies, the settings, the -j count and the project's variables, and `nuke test` building first when it asks;
then the IED solution's display folders from the projects' groups."""

import json
import os
import tomllib
import unittest

from support import WorkspaceTest, read
from nuke import cli, ied_files, languages, manifest, workspace as workspaces
from nuke.errors import NukeError

DESCRIPTION = """[language]
id = "toy"
module = "driver/__init__.py"
libraries = ["libraries/*"]
build-before-test = true
"""

# The toy driver records what it saw in <workspace>/calls.jsonl; a project's [project.driver] reflection = true needs
# the package's "Meta" library, as the C++ driver's reflection needs its Reflection library.
DRIVER = """import json, os
from .record import record

def implicit_depends(spec):
	return ["Meta"] if spec.driver_options.get("reflection") else []

def variables(ctx):
	return {"home": ctx.settings.get("home", "nowhere"), "folder": ctx.workspace_dir}

def build(ctx):
	record(ctx, "build", depends=[d.project.name for d in ctx.depends], settings=ctx.settings, jobs=ctx.jobs,
		expanded=ctx.expand("${toy.home}/${project.name}"))
	os.makedirs(ctx.bin_dir, exist_ok=True)
	with open(os.path.join(ctx.bin_dir, ctx.project.name + ".exe"), "w") as f:
		f.write("built")
	return []

def program(ctx):
	return ctx.project.name + ".exe" if ctx.project.kind == "executable" else None

def has_tests(ctx):
	return ctx.project.tests is True

def test(ctx):
	record(ctx, "test", built=os.path.isfile(os.path.join(ctx.bin_dir, ctx.project.name + ".exe")))
	return 0
"""

RECORD = """import json, os

def record(ctx, what, **fields):
	with open(os.path.join(ctx.workspace_dir, "calls.jsonl"), "a") as f:
		f.write(json.dumps(dict(fields, call=what, project=ctx.project.name)) + "\\n")
"""


class PackageExtensionTest(WorkspaceTest):
	def setUp(self):
		super().setUp()
		languages._drivers.clear()

	def toy_package(self, folder="../pkg"):
		self.files({f"{folder}/nuke/nuke-language.toml": DESCRIPTION, f"{folder}/nuke/driver/__init__.py": DRIVER,
			f"{folder}/nuke/driver/record.py": RECORD,
			f"{folder}/libraries/Meta/nuke.toml": "[project]\nname = \"Meta\"\nkind = \"library\"\nlanguage = \"toy\"\ngroup = \"Toy Libraries\"\n",
			f"{folder}/libraries/Base/nuke.toml": "[project]\nname = \"Base\"\nkind = \"library\"\nlanguage = \"toy\"\n",
			f"{folder}/libraries/Unused/nuke.toml": "[project]\nname = \"Unused\"\nkind = \"library\"\nlanguage = \"toy\"\n"})

	def workspace(self, game="", extra=""):
		self.toy_package()
		self.files({"nuke.toml": "[workspace]\nname = \"W\"\n[languages.toy]\npackage = \"../pkg\"\nhome = \"C:/toys\"\n" + extra,
			"Game/nuke.toml": "[project]\nname = \"Game\"\nlanguage = \"toy\"\n" + game})

	def calls(self):
		path = self.path("calls.jsonl")
		if not os.path.isfile(path):
			return []
		with open(path, encoding="utf-8") as f:
			return [json.loads(line) for line in f if line.strip()]

	def load(self):
		return workspaces.load_workspace(self.root)


class SettingsTests(PackageExtensionTest):
	def parse(self, text):
		return manifest.parse(tomllib.loads(text), self.path("nuke.toml")).workspace

	def test_a_language_is_a_folder_or_a_table_of_settings(self):
		spec = self.parse("[workspace]\n[languages]\nrust = \"tools/rust\"\n[languages.cpp]\nllvm = \"vendor/llvm\"\n"
			"[languages.toy]\npackage = \"pkg\"\nhome = \"x\"\n")
		self.assertEqual({"rust": "tools/rust", "toy": "pkg"}, spec.languages, "folders and `package` keys are pins")
		self.assertEqual({"cpp": {"llvm": "vendor/llvm"}, "toy": {"home": "x"}}, spec.language_settings)

	def test_wrong_shapes_are_refused(self):
		for bad in ("[languages]\ncpp = 3\n", "[languages.cpp]\npackage = 3\n"):
			with self.assertRaises(NukeError):
				self.parse("[workspace]\n" + bad)
		with self.assertRaises(NukeError) as project_only:
			manifest.parse(tomllib.loads("[project]\n[languages.cpp]\nllvm = \"x\"\n"), self.path("P", "nuke.toml"))
		self.assertIn("belong to the workspace file", str(project_only.exception))

	def test_a_description_is_checked(self):
		self.files({"bad/nuke/nuke-language.toml": "[language]\nid = \"b\"\nmodule = \"d.py\"\nlibraries = \"x\"\n", "bad/nuke/d.py": ""})
		with self.assertRaises(NukeError) as wrong:
			languages.read_package(self.path("bad"))
		self.assertIn("libraries must be a list", str(wrong.exception))
		self.files({"bad/nuke/nuke-language.toml": "[language]\nid = \"b\"\nmodule = \"d.py\"\nbuild-before-test = \"yes\"\n"})
		with self.assertRaises(NukeError):
			languages.read_package(self.path("bad"))


class LibraryTests(PackageExtensionTest):
	def test_a_library_depended_on_joins_the_workspace(self):
		self.workspace(game="depends = [\"Base\"]\n")
		loaded = self.load()
		self.assertEqual(["Game", "Base"], list(loaded.projects), "only what is depended on, never Unused")
		self.assertTrue(loaded.projects["Base"].dir.endswith("/pkg/libraries/Base"))

	def test_the_driver_adds_its_dependencies(self):
		self.workspace(game="depends = [\"Base\"]\n[project.driver]\nreflection = true\n")
		loaded = self.load()
		self.assertEqual(("Base", "Meta"), loaded.projects["Game"].depends, "implicit_depends after the manifest's own")
		self.assertIn("Meta", loaded.projects, "and the library it names comes with it")

	def test_the_workspaces_own_project_wins_a_name(self):
		self.workspace(game="depends = [\"Base\"]\n", extra="projects = [\"Game\", \"Base\"]\n")
		self.files({"Base/nuke.toml": "[project]\nname = \"Base\"\nkind = \"library\"\nlanguage = \"toy\"\n"})
		self.assertTrue(self.load().projects["Base"].dir.endswith("ws/Base"))

	def test_an_unknown_dependency_is_still_an_error(self):
		self.workspace(game="depends = [\"Nowhere\"]\n")
		code, out = self.nuke("build", "Game")
		self.assertEqual(1, code)
		self.assertIn("no project named 'Nowhere'", out)

	def test_libraries_build_first_into_the_workspace(self):
		self.workspace(game="depends = [\"Base\"]\n")
		code, out = self.nuke("build", "Game", "-j", "2")
		self.assertEqual(0, code, out)
		builds = [c for c in self.calls() if c["call"] == "build"]
		self.assertEqual(["Base", "Game"], [c["project"] for c in builds])
		game = builds[1]
		self.assertEqual((["Base"], {"home": "C:/toys"}, 2), (game["depends"], game["settings"], game["jobs"]))
		self.assertEqual("C:/toys/Game", game["expanded"], "${toy.home} from the driver, ${project.name} from Nuke")
		self.assertTrue(os.path.isfile(self.path("build", "bin", "Development-windows-x64", "Base", "Base.exe")))


class VariableTests(PackageExtensionTest):
	def test_steps_read_the_drivers_variables(self):
		self.workspace(game="[[project.steps]]\nphase = \"post-build\"\npython = \"step.py:show\"\n")
		self.files({"Game/step.py": "def show(ctx):\n\tctx.log.line(ctx.expand('${toy.home}|${toy.folder}'))\n"})
		code, out = self.nuke("build", "Game")
		self.assertEqual(0, code, out)
		self.assertIn("C:/toys|" + self.root.replace("\\", "/"), out.replace("\\", "/"))

	def test_an_unknown_one_is_named(self):
		self.workspace(game="[[project.steps]]\nphase = \"post-build\"\nrun = [\"echo\", \"${toy.nothing}\"]\n")
		code, out = self.nuke("build", "Game")
		self.assertEqual(1, code)
		self.assertIn("unknown variable ${toy.nothing}", out)


class JobsTests(unittest.TestCase):
	def test_jobs(self):
		os.environ.pop(cli.JOBS_ENV, None)
		self.assertIsNone(cli.choose_jobs(None))
		self.assertEqual(3, cli.choose_jobs("3"))
		os.environ[cli.JOBS_ENV] = "5"
		try:
			self.assertEqual(5, cli.choose_jobs(None))
			self.assertEqual(2, cli.choose_jobs("2"), "-j wins over the environment")
		finally:
			os.environ.pop(cli.JOBS_ENV, None)
		for bad in ("0", "many"):
			with self.assertRaises(NukeError):
				cli.choose_jobs(bad)


class TestBeforeBuildTests(PackageExtensionTest):
	def test_nuke_test_builds_first(self):
		self.workspace(game="depends = [\"Base\"]\ntests = true\n")
		code, out = self.nuke("test", "Game")
		self.assertEqual(0, code, out)
		self.assertEqual([("build", "Base"), ("build", "Game"), ("test", "Game")], [(c["call"], c["project"]) for c in self.calls()])
		self.assertTrue(self.calls()[-1]["built"])


class SolutionGroupTests(PackageExtensionTest):
	def test_groups_nest_and_sort(self):
		class Spec:
			def __init__(self, name, group):
				self.name, self.group = name, group
		folders = ied_files.groups([Spec("b", "C4"), Spec("a", "C4"), Spec("t", "Engine/Tools"), Spec("e", "Engine"), Spec("x", "")])
		self.assertEqual([{"name": "C4", "projects": ["a", "b"]},
			{"name": "Engine", "projects": ["e"], "groups": [{"name": "Tools", "projects": ["t"]}]}], folders)
		self.assertEqual([], ied_files.groups([Spec("x", "")]))

	def test_the_solution_has_folders_and_leaves_out_libraries_outside_it(self):
		self.toy_package()
		self.files({"nuke.toml": "[workspace]\nname = \"W\"\n[languages]\ntoy = \"../pkg\"\n",
			"Game/nuke.toml": "[project]\nname = \"Game\"\nlanguage = \"toy\"\ngroup = \"Games\"\ndepends = [\"Meta\"]\n"})
		code, out = self.nuke("init", "--ied")
		self.assertEqual(0, code, out)
		solution = json.loads(read(self.path("W.iedsln")))
		self.assertEqual([{"name": "Games", "projects": ["Game"]}], solution["groups"], "Meta is not in the solution")
		self.assertFalse(os.path.exists(self.path("build", "ied", "Meta.iedproj")))
		self.assertTrue(os.path.exists(self.path("build", "ied", "Game.iedproj")))


if __name__ == "__main__":
	unittest.main()
