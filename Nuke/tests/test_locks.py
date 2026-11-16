"""Nuke processes on one machine (each IED window's workspace server runs its own, docs/01 in IED): the machine lock of
locks.py holds one process at a time and goes with a process that dies; the package cache's mirror is cloned once when
two Nukes clone it at the same moment, and fetches into it take turns; a file written with write_if_changed is never
seen half written."""

import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest

from support import NUKE_DIR

from nuke import fsutil, locks
from nuke.errors import NukeError

HOLDER = r"""
import sys, time
sys.path.insert(0, sys.argv[1])
from nuke import locks
with locks.held(sys.argv[2], "the holder"):
	print("held", flush=True)
	time.sleep(float(sys.argv[3]))
"""

MIRROR = r"""
import sys
sys.path.insert(0, sys.argv[1])
from nuke.packages import git
try:
	if sys.argv[2] == "clone":
		print("made" if git.ensure_mirror(sys.argv[3], sys.argv[4]) else "found", flush=True)
	else:
		git.fetch(sys.argv[3], sys.argv[4])
		print("fetched", flush=True)
except Exception as error:
	print("failed: %s" % error, flush=True)
	sys.exit(1)
"""


READER = r"""
import os, sys, time
path, stop = sys.argv[1], sys.argv[2]
seen = set()
print("reading", flush=True)
while not os.path.exists(stop):
	try:
		with open(path, encoding="utf-8") as f:
			seen.add(len(f.read()))
	except OSError:
		pass
	time.sleep(0.001)
print(" ".join(str(n) for n in sorted(seen)), flush=True)
"""


def git(*args, cwd=None):
	env = dict(os.environ, GIT_AUTHOR_NAME="Test", GIT_AUTHOR_EMAIL="test@example.com", GIT_COMMITTER_NAME="Test",
		GIT_COMMITTER_EMAIL="test@example.com")
	result = subprocess.run(["git", *args], cwd=cwd, env=env, capture_output=True, text=True)
	if result.returncode != 0:
		raise AssertionError(f"git {' '.join(args)}: {result.stderr}")
	return result.stdout.strip()


def slow_git(folder, seconds, verb):
	"""A git that waits `seconds` before each `verb` (clone, fetch), so two Nukes overlap for sure."""
	real = shutil.which("git")
	if os.name == "nt":
		path = os.path.join(folder, "slow-git.cmd")
		pings = int(seconds) + 1
		with open(path, "w", newline="\r\n") as f:
			f.write(f'@echo off\nif "%1"=="{verb}" ping -n {pings} 127.0.0.1 > nul\n"{real}" %*\nexit /b %errorlevel%\n')
	else:
		path = os.path.join(folder, "slow-git.sh")
		with open(path, "w", newline="\n") as f:
			f.write(f'#!/bin/sh\nif [ "$1" = "{verb}" ]; then sleep {seconds}; fi\nexec "{real}" "$@"\n')
		os.chmod(path, 0o755)
	return path


class LockTests(unittest.TestCase):
	def key(self, name):
		return f"nuke-tests:{name}:{os.getpid()}:{time.time()}"

	def hold_in_another_process(self, key, seconds):
		child = subprocess.Popen([sys.executable, "-I", "-c", HOLDER, NUKE_DIR, key, str(seconds)], stdout=subprocess.PIPE, text=True)
		self.assertEqual("held", child.stdout.readline().strip())
		return child

	def test_one_process_at_a_time(self):
		key = self.key("one")
		child = self.hold_in_another_process(key, 1.5)
		try:
			with self.assertRaises(NukeError) as refused:
				with locks.held(key, "fetching x", wait=0.3):
					pass
			self.assertIn("fetching x: another Nuke has held it", str(refused.exception))
			told = []
			started = time.monotonic()
			with locks.held(key, "fetching x", wait=30, waiting=lambda: told.append(1)):
				waited = time.monotonic() - started
			self.assertGreater(waited, 0.5, "it waited for the holder")
			self.assertEqual([1], told, "and said so once")
		finally:
			child.wait(30)
			child.stdout.close()

	def test_a_killed_holder_lets_go(self):
		key = self.key("killed")
		child = self.hold_in_another_process(key, 60)
		child.kill()
		child.wait(30)
		child.stdout.close()
		started = time.monotonic()
		with locks.held(key, "x", wait=10):
			pass
		self.assertLess(time.monotonic() - started, 5, "nothing to clear by hand")

	def test_one_process_takes_it_again_and_its_threads_take_turns(self):
		key = self.key("again")
		with locks.held(key, "outer"):
			with locks.held(key, "inner", wait=0):
				pass
		order = []

		def worker(name):
			with locks.held(key, name):
				order.append(name + " in")
				time.sleep(0.2)
				order.append(name + " out")

		threads = [threading.Thread(target=worker, args=(n,)) for n in ("a", "b")]
		for t in threads:
			t.start()
		for t in threads:
			t.join()
		self.assertEqual(4, len(order))
		self.assertTrue(order[0].endswith(" in") and order[1] == order[0].replace(" in", " out"), order)


