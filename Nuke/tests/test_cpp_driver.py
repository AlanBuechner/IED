"""The C/C++ plugin's driver (plugins/cpp/nuke; IED docs/26 section 6) with the real clang of the repository's LLVM
release (vendor/llvm) and the MSVC runtime and Windows SDK setup.bat installs (vendor/msvc), else this machine's Visual
Studio: programs linking static libraries and their links, a DLL,
compiles only when they have to, a failed compile leaving nothing behind, stale objects removed, object name clashes,
a link past the Windows command line, the compile database, copies and the assets link, test programs, and the checks
of the driver's settings, and where the LLVM release and the MSVC kit come from. Skipped without the LLVM release or an
MSVC runtime and Windows SDK."""

import importlib
import json
import os
import shutil
import subprocess
import sys
import time
import unittest

from support import WorkspaceTest, read
from nuke import languages

REPOSITORY = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LLVM = os.path.join(REPOSITORY, "vendor", "llvm").replace("\\", "/")
KIT = os.path.join(REPOSITORY, "vendor", "msvc").replace("\\", "/")
CONFIG = "Development-windows-x64"


def driver():
	return languages.load_driver(languages.read_package(os.path.join(REPOSITORY, "plugins", "cpp")))


def module(name):
	"""One of the driver package's modules."""
	return importlib.import_module(f"{driver().__name__}.{name}")


def has_toolchain():
	if not os.path.isfile(os.path.join(LLVM, "bin", "clang.exe")):
		return False
	try:
		module("msvc").find(KIT)
	except ValueError:
		return False
	return True


class LlvmFolderTests(unittest.TestCase):
	"""Where the LLVM release comes from: the workspace's setting, else clang on PATH, else the checkout's (setup.bat)."""

	def setUp(self):
		self.toolchain = module("toolchain")
		self.settings = module("settings")
		self.root = os.path.join(REPOSITORY, "build", "nuke-tests", "llvm-folder")
		shutil.rmtree(self.root, ignore_errors=True)
		os.makedirs(os.path.join(self.root, "llvm", "bin"))
		open(os.path.join(self.root, "llvm", "bin", "clang.exe"), "w").close()
		self.path = os.environ.get("PATH", "")
		os.environ["PATH"] = self.root   # no clang on it

	def tearDown(self):
		os.environ["PATH"] = self.path
		shutil.rmtree(self.root, ignore_errors=True)

	def folder(self, table, checkout):
		return self.toolchain.llvm_folder(self.settings.read_settings(table), self.root, checkout)

	def test_the_workspace_then_the_checkout(self):
		checkout = os.path.join(self.root, "llvm")
		self.assertEqual(self.folder({"llvm": "named"}, checkout), os.path.join(self.root, "named").replace("\\", "/"), "the workspace's own")
		self.assertEqual(self.folder({}, checkout), checkout.replace("\\", "/"), "no setting, no clang on PATH: the checkout's")
		with self.assertRaises(ValueError) as none:
			self.folder({}, os.path.join(self.root, "missing"))
		self.assertIn("setup.bat", str(none.exception))
		self.assertEqual(self.toolchain.checkout_llvm("C:/IED").replace("\\", "/"), "C:/IED/vendor/llvm", "Nuke's home's")


