"""Projects in other languages than C4 (IED docs/26 section 6): their packages pinned in [languages], found in the
workspace's .ied/plugins/ or among IED's plugins; the manifest's rules for their kinds; and the cargo and go drivers
building, testing, running and cleaning through a stand-in tool - configurations onto profiles and flags, only the
reported artifacts copied into build/bin, the tool's own folders left alone - then the real cargo and go when they are
installed."""

import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import tomllib
import types
import unittest

from support import WorkspaceTest, read, write_files
from nuke import cli, languages, manifest, toolchains
from nuke.errors import NukeError
from nuke.log import Log
from nuke.packages import checkouts

FAKE_TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fake_tool.py")
CARGO_TOML = "[package]\nname = \"game\"\nversion = \"0.1.0\"\nedition = \"2021\"\n"


def wrapper(folder, tool):
	"""A .cmd that runs the stand-in as `tool`, for NUKE_TOOL_<TOOL>."""
	path = os.path.join(folder, f"fake-{tool}.cmd")
	with open(path, "w", encoding="utf-8") as f:
		f.write(f"@\"{sys.executable}\" -I \"{FAKE_TOOL}\" {tool} %*\r\n")
	return path


class LanguageTest(WorkspaceTest):
	def setUp(self):
		super().setUp()
		self.tool_log = os.path.join(self.tmp, "tool.log")
		os.environ["FAKE_TOOL_LOG"] = self.tool_log
		os.environ["NUKE_TOOL_CARGO"] = wrapper(self.tmp, "cargo")
		os.environ["NUKE_TOOL_GO"] = wrapper(self.tmp, "go")
		for name in ("FAKE_TOOL_FAIL", "FAKE_TOOL_TEST_CODE"):
			os.environ.pop(name, None)
		languages._drivers.clear()

	def tearDown(self):
		for name in ("FAKE_TOOL_LOG", "NUKE_TOOL_CARGO", "NUKE_TOOL_GO", "FAKE_TOOL_FAIL", "FAKE_TOOL_TEST_CODE"):
			os.environ.pop(name, None)
		super().tearDown()

	def calls(self):
		if not os.path.isfile(self.tool_log):
			return []
		with open(self.tool_log, encoding="utf-8") as f:
			return [json.loads(line) for line in f if line.strip()]

	def rust_workspace(self, extra=""):
		self.files({"nuke.toml": "[workspace]\nname = \"Mixed\"\n" + extra, "Game/nuke.toml": "[project]\nlanguage = \"rust\"\n",
			"Game/Cargo.toml": CARGO_TOML, "Game/src/main.rs": "fn main() {}\n"})

	def bin(self, config="Development"):
		return self.path("build", "bin", f"{config}-windows-x64", "Game")