class MirrorTests(unittest.TestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="nuke-locks-")
		self.url = os.path.join(self.root, "remote.git").replace("\\", "/")
		work = os.path.join(self.root, "work")
		git("init", "--quiet", "--bare", "-b", "main", self.url)
		git("clone", "--quiet", self.url, work)
		git("checkout", "--quiet", "-b", "main", cwd=work)
		with open(os.path.join(work, "a.txt"), "w") as f:
			f.write("a\n")
		git("add", "-A", cwd=work)
		git("commit", "--quiet", "-m", "first", cwd=work)
		git("push", "--quiet", "origin", "main", cwd=work)
		self.work = work
		self.mirror = os.path.join(self.root, "cache", "git", "remote-0123.git")

	def tearDown(self):
		shutil.rmtree(self.root, ignore_errors=True)

	def two_at_once(self, verb, seconds):
		env = dict(os.environ, NUKE_TOOL_GIT=slow_git(self.root, seconds, verb))
		started = time.monotonic()
		children = [subprocess.Popen([sys.executable, "-I", "-c", MIRROR, NUKE_DIR, verb, self.mirror, self.url], stdout=subprocess.PIPE,
			text=True, env=env) for _ in range(2)]
		said = []
		for child in children:
			said.append(child.stdout.read().strip())
			child.wait(120)
			child.stdout.close()
		return said, [c.returncode for c in children], time.monotonic() - started

	def test_two_first_clones_make_one_mirror(self):
		said, codes, _ = self.two_at_once("clone", 2)
		self.assertEqual([0, 0], codes, said)
		self.assertEqual(["found", "made"], sorted(said), "one clones it, the other finds it made")
		self.assertTrue(os.path.isfile(os.path.join(self.mirror, "HEAD")))
		self.assertFalse(os.path.exists(self.mirror + ".partial"), "nothing left half made")

	def test_two_fetches_into_one_mirror_take_turns(self):
		from nuke.packages import git as nuke_git
		nuke_git.ensure_mirror(self.mirror, self.url)
		git("checkout", "--quiet", "-b", "feature", cwd=self.work)
		git("commit", "--quiet", "--allow-empty", "-m", "second", cwd=self.work)
		git("push", "--quiet", "origin", "feature", cwd=self.work)
		said, codes, took = self.two_at_once("fetch", 1)
		self.assertEqual([0, 0], codes, said)
		self.assertGreater(took, 1.9, "the second waited for the first: %.1f s" % took)
		self.assertTrue(nuke_git.run(["rev-parse", "--verify", "--quiet", "refs/heads/feature"], cwd=self.mirror, check=False), "the branch came")


class WriteTests(unittest.TestCase):
	def test_a_reader_never_sees_a_file_half_written(self):
		folder = tempfile.mkdtemp(prefix="nuke-write-")
		path = os.path.join(folder, "Big.iedproj")
		texts = ["A" * 3000000, "B" * 3000000]
		fsutil.write_if_changed(path, texts[0])
		# Written beside and renamed into place: a new file each time, never the old one cut to nothing and filled again.
		before = os.stat(path).st_ino
		self.assertTrue(fsutil.write_if_changed(path, texts[1]))
		self.assertNotEqual(before, os.stat(path).st_ino, "the file was replaced, not written in place")
		# Another process reads it the whole time, as IED reads a generated project file while Nuke writes it.
		stop = os.path.join(folder, "stop")
		reader = subprocess.Popen([sys.executable, "-I", "-c", READER, path, stop], stdout=subprocess.PIPE, text=True)
		self.assertEqual("reading", reader.stdout.readline().strip())
		try:
			for i in range(2, 122):
				self.assertTrue(fsutil.write_if_changed(path, texts[i % 2]))
		finally:
			with open(stop, "w"):
				pass
			seen = reader.stdout.read().split()
			reader.wait(30)
			reader.stdout.close()
			os.remove(stop)
		self.assertEqual(["3000000"], seen, "only whole files are read")
		self.assertEqual(["Big.iedproj"], os.listdir(folder), "no file left beside it")
		shutil.rmtree(folder, ignore_errors=True)


if __name__ == "__main__":
	unittest.main()