class KitTests(unittest.TestCase):
	"""The MSVC runtime and Windows SDK: setup.bat's kit (Visual Studio's layout) when the checkout has one."""

	def setUp(self):
		self.msvc = module("msvc")
		self.root = os.path.join(REPOSITORY, "build", "nuke-tests", "msvc-kit").replace("\\", "/")
		shutil.rmtree(self.root, ignore_errors=True)
		for folder in ("VC/Tools/MSVC/14.51.36231/include", "VC/Tools/MSVC/14.51.36231/lib/x64", "VC/Tools/MSVC/14.44.35207/include",
				"Windows Kits/10/Include/10.0.26100.0/um", "Windows Kits/10/Include/10.0.26100.0/ucrt",
				"Windows Kits/10/Lib/10.0.26100.0/um/x64", "Windows Kits/10/Lib/10.0.26100.0/ucrt/x64"):
			os.makedirs(os.path.join(self.root, *folder.split("/")))

	def tearDown(self):
		shutil.rmtree(self.root, ignore_errors=True)

	def test_the_kit_with_its_newest_toolset(self):
		sdk = self.msvc.find(self.root)
		toolset = f"{self.root}/VC/Tools/MSVC/14.51.36231"
		kits = f"{self.root}/Windows Kits/10"
		self.assertEqual(sdk.sysroot, self.root, "clang's driver is given the kit")
		self.assertEqual(sdk.ms_version, "19.51.36231", "cl's version, from the toolset's name: no cl.exe to run")
		self.assertEqual(sdk.include_dirs, (f"{toolset}/include", f"{kits}/Include/10.0.26100.0/ucrt", f"{kits}/Include/10.0.26100.0/um"),
			"the newest toolset's headers, then the SDK's that exist")
		self.assertEqual(sdk.library_dirs, (f"{kits}/Lib/10.0.26100.0/um/x64", f"{kits}/Lib/10.0.26100.0/ucrt/x64", f"{toolset}/lib/x64"))

	def test_a_kit_without_an_sdk_says_to_run_setup(self):
		shutil.rmtree(os.path.join(self.root, "Windows Kits"))
		with self.assertRaises(ValueError) as missing:
			self.msvc.find(self.root)
		self.assertIn("setup.bat", str(missing.exception))

	def test_the_checkout_kit_is_in_nukes_home(self):
		self.assertEqual(module("toolchain").checkout_kit("C:/IED").replace("\\", "/"), "C:/IED/vendor/msvc")


@unittest.skipUnless(sys.platform == "win32" and has_toolchain(), "needs vendor/llvm and vendor/msvc (setup.bat) or Visual Studio's C++ tools")
class CppTest(WorkspaceTest):
	def setUp(self):
		super().setUp()
		languages._drivers.clear()

	def workspace(self, projects, settings=""):
		files = {"nuke.toml": f"[workspace]\nname = \"Cpp\"\n[languages.cpp]\nllvm = \"{LLVM}\"\n{settings}"}
		for name, (manifest, sources) in projects.items():
			files[f"{name}/nuke.toml"] = f"[project]\nname = \"{name}\"\nlanguage = \"cpp\"\n{manifest}"
			files.update({f"{name}/{path}": text for path, text in sources.items()})
		self.files(files)

	def bin(self, project, name=None):
		return self.path("build", "bin", CONFIG, project, name or "")

	def int(self, project, name=None):
		return self.path("build", "int", CONFIG, project, name or "")

	def build(self, *words):
		code, out = self.nuke("build", *words, "-j", "2")
		self.assertEqual(0, code, out)
		return out

	def library_and_program(self):
		self.workspace({
			"Lib": ("kind = \"library\"\nlinks = [\"shlwapi.lib\"]\n", {
				"src/Lib/Lib.h": "#pragma once\nint Answer();\n",
				"src/Lib/Lib.cpp": "#include \"Lib/Lib.h\"\n#include <windows.h>\n#include <shlwapi.h>\n"
					"int Answer() { return PathIsRelativeA(\"a\\\\b\") ? 42 : 0; }\n"}),
			"App": ("depends = [\"Lib\"]\n", {"src/Main.cpp": "#include \"Lib/Lib.h\"\nint main() { return Answer(); }\n",
				"src/Other.c": "int other(void) { return PROJECT_VALUE; }\n"}),
		})
		with open(self.path("App", "nuke.toml"), "a", encoding="utf-8") as f:
			f.write("defines = { PROJECT_VALUE = \"3\" }\n")


