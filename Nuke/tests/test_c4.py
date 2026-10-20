"""The C4 project kinds against the stub compiler: command lines, build descriptions, incremental builds."""

import json
import os
import time
import tomllib
import unittest

from support import read, WorkspaceTest


class C4BuildTests(WorkspaceTest):
	def setUp(self):
		super().setUp()
		self.files({
			"nuke.toml": "[workspace]\ntoolchain = \"1.0.0\"\ndefault-project = \"Game\"\n"
				"[configurations.Development]\ndefines = { WIDE = \"1\" }\n",
			"Game/nuke.toml": "[project]\ndepends = [\"Engine\", \"Tools\"]\nlinks = [\"user32\"]\ncompiler-args = [\"-march=x86-64-v3\"]\n"
				"defines = { GAME = \"1\" }\n[[project.copy]]\nfrom = \"data\"\n"
				"[project.config.Dist]\ndefines = { SHIP = \"1\" }\nlinks = [\"ship\"]\n",
			"Game/src/main.cf": "int32 main() { return 0; }\n",
			"Game/data/level.txt": "level",
			"Engine/nuke.toml": "[project]\nkind = \"module\"\nlinks = [\"winmm\", \"user32\"]\nincludes = [\"third\"]\n"
				"[[project.copy]]\nfrom = \"shaders\"\nto = \"shaders\"\n",
			"Engine/src/engine.ch": "",
			"Engine/third/lib.h": "",
			"Engine/shaders/a.hlsl": "",
			"Tools/nuke.toml": "[project]\nkind = \"custom\"\n",
		})

	def bin(self, *parts):
		return self.path("build", "bin", "Development-windows-x64", *parts)

	def int(self, *parts):
		return self.path("build", "int", "Development-windows-x64", *parts)

	def test_command_line(self):
		code, out = self.nuke("build")
		self.assertEqual(code, 0, out)
		args = self.compiles()[0]
		self.assertEqual(args[0:2], ["--projects", self.int("Game", "projects.toml").replace("\\", "/")])
		self.assertIn("-O3", args)
		self.assertIn("-g", args)
		self.assertIn("--incremental", args)
		self.assertNotIn("-test", args)
		self.assertEqual(args[args.index("-D") + 1], "WIDE=1")
		links = [args[i + 1] for i, a in enumerate(args) if a == "-l"]
		self.assertEqual(links, ["user32", "winmm"])
		self.assertIn("-march=x86-64-v3", args)
		self.assertEqual(args[-2:], ["-o", self.bin("Game", "Game.exe").replace("\\", "/")])

	def test_configuration_flags_and_overrides(self):
		self.nuke("build", "dist")
		args = self.compiles()[0]
		self.assertIn("-O3", args)
		self.assertNotIn("-g", args)
		self.assertNotIn("--incremental", args)  # Dist optimizes the whole program as one
		self.assertIn("ship", args)
		description = tomllib.loads(read(self.path("build", "int", "Dist-windows-x64", "Game", "projects.toml")))
		self.assertEqual(description["project"][0]["defines"]["SHIP"], "1")
		self.nuke("build", "debug")
		self.assertIn("-O0", self.compiles()[1])
		self.assertIn("--incremental", self.compiles()[1])

	def test_incremental_can_be_turned_off(self):
		self.files({"nuke.toml": "[workspace]\ntoolchain = \"1.0.0\"\ndefault-project = \"Game\"\n"
			"[configurations.Development]\nincremental = false\n"})
		code, out = self.nuke("build")
		self.assertEqual(code, 0, out)
		self.assertNotIn("--incremental", self.compiles()[0])

	def test_an_older_compiler_is_not_given_incremental(self):
		os.environ["FAKE_C4_WITHOUT"] = "--incremental"
		code, out = self.nuke("build")
		self.assertEqual(code, 0, out)
		self.assertNotIn("--incremental", self.compiles()[0])

	def test_build_description(self):
		self.nuke("build")
		description = tomllib.loads(read(self.int("Game", "projects.toml")))
		self.assertEqual(description["root"], "Game")
		game, engine = description["project"]
		self.assertEqual(game["depends"], ["Engine"])
		self.assertEqual(game["defines"], {"GAME": "1", "DEVELOPMENT": "1", "PROJECT_NAME": "Game"})
		self.assertEqual(engine["sources"], [self.path("Engine", "src").replace("\\", "/")])
		self.assertEqual(engine["includes"], [self.path("Engine", "third").replace("\\", "/")])

	def test_copies_and_ide_options(self):
		self.nuke("build")
		self.assertTrue(os.path.isfile(self.bin("Game", "data", "level.txt")))
		self.assertTrue(os.path.isfile(self.bin("Game", "shaders", "a.hlsl")))
		options = json.loads(read(self.path(".ied", "local", "lsp-options", "Game.json")))
		self.assertEqual(options["toolchain"], "1.0.0")
		self.assertEqual(options["target"], ["-march=x86-64-v3"])
		self.assertIn(self.path("Engine", "third").replace("\\", "/"), options["includePaths"])

	def test_up_to_date_until_something_changes(self):
		self.nuke("build")
		code, out = self.nuke("build")
		self.assertIn("Game: up to date", out)
		self.assertEqual(len(self.compiles()), 1)
		source = self.path("Engine", "src", "engine.ch")
		later = time.time() + 5
		os.utime(source, (later, later))
		self.nuke("build")
		self.assertEqual(len(self.compiles()), 2)

	def test_a_new_file_rebuilds(self):
		self.nuke("build")
		self.files({"Game/src/extra.cf": ""})
		later = os.path.getmtime(self.bin("Game", "Game.exe")) - 100
		os.utime(self.path("Game", "src", "extra.cf"), (later, later))
		self.nuke("build")
		self.assertEqual(len(self.compiles()), 2)

	def test_a_changed_manifest_rebuilds(self):
		self.nuke("build")
		self.files({"Game/nuke.toml": "[project]\ndepends = [\"Engine\"]\ndefines = { GAME = \"2\" }\n"})
		self.nuke("build")
		self.assertEqual(len(self.compiles()), 2)

	def test_failed_compile_is_never_up_to_date(self):
		self.nuke("build")
		later = time.time() + 5
		os.utime(self.path("Game", "src", "main.cf"), (later, later))
		os.environ["FAKE_C4_FAIL"] = "3"
		code, out = self.nuke("build", "run")
		self.assertEqual(code, 3)
		self.assertIn("compiling Game failed (exit code 3)", out)
		self.assertIn("error: forced failure", out)
		del os.environ["FAKE_C4_FAIL"]
		self.nuke("build")
		self.assertEqual(len(self.compiles()), 3)

	def test_modules_build_nothing_on_their_own(self):
		code, out = self.nuke("build", "Engine")
		self.assertEqual(code, 0, out)
		self.assertEqual(self.compiles(), [])

	def test_clean_removes_this_configuration_only(self):
		self.nuke("build")
		self.nuke("build", "debug")
		self.nuke("clean")
		self.assertFalse(os.path.exists(self.bin("Game")))
		self.assertTrue(os.path.exists(self.path("build", "bin", "Debug-windows-x64", "Game")))

	def test_rebuild(self):
		self.nuke("build")
		self.nuke("rebuild")
		self.assertEqual(len(self.compiles()), 2)

	def test_missing_toolchain(self):
		self.files({"nuke.toml": "[workspace]\ntoolchain = \"5.0.0\"\ndefault-project = \"Game\"\n"})
		code, out = self.nuke("build")
		self.assertEqual(code, 1)
		self.assertIn("toolchain '5.0.0' is not installed", out)

	def test_unpinned_toolchain_warns_and_uses_latest(self):
		self.files({"nuke.toml": "[workspace]\ndefault-project = \"Game\"\n"})
		code, out = self.nuke("build")
		self.assertEqual(code, 0, out)
		self.assertIn("pins no toolchain", out)


