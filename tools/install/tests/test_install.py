"""The installer (tools/install/install_ied.py): a mirror copies what changed, leaves out what it skips and removes what
is gone; a program in use is renamed aside and replaced, and the next install removes the old copy; a program's PDB
comes and goes with the build; the PATH gains the folder and its bin once and loses only them; the install carries
Nuke; an uninstall removes the folder whole or, while a program in it runs, not at all; every copy is flushed before
install.json says the install is complete, and after one that did not say so every file is compared by content."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import install_ied  # noqa: E402


def write(path, text):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8") as f:
		f.write(text)


def report():
	return {"copied": 0, "removed": 0, "in_use": []}


class MirrorTests(unittest.TestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-install-")
		self.addCleanup(shutil.rmtree, self.root, True)
		self.source = os.path.join(self.root, "source")
		self.target = os.path.join(self.root, "target")

	def test_copies_what_changed_skips_and_removes_what_is_gone(self):
		write(os.path.join(self.source, "index.html"), "<p>one</p>")
		write(os.path.join(self.source, "src", "main.js"), "main")
		write(os.path.join(self.source, "mocks", "big.png"), "mock")
		first = report()
		install_ied.mirror(self.source, self.target, {"mocks"}, first)
		self.assertEqual(2, first["copied"])
		self.assertFalse(os.path.exists(os.path.join(self.target, "mocks")), "a skipped folder is left out")
		again = report()
		install_ied.mirror(self.source, self.target, {"mocks"}, again)
		self.assertEqual(0, again["copied"], "nothing changed, nothing copied")
		write(os.path.join(self.source, "src", "main.js"), "main, changed")
		os.remove(os.path.join(self.source, "index.html"))
		write(os.path.join(self.target, "src", "stale", "old.js"), "old")
		changed = report()
		install_ied.mirror(self.source, self.target, {"mocks"}, changed)
		self.assertEqual((1, 2), (changed["copied"], changed["removed"]))
		with open(os.path.join(self.target, "src", "main.js"), encoding="utf-8") as f:
			self.assertEqual("main, changed", f.read())
		self.assertFalse(os.path.exists(os.path.join(self.target, "index.html")))
		self.assertFalse(os.path.exists(os.path.join(self.target, "src", "stale")), "an emptied folder goes too")

	def test_a_copy_a_crash_zero_filled_is_found_by_its_content(self):
		"""2026-11-22: a crash minutes after an install left its copies their size and time, full of zeros."""
		source = os.path.join(self.source, "src", "shell.js")
		write(source, "export const shell = 1;")
		install_ied.mirror(self.source, self.target, set(), report())
		copy = os.path.join(self.target, "src", "shell.js")
		with open(copy, "r+b") as f:
			f.write(b"\0" * os.path.getsize(source))
		os.utime(copy, ns=(os.stat(source).st_atime_ns, os.stat(source).st_mtime_ns))
		by_time = report()
		install_ied.mirror(self.source, self.target, set(), by_time)
		self.assertEqual(0, by_time["copied"], "size and time cannot tell")
		by_content = dict(report(), content=True)
		install_ied.mirror(self.source, self.target, set(), by_content)
		self.assertEqual(1, by_content["copied"])
		with open(copy, encoding="utf-8") as f:
			self.assertEqual("export const shell = 1;", f.read())

	def test_each_copy_is_kept_for_the_flush(self):
		write(os.path.join(self.source, "a.js"), "a")
		write(os.path.join(self.source, "b", "c.js"), "c")
		result = report()
		install_ied.mirror(self.source, self.target, set(), result)
		self.assertEqual(sorted([os.path.join(self.target, "a.js"), os.path.join(self.target, "b", "c.js")]), sorted(result["written"]))
		install_ied.flush_all(result["written"])

	def test_a_file_renamed_in_case_or_moved_keeps_its_content(self):
		write(os.path.join(self.target, "code", "activate.cf"), "same")
		write(os.path.join(self.target, "code", "run.cf"), "old")
		write(os.path.join(self.target, "code", "moved.cf"), "moved")
		for name in ("run.cf", "moved.cf"):
			os.utime(os.path.join(self.target, "code", name), (1000000000, 1000000000))   # installed long before the change
		write(os.path.join(self.source, "code", "Activate.cf"), "same")
		write(os.path.join(self.source, "code", "Run.cf"), "new")
		write(os.path.join(self.source, "code", "run", "Moved.cf"), "moved")
		result = report()
		install_ied.mirror(self.source, self.target, set(), result)
		self.assertEqual(["Activate.cf", "Run.cf", "run"], sorted(os.listdir(os.path.join(self.target, "code"))), "the source's spelling, nothing lost")
		with open(os.path.join(self.target, "code", "Activate.cf"), encoding="utf-8") as f:
			self.assertEqual("same", f.read(), "an unchanged file renamed only in case is kept, not removed")
		with open(os.path.join(self.target, "code", "Run.cf"), encoding="utf-8") as f:
			self.assertEqual("new", f.read())
		self.assertEqual(["Moved.cf"], os.listdir(os.path.join(self.target, "code", "run")))


	def test_what_a_checkout_built_in_place_stays_out_at_any_depth(self):
		write(os.path.join(self.source, "c4", "ied-plugin.json"), "{}")
		write(os.path.join(self.source, "c4", "samples", "tool", "build", "bin", "tool.exe"), "built")
		write(os.path.join(self.source, "c4", "samples", "tool", "src", "main.cpp"), "int main() {}")
		write(os.path.join(self.source, "rust", "profiler", "target", "debug", "x.rlib"), "cargo")
		write(os.path.join(self.source, "cpp", "nuke", "driver", "__pycache__", "model.pyc"), "bytecode")
		write(os.path.join(self.target, "rust", "profiler", "target", "old.rlib"), "installed before")
		result = report()
		install_ied.mirror(self.source, self.target, set(), result, install_ied.BUILT_IN_PLACE)
		self.assertEqual(2, result["copied"], "the manifest and the sample's source")
		self.assertTrue(os.path.isfile(os.path.join(self.target, "c4", "samples", "tool", "src", "main.cpp")))
		for gone in (("c4", "samples", "tool", "build"), ("rust", "profiler", "target"), ("cpp", "nuke", "driver", "__pycache__")):
			self.assertFalse(os.path.exists(os.path.join(self.target, *gone)), "/".join(gone))


class ServicesTests(unittest.TestCase):
	def test_setup_installs_the_services_through_the_private_python(self):
		repo = tempfile.mkdtemp(prefix="ied-install-")
		self.addCleanup(shutil.rmtree, repo, True)
		command = install_ied.services_command(repo)
		self.assertEqual(["cmd", "/d", "/c", os.path.join(repo, "setup.bat")], command[:4], "without the private Python, setup.bat installs it first")
		write(os.path.join(repo, "vendor", "python", "python.exe"), "")
		command = install_ied.services_command(repo)
		self.assertEqual([os.path.join(repo, "vendor", "python", "python.exe"), "-I", os.path.join(repo, "tools", "setup", "setup.py")], command[:3])
		self.assertEqual(["--only", "searxng,pip,searxng-packages,tor,codelldb", "--no-path"], command[3:], "only the services, never PATH")


class CppToolsTests(unittest.TestCase):
	"""The C/C++ plugin's clangd (with clang's builtin headers) and CodeLLDB go into its folder of the install, and the
	mirror of plugins/ leaves them there."""

	def test_the_cpp_plugin_gets_its_tools_and_keeps_them(self):
		root = tempfile.mkdtemp(prefix="ied-install-")
		self.addCleanup(shutil.rmtree, root, True)
		repo, target = os.path.join(root, "repo"), os.path.join(root, "IED")
		write(os.path.join(repo, "vendor", "llvm", "bin", "clangd.exe"), "clangd")
		write(os.path.join(repo, "vendor", "llvm", "lib", "clang", "22", "include", "stddef.h"), "stddef")
		write(os.path.join(repo, "vendor", "llvm", "lib", "clang", "22", "lib", "windows", "asan.lib"), "not needed")
		write(os.path.join(repo, "vendor", "codelldb", "adapter", "codelldb.exe"), "codelldb")
		write(os.path.join(repo, "vendor", "codelldb", "lldb", "bin", "liblldb.dll"), "lldb")
		write(os.path.join(repo, "plugins", "cpp", "ied-plugin.json"), "{}")
		done = dict(report(), missing=[])
		install_ied.mirror(os.path.join(repo, "plugins"), os.path.join(target, "plugins"), set(), done, keep=install_ied.CPP_TOOLS_FOLDERS)
		install_ied.install_cpp_tools(repo, target, done)
		cpp = os.path.join(target, "plugins", "cpp")
		for path in (("clangd", "bin", "clangd.exe"), ("clangd", "lib", "clang", "22", "include", "stddef.h"), ("codelldb", "adapter", "codelldb.exe"), ("codelldb", "lldb", "bin", "liblldb.dll")):
			self.assertTrue(os.path.isfile(os.path.join(cpp, *path)), path)
		self.assertFalse(os.path.exists(os.path.join(cpp, "clangd", "lib", "clang", "22", "lib")), "only the headers clangd reads")
		self.assertEqual([], done["missing"])
		again = dict(report(), missing=[])
		install_ied.mirror(os.path.join(repo, "plugins"), os.path.join(target, "plugins"), set(), again, keep=install_ied.CPP_TOOLS_FOLDERS)
		install_ied.install_cpp_tools(repo, target, again)
		self.assertEqual((0, 0), (again["copied"], again["removed"]), "the next install leaves them as they are")
		shutil.rmtree(os.path.join(repo, "vendor", "codelldb"))
		without = dict(report(), missing=[])
		install_ied.install_cpp_tools(repo, target, without)
		self.assertEqual(["CodeLLDB (vendor/llvm, vendor/codelldb)"], without["missing"], "what the checkout lacks is said")


class GpuNativeTests(unittest.TestCase):
	"""The GPU debugger's capture library and replayer go into its plugin's bin, where an installed IED looks for them,
	and the mirror of plugins/ leaves them there."""

	def test_the_gpu_plugin_gets_flashbang_and_keeps_it(self):
		root = tempfile.mkdtemp(prefix="ied-install-")
		self.addCleanup(shutil.rmtree, root, True)
		repo, bin_root, target = os.path.join(root, "repo"), os.path.join(root, "bin"), os.path.join(root, "IED")
		for name in install_ied.GPU_NATIVE_FILES:
			write(os.path.join(bin_root, "flashbang", name), name)
		write(os.path.join(bin_root, "flashbang", "flashbang-tests.exe"), "not installed")
		write(os.path.join(repo, "plugins", "gpu", "ied-plugin.json"), "{}")
		done = dict(report(), missing=[])
		install_ied.mirror(os.path.join(repo, "plugins"), os.path.join(target, "plugins"), set(), done, keep=install_ied.KEPT_PLUGIN_FOLDERS)
		install_ied.install_gpu_native(bin_root, target, done)
		native = os.path.join(target, "plugins", "gpu", "bin")
		self.assertEqual(sorted(install_ied.GPU_NATIVE_FILES), sorted(os.listdir(native)), "the library, its layer manifest and the replayer")
		self.assertEqual([], done["missing"])
		again = dict(report(), missing=[])
		install_ied.mirror(os.path.join(repo, "plugins"), os.path.join(target, "plugins"), set(), again, keep=install_ied.KEPT_PLUGIN_FOLDERS)
		install_ied.install_gpu_native(bin_root, target, again)
		self.assertEqual((0, 0), (again["copied"], again["removed"]), "the next install leaves them as they are")
		os.remove(os.path.join(bin_root, "flashbang", "flashbang-replay.exe"))
		without = dict(report(), missing=[])
		install_ied.install_gpu_native(bin_root, target, without)
		self.assertEqual(["flashbang-replay.exe (nuke build flashbang)"], without["missing"], "what the build lacks is said")


class RecordTests(unittest.TestCase):
	def test_the_record_names_its_checkout(self):
		root = tempfile.mkdtemp(prefix="ied-install-")
		self.addCleanup(shutil.rmtree, root, True)
		record = install_ied.install_record(os.path.join(root, "IED"), os.path.join(root, "IED", "build", "bin", "Dist-windows-x64"), "a" * 40, True, 1792630000123)
		self.assertEqual({"source", "commit", "dirty", "installedMs", "bin", "complete"}, set(record))
		self.assertTrue(record["complete"])
		self.assertEqual((root.replace("\\", "/") + "/IED", "a" * 40, True, 1792630000123), (record["source"], record["commit"], record["dirty"], record["installedMs"]))
		self.assertNotIn("\\", record["bin"], "forward slashes")
		install_ied.write_record(root, record)
		with open(os.path.join(root, "install.json"), encoding="utf-8") as f:
			self.assertEqual(record, json.load(f))
		self.assertFalse(os.path.exists(os.path.join(root, "install.json.tmp")), "renamed into place")

	def test_only_a_record_that_says_complete_is_a_finished_install(self):
		root = tempfile.mkdtemp(prefix="ied-install-")
		self.addCleanup(shutil.rmtree, root, True)
		self.assertIsNone(install_ied.read_record(root), "no install.json")
		self.assertFalse(install_ied.finished(None))
		with open(os.path.join(root, "install.json"), "wb") as f:
			f.write(b"\0" * 230)
		self.assertIsNone(install_ied.read_record(root), "zero-filled by a crash: unreadable")
		for record, done in (({"source": "C:/IED"}, False), ({"complete": False}, False), ({"complete": True}, True), ({"complete": "yes"}, False)):
			install_ied.write_record(root, record)
			self.assertEqual(done, install_ied.finished(install_ied.read_record(root)), record)

	def test_the_checkout_state(self):
		head, dirty = install_ied.checkout_state(install_ied.REPO)
		self.assertRegex(head, r"^[0-9a-f]{40}$", "this checkout's full commit")
		self.assertIsInstance(dirty, bool)


class UnfinishedInstallTests(unittest.TestCase):
	"""install(): install.json says `complete: false` while it runs and `complete: true` last, and an install that finds
	it otherwise compares every file by content (the trees, services and tools stubbed: only the programs are copied)."""

	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-install-")
		self.addCleanup(shutil.rmtree, self.root, True)
		self.bin = os.path.join(self.root, "bin")
		self.target = os.path.join(self.root, "IED")
		for program, files in install_ied.PROGRAMS.items():
			for name in files:
				write(os.path.join(self.bin, program, name), name)
		self.during = []
		for patch in (
			mock.patch.multiple(install_ied, TREES=(), SERVICES=()),
			mock.patch.object(install_ied, "ensure_services", lambda repo: True),
			mock.patch.object(install_ied, "install_cpp_tools", lambda repo, target, report: None),
			mock.patch.object(install_ied, "install_gpu_native", lambda bin_root, target, report: None),
			mock.patch.object(install_ied, "checkout_state", lambda repo: ("b" * 40, False)),
			mock.patch.object(install_ied, "install_symbols", lambda bin_root, target, report: self.during.append(install_ied.read_record(target))),
		):
			patch.start()
			self.addCleanup(patch.stop)

	def test_the_record_says_complete_only_last(self):
		flushed = []
		with mock.patch.object(install_ied, "flush", lambda path: flushed.append((path, install_ied.read_record(self.target)["complete"]))):
			first = install_ied.install(self.bin, self.target)
		programs = sorted(os.path.join(self.target, name) for files in install_ied.PROGRAMS.values() for name in files)
		self.assertEqual(programs, sorted(path for path, _ in flushed), "every copy flushed")
		self.assertEqual({False}, {complete for _, complete in flushed}, "before install.json says complete")
		self.assertEqual((False, False), (self.during[-1]["complete"], first["unfinished"]), "under way while it copies; a new folder was no unfinished install")
		self.assertTrue(install_ied.finished(install_ied.read_record(self.target)))
		again = install_ied.install(self.bin, self.target)
		self.assertEqual((False, False, 0), (again["content"], again["unfinished"], again["copied"]), "after a finished install: by size and time")
		self.assertTrue(install_ied.install(self.bin, self.target, verify=True)["content"], "--verify")

	def test_after_an_unfinished_install_every_file_is_compared_by_content(self):
		install_ied.install(self.bin, self.target)
		program = os.path.join(self.target, "ied.exe")
		source = os.path.join(self.bin, "ied", "ied.exe")
		with open(program, "r+b") as f:
			f.write(b"\0" * os.path.getsize(source))
		os.utime(program, ns=(os.stat(source).st_atime_ns, os.stat(source).st_mtime_ns))
		with open(os.path.join(self.target, "install.json"), "r+b") as f:
			f.write(b"\0" * os.path.getsize(os.path.join(self.target, "install.json")))
		repaired = install_ied.install(self.bin, self.target)
		self.assertEqual((True, True, 1), (repaired["unfinished"], repaired["content"], repaired["copied"]), "the zeroed program found and copied again")
		with open(program, encoding="utf-8") as f:
			self.assertEqual("ied.exe", f.read())
		self.assertTrue(install_ied.finished(install_ied.read_record(self.target)))


class PathTests(unittest.TestCase):
	FOLDER = r"C:\Users\me\AppData\Local\Programs\IED"

	def test_the_folder_goes_last_once(self):
		self.assertEqual(r"C:\a;C:\b;" + self.FOLDER, install_ied.with_folder(r"C:\a;C:\b;", self.FOLDER))
		self.assertEqual(self.FOLDER, install_ied.with_folder("", self.FOLDER))
		self.assertIsNone(install_ied.with_folder(r"C:\a;c:\users\ME\appdata\local\programs\ied\;C:\b", self.FOLDER), "there already, in other case")
		self.assertIsNone(install_ied.with_folder('"%s"' % self.FOLDER, self.FOLDER), "there already, quoted")

	def test_only_the_folder_goes(self):
		self.assertEqual(r"C:\a;%USERPROFILE%\bin", install_ied.without_folder(r"C:\a;" + self.FOLDER + r";%USERPROFILE%\bin", self.FOLDER))
		self.assertIsNone(install_ied.without_folder(r"C:\a;C:\b", self.FOLDER), "nothing to take out")

	def test_the_install_and_its_bin_go_and_leave_together(self):
		folders = install_ied.path_folders(self.FOLDER)
		self.assertEqual([self.FOLDER, self.FOLDER + r"\bin"], folders, "ied's folder and Nuke's launchers")
		self.assertEqual(r"C:\a;%s;%s\bin" % (self.FOLDER, self.FOLDER), install_ied.with_folders(r"C:\a;" + self.FOLDER, folders, install_ied.with_folder), "an older install's PATH gains bin")
		self.assertIsNone(install_ied.with_folders(r"%s\bin;%s" % (self.FOLDER, self.FOLDER), folders, install_ied.with_folder), "both there")
		self.assertEqual(r"C:\a", install_ied.with_folders(r"%s;C:\a;%s\bin" % (self.FOLDER, self.FOLDER), folders, install_ied.without_folder))


class NukeTests(unittest.TestCase):
	def test_the_install_carries_nuke_and_its_launchers(self):
		trees = [tree for tree, _ in install_ied.TREES]
		for tree in ("Nuke/nuke", "Nuke/templates", "bin"):
			self.assertIn(tree, trees)
			self.assertTrue(os.path.isdir(os.path.join(install_ied.REPO, tree)), tree)
		self.assertTrue(os.path.isfile(os.path.join(install_ied.REPO, "bin", "nuke.cmd")))
		self.assertIn("__pycache__", install_ied.BUILT_IN_PLACE, "no bytecode of the checkout's runs")


@unittest.skipUnless(os.name == "nt", "Windows: a running program's file")
class SymbolsTests(unittest.TestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-install-")
		self.addCleanup(shutil.rmtree, self.root, True)
		self.bin = os.path.join(self.root, "bin")
		self.target = os.path.join(self.root, "target")
		os.makedirs(self.target)

	def test_a_program_s_pdb_goes_beside_it_and_leaves_with_it(self):
		write(os.path.join(self.bin, "ied-server", "ied-server.pdb"), "server symbols")
		write(os.path.join(self.bin, "ied-desktop", "ied-desktop.pdb"), "desktop symbols")
		write(os.path.join(self.target, "ied.pdb"), "an older build's")
		result = report()
		install_ied.install_symbols(self.bin, self.target, result)
		self.assertEqual((2, 1), (result["copied"], result["removed"]))
		with open(os.path.join(self.target, "ied-server.pdb"), encoding="utf-8") as f:
			self.assertEqual("server symbols", f.read())
		self.assertFalse(os.path.exists(os.path.join(self.target, "ied.pdb")), "a PDB the build no longer has never names its frames")
		again = report()
		install_ied.install_symbols(self.bin, self.target, again)
		self.assertEqual((0, 0), (again["copied"], again["removed"]), "nothing changed, nothing copied")


class ProgramInUseTests(unittest.TestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-install-")
		self.process = None
		self.addCleanup(self.cleanup)

	def cleanup(self):
		if self.process and self.process.poll() is None:
			self.process.kill()
			self.process.wait(10)
		shutil.rmtree(self.root, True)

	def test_a_running_program_is_renamed_aside_and_replaced(self):
		ping = os.path.join(os.environ["SystemRoot"], "System32", "PING.EXE")
		installed = os.path.join(self.root, "ied.exe")
		shutil.copy2(ping, installed)
		self.process = subprocess.Popen([installed, "-n", "30", "127.0.0.1"], stdout=subprocess.DEVNULL)
		time.sleep(0.5)
		newer = os.path.join(self.root, "new", "ied.exe")
		os.makedirs(os.path.dirname(newer))
		shutil.copy2(ping, newer)
		with open(newer, "ab") as f:
			f.write(b"\0" * 16)
		result = report()
		install_ied.copy_program(newer, installed, result)
		self.assertEqual(["ied.exe"], result["in_use"])
		self.assertEqual(os.path.getsize(newer), os.path.getsize(installed), "the new program is in place")
		old = [name for name in os.listdir(self.root) if name.endswith(".old")]
		self.assertEqual(1, len(old), os.listdir(self.root))
		self.process.kill()
		self.process.wait(10)
		install_ied.remove_old(self.root)
		self.assertEqual([], [name for name in os.listdir(self.root) if name.endswith(".old")], "the next install removes it")

	def test_a_service_in_use_is_renamed_aside_in_a_tree(self):
		ping = os.path.join(os.environ["SystemRoot"], "System32", "PING.EXE")
		source = os.path.join(self.root, "source")
		target = os.path.join(self.root, "target")
		write(os.path.join(source, "tor", "geoip"), "data")
		os.makedirs(os.path.join(source, "tor"), exist_ok=True)
		shutil.copy2(ping, os.path.join(source, "tor", "tor.exe"))
		install_ied.mirror(source, target, set(), report())
		self.process = subprocess.Popen([os.path.join(target, "tor", "tor.exe"), "-n", "30", "127.0.0.1"], stdout=subprocess.DEVNULL)
		time.sleep(0.5)
		with open(os.path.join(source, "tor", "tor.exe"), "ab") as f:
			f.write(b"\0" * 16)
		result = report()
		install_ied.mirror(source, target, set(), result)
		self.assertEqual(["tor.exe"], result["in_use"], "a running Tor (or a loaded .pyd) does not stop the install")
		self.assertEqual(os.path.getsize(os.path.join(source, "tor", "tor.exe")), os.path.getsize(os.path.join(target, "tor", "tor.exe")))
		self.process.kill()
		self.process.wait(10)
		install_ied.mirror(source, target, set(), report())
		self.assertEqual(["geoip", "tor.exe"], sorted(os.listdir(os.path.join(target, "tor"))), "the next install removes the old copy")

	def test_an_uninstall_leaves_a_running_install_whole(self):
		installed = os.path.join(self.root, "IED")
		os.makedirs(os.path.join(installed, "frontend"))
		write(os.path.join(installed, "frontend", "index.html"), "<p>IED</p>")
		write(os.path.join(installed, "install.json"), "{}")
		shutil.copy2(os.path.join(os.environ["SystemRoot"], "System32", "PING.EXE"), os.path.join(installed, "ied-server.exe"))
		self.process = subprocess.Popen([os.path.join(installed, "ied-server.exe"), "-n", "30", "127.0.0.1"], stdout=subprocess.DEVNULL)
		time.sleep(0.5)
		with self.assertRaises(SystemExit):
			install_ied.remove_install(installed)
		self.assertTrue(os.path.isfile(os.path.join(installed, "frontend", "index.html")), "nothing removed while it runs")
		self.process.kill()
		self.process.wait(10)
		install_ied.remove_install(installed)
		self.assertEqual([], os.listdir(self.root), "all of it once it stopped")

	def test_a_folder_that_is_no_install_is_left_alone(self):
		write(os.path.join(self.root, "notes.txt"), "mine")
		with self.assertRaises(SystemExit):
			install_ied.remove_install(self.root)
		self.assertTrue(os.path.isfile(os.path.join(self.root, "notes.txt")))

	def test_a_running_ied_is_told_to_restart_for_any_change(self):
		running = [os.path.join(self.root, "IED", "ied-server.exe"), os.path.join(self.root, "IED", "ied-desktop.exe")]
		replaced = dict(report(), copied=3, in_use=["ied-server.exe"])
		self.assertIn("restart it to use the new one", install_ied.restart_note(replaced, running))
		changed = dict(report(), copied=10)
		self.assertIn("(ied-desktop.exe, ied-server.exe) with the plugins it read when it started; restart it", install_ied.restart_note(changed, running),
			"only a manifest or a page changed: the running server still has the old plugins")
		self.assertIsNone(install_ied.restart_note(changed, []), "nothing runs")
		self.assertIsNone(install_ied.restart_note(report(), running), "nothing changed")


if __name__ == "__main__":
	unittest.main()
