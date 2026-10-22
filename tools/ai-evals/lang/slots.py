"""Model server slots shared by every eval run on this machine: a job takes one of a few lock files before its turn and
gives it back after, so runs started side by side never send the server more requests than it has slots (one more
would wait in the server's queue, and IED's prefill watchdog would take the wait for a stall)."""

import asyncio
import ctypes
import os
from ctypes import wintypes

import common

FOLDER = os.path.join(common.WORK, "slots")
STILL_ACTIVE = 259


def process_alive(pid):
	handle = ctypes.WinDLL("kernel32").OpenProcess(0x1000, False, pid)   # PROCESS_QUERY_LIMITED_INFORMATION
	if not handle:
		return False
	code = wintypes.DWORD()
	ctypes.WinDLL("kernel32").GetExitCodeProcess(handle, ctypes.byref(code))
	ctypes.WinDLL("kernel32").CloseHandle(handle)
	return code.value == STILL_ACTIVE


def try_take(index, owner):
	path = os.path.join(FOLDER, "slot-%d.lock" % index)
	try:
		with open(path, encoding="utf-8") as f:
			pid = int(f.read().split()[0])
		if not process_alive(pid):
			os.remove(path)
	except (OSError, ValueError, IndexError):
		pass
	try:
		descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
	except FileExistsError:
		return None
	with os.fdopen(descriptor, "w", encoding="utf-8") as f:
		f.write("%d %s\n" % (os.getpid(), owner))
	return path


class Slot:
	"""async with Slot(count, owner): one of `count` slots, waited for."""

	def __init__(self, count, owner):
		self.count = count
		self.owner = owner
		self.path = None

	async def __aenter__(self):
		os.makedirs(FOLDER, exist_ok=True)
		while self.path is None:
			for index in range(self.count):
				self.path = try_take(index, self.owner)
				if self.path:
					break
			if self.path is None:
				await asyncio.sleep(5)
		return self

	async def __aexit__(self, *exc):
		try:
			os.remove(self.path)
		except OSError:
			pass
