"""The installer (tools/install/install_ied.py): a mirror copies what changed, leaves out what it skips and removes what
is gone; a program in use is renamed aside and replaced, and the next install removes the old copy; the PATH gains the
folder once and loses only it; an uninstall removes the folder whole or, while a program in it runs, not at all."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

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


class ServicesTests(unittest.TestCase):
	def test_setup_installs_the_services_through_the_private_python(self):
		repo = tempfile.mkdtemp(prefix="ied-install-")
		self.addCleanup(shutil.rmtree, repo, True)
		command = install_ied.services_command(repo)
		self.assertEqual(["cmd", "/d", "/c", os.path.join(repo, "setup.bat")], command[:4], "without the private Python, setup.bat installs it first")
		write(os.path.join(repo, "vendor", "python", "python.exe"), "")
		command = install_ied.services_command(repo)
		self.assertEqual([os.path.join(repo, "vendor", "python", "python.exe"), "-I", os.path.join(repo, "tools", "setup", "setup.py")], command[:3])
		self.assertEqual(["--only", "searxng,pip,searxng-packages,tor", "--no-path"], command[3:], "only the services, never PATH")


class RecordTests(unittest.TestCase):
	def test_the_record_names_its_checkout(self):
		root = tempfile.mkdtemp(prefix="ied-install-")
		self.addCleanup(shutil.rmtree, root, True)
		record = install_ied.install_record(os.path.join(root, "IED"), os.path.join(root, "IED", "build", "bin", "Dist-windows-x64"), "a" * 40, True, 1792630000123)
		self.assertEqual({"source", "commit", "dirty", "installedMs", "bin"}, set(record))
		self.assertEqual((root.replace("\\", "/") + "/IED", "a" * 40, True, 1792630000123), (record["source"], record["commit"], record["dirty"], record["installedMs"]))
		self.assertNotIn("\\", record["bin"], "forward slashes")
		install_ied.write_record(root, record)
		with open(os.path.join(root, "install.json"), encoding="utf-8") as f:
			self.assertEqual(record, json.load(f))
		self.assertFalse(os.path.exists(os.path.join(root, "install.json.tmp")), "renamed into place")

	def test_the_checkout_state(self):
		head, dirty = install_ied.checkout_state(install_ied.REPO)
		self.assertRegex(head, r"^[0-9a-f]{40}$", "this checkout's full commit")
		self.assertIsInstance(dirty, bool)


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


@unittest.skipUnless(os.name == "nt", "Windows: a running program's file")
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
