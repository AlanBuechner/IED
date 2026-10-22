"""fsutil.copy_if_newer: a file is copied when the copy is missing or the source changed since it was copied, and not
again while both stay as they are (the copy's stamp is the source's less a nudge, for coarse file times).
fsutil.move_aside_if_running: a running program is renamed so a build can write its file; nothing else moves."""

import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

import support  # noqa: F401
from nuke import fsutil


class CopyIfNewerTests(unittest.TestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="nuke-fs-")
		self.source = os.path.join(self.root, "a.txt")
		self.dest = os.path.join(self.root, "out", "a.txt")
		with open(self.source, "w") as f:
			f.write("one")

	def tearDown(self):
		shutil.rmtree(self.root, ignore_errors=True)

	def test_copied_once_while_nothing_changes(self):
		self.assertTrue(fsutil.copy_if_newer(self.source, self.dest), "missing: copied")
		self.assertFalse(fsutil.copy_if_newer(self.source, self.dest), "unchanged: not again")
		self.assertFalse(fsutil.copy_if_newer(self.source, self.dest))

	def test_a_changed_source_is_copied_again(self):
		fsutil.copy_if_newer(self.source, self.dest)
		with open(self.source, "w") as f:
			f.write("two, longer")
		stamp = time.time() + 5
		os.utime(self.source, (stamp, stamp))
		self.assertTrue(fsutil.copy_if_newer(self.source, self.dest), "edited: copied")
		with open(self.dest) as f:
			self.assertEqual("two, longer", f.read())

	def test_a_copy_someone_changed_is_replaced(self):
		fsutil.copy_if_newer(self.source, self.dest)
		with open(self.dest, "w") as f:
			f.write("hand edit")
		self.assertTrue(fsutil.copy_if_newer(self.source, self.dest), "not the copy it made: copied")


if __name__ == "__main__":
	unittest.main()


class MoveAsideTests(unittest.TestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="nuke-aside-")
		self.program = os.path.join(self.root, "game.exe")
		shutil.copy2(sys.executable, self.program)

	def tearDown(self):
		shutil.rmtree(self.root, ignore_errors=True)

	def test_a_program_that_is_not_running_stays(self):
		self.assertIsNone(fsutil.move_aside_if_running(self.program))
		self.assertTrue(os.path.isfile(self.program))
		self.assertIsNone(fsutil.move_aside_if_running(os.path.join(self.root, "missing.exe")), "nothing to move")

	@unittest.skipUnless(sys.platform == "win32", "only Windows refuses to write a running program")
	def test_a_running_program_is_moved_aside_and_its_file_written(self):
		running = subprocess.Popen([self.program, "-c", "import time; time.sleep(60)"])
		try:
			for _ in range(50):
				try:
					with open(self.program, "r+b"):
						time.sleep(0.05)
				except PermissionError:
					break
			aside = fsutil.move_aside_if_running(self.program)
			self.assertIsNotNone(aside, "it runs")
			self.assertTrue(aside.endswith(".old") and os.path.isfile(aside))
			with open(self.program, "wb") as f:
				f.write(b"the new build")
			self.assertIsNone(running.poll(), "the old one keeps running")
		finally:
			running.kill()
			running.wait(10)
		self.assertIsNone(fsutil.move_aside_if_running(self.program), "the new one is not running")
		self.assertFalse(os.path.exists(aside), "the old copy goes once nothing runs it")