class ProgramTests(CppTest):
	def test_a_program_links_its_libraries_and_their_links(self):
		self.library_and_program()
		out = self.build("App")
		self.assertIn("App: 2 compiled, 0 failed, 0 up to date", out)
		self.assertEqual(42, subprocess.run([self.bin("App", "App.exe")]).returncode, "Lib's code, with Lib's shlwapi.lib")
		self.assertTrue(os.path.isfile(self.bin("Lib", "Lib.lib")))
		self.assertTrue(os.path.isfile(self.bin("App", "App.pdb")), "Development keeps debug information")
		archive = subprocess.run([os.path.join(LLVM, "bin", "llvm-ar.exe"), "t", self.bin("Lib", "Lib.lib")], capture_output=True, text=True)
		self.assertEqual(["Lib.obj"], [os.path.basename(line) for line in archive.stdout.split()], "a static library holds its own objects")
		args = read(self.int("App", "linker.args"))
		self.assertIn("shlwapi.lib", args)
		self.assertIn("Lib.lib", args)

	def test_a_second_build_does_nothing_and_a_header_edit_compiles_its_includers(self):
		self.library_and_program()
		self.build("App")
		out = self.build("App")
		self.assertNotIn("compiled", out)
		self.assertNotIn("linked", out)
		time.sleep(0.05)
		with open(self.path("Lib", "src", "Lib", "Lib.h"), "a", encoding="utf-8") as f:
			f.write("int Unused();\n")
		out = self.build("App")
		self.assertIn("Lib: 1 compiled, 0 failed, 0 up to date", out)
		self.assertIn("App: 1 compiled, 0 failed, 1 up to date", out, "Main.cpp includes Lib.h, Other.c does not")
		self.assertIn("App: linked App.exe", out)

	def test_other_arguments_compile_again(self):
		self.library_and_program()
		self.build("App")
		with open(self.path("App", "nuke.toml"), "a", encoding="utf-8") as f:
			f.write("compiler-args = [\"-Wno-unused-variable\"]\n")
		self.assertIn("App: 2 compiled", self.build("App"))

	def test_debug_and_dist(self):
		self.library_and_program()
		self.build("App", "Debug")
		args = read(self.path("build", "int", "Debug-windows-x64", "App", "Main.args"))
		self.assertIn("-O0", args)
		self.assertIn("-DDEBUG", args)
		self.build("App", "Dist")
		self.assertFalse(os.path.exists(self.path("build", "bin", "Dist-windows-x64", "App", "App.pdb")), "Dist has no debug information")
		self.assertNotIn("-gcodeview", read(self.path("build", "int", "Dist-windows-x64", "App", "Main.args")))

	def test_run_starts_the_program(self):
		self.library_and_program()
		code, out = self.nuke("run", "App")
		self.assertEqual(42, code, out)


class FailureTests(CppTest):
	def test_a_failed_compile_stops_the_build_and_leaves_nothing(self):
		self.workspace({"App": ("", {"src/Main.cpp": "int main() { return 0; }\n", "src/Broken.cpp": "int broken() { return 1 }\n"})})
		code, out = self.nuke("build", "App")
		self.assertNotEqual(0, code)
		self.assertRegex(out, r"Broken\.cpp\(1,\d+\): error: expected ';'", "clang's diagnostic in MSVC's format")
		self.assertIn("App: compiling 1 file(s) failed", out)
		self.assertFalse(os.path.exists(self.int("App", "Broken.obj")))
		self.assertFalse(os.path.exists(self.int("App", "Broken.d")))
		self.assertFalse(os.path.exists(self.bin("App", "App.exe")), "nothing links after a failed compile")

	def test_stale_objects_go_and_clashing_names_are_refused(self):
		self.workspace({"App": ("", {"src/Main.cpp": "int main() { return 0; }\n", "src/Gone.cpp": "int gone() { return 1; }\n"})})
		self.build("App")
		os.remove(self.path("App", "src", "Gone.cpp"))
		out = self.build("App")
		self.assertIn("App: removed the stale Gone.obj", out)
		self.assertFalse(os.path.exists(self.int("App", "Gone.obj")))
		self.assertTrue(os.path.exists(self.int("App", "linker.args")), "the linker's own record stays")
		self.files({"App/src/a/Types.cpp": "int a() { return 1; }\n", "App/src/b/Types.cpp": "int b() { return 2; }\n"})
		code, out = self.nuke("build", "App")
		self.assertEqual(1, code)
		self.assertIn("both compile to Types.obj", out)

	def test_unknown_settings_and_options_are_named(self):
		self.workspace({"App": ("[project.driver]\nreflect = true\n", {"src/Main.cpp": "int main() { return 0; }\n"})})
		code, out = self.nuke("build", "App")
		self.assertEqual(1, code)
		self.assertIn("unknown key project.driver.reflect", out)
		self.workspace({"App": ("", {"src/Main.cpp": "int main() { return 0; }\n"})}, settings="compiler = \"x\"\n")
		code, out = self.nuke("build", "App")
		self.assertEqual(1, code)
		self.assertIn("unknown key languages.cpp.compiler", out)

	def test_a_missing_link_is_named(self):
		self.workspace({"App": ("links = [\"nowhere.lib\"]\n", {"src/Main.cpp": "int main() { return 0; }\n"})})
		code, out = self.nuke("build", "App")
		self.assertEqual(1, code)
		self.assertIn("link nowhere.lib is not in", out)

	def test_the_project_name_is_the_manifests(self):
		self.files({"nuke.toml": f"[workspace]\n[languages.cpp]\nllvm = \"{LLVM}\"\n",
			"not-an-identifier/nuke.toml": "[project]\nname = \"Named\"\nlanguage = \"cpp\"\n",
			"not-an-identifier/src/Main.cpp": "#define TEXT(x) #x\n#define NAME(x) TEXT(x)\n#include <cstring>\n"
				"int main() { return std::strcmp(NAME(PROJECT_NAME), \"Named\") == 0 ? 0 : 1; }\n"})
		self.build("Named")
		self.assertEqual(0, subprocess.run([self.bin("Named", "Named.exe")]).returncode)