class PackageTests(LanguageTest):
	def test_ieds_own_packages_are_found_beside_nuke(self):
		packages = languages.find_packages(self.root, {})
		self.assertEqual(("rust.nuke", False), (packages["rust"].preset, packages["rust"].pinned))
		self.assertTrue(packages["go"].module.endswith("plugins/go/nuke/gotool.py"), packages["go"].module)

	def test_a_pin_and_the_workspaces_own_plugins_come_first(self):
		description = "[language]\nid = \"{0}\"\nmodule = \"d.py\"\n"
		driver = "def build(ctx):\n\treturn []\ndef program(ctx):\n\treturn None\ndef test(ctx):\n\treturn 0\n"
		self.files({"lang/rust/nuke/nuke-language.toml": description.format("rust"), "lang/rust/nuke/d.py": driver,
			".ied/plugins/markup/nuke/nuke-language.toml": description.format("dialogue"), ".ied/plugins/markup/nuke/d.py": driver})
		packages = languages.find_packages(self.root, {"rust": "lang/rust"})
		self.assertEqual((True, "rust.nuke"), (packages["rust"].pinned, packages["rust"].preset), "the pin, with the conventional preset")
		self.assertTrue(packages["rust"].folder.endswith("ws/lang/rust"))
		self.assertTrue(packages["dialogue"].folder.endswith(".ied/plugins/markup"), "the workspace's own language")
		with self.assertRaises(NukeError) as wrong:
			languages.find_packages(self.root, {"go": "lang/rust"})
		self.assertIn("that package is for rust", str(wrong.exception))
		with self.assertRaises(NukeError):
			languages.find_packages(self.root, {"go": "nowhere"})

	def test_kinds_and_languages(self):
		def project(text):
			return manifest.parse(tomllib.loads(text), self.path("nuke.toml")).project
		game = project("[project]\nname = \"G\"\nlanguage = \"rust\"\n")
		self.assertEqual((False, True), (game.is_c4, game.is_driven))
		self.assertEqual((True, False), (project("[project]\nname = \"C\"\n").is_c4, project("[project]\nname = \"C\"\n").is_driven))
		self.assertEqual({"package": "./cmd/game"}, project("[project]\nname = \"G\"\nlanguage = \"go\"\n[project.driver]\npackage = \"./cmd/game\"\n").driver_options)
		for bad, said in (("kind = \"module\"\nlanguage = \"rust\"", "a module is C4 source"), ("kind = \"library\"", "for another language")):
			with self.assertRaises(NukeError) as refused:
				project(f"[project]\nname = \"B\"\n{bad}\n")
			self.assertIn(said, str(refused.exception))
		with self.assertRaises(NukeError):
			manifest.parse({"project": {"name": "P"}, "languages": {"rust": "x"}}, self.path("Game", "nuke.toml"))


class CargoTests(LanguageTest):
	def test_build_copies_only_the_reported_artifacts(self):
		self.rust_workspace()
		code, out = self.nuke("build", "Game")
		self.assertEqual(code, 0, out)
		self.assertEqual(["game.exe", "game.pdb"], sorted(os.listdir(self.bin())), "executables and their debug files, not Rust's libraries")
		self.assertIn("Compiling game", out, "cargo's stderr is in the log")
		call = self.calls()[0]
		self.assertEqual(("build", "--release", "true"), (call["args"][0], call["args"][-1], call["env"]["CARGO_PROFILE_RELEASE_DEBUG"]),
			"Development: release with debug info, from the environment")
		self.assertTrue(call["cwd"].endswith("ws/Game"))
		self.assertTrue(os.path.isfile(self.path("Game", "target", "release", "libgame.rlib")), "cargo's own folder stays")
		self.assertEqual(CARGO_TOML, read(self.path("Game", "Cargo.toml")), "the manifest is not touched")

	def test_a_second_build_copies_nothing(self):
		self.rust_workspace()
		self.nuke("build", "Game")
		code, out = self.nuke("build", "Game")
		self.assertEqual(code, 0, out)
		self.assertNotIn("copied", out, "the artifacts did not change")

	def test_debug_is_the_dev_profile(self):
		self.rust_workspace()
		code, out = self.nuke("build", "Debug", "Game")
		self.assertEqual(code, 0, out)
		call = self.calls()[0]
		self.assertNotIn("--release", call["args"])
		self.assertIsNone(call["env"]["CARGO_PROFILE_RELEASE_DEBUG"])
		self.assertTrue(os.path.isfile(os.path.join(self.bin("Debug"), "game.exe")))

	def test_a_failed_build_fails_nuke(self):
		self.rust_workspace()
		os.environ["FAKE_TOOL_FAIL"] = "1"
		code, out = self.nuke("build", "Game")
		self.assertNotEqual(code, 0, out)
		self.assertIn("error: it failed", out)

	def test_tests_run_through_cargo(self):
		self.rust_workspace()
		os.environ["FAKE_TOOL_TEST_CODE"] = "101"
		code, out = self.nuke("test", "--filter", "parses", "Game")
		self.assertEqual(code, 1, out)
		self.assertIn("FAILED  Game", out)
		self.assertEqual(["test", "parses"], [self.calls()[-1]["args"][0], self.calls()[-1]["args"][-1]])

	def test_clean_leaves_cargos_folder(self):
		self.rust_workspace()
		self.nuke("build", "Game")
		code, out = self.nuke("clean", "Game")
		self.assertEqual(code, 0, out)
		self.assertFalse(os.path.exists(self.bin()))
		self.assertTrue(os.path.isfile(self.path("Game", "target", "release", "game.exe")))

	def test_list_names_the_language_and_the_program(self):
		self.rust_workspace()
		code, out = self.nuke("list", "--json")
		self.assertEqual(code, 0, out)
		game = [p for p in json.loads(out[out.index("{"):])["projects"] if p["name"] == "Game"][0]
		self.assertEqual("rust", game["language"])
		self.assertTrue(game["output"].endswith("/Game/game.exe"), game["output"])

	def test_a_language_without_a_driver_is_named(self):
		self.files({"nuke.toml": "[workspace]\n", "Tool/nuke.toml": "[project]\nlanguage = \"zig\"\n"})
		code, out = self.nuke("build", "Tool")
		self.assertEqual(code, 1, out)
		self.assertIn("no Nuke driver for zig", out)


