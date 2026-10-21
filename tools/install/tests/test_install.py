"""The installer (tools/install/install_ied.py): a mirror copies what changed, leaves out what it skips and removes what
is gone; a program in use is renamed aside and replaced, and the next install removes the old copy; the PATH gains the
folder once and loses only it; an uninstall removes the folder whole or, while a program in it runs, not at all."""

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


if __name__ == "__main__":
	unittest.main()
