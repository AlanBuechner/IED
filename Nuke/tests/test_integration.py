"""End to end with a real C4 toolchain from the store (dev, else the latest installed); skipped when there is none."""

import os
import shutil
import tempfile
import unittest

import support  # noqa: F401
from nuke import cli, toolchains
from nuke.log import Log
from support import read, write_files

import io

STORE = toolchains.ToolchainStore(toolchains.default_store_root())
REPOSITORY = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SAMPLE = os.path.join(REPOSITORY, "plugins", "c4", "samples", "c4-outline")
CLANG = os.path.join(REPOSITORY, "vendor", "llvm", "bin", "clang.exe")


def available_toolchain():
	installed = STORE.installed()
	dev = [t for t in installed if t.version == toolchains.DEV]
	return (dev or installed[-1:] or [None])[0]


TOOLCHAIN = available_toolchain()


@unittest.skipIf(TOOLCHAIN is None, f"no C4 toolchain installed in {STORE.kind_dir()}")
class RealToolchainTests(unittest.TestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="nuke-it-")

	def tearDown(self):
		shutil.rmtree(self.root, ignore_errors=True)

	def nuke(self, *argv, cwd=None):
		out = io.StringIO()
		code = cli.main(list(argv), log=Log(out), store=STORE, cwd=cwd or self.root)
		return code, out.getvalue()

	def test_init_build_run(self):
		folder = os.path.join(self.root, "Hello")
		os.makedirs(folder)
		code, out = self.nuke("init", "--toolchain", TOOLCHAIN.version, cwd=folder)
		self.assertEqual(code, 0, out)
		self.assertFalse(os.path.exists(os.path.join(folder, ".ied", "skills")), "the skills come with the IED plugins")
		self.assertIn("build/", read(os.path.join(folder, ".gitignore")))
		code, out = self.nuke("build", "run", cwd=folder)
		self.assertEqual(code, 0, out)
		self.assertTrue(os.path.isfile(os.path.join(folder, "build", "int", "Development-windows-x64", "Hello", "projects.toml")))
		code, out = self.nuke("build", cwd=folder)
		self.assertIn("Hello: up to date", out)

	def test_modules_defines_links_exit_code_and_failures(self):
		write_files(self.root, {
			"nuke.toml": f'[workspace]\ntoolchain = "{TOOLCHAIN.version}"\ndefault-project = "Game"\n',
			"Engine/nuke.toml": '[project]\nkind = "module"\nlinks = ["winmm"]\ndefines = { ENGINE_FAST = "1" }\n',
			"Engine/src/engine.ch": "#ifdef ENGINE_FAST\nint32 EngineValue() { return 7; }\n#else\nint32 EngineValue() { return 1; }\n#endif\n",
			"Game/nuke.toml": '[project]\ndepends = ["Engine"]\n',
			"Game/src/main.cf": 'include "bedrock.ch"\ninclude "engine.ch"\n\n#ifdef ENGINE_FAST\nint32 LeakedDefine() { return 100; }\n#else\n'
				"int32 LeakedDefine() { return 0; }\n#endif\n\nint32 main() {\n\treturn EngineValue() + LeakedDefine();\n}\n",
		})
		code, out = self.nuke("run")
		self.assertEqual(code, 7, out)
		write_files(self.root, {"Game/src/main.cf": "int32 main() {\n\treturn missing_function();\n}\n"})
		code, out = self.nuke("build", "run")
		self.assertNotEqual(code, 0, out)
		self.assertIn("compiling Game failed", out)
		self.assertNotIn("nuke: running", out)

	def test_tests_pass_fail_and_filter(self):
		write_files(self.root, {
			"nuke.toml": f'[workspace]\ntoolchain = "{TOOLCHAIN.version}"\n',
			"Math/nuke.toml": '[project]\nkind = "module"\n',
			"Math/src/math.ch": "int32 Add(int32 a, int32 b) { return a + b; }\n",
			"Math/src/tests/math_tests.cf": 'include "bedrock.ch"\ninclude "math.ch"\n\n[test("math")]\nclass MathTests {\n'
				'\t[test("add")]\n\tpublic static void TestAdd() { assert_true(Add(2, 2) == 4, "2 + 2"); }\n'
				'\t[test("broken")]\n\tpublic static void TestBroken() { assert_true(Add(2, 2) == 5, "deliberately wrong"); }\n}\n',
		})
		code, out = self.nuke("test")
		self.assertEqual(code, 1, out)
		self.assertIn("FAILED  Math", out)
		self.assertTrue(os.path.isfile(os.path.join(self.root, "build", "int", "Development-windows-x64", "Math", "tests", "results.json")))
		code, out = self.nuke("test", "--filter", "math.add")
		self.assertEqual(code, 0, out)
		self.assertIn("1 of 1 test project(s) passed", out)

	@unittest.skipUnless(TOOLCHAIN and os.path.isfile(os.path.join(TOOLCHAIN.dir, "C4Api", "C4Api.dll")) and os.path.isfile(CLANG),
		"needs a toolchain that ships C4Api and the checkout's LLVM release")
	def test_a_tool_links_the_toolchains_c4_api(self):
		"""The c4 plugin's sample (plugins/c4/samples/c4-outline), a workspace of its own as a third party's tool is: a C++
		program that depends on C4Api gets the toolchain's header, import library and DLL, and outlines C4 sources."""
		folder = os.path.join(self.root, "c4-outline")
		shutil.copytree(SAMPLE, folder, ignore=shutil.ignore_patterns("build"))
		manifest = os.path.join(folder, "nuke.toml")
		write_files(folder, {"nuke.toml": read(manifest).replace('toolchain = "dev"', f'toolchain = "{TOOLCHAIN.version}"')})
		code, out = self.nuke("test", cwd=folder)
		self.assertEqual(code, 0, out)
		self.assertIn("c4-outline: 3 fixture(s), 0 problem(s)", out)
		self.assertTrue(os.path.isfile(os.path.join(folder, "build", "bin", "Development-windows-x64", "c4-outline", "C4Api.dll")), "the DLL beside the tool")


if __name__ == "__main__":
	unittest.main()