class GoTests(LanguageTest):
	def test_go_builds_into_the_bin_folder(self):
		self.files({"nuke.toml": "[workspace]\n", "Server/nuke.toml": "[project]\nlanguage = \"go\"\n[project.driver]\npackage = \"./cmd/server\"\n",
			"Server/go.mod": "module example.com/server\n"})
		code, out = self.nuke("build", "Debug", "Server")
		self.assertEqual(code, 0, out)
		call = self.calls()[0]
		output = call["args"][call["args"].index("-o") + 1].replace("\\", "/")
		self.assertTrue(output.endswith("build/bin/Debug-windows-x64/Server/Server.exe"), output)
		self.assertEqual(("-gcflags=all=-N -l", "./cmd/server", "amd64", "windows"), (call["args"][3], call["args"][-1], call["env"]["GOARCH"], call["env"]["GOOS"]))
		self.assertTrue(os.path.isfile(self.path("build", "bin", "Debug-windows-x64", "Server", "Server.exe")))

	def test_a_go_library_is_compiled_to_check_it(self):
		self.files({"nuke.toml": "[workspace]\n", "Lib/nuke.toml": "[project]\nkind = \"library\"\nlanguage = \"go\"\n"})
		code, out = self.nuke("build", "Lib")
		self.assertEqual(code, 0, out)
		self.assertEqual("./...", self.calls()[0]["args"][-1])


def real(tool):
	return shutil.which(tool) is not None


