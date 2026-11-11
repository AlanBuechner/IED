"""fsutil.copy_if_newer: a file is copied when the copy is missing or the source changed since it was copied, and not
again while both stay as they are (the copy's stamp is the source's less a nudge, for coarse file times).
fsutil.move_aside_if_running: a running program is renamed so a build can write its file; nothing else moves."""

import os
import shutil
import stat
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

	def test_a_folder_copy_leaves_out_what_it_excludes(self):
		folder = os.path.join(self.root, "lib")
		for relative in ("nuke.toml", "Text/a.ch", "Text/nuke.toml", "Text/b.tmp"):
			os.makedirs(os.path.dirname(os.path.join(folder, relative)), exist_ok=True)
			with open(os.path.join(folder, relative), "w") as f:
				f.write(relative)
		out = os.path.join(self.root, "copied")
		self.assertEqual(1, fsutil.copy_tree_if_newer(folder, out, ("nuke.toml", "Text/*.tmp")))
		self.assertEqual(["a.ch"], os.listdir(os.path.join(out, "Text")), "a name matches at any depth, a path from the folder")

	def test_a_copy_someone_changed_is_replaced(self):
		fsutil.copy_if_newer(self.source, self.dest)
		with open(self.dest, "w") as f:
			f.write("hand edit")
		self.assertTrue(fsutil.copy_if_newer(self.source, self.dest), "not the copy it made: copied")

	def test_a_read_only_source_gives_a_copy_that_can_be_replaced_and_cleaned(self):
		os.chmod(self.source, stat.S_IREAD)
		try:
			self.assertTrue(fsutil.copy_if_newer(self.source, self.dest))
			self.assertTrue(os.access(self.dest, os.W_OK), "the copy is writable")
			stamp = time.time() + 5
			os.utime(self.source, (stamp, stamp))
			self.assertTrue(fsutil.copy_if_newer(self.source, self.dest), "copied again over the old copy")
			self.assertTrue(fsutil.remove_tree(os.path.dirname(self.dest)), "and the folder is cleaned")
			self.assertFalse(os.path.exists(self.dest))
		finally:
			os.chmod(self.source, stat.S_IREAD | stat.S_IWRITE)

	def test_a_folder_holding_read_only_files_is_removed(self):
		folder = os.path.join(self.root, "bin", "D3D12")
		os.makedirs(folder)
		locked = os.path.join(folder, "D3D12Core.dll")
		with open(locked, "w") as f:
			f.write("dll")
		os.chmod(locked, stat.S_IREAD)
		self.assertTrue(fsutil.remove_tree(os.path.join(self.root, "bin")))
		self.assertFalse(os.path.exists(folder))
		self.assertFalse(fsutil.remove_tree(os.path.join(self.root, "bin")), "nothing left to remove")


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