class C4TestHarnessTests(WorkspaceTest):
	def test_harness_is_compiled_separately_with_test_flags(self):
		self.files({"nuke.toml": "[workspace]\ntoolchain = \"1.0.0\"\n",
			"Engine/nuke.toml": "[project]\nkind = \"module\"\n", "Engine/src/tests/math_tests.cf": "",
			"Game/nuke.toml": "[project]\ndepends = [\"Engine\"]\n", "Game/src/main.cf": ""})
		from nuke import workspace as workspaces, toolchains, configs
		from nuke.session import Session
		from nuke.c4 import compile as c4compile
		from nuke.log import Log
		ws = workspaces.load_workspace(self.root)
		session = Session(ws, configs.BUILT_IN[1], "x64", "windows", Log(), self.store)
		self.assertTrue(session.has_tests(ws.project("Engine")))
		self.assertFalse(session.has_tests(ws.project("Game")))
		job = c4compile.plan(session, ws.project("Engine"), test=True)
		self.assertIn("-test", job.argv)
		self.assertTrue(job.output.endswith("Engine/Engine-tests.exe"))
		self.assertTrue(job.int_dir.endswith("Engine/tests"))
		self.assertEqual(job.argv[job.argv.index("-test-json") + 1], job.test_results)

	def test_naming_a_project_without_tests_is_an_error(self):
		self.files({"nuke.toml": "[workspace]\ntoolchain = \"1.0.0\"\n", "Game/nuke.toml": "[project]\n", "Game/src/main.cf": ""})
		code, out = self.nuke("test", "Game")
		self.assertEqual(code, 1)
		self.assertIn("Game has no tests", out)
		code, out = self.nuke("test")
		self.assertEqual(code, 0, out)
		self.assertIn("no project has tests", out)


if __name__ == "__main__":
	unittest.main()
