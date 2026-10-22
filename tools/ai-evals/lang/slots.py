"""Model server slots shared by every eval run on this machine: a job takes one of a few lock files before its turn and
gives it back after, so runs started side by side never send the server more requests than it has slots (one more
would wait in the server's queue, and IED's prefill watchdog would take the wait for a stall)."""

import asyncio
import ctypes
import msvcrt
import os
import time
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


class Exclusive:
	"""Holds the folder's mutex (a byte lock on slots.mutex, which the system drops if its holder dies): runners that
	see the same stale lock otherwise all remove it, and each then believes the slot it re-creates is its own."""

	def __enter__(self):
		self.descriptor = os.open(os.path.join(FOLDER, "slots.mutex"), os.O_RDWR | os.O_CREAT)
		while True:
			try:
				msvcrt.locking(self.descriptor, msvcrt.LK_NBLCK, 1)
				return self
			except OSError:
				time.sleep(0.05)

	def __exit__(self, *exc):
		os.lseek(self.descriptor, 0, os.SEEK_SET)
		msvcrt.locking(self.descriptor, msvcrt.LK_UNLCK, 1)
		os.close(self.descriptor)


def try_take(index, owner):
	path = os.path.join(FOLDER, "slot-%d.lock" % index)
	with Exclusive():
		try:
			with open(path, encoding="utf-8") as f:
				pid = int(f.read().split()[0])
			if not process_alive(pid):
				os.remove(path)
		except (OSError, ValueError, IndexError):
			pass
		try:
			descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
		except (FileExistsError, PermissionError):   # PermissionError: removed but still open somewhere (delete pending)
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
