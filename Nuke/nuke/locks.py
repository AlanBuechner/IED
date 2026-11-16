"""Locks between Nuke processes on this machine. The package cache is the machine's (`<nuke home>/.cache/packages`), and
several Nukes reach one mirror at once: each IED window's workspace server runs `nuke package status --fetch` when it
opens and on a timer, and builds fetch too. Git does not let two fetches into one repository both succeed (one fails on a
ref lock), and two first clones of a mirror would delete each other's.

A lock is a byte the operating system locks in a file of its own, under the temp folder: it goes with its process however
that ends, so no stale lock is ever left to clear by hand. Within one process it may be taken again (a fetch inside a
fetch of the same mirror); threads of one process take turns."""

import contextlib
import hashlib
import os
import tempfile
import threading
import time

from .errors import NukeError

WAIT_SECONDS = 1800
POLL_SECONDS = 0.1

_guard = threading.Lock()
_held = {}   # lock file path -> [threading.RLock, how many times this process holds it, the open file]


def folder():
	return os.path.join(tempfile.gettempdir(), "nuke-locks")


def path_for(key):
	"""The lock file of `key` (a path is folded to one spelling first)."""
	text = os.path.normcase(os.path.abspath(key)) if os.path.isabs(key) else key
	return os.path.join(folder(), hashlib.sha1(text.replace("\\", "/").encode("utf-8")).hexdigest()[:24] + ".lock")


if os.name == "nt":
	import msvcrt

	def _try(f):
		f.seek(0)
		try:
			msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
			return True
		except OSError:
			return False

	def _untake(f):
		f.seek(0)
		msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
else:
	import fcntl

	def _try(f):
		try:
			fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
			return True
		except OSError:
			return False

	def _untake(f):
		fcntl.flock(f.fileno(), fcntl.LOCK_UN)


@contextlib.contextmanager
def held(key, what, wait=WAIT_SECONDS, waiting=None):
	"""Holds the machine-wide lock of `key` for the `with` block. While another process holds it, waits at most `wait`
	seconds - calling `waiting()` once when the wait starts - then raises NukeError saying `what` waited."""
	path = path_for(key)
	with _guard:
		entry = _held.setdefault(path, [threading.RLock(), 0, None])
	turn = entry[0]
	turn.acquire()
	try:
		if entry[1] == 0:
			os.makedirs(folder(), exist_ok=True)
			f = open(path, "a+b")
			deadline = time.monotonic() + wait
			told = False
			while not _try(f):
				if time.monotonic() >= deadline:
					f.close()
					raise NukeError(f"{what}: another Nuke has held it for {int(wait)} s (its lock: {path})")
				if not told and waiting:
					waiting()
				told = True
				time.sleep(POLL_SECONDS)
			entry[2] = f
		entry[1] += 1
		try:
			yield
		finally:
			entry[1] -= 1
			if entry[1] == 0:
				f = entry[2]
				entry[2] = None
				try:
					_untake(f)
				finally:
					f.close()
	finally:
		turn.release()
