"""The installer (tools/install/install_ied.py): a mirror copies what changed, leaves out what it skips and removes what
is gone; a program in use is renamed aside and replaced, and the next install removes the old copy."""

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


if __name__ == "__main__":
	unittest.main()
