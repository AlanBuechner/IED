"""C4's build server (C4.exe --serve-on, C4/docs/compiler/07 "The build server"): one compiler process per program - its
command line and folder - kept between builds, so the build after an edit reuses what the build before made of the files
the edit did not touch, and lowers and emits again only the bodies it changed when it changed nothing else.

A build asks the program's server over a named pipe (a Unix socket on Linux) and streams its answer - the compiler's
output, then `done <exit code>` - as a one-shot compile's output would be. A server is started when none answers: a
process of its own, outside the build's job, that ends after `IDLE_SECONDS` without a build. It is given the compiler's
stamp (its file's time and size) and asked with it, so a server of a compiler since rebuilt answers `stale` and ends, and
a new one is started. When no server can be reached the build is compiled one-shot (`build` returns None). `nuke clean`
ends the server of each program it cleans (`stop`): a server holds its program's folder (its working folder) and a
build's memory until it ends."""

import hashlib
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

IDLE_SECONDS = 1800            # NUKE_C4_SERVER_IDLE overrides it (tests end their servers soon)
CONNECT_SECONDS = 20           # for a server just started to listen
BUSY_SECONDS = 600             # for another build of the same program to end
POLL_SECONDS = 0.05

WINDOWS = sys.platform.startswith("win")
# The servers this process started: they outlive it on purpose, and Python warns of a process object freed while its
# process runs.
_STARTED = []
ERROR_FILE_NOT_FOUND = 2
ERROR_PIPE_BUSY = 231
DETACHED_PROCESS = 0x00000008
CREATE_NEW_PROCESS_GROUP = 0x00000200
CREATE_BREAKAWAY_FROM_JOB = 0x01000000
CREATE_NO_WINDOW = 0x08000000


def identity(argv, cwd):
	"""The server's name: one per command line and folder."""
	key = hashlib.sha256(json.dumps([list(argv), cwd or ""]).encode("utf-8")).hexdigest()[:20]
	return f"c4-serve-{key}"


def stamp(compiler):
	"""The compiler's file as it is now: a server of another compiler is stale."""
	status = os.stat(compiler)
	return f"{status.st_mtime_ns}-{status.st_size}"


def address(name):
	return rf"\\.\pipe\{name}" if WINDOWS else os.path.join(tempfile.gettempdir(), f"{name}.sock")


class _Connection:
	"""Lines to and from a server."""

	def __init__(self, channel):
		self._channel = channel
		self._pending = b""

	def send(self, line):
		data = (line + "\n").encode("utf-8")
		if WINDOWS:
			self._channel.write(data)
			self._channel.flush()
		else:
			self._channel.sendall(data)

	def lines(self):
		while True:
			while b"\n" in self._pending:
				line, self._pending = self._pending.split(b"\n", 1)
				yield line.decode("utf-8", errors="replace").rstrip("\r")
			chunk = self._channel.read(65536) if WINDOWS else self._channel.recv(65536)
			if not chunk:
				if self._pending:
					yield self._pending.decode("utf-8", errors="replace")
				return
			self._pending += chunk

	def close(self):
		self._channel.close()


def _connect(name, wait, started=None):
	"""A connection to the server `name`, waiting `wait` seconds at most for it to listen (and no longer once the process
	`started` for it has ended); None when none answers."""
	deadline = time.monotonic() + wait
	busy_deadline = time.monotonic() + BUSY_SECONDS
	while True:
		try:
			if WINDOWS:
				return _Connection(open(address(name), "r+b", buffering=0))
			channel = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
			try:
				channel.connect(address(name))
			except OSError:
				channel.close()
				raise
			return _Connection(channel)
		except OSError as error:
			busy = getattr(error, "winerror", None) == ERROR_PIPE_BUSY
			if time.monotonic() > (busy_deadline if busy else deadline) or (not busy and started and started.poll() is not None):
				return None
			time.sleep(POLL_SECONDS)


def _start(argv, name, compiler_stamp, cwd, log):
	"""Starts the server `name` for `argv`, out of the build's job where the job lets it go: its process, or None."""
	idle = os.environ.get("NUKE_C4_SERVER_IDLE", str(IDLE_SECONDS))
	command = list(argv) + ["--serve-on", name, "--serve-stamp", compiler_stamp, "--serve-idle", idle]
	quiet = dict(stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=cwd, close_fds=True)
	attempts = [DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB,
		DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW] if WINDOWS else [None]
	for flags in attempts:
		try:
			if WINDOWS:
				process = subprocess.Popen(command, creationflags=flags, **quiet)
			else:
				process = subprocess.Popen(command, start_new_session=True, **quiet)
			log.detail(f"started the C4 build server {name}")
			_STARTED.append(process)
			return process
		except OSError as error:
			log.detail(f"cannot start the C4 build server this way: {error}")
	return None


def build(argv, compiler, log, cwd=None, chatter=None):
	"""Compiles `argv` through its server, starting one when none answers: the compiler's exit code, its output streamed
	to `log` as process.run streams it. None when no server could be reached: the caller compiles one-shot."""
	name = identity(argv, cwd)
	compiler_stamp = stamp(compiler)
	for _ in range(2):  # a stale server ends: the second round starts a new one
		connection = _connect(name, 0)
		if connection is None:
			started = _start(argv, name, compiler_stamp, cwd, log)
			connection = _connect(name, CONNECT_SECONDS, started) if started else None
			if connection is None:
				return None
		answer = _ask(connection, compiler_stamp, log, chatter)
		if answer != STALE:
			return answer
	return None


STALE = "stale"
QUIT = "quit"
BYE = "bye"


def stop(argv, cwd=None):
	"""Ends the server of `argv` when one runs: whether it said it ends (a compiler older than `quit` does not)."""
	connection = _connect(identity(argv, cwd), 0)
	if connection is None:
		return False
	try:
		connection.send(QUIT)
		return any(line == BYE for line in connection.lines())
	except OSError:
		return False
	finally:
		connection.close()


def _ask(connection, compiler_stamp, log, chatter):
	"""One build on a connected server: its exit code; STALE when the server is of another compiler; None when it ended
	without answering."""
	try:
		connection.send(f"build {compiler_stamp}")
		for line in connection.lines():
			if line == STALE:
				return STALE
			if line.startswith("done "):
				return int(line[5:])
			if chatter is None or log.verbose or not chatter.match(line):
				log.write(line + "\n")
		return None
	except (OSError, ValueError):
		return None
	finally:
		connection.close()
