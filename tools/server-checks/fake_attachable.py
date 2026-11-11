"""A program that profiles and was started without IED_PROFILER, as IED's Attach to Process finds one (docs/16
"Attaching to a running program"): it listens on the local pipe ied-itp-<its pid> and answers one line each way -
hello, attach <target> (it then streams a small capture there over the IED Trace Protocol and keeps the connection
open), detach, and for the checks spawn <app> (a child of its own, started now). With --child <app> it first starts a
copy of itself as its child. It prints "ready <pid>" once it listens, and runs until its standard input closes.

	python -I tools/server-checks/fake_attachable.py <app> [--child <app>]
"""

import _winapi
import os
import socket
import subprocess
import sys
import threading

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "pylib"))
import itp  # noqa: E402

MS = 1_000_000
PIPE_ACCESS_DUPLEX = 0x3
PIPE_TYPE_BYTE = 0x0
PIPE_WAIT = 0x0
PIPE_UNLIMITED_INSTANCES = 255


def stream(app, token):
	"""Hello, a thread with a few zones and frames: enough to be a live capture."""
	w = itp.Writer().hello(app=app, pid=os.getpid(), freq=1_000_000_000, start_ticks=1000, start_unix_ns=1_700_000_000_000_000_000, token=token)
	defs = w.chunk(itp.GLOBAL_CHUNK, 0, 1000)
	tick = w.srcloc("Tick", defs, function="Main", file="src/main.cf", line=3)
	main = w.string("Main Thread", defs)
	defs.event("ThreadDef", threadId=1, osThreadId=100, groupHint=0)
	w.add(defs)
	chunk = w.chunk(itp.THREAD_CHUNK, 1, 1000).event("ThreadName", 1000, nameStr=main)
	for i in range(3):
		start = 1000 + i * 16 * MS
		chunk.event("FrameMark", start, frameSetStr=0)
		chunk.event("ZoneBegin", start + MS, srcLocId=tick).event("ZoneEnd", start + 5 * MS)
	w.add(chunk)
	return w.bytes()


class Attachable:
	def __init__(self, app):
		self.app = app
		self.sock = None
		self.children = []

	def answer(self, line):
		if line == "hello":
			return "itp-attach 1 %s %s" % ("attached" if self.sock else "idle", self.app)
		if line.startswith("attach "):
			target = line[7:].strip()
			if not target.startswith("127.0.0.1:"):
				return "error the target is not on this machine"
			if self.sock:
				return "busy"
			address, _, token = target.partition(";token=")
			host, port = address.rsplit(":", 1)
			try:
				self.sock = socket.create_connection((host, int(port)), timeout=10)
				self.sock.sendall(stream(self.app, token))
			except OSError as e:
				self.sock = None
				return "error " + str(e)
			return "ok"
		if line.startswith("spawn "):
			# For the checks: a child started after an attach, which IED follows.
			late = subprocess.Popen([sys.executable, "-I", os.path.abspath(__file__), line[6:].strip()], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
			self.children.append(late)
			return "ok " + late.stdout.readline().decode().split()[1]
		if line == "detach":
			if self.sock:
				self.sock.close()
				self.sock = None
			return "ok"
		return "error not a request"

	def serve(self):
		name = "\\\\.\\pipe\\ied-itp-%d" % os.getpid()
		first = True
		while True:
			pipe = _winapi.CreateNamedPipe(name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, _winapi.NULL)
			if first:
				print("ready %d" % os.getpid(), flush=True)
				first = False
			try:
				_winapi.ConnectNamedPipe(pipe, False)
			except OSError:
				pass
			try:
				data = b""
				while b"\n" not in data:
					chunk, _ = _winapi.ReadFile(pipe, 4096)
					if not chunk:
						break
					data += chunk
				line = data.split(b"\n")[0].decode("utf-8", "replace").strip()
				if line:
					_winapi.WriteFile(pipe, (self.answer(line) + "\n").encode())
			except OSError:
				pass
			finally:
				_winapi.CloseHandle(pipe)


def main():
	app = sys.argv[1]
	child = None
	if "--child" in sys.argv:
		child = subprocess.Popen([sys.executable, "-I", os.path.abspath(__file__), sys.argv[sys.argv.index("--child") + 1]], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
		print("child %s" % child.stdout.readline().decode().split()[1], flush=True)
	attachable = Attachable(app)
	threading.Thread(target=attachable.serve, daemon=True).start()
	sys.stdin.read()
	for late in attachable.children:
		late.stdin.close()
	if child:
		child.stdin.close()
		child.wait(5)


main()