class LibraryTests(CppTest):
	def test_a_dll_with_its_exclusions(self):
		self.workspace({"Plugin": ("kind = \"library\"\nlinks = [\"user32.lib\", \"shlwapi.lib\", \"!shlwapi.lib\"]\n"
			"defines = { PLUGIN_EXPORTS = \"\" }\n[project.driver]\nlibrary = \"dynamic\"\nlinker-args = [\"-fuse-ld=lld\"]\n",
			{"src/Plugin.cpp": "#ifdef PLUGIN_EXPORTS\n__declspec(dllexport)\n#endif\nint PluginAnswer() { return 7; }\n"})})
		self.build("Plugin")
		self.assertTrue(os.path.isfile(self.bin("Plugin", "Plugin.dll")))
		self.assertTrue(os.path.isfile(self.bin("Plugin", "Plugin.lib")), "lld writes the import library")
		args = read(self.int("Plugin", "linker.args"))
		self.assertIn("-shared", args)
		self.assertIn("user32.lib", args)
		self.assertNotIn("shlwapi.lib", args, "!shlwapi.lib leaves it out")

	def test_a_glob_source_and_includes(self):
		self.workspace({"Vendored": ("kind = \"library\"\nsources = [\"upstream/src/**/*.cpp\"]\nincludes = [\"upstream/include\"]\n", {
			"upstream/include/up.h": "int Up();\n", "upstream/src/deep/up.cpp": "#include \"up.h\"\nint Up() { return 5; }\n",
			"upstream/tests/test.cpp": "this is not compiled\n"}),
			"App": ("depends = [\"Vendored\"]\n", {"src/Main.cpp": "#include \"up.h\"\nint main() { return Up(); }\n"})})
		self.build("App")
		self.assertEqual(5, subprocess.run([self.bin("App", "App.exe")]).returncode)

	def test_includes_and_links_travel_through_static_libraries_only(self):
		self.workspace({
			"Base": ("kind = \"library\"\nlinks = [\"shlwapi.lib\"]\n", {"src/Base.h": "int Base();\n", "src/Base.cpp": "int Base() { return 1; }\n"}),
			"Mid": ("kind = \"library\"\ndepends = [\"Base\"]\n", {"src/Mid.cpp": "#include \"Base.h\"\nint Mid() { return Base() + 1; }\n"}),
			"Tool": ("depends = [\"Mid\"]\n", {"src/Main.cpp": "int Mid();\nint main() { return Mid(); }\n"}),
			"App": ("depends = [\"Tool\"]\n", {"src/Main.cpp": "int main() { return 0; }\n"}),
		})
		self.build("App")
		self.assertEqual(2, subprocess.run([self.bin("Tool", "Tool.exe")]).returncode, "Tool links Mid and, through it, Base")
		self.assertIn("shlwapi.lib", read(self.int("Tool", "linker.args")), "Base's links reach the program")
		app = read(self.int("App", "linker.args"))
		self.assertNotIn("Mid.lib", app, "a program in depends only orders the build")
		self.assertNotIn(self.path("Mid", "src").replace("\\", "/"), read(self.int("App", "Main.args")))


