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

	def test_compiler_args_and_links_expand_variables(self):
		self.files({"Game/nuke.toml": "[project]\ncompiler-args = [\"-l\", \"${int}/app.res\"]\nlinks = [\"${project.dir}/lib/x.lib\"]\n"})
		code, out = self.nuke("build")
		self.assertEqual(code, 0, out)
		args = self.compiles()[0]
		links = [args[i + 1] for i, a in enumerate(args) if a == "-l"]
		self.assertIn(self.int("Game", "app.res").replace("\\", "/"), links)
		self.assertIn(self.path("Game", "lib", "x.lib").replace("\\", "/"), links)

	def test_windows_subsystem_reaches_the_program_not_its_tests(self):
		self.files({"Game/nuke.toml": "[project]\nsubsystem = \"windows\"\n", "Game/src/tests/t.cf": "\n"})
		code, out = self.nuke("build")
		self.assertEqual(code, 0, out)
		args = self.compiles()[0]
		self.assertEqual(args[args.index("--subsystem") + 1], "windows")
		from nuke import workspace as workspaces, configs
		from nuke.session import Session
		from nuke.c4 import compile as c4compile
		from nuke.log import Log
		ws = workspaces.load_workspace(self.root)
		session = Session(ws, configs.BUILT_IN[1], "x64", "windows", Log(), self.store)
		self.assertNotIn("--subsystem", c4compile.plan(session, ws.project("Game"), test=True).argv)

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

	def test_a_linux_build_targets_linux_and_takes_its_system_tables(self):
		# C4 D-86: --system linux cross-compiles; [project.system.<name>] adds what one system needs, and a copy may
		# name the systems it is for.
		self.files({"Game/nuke.toml": "[project]\ndepends = [\"Engine\", \"Tools\"]\n"
			"[project.system.windows]\nlinks = [\"ws2_32\"]\ncompiler-args = [\"--script-host\"]\n"
			"[project.system.linux]\ndefines = { POSIX_BUILD = \"1\" }\nlinks = [\"m\"]\n"
			"[[project.copy]]\nfrom = \"data\"\nsystems = [\"windows\"]\n"})
		code, out = self.nuke("build", "--system", "linux")
		self.assertEqual(code, 0, out)
		args = self.compiles()[0]
		self.assertEqual(args[args.index("--target") + 1], "linux-x64")
		links = [args[i + 1] for i in range(len(args) - 1) if args[i] == "-l"]
		self.assertIn("m", links)
		self.assertNotIn("ws2_32", links)
		self.assertNotIn("--script-host", args)
		self.assertEqual(args[args.index("-o") + 1].replace("\\", "/").split("/")[-1], "Game")   # no .exe on Linux
		bin_linux = self.path("build", "bin", "Development-linux-x64", "Game")
		self.assertFalse(os.path.exists(os.path.join(bin_linux, "data")), "a Windows-only copy is not made for Linux")
		description = tomllib.loads(read(self.path("build", "int", "Development-linux-x64", "Game", "projects.toml")))
		self.assertEqual(description["project"][0]["defines"]["POSIX_BUILD"], "1")
		code, out = self.nuke("build")
		self.assertEqual(code, 0, out)
		windows = self.compiles()[1]
		self.assertNotIn("--target", windows)
		self.assertIn("--script-host", windows)
		self.assertIn("ws2_32", [windows[i + 1] for i in range(len(windows) - 1) if windows[i] == "-l"])
		self.assertTrue(os.path.exists(self.bin("Game", "data")), "the copy is made for Windows")

	def test_sources_includes_and_copies_expand_variables(self):
		"""A manifest string expands its ${variables}: a module's include folder in the toolchain (C4Api's header for C4's
		interop), its sources in the workspace, and a copy from anywhere (C4Api.dll beside a C4 tool)."""
		self.files({"Engine/nuke.toml": "[project]\nkind = \"module\"\nsources = [\"${workspace.dir}/Engine/src\"]\n"
			"includes = [\"${toolchain.dir}/C4Api\", \"${project.dir}/third\"]\n"
			"[[project.copy]]\nfrom = \"${project.dir}/shaders\"\nto = \"${project.name}-shaders\"\n"})
		code, out = self.nuke("build")
		self.assertEqual(code, 0, out)
		engine = tomllib.loads(read(self.int("Game", "projects.toml")))["project"][1]
		self.assertEqual(engine["sources"], [self.path("Engine", "src").replace("\\", "/")])
		toolchain = self.store.resolve("1.0.0", self.root).dir
		self.assertEqual(engine["includes"], [toolchain.replace("\\", "/") + "/C4Api", self.path("Engine", "third").replace("\\", "/")])
		self.assertTrue(os.path.isfile(self.bin("Game", "Engine-shaders", "a.hlsl")), "the copy, its source and place expanded")

	def test_build_description(self):
		self.nuke("build")
		description = tomllib.loads(read(self.int("Game", "projects.toml")))
		self.assertEqual(description["root"], "Game")
		game, engine = description["project"]
		self.assertEqual(game["depends"], ["Engine"])
		game_dir = '"' + self.path("Game").replace("\\", "/") + '"'
		self.assertEqual(game["defines"], {"GAME": "1", "DEVELOPMENT": "1", "PROJECT_NAME": "Game", "PROJECT_DIR": game_dir})
		self.assertEqual(engine["defines"]["PROJECT_DIR"], '"' + self.path("Engine").replace("\\", "/") + '"', "each project's own folder")
		self.assertEqual(engine["sources"], [self.path("Engine", "src").replace("\\", "/")])
		self.assertEqual(engine["includes"], [self.path("Engine", "third").replace("\\", "/")])
		self.assertNotIn("reflect", game, "a project in reflection's scope says nothing")

	def test_a_project_out_of_reflections_scope_says_so_in_the_description(self):
		self.files({"Engine/nuke.toml": "[project]\nkind = \"module\"\nreflect = \"off\"\n"})
		code, out = self.nuke("build")
		self.assertEqual(code, 0, out)
		game, engine = tomllib.loads(read(self.int("Game", "projects.toml")))["project"]
		self.assertEqual(engine["reflect"], "off")
		self.assertNotIn("reflect", game)

	def test_copies_and_ide_options(self):
		self.nuke("build")
		self.assertTrue(os.path.isfile(self.bin("Game", "data", "level.txt")))
		self.assertTrue(os.path.isfile(self.bin("Game", "shaders", "a.hlsl")))
		options = json.loads(read(self.path(".ied", "local", "lsp-options", "Game.json")))
		self.assertEqual(options["toolchain"], "1.0.0")
		self.assertEqual(options["target"], ["-march=x86-64-v3"])
		self.assertIn(self.path("Engine", "third").replace("\\", "/"), options["includePaths"])
		self.assertFalse(options["scriptHost"], "built without --script-host")

	def test_a_script_host_gives_the_editor_its_plugin_folders(self):
		"""[project.script-host]: each plugin folder its globs match is a script scope in the program's IDE options, compiled
		alone against the program's interface beside its executable."""
		self.files({"Game/nuke.toml": "[project]\ndepends = [\"Engine\", \"Tools\"]\ncompiler-args = [\"--script-host\"]\n"
			"[project.script-host]\nfolders = [\"plugins/*/src\"]\nallow = [\"Sdk/\", \"Web/\"]\n",
			"Game/plugins/tv/src/Tv.cf": "\n", "Game/plugins/manga/src/Manga.cf": "\n", "Game/plugins/notes.txt": "\n"})
		code, out = self.nuke("build")
		self.assertEqual(code, 0, out)
		options = json.loads(read(self.path(".ied", "local", "lsp-options", "Game.json")))
		self.assertTrue(options["scriptHost"], "the editor lowers every class's vtables, as the --script-host build does")
		scopes = options["scripts"]
		self.assertEqual([s["folders"] for s in scopes], [[self.path("Game", "plugins", "manga", "src").replace("\\", "/")],
			[self.path("Game", "plugins", "tv", "src").replace("\\", "/")]], "one scope a plugin, in name order")
		self.assertEqual(scopes[0]["interface"].replace("\\", "/"), self.bin("Game", "Game.c4i").replace("\\", "/"))
		self.assertEqual(scopes[0]["allow"], ["Sdk/", "Web/"])

	def test_a_script_host_needs_its_folders(self):
		self.files({"Game/nuke.toml": "[project]\ndepends = [\"Engine\", \"Tools\"]\n[project.script-host]\nallow = [\"Sdk/\"]\n"})
		code, out = self.nuke("build")
		self.assertNotEqual(code, 0, out)
		self.assertIn("script-host.folders", out)

	def test_the_options_of_a_project_gone_are_removed(self):
		self.nuke("build")
		stale = self.path(".ied", "local", "lsp-options", "OldTool.json")
		with open(stale, "w", encoding="utf-8") as f:
			f.write("{}")
		code, out = self.nuke("build")
		self.assertEqual(code, 0, out)
		self.assertFalse(os.path.exists(stale), "a project no longer in the workspace has no options left")
		self.assertTrue(os.path.isfile(self.path(".ied", "local", "lsp-options", "Game.json")), "a live one keeps its own")

	def test_a_compile_that_fails_still_writes_the_ide_options(self):
		os.environ["FAKE_C4_FAIL"] = "1"
		try:
			code, out = self.nuke("build")
		finally:
			del os.environ["FAKE_C4_FAIL"]
		self.assertNotEqual(code, 0, out)
		options = json.loads(read(self.path(".ied", "local", "lsp-options", "Game.json")))
		self.assertEqual(self.int("Game", "tests", "projects.toml").replace("\\", "/"), options["projects"].replace("\\", "/"))
		self.assertTrue(os.path.isfile(self.int("Game", "tests", "projects.toml")), "the description the options name is there")

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

	def test_a_new_cbind_rebuilds(self):
		"""C4 binds a program's foreign headers again only when it runs: a toolchain whose CBind changed recompiles."""
		self.nuke("build")
		cbind = os.path.join(self.store.resolve("1.0.0", self.root).dir, "CBind", "CBind.exe")
		later = time.time() + 5
		os.utime(cbind, (later, later))
		code, out = self.nuke("build")
		self.assertNotIn("Game: up to date", out)
		self.assertEqual(len(self.compiles()), 2)

	def test_an_edit_made_while_compiling_rebuilds(self):
		os.environ["FAKE_C4_EDIT"] = self.path("Game", "src", "main.cf")
		try:
			self.nuke("build")
		finally:
			del os.environ["FAKE_C4_EDIT"]
		self.assertLessEqual(os.path.getmtime(self.path("Game", "src", "main.cf")), os.path.getmtime(self.bin("Game", "Game.exe")))
		code, out = self.nuke("build")
		self.assertNotIn("Game: up to date", out)
		self.assertEqual(len(self.compiles()), 2)
		code, out = self.nuke("build")
		self.assertIn("Game: up to date", out)

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

	def test_a_test_harness_gets_the_copies_a_program_would(self):
		self.files({"nuke.toml": "[workspace]\ntoolchain = \"1.0.0\"\n",
			"Engine/nuke.toml": "[project]\nkind = \"module\"\n[[project.copy]]\nfrom = \"shaders\"\n",
			"Engine/shaders/a.hlsl": "", "Engine/src/tests/engine_tests.cf": ""})
		try:
			self.nuke("test", "Engine")
		except OSError:
			pass        # the stub compiler's harness is no program; the copies come before it runs
		copied = self.path("build", "bin", "Development-windows-x64", "Engine", "shaders", "a.hlsl")
		self.assertTrue(os.path.isfile(copied), "the module's copies beside Engine-tests.exe")

	def dev_dependencies(self):
		"""Engine's tests run on a backend (Dx12, with a DLL it copies) that the engine itself never names; Game holds Engine."""
		self.files({"nuke.toml": "[workspace]\ntoolchain = \"1.0.0\"\ndefault-project = \"Game\"\n",
			"Rhi/nuke.toml": "[project]\nkind = \"module\"\n", "Rhi/src/rhi.ch": "",
			"Dx12/nuke.toml": "[project]\nkind = \"module\"\ndepends = [\"Rhi\"]\n[[project.copy]]\nfrom = \"dxc.dll\"\n",
			"Dx12/dxc.dll": "", "Dx12/src/dx12.ch": "",
			"Engine/nuke.toml": "[project]\nkind = \"module\"\n[dependencies]\nRhi = { source = \"Rhi\" }\n"
				"[dev-dependencies]\nDx12 = { source = \"Dx12\" }\n",
			"Engine/src/engine.ch": "", "Engine/src/tests/engine_tests.cf": "",
			"Game/nuke.toml": "[project]\n[dependencies]\nEngine = { source = \"Engine\" }\n",
			"Game/src/main.cf": "", "Game/src/tests/game_tests.cf": ""})
		from nuke import workspace as workspaces, configs
		from nuke.session import Session
		from nuke.log import Log
		ws = workspaces.load_workspace(self.root)
		return ws, Session(ws, configs.BUILT_IN[1], "x64", "windows", Log(), self.store)

	def test_dev_dependencies_join_the_test_harnesses_and_no_program(self):
		from nuke.c4 import compile as c4compile
		ws, session = self.dev_dependencies()
		self.assertEqual(("Dx12",), ws.project("Engine").dev_depends)
		self.assertNotIn("Dx12", ws.project("Engine").depends)

		def engine_in(job):
			return next(m for m in job.modules if m.name == "Engine")

		program = c4compile.plan(session, ws.project("Game"))
		self.assertNotIn("Dx12", [m.name for m in program.modules], "a program never holds the tests' packages")
		self.assertEqual(("Rhi",), engine_in(program).depends, "so the engine's own code cannot name them")
		for root in ("Engine", "Game"):
			with self.subTest(harness=root):
				harness = c4compile.plan(session, ws.project(root), test=True)
				self.assertIn("Dx12", [m.name for m in harness.modules])
				self.assertEqual(("Rhi", "Dx12"), engine_in(harness).depends,
					"the compiler compiles the tests of every project a harness holds: the engine's use its backend in Game's too")

	def test_a_test_harness_gets_its_dev_dependencies_copies(self):
		self.dev_dependencies()
		try:
			self.nuke("test", "Engine")
		except OSError:
			pass        # the stub compiler's harness is no program; the copies come before it runs
		self.assertTrue(os.path.isfile(self.path("build", "bin", "Development-windows-x64", "Engine", "dxc.dll")))
		self.nuke("build", "Game")
		self.assertFalse(os.path.isfile(self.path("build", "bin", "Development-windows-x64", "Game", "dxc.dll")))

	def test_a_programs_editor_options_hold_its_tests_packages(self):
		"""The language server analyzes a program as its -test build sees it, the engine's tests/ included (C4 docs/tools/03):
		the options Game's build writes name its harness's description, where the engine's tests reach their backend - in the
		program's own description that include is an error the build never has, and it turns off every body's checks (C4 BUG-475)."""
		self.dev_dependencies()
		code, out = self.nuke("build", "Game")
		self.assertEqual(code, 0, out)
		options = json.loads(read(self.path(".ied", "local", "lsp-options", "Game.json")))
		harness = self.path("build", "int", "Development-windows-x64", "Game", "tests", "projects.toml")
		self.assertEqual(harness.replace("\\", "/"), options["projects"].replace("\\", "/"))
		description = tomllib.loads(read(options["projects"]))
		engine = next(p for p in description["project"] if p["name"] == "Engine")
		self.assertEqual(["Rhi", "Dx12"], engine["depends"])
		self.assertIn(self.path("Dx12", "src").replace("\\", "/"), options["includePaths"])
		self.assertFalse(os.path.isfile(self.path("build", "bin", "Development-windows-x64", "Game", "dxc.dll")), "the build itself is the program's")

	def test_a_dev_dependency_that_holds_the_project_is_a_cycle(self):
		self.files({"nuke.toml": "[workspace]\ntoolchain = \"1.0.0\"\n",
			"Engine/nuke.toml": "[project]\nkind = \"module\"\n[dev-dependencies]\nKit = { source = \"Kit\" }\n",
			"Kit/nuke.toml": "[project]\nkind = \"module\"\n[dependencies]\nEngine = { source = \"Engine\" }\n"})
		code, out = self.nuke("list")
		self.assertEqual(code, 1)
		self.assertIn("dependency cycle in a test harness, through [dev-dependencies]", out)

	def test_naming_a_project_without_tests_is_an_error(self):
		self.files({"nuke.toml": "[workspace]\ntoolchain = \"1.0.0\"\n", "Game/nuke.toml": "[project]\n", "Game/src/main.cf": ""})
		code, out = self.nuke("test", "Game")
		self.assertEqual(code, 1)
		self.assertIn("Game has no tests", out)
		code, out = self.nuke("test")
		self.assertEqual(code, 0, out)
		self.assertIn("no project has tests", out)