class DevelopCheckoutTests(LanguageTest):
	"""What the go and cargo drivers write while the workspace develops a package source (IED docs/31 section 7): the
	clone's modules and crates the project gets, followed through one another, and nothing for a project that gets none.
	The real tools build against them in test_packages.NativeToolTests."""

	def setUp(self):
		super().setUp()
		self.gowork = os.environ.pop("GOWORK", None)
		self.clone = self.path("packages", "engine")

	def tearDown(self):
		if self.gowork is not None:
			os.environ["GOWORK"] = self.gowork
		super().tearDown()

	def context(self, name, url="https://github.com/Alan/Engine.git"):
		os.makedirs(self.clone, exist_ok=True)
		subprocess.run(["git", "init", "--quiet", self.clone], check=True)
		project = types.SimpleNamespace(name=name, dir=self.path(name), kind="executable", driver_options={})
		return languages.DriverContext(project, None, "x64", "windows", "", "", Log(io.StringIO()),
			checkouts=(checkouts.Checkout("engine", url, self.clone),), develop_dir=self.path("build", "develop", name))

	def driver(self, language):
		return languages.load_driver(languages.find_packages(self.root, {})[language])

	def test_go_uses_the_clone_modules_the_project_requires(self):
		self.files({
			"Server/go.mod": "module example.com/server\n\ngo 1.21\n\nrequire (\n\texample.com/engine/net v0.1.0 // indirect\n"
				"\tgolang.org/x/text v0.3.0\n)\n",
			"packages/engine/net/go.mod": "module example.com/engine/net\n\ngo 1.22.1\n\nrequire example.com/engine/wire v0.1.0\n",
			"packages/engine/wire/go.mod": "module \"example.com/engine/wire\" // quoted\ngo 1.20rc1\n",
			"packages/engine/tools/go.mod": "module example.com/engine/tools\n\ngo 1.25\n",
		})
		work = read(self.driver("go").develop_env(self.context("Server"))["GOWORK"])
		self.assertIn("\ngo 1.22.1\n", work, "the newest go directive of the modules it uses")
		uses = [json.loads(line.strip()) for line in work.splitlines() if line.startswith("\t")]
		self.assertEqual([os.path.normpath(self.path(*p.split("/"))) for p in ("Server", "packages/engine/net", "packages/engine/wire")], uses,
			"the project, what it requires of the clone and what that requires; not the rest of the clone")

	def test_go_without_a_clone_module_gets_no_go_work(self):
		self.files({"Server/go.mod": "module example.com/server\n\ngo 1.21\n\nrequire example.com/elsewhere v1.0.0\n",
			"packages/engine/net/go.mod": "module example.com/engine/net\n", "build/develop/Server/go.work": "go 1.21\n"})
		self.assertEqual({}, self.driver("go").develop_env(self.context("Server")))
		self.assertFalse(os.path.exists(self.path("build", "develop", "Server", "go.work")), "a stale one goes")

	@unittest.skipUnless(real("go"), "needs go")
	def test_go_keeps_what_the_projects_own_go_work_has(self):
		self.files({"go.work": "go 1.23\n\nuse (\n\t./Server\n\t./Shared\n)\n\nreplace example.com/old v1.0.0 => ./Fork\n",
			"Server/go.mod": "module example.com/server\n\ngo 1.21\n\nrequire example.com/engine/net v0.1.0\n",
			"Shared/go.mod": "module example.com/shared\n\ngo 1.21\n",
			"packages/engine/net/go.mod": "module example.com/engine/net\n\ngo 1.21\n"})
		os.environ.pop("NUKE_TOOL_GO")
		work = read(self.driver("go").develop_env(self.context("Server"))["GOWORK"])
		self.assertIn("\ngo 1.23\n", work)
		self.assertIn(json.dumps(os.path.normpath(self.path("Shared"))), work)
		self.assertIn(f"replace example.com/old v1.0.0 => {json.dumps(os.path.normpath(self.path('Fork')))}", work)

	def test_cargo_patches_the_crates_it_gets_from_the_developed_repository(self):
		self.files({
			"packages/engine/Cargo.toml": "[workspace]\nmembers = [\"mathx\", \"geom\", \"unused\"]\n",
			"packages/engine/mathx/Cargo.toml": "[package]\nname = \"mathx\"\nversion = \"0.1.0\"\n",
			"packages/engine/geom/Cargo.toml": "[package]\nname = \"geom\"\nversion = \"0.1.0\"\n",
			"packages/engine/unused/Cargo.toml": "[package]\nname = \"unused\"\nversion = \"0.1.0\"\n",
			"Game/Cargo.lock": "version = 4\n\n[[package]]\nname = \"geom\"\nversion = \"0.1.0\"\n"
				"source = \"git+https://github.com/Alan/Engine.git?tag=geom%2Fv0.1.0#0123abcd\"\n\n"
				"[[package]]\nname = \"serde\"\nversion = \"1.0.0\"\nsource = \"registry+https://github.com/rust-lang/crates.io-index\"\n",
		})
		ctx = self.context("Game", url="git@github.com:Alan/Engine.git")
		cargo = self.driver("rust")
		metadata = {"workspace_root": self.path("Game"), "packages": [{"dependencies": [
			{"name": "mathx", "source": "git+https://github.com/Alan/Engine?tag=mathx/v0.1.0"},
			{"name": "other", "source": "git+https://github.com/someone/else"},
			{"name": "serde", "source": "registry+https://github.com/rust-lang/crates.io-index"}]}]}
		own_lock = self.path("Game", "Cargo.lock")
		crates = cargo.git_crates(metadata, own_lock)
		self.assertEqual([("https://github.com/alan/engine", "geom"), ("https://github.com/alan/engine", "mathx"),
			("https://github.com/someone/else", "other")], crates, "its manifests' git dependencies and its Cargo.lock's")
		tables = cargo.patch_tables(ctx.checkouts, crates)
		self.assertEqual({"https://github.com/alan/engine": {name: os.path.normpath(os.path.join(self.clone, name)) for name in ("geom", "mathx")}},
			tables, "the source spelled another way is the same repository; crates of other repositories stay theirs")
		config = tomllib.loads(cargo.render_config(tables, cargo.develop_lock(ctx, own_lock)))
		self.assertEqual(os.path.normpath(os.path.join(self.clone, "mathx")), config["patch"]["https://github.com/alan/engine"]["mathx"]["path"])
		lock = config["resolver"]["lockfile-path"]
		self.assertEqual(read(own_lock), read(lock), "builds start from the project's own lock")
		write_files(os.path.dirname(lock), {"Cargo.lock": "# cargo's resolution with the patch\n"})
		cargo.develop_lock(ctx, own_lock)
		self.assertEqual("# cargo's resolution with the patch\n", read(lock))
		os.utime(own_lock, (time.time() + 10, time.time() + 10))
		cargo.develop_lock(ctx, own_lock)
		self.assertEqual(read(own_lock), read(lock), "a newer lock of the project's (cargo update) is taken again")