class PrebuiltTests(CppTest):
	"""A prebuilt library (an SDK's, as the C4 toolchain's C4Api): nothing built, its include folders, files and copies
	reaching what depends on it."""

	def sdk(self):
		"""An SDK made of a static library's build: sdk/include/Lib/Lib.h, sdk/lib/Lib.lib and a file to ship."""
		self.workspace({"Lib": ("kind = \"library\"\n", {"src/Lib/Lib.h": "#pragma once\nint Answer();\n",
			"src/Lib/Lib.cpp": "#include \"Lib/Lib.h\"\n#include <windows.h>\n#include <shlwapi.h>\n"
				"int Answer() { return PathIsRelativeA(\"a\\\\b\") ? 42 : 0; }\n"})})
		self.build("Lib")
		os.makedirs(self.path("sdk", "include", "Lib"))
		os.makedirs(self.path("sdk", "lib"))
		shutil.copy2(self.path("Lib", "src", "Lib", "Lib.h"), self.path("sdk", "include", "Lib", "Lib.h"))
		shutil.copy2(self.bin("Lib", "Lib.lib"), self.path("sdk", "lib", "Lib.lib"))
		self.files({"sdk/shipped.txt": "beside the program"})

	def prebuilt(self, manifest):
		self.workspace({
			"Sdk": ("kind = \"library\"\n" + manifest + "[project.driver]\nlibrary = \"prebuilt\"\n", {}),
			"App": ("depends = [\"Sdk\"]\n", {"src/Main.cpp": "#include \"Lib/Lib.h\"\nint main() { return Answer(); }\n"}),
		})

	def test_a_program_gets_its_includes_files_and_copies(self):
		self.sdk()
		self.prebuilt("sources = []\nincludes = [\"../sdk/include\"]\nlinks = [\"../sdk/lib/Lib.lib\", \"shlwapi.lib\"]\n"
			"[[project.copy]]\nfrom = \"../sdk/shipped.txt\"\n")
		out = self.build("App")
		self.assertIn("App: copied 1 file(s) beside it", out)
		self.assertEqual(42, subprocess.run([self.bin("App", "App.exe")]).returncode, "the SDK's library linked, with its own links")
		self.assertEqual("beside the program", read(self.bin("App", "shipped.txt")))
		self.assertFalse(os.path.exists(self.bin("Sdk")), "nothing of its own is built")
		self.assertIn(self.path("sdk", "lib", "Lib.lib").replace("\\", "/"), read(self.int("App", "linker.args")).replace("\\", "/"))

	def test_missing_files_and_sources_are_named(self):
		self.sdk()
		self.prebuilt("sources = []\nincludes = [\"../sdk/include\"]\nlinks = [\"../sdk/lib/Gone.lib\"]\n")
		code, out = self.nuke("build", "App")
		self.assertEqual(1, code, out)
		self.assertIn("its prebuilt files are not there", out)
		self.assertIn("Gone.lib", out)
		self.prebuilt("includes = [\"../sdk/include\"]\n")
		self.files({"Sdk/src/x.cpp": "int x() { return 0; }\n"})
		code, out = self.nuke("build", "App")
		self.assertEqual(1, code, out)
		self.assertIn("a prebuilt library compiles nothing", out)


class LinkTests(CppTest):
	def test_a_link_past_the_command_line_limit(self):
		folder = self.path("a_folder_whose_name_is_long_enough_to_matter", "and_another_one_below_it", "and_a_third_one_too")
		os.makedirs(folder)
		source = os.path.join(folder, "empty.c")
		with open(source, "w") as f:
			f.write("int unused(void);\n")
		first = os.path.join(folder, "an_object_with_a_long_name_000.obj")
		subprocess.run([os.path.join(LLVM, "bin", "clang.exe"), "-c", "-target", "x86_64-pc-windows-msvc", source, "-o", first], check=True)
		objects = [first]
		for i in range(1, 400):
			objects.append(os.path.join(folder, f"an_object_with_a_long_name_{i:03}.obj"))
			shutil.copy(first, objects[-1])
		self.assertGreater(sum(len(o) + 1 for o in objects), 32767)
		target = module("model").Target("Probe", folder, "static", folder, folder, (), (), (), (), (), (), (), False, (), False)
		toolchain = module("toolchain").Toolchain(LLVM, os.path.join(LLVM, "bin", "clang.exe"), (), (), "19.0.0", module("settings").Settings())

		class Quiet:
			def line(self, text):
				pass

			def write(self, text):
				pass

		class Configuration:
			debug_info = False
		code, ran = module("link").link(toolchain, Configuration(), target, objects, [], Quiet())
		self.assertEqual((0, True), (code, ran))
		self.assertTrue(os.path.isfile(target.output))