class NativeLibraryTests(WorkspaceTest):
	"""A C4 program depending on a project in another language takes what that language's driver says the library gives
	(native_library(ctx)): its header folders join the module's includes, its files the program's links."""

	DRIVER = ("def build(ctx):\n\treturn []\ndef program(ctx):\n\treturn None\ndef test(ctx):\n\treturn 0\n"
		"def native_library(ctx):\n\tif ctx.project.kind != \"library\":\n\t\treturn None\n"
		"\treturn {\"includes\": [ctx.project.dir + \"/include\"], \"links\": [ctx.bin_dir + \"/\" + ctx.project.name + \".lib\", \"ws2_32.lib\"]}\n")

	def setUp(self):
		super().setUp()
		from nuke import languages
		languages._drivers.clear()   # each test writes its own driver at the same path
		self.files({
			"nuke.toml": "[workspace]\ntoolchain = \"1.0.0\"\ndefault-project = \"Game\"\n",
			".ied/plugins/native/nuke/nuke-language.toml": "[language]\nid = \"native\"\nmodule = \"d.py\"\n",
			".ied/plugins/native/nuke/d.py": self.DRIVER,
			"Game/nuke.toml": "[project]\ndepends = [\"Engine\", \"Net\", \"Tool\"]\nlinks = [\"user32\"]\n",
			"Game/src/main.cf": "int32 main() { return 0; }\n",
			"Engine/nuke.toml": "[project]\nkind = \"module\"\ndepends = [\"Audio\"]\n",
			"Engine/src/engine.ch": "",
			"Net/nuke.toml": "[project]\nkind = \"library\"\nlanguage = \"native\"\n",
			"Net/include/Net.h": "",
			"Audio/nuke.toml": "[project]\nkind = \"library\"\nlanguage = \"native\"\n",
			"Audio/include/Audio.h": "",
			"Tool/nuke.toml": "[project]\nkind = \"executable\"\nlanguage = \"native\"\n",
		})

	def plan(self):
		from nuke import workspace as workspaces, configs
		from nuke.session import Session
		from nuke.c4 import compile as c4compile
		from nuke.log import Log
		ws = workspaces.load_workspace(self.root)
		return c4compile.plan(Session(ws, configs.BUILT_IN[1], "x64", "windows", Log(), self.store), ws.project("Game"))

	def norm(self, *parts):
		return self.path(*parts).replace("\\", "/")

	def test_each_module_gets_its_libraries_headers_and_the_program_their_files(self):
		job = self.plan()
		modules = {module.name: module for module in job.modules}
		self.assertEqual((self.norm("Net", "include"),), modules["Game"].includes, "Game's own library; Tool, a program, gives nothing")
		self.assertEqual((self.norm("Audio", "include"),), modules["Engine"].includes, "the module that depends on it has its headers")
		links = [job.argv[i + 1] for i, a in enumerate(job.argv) if a == "-l"]
		net = self.norm("build", "bin", "Development-windows-x64", "Net", "Net.lib")
		audio = self.norm("build", "bin", "Development-windows-x64", "Audio", "Audio.lib")
		self.assertEqual(["user32", net, "ws2_32.lib", audio], links, "the manifest's links, then every library's, each once")
		self.assertIn(net, job.tracked, "a library built again compiles the program again")
		self.assertIn(self.norm("Net", "include", "Net.h"), job.tracked)

	def test_a_driver_answer_of_another_shape_is_named(self):
		from nuke.errors import NukeError
		self.files({".ied/plugins/native/nuke/d.py": self.DRIVER.replace('{"includes"', '{"headers"')})
		with self.assertRaises(NukeError) as wrong:
			self.plan()
		self.assertIn("native_library gave", str(wrong.exception))


if __name__ == "__main__":
	unittest.main()
