"""fsutil.copy_if_newer: a file is copied when the copy is missing or the source changed since it was copied, and not
again while both stay as they are (the copy's stamp is the source's less a nudge, for coarse file times)."""

import os
import shutil
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