class DatabaseTests(CppTest):
	def test_the_compile_database(self):
		self.workspace({"App": ("compiler-args = [\"-O0\", \"-Wno-unused\"]\nincludes = [\"include\"]\ndefines = { PROBE_VALUE = \"7\" }\n", {
			"include/probe.h": "#pragma once\n#include <vector>\ninline int Probe() { return PROBE_VALUE; }\n",
			"src/Main.cpp": "#include \"probe.h\"\nstd::vector<int> g_values = { Probe() };\nint main() { return 0; }\n",
			"src/Probe.c": "int c_probe(void) { return 1; }\n"})})
		database = self.path("build", "compile_commands.json")
		os.makedirs(os.path.dirname(database), exist_ok=True)
		with open(database, "w", encoding="utf-8") as f:
			other = self.path("Other", "other.cpp").replace("\\", "/")
			json.dump([{"directory": self.root, "file": other, "arguments": ["other"]},
				{"directory": self.root, "file": self.path("Gone", "gone.cpp").replace("\\", "/"), "arguments": ["gone"]}], f)
		self.files({"Other/other.cpp": "int other() { return 0; }\n"})
		self.build("App")
		entries = json.loads(read(database))
		self.assertEqual(["Main.cpp", "Probe.c", "other.cpp"], sorted(os.path.basename(e["file"]) for e in entries), "another project's entry stays, a gone file's goes")
		cpp = next(e for e in entries if e["file"].endswith("Main.cpp"))["arguments"]
		c = next(e for e in entries if e["file"].endswith("Probe.c"))["arguments"]
		self.assertNotIn("-cc1", cpp)
		self.assertIn("-isystem", cpp)
		self.assertIn("-Wno-unused", cpp, "driver flags of compiler-args")
		self.assertNotIn("-O0", cpp, "cc1-only arguments stay out")
		self.assertEqual("c", c[c.index("-x") + 1])
		self.assertNotIn("-fcxx-exceptions", c)
		check = subprocess.run([a for a in cpp if a != "-c"] + ["-fsyntax-only"], cwd=self.path("App"), capture_output=True, text=True)
		self.assertEqual(0, check.returncode, check.stderr)


class CopyTests(CppTest):
	def test_copies_and_the_assets_link(self):
		self.workspace({
			"Data": ("kind = \"library\"\n[[project.copy]]\nfrom = \"shipped\"\nexclude = [\"nuke.toml\"]\n", {"src/Data.cpp": "int Data() { return 0; }\n",
				"shipped/a.txt": "a", "shipped/deep/b.txt": "b", "shipped/deep/nuke.toml": "[project]\n"}),
			"App": ("depends = [\"Data\"]\n[[project.copy]]\nfrom = \"${cpp.llvm}/bin/clang.exe\"\nto = \"toolchain/bin/clang.exe\"\n",
				{"src/Main.cpp": "int main() { return 0; }\n", "assets/x.txt": "x"}),
		})
		out = self.build("App")
		self.assertIn("App: copied 3 file(s) beside it", out)
		self.assertEqual("b", read(self.bin("App", "shipped/deep/b.txt")), "a library's copies land beside the program")
		self.assertTrue(os.path.isfile(self.bin("App", "toolchain/bin/clang.exe")))
		self.assertEqual("x", read(self.bin("App", "assets/x.txt")), "assets/ is linked")
		self.assertFalse(os.path.exists(self.bin("App", "shipped/deep/nuke.toml")), "what a copy excludes stays behind, at any depth")
		os.remove(self.path("Data", "shipped", "a.txt"))
		self.build("App")
		self.assertFalse(os.path.exists(self.bin("App", "shipped/a.txt")), "a folder is mirrored")
		self.assertNotIn("copied", self.build("App"))