class RealToolTests(unittest.TestCase):
	"""The drivers with the real cargo and go (skipped without them), each in a workspace of its own and one together
	with the other."""

	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="nuke-lang-")
		languages._drivers.clear()
		for name in ("NUKE_TOOL_CARGO", "NUKE_TOOL_GO"):
			os.environ.pop(name, None)

	def tearDown(self):
		shutil.rmtree(self.root, ignore_errors=True)

	def nuke(self, *argv):
		out = io.StringIO()
		code = cli.main(list(argv), log=Log(out), store=toolchains.ToolchainStore(os.path.join(self.root, "store")), cwd=self.root)
		return code, out.getvalue()

	@unittest.skipUnless(real("cargo") and real("go"), "needs cargo and go")
	def test_a_rust_and_a_go_project_build_test_and_run(self):
		write_files(self.root, {
			"nuke.toml": "[workspace]\nname = \"Polyglot\"\ndefault-project = \"Game\"\n",
			"Game/nuke.toml": "[project]\nlanguage = \"rust\"\ndepends = [\"Server\"]\n",
			"Game/Cargo.toml": "[package]\nname = \"rusty-game\"\nversion = \"0.1.0\"\nedition = \"2021\"\n",
			"Game/src/main.rs": "fn main() { println!(\"hello from rust {}\", std::env::args().nth(1).unwrap_or_default()); }\n"
				"#[cfg(test)]\nmod tests {\n    #[test]\n    fn adds() { assert_eq!(2 + 2, 4); }\n}\n",
			"Server/nuke.toml": "[project]\nlanguage = \"go\"\n",
			"Server/go.mod": "module example.com/server\n\ngo 1.21\n",
			"Server/main.go": "package main\n\nimport \"fmt\"\n\nfunc main() { fmt.Println(\"hello from go\") }\n",
			"Server/main_test.go": "package main\n\nimport \"testing\"\n\nfunc TestAdds(t *testing.T) { if 2+2 != 4 { t.Fatal(\"no\") } }\n",
		})
		code, out = self.nuke("build", "run", "--", "now")
		self.assertEqual(code, 0, out)
		self.assertLess(out.index("Server: building (go)"), out.index("Game: building (rust)"), "the Go project, a dependency, builds first")
		self.assertIn("hello from rust now", subprocess.run([os.path.join(self.root, "build", "bin", "Development-windows-x64", "Game", "rusty-game.exe"), "now"], capture_output=True, text=True).stdout)
		self.assertTrue(os.path.isdir(os.path.join(self.root, "Game", "target")), "cargo's own folder")
		self.assertEqual("hello from go\n", subprocess.run([os.path.join(self.root, "build", "bin", "Development-windows-x64", "Server", "Server.exe")], capture_output=True, text=True).stdout)
		code, out = self.nuke("test")
		self.assertEqual(code, 0, out)
		self.assertIn("2 of 2 test project(s) passed", out)


if __name__ == "__main__":
	unittest.main()