class TestProgramTests(CppTest):
	def test_nuke_test_builds_and_runs_with_the_filter(self):
		self.workspace({"Checks": ("tests = true\n", {"src/Main.cpp": "#include <cstring>\n"
			"int main(int argc, char** argv) { return argc == 3 && std::strcmp(argv[1], \"-filter=math.*\") == 0 && std::strcmp(argv[2], \"extra\") == 0 ? 0 : 9; }\n"})})
		code, out = self.nuke("test", "Checks", "--filter", "math.*", "--", "extra")
		self.assertEqual(0, code, out)
		self.assertIn("passed  Checks", out)
		code, out = self.nuke("test", "Checks")
		self.assertEqual(1, code, out)
		self.assertIn("FAILED  Checks", out)

	def test_a_library_has_no_tests(self):
		self.workspace({"Lib": ("kind = \"library\"\n", {"src/Lib.cpp": "int f() { return 0; }\n"})})
		code, out = self.nuke("test", "Lib")
		self.assertEqual(1, code)
		self.assertIn("Lib has no tests", out)


class VariableTests(CppTest):
	def test_the_llvm_variables(self):
		self.workspace({"App": ("[[project.steps]]\nphase = \"post-build\"\nrun = [\"${cpp.clang}\", \"--version\"]\n",
			{"src/Main.cpp": "int main() { return 0; }\n"})})
		self.assertIn("clang version", self.build("App"))


FAKE_COMPILE = """import sys, time
source, obj, dep, edit = sys.argv[1:5]
time.sleep(0.3)
if edit == "edit":
	with open(source, "a") as f:
		f.write("// edited while it compiled\\n")
time.sleep(0.3)
open(obj, "w").write("object")
open(dep, "w").write(dep + ": " + source + "\\n")
"""


class ObjectStampTests(unittest.TestCase):
	def setUp(self):
		import tempfile
		self.tmp = tempfile.mkdtemp(prefix="cpp-stamp-")
		self.source = os.path.join(self.tmp, "Main.cpp").replace("\\", "/")
		with open(self.source, "w") as f:
			f.write("int main() { return 0; }\n")
		self.compiler = os.path.join(self.tmp, "fake_compile.py")
		with open(self.compiler, "w") as f:
			f.write(FAKE_COMPILE)
		old = time.time() - 60
		os.utime(self.compiler, (old, old))

	def tearDown(self):
		shutil.rmtree(self.tmp, ignore_errors=True)

	def job(self, edit):
		objects = module("objects")
		base = os.path.join(self.tmp, "Main")
		return objects.Job(self.source, base + ".obj", base + ".d", base + ".args", [sys.executable, self.compiler, self.source, base + ".obj", base + ".d", edit])

	def decided(self, job):
		module("objects").decide(job, module("objects").Stats(), self.compiler, None)
		return job.compile_needed

	def test_bug_346_a_file_edited_during_its_compile_compiles_again(self):
		objects = module("objects")
		first = self.job("edit")
		first.compile_needed = True
		self.assertEqual(0, objects.run_job(first, self.tmp), first.log)
		self.assertTrue(self.decided(self.job("edit")), "the edit came after the compile began: the object is older than it")

	def test_an_untouched_file_is_up_to_date(self):
		objects = module("objects")
		first = self.job("keep")
		first.compile_needed = True
		self.assertEqual(0, objects.run_job(first, self.tmp), first.log)
		self.assertFalse(self.decided(self.job("keep")))
		self.assertTrue(self.decided(self.job("other arguments")), "a changed command compiles again")


class DepfileTests(unittest.TestCase):
	def test_escaped_spaces_and_continued_lines(self):
		parse = module("depfile").parse
		text = "C:/int/Main.d: C:/src/Main.cpp \\\r\n  C:/Program\\ Files/x.h C:/src/a$$b.h \\\n  C:\\mixed\\path.h\n"
		self.assertEqual(["C:/src/Main.cpp", "C:/Program Files/x.h", "C:/src/a$b.h", "C:\\mixed\\path.h"], parse(text))
		self.assertEqual([], parse(""))


if __name__ == "__main__":
	unittest.main()
