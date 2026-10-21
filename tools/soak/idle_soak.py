"""The M1 idle soak (docs/18 "M1 exit", docs/19 "Soak / perf"): a real ied-server with a workspace and three connected
clients (desktop, tablet, phone), idle but for their pings and a light request now and then, for --hours (24 by
default). Each --interval it samples the server's memory and handles. It fails when the server exits, a client is
dropped or sees a protocol violation, a request fails, or memory ends above the budget (docs/19: 300 MB idle) or grows
past --growth percent of the level after the warm-up.

	python -I tools/soak/idle_soak.py --server build/bin/Development-windows-x64/ied-server/ied-server.exe [--hours 24]

The server runs from a copy, so the build output stays free to rebuild. A JSON report goes to build/soak/.
"""

import argparse
import asyncio
import ctypes
import ctypes.wintypes
import datetime
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools", "pylib"))
sys.path.insert(0, os.path.join(REPO, "tools", "conformance"))

from frames import FrameChecker  # noqa: E402
from iedclient import Client  # noqa: E402

MB = 1024 * 1024
BUDGET_MB = 300
TOPICS = ["server", "plugins", "ws:w1", "out:w1:ied"]
DEVICE_CLASSES = ("desktop", "tablet", "phone")


class ProcessMemoryCounters(ctypes.Structure):
	_fields_ = [("cb", ctypes.wintypes.DWORD), ("PageFaultCount", ctypes.wintypes.DWORD), ("PeakWorkingSetSize", ctypes.c_size_t),
		("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
		("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t),
		("PeakPagefileUsage", ctypes.c_size_t), ("PrivateUsage", ctypes.c_size_t)]


def process_sample(pid):
	"""(working set bytes, private bytes, handles) of a process."""
	kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
	psapi = ctypes.WinDLL("psapi", use_last_error=True)
	handle = kernel32.OpenProcess(0x1000 | 0x0010, False, pid)
	if not handle:
		raise OSError("cannot open process %d" % pid)
	try:
		counters = ProcessMemoryCounters()
		counters.cb = ctypes.sizeof(counters)
		psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb)
		handles = ctypes.wintypes.DWORD()
		kernel32.GetProcessHandleCount(handle, ctypes.byref(handles))
		return counters.WorkingSetSize, counters.PrivateUsage, handles.value
	finally:
		kernel32.CloseHandle(handle)


class JobLimits(ctypes.Structure):
	"""JOBOBJECT_EXTENDED_LIMIT_INFORMATION; only LimitFlags is set."""
	_fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64), ("LimitFlags", ctypes.wintypes.DWORD),
		("MinimumWorkingSetSize", ctypes.c_size_t), ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", ctypes.wintypes.DWORD),
		("Affinity", ctypes.c_size_t), ("PriorityClass", ctypes.wintypes.DWORD), ("SchedulingClass", ctypes.wintypes.DWORD),
		("IoCounters", ctypes.c_uint64 * 6), ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
		("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]


def bind_to_this_process(child):
	"""Puts `child` in a job that ends it when this script ends, however it ends (a killed soak left its server
	running before). Returns the job handle, which must stay open."""
	kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
	kernel32.CreateJobObjectW.restype = ctypes.wintypes.HANDLE
	job = kernel32.CreateJobObjectW(None, None)
	limits = JobLimits()
	limits.LimitFlags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
	kernel32.SetInformationJobObject(job, 9, ctypes.byref(limits), ctypes.sizeof(limits))  # JobObjectExtendedLimitInformation
	if not kernel32.AssignProcessToJobObject(job, ctypes.wintypes.HANDLE(child._handle)):
		raise SystemExit("the server could not be bound to this script (error %d)" % ctypes.get_last_error())
	return job


class Soak:
	def __init__(self, args):
		self.args = args
		self.root = tempfile.mkdtemp(prefix="ied-soak-")
		self.samples = []
		self.failures = []
		self.process = None
		self.job = None
		self.clients = []

	def start_server(self):
		executable = os.path.join(self.root, "ied-server.exe")
		shutil.copy2(self.args.server, executable)
		workspace = os.path.join(self.root, "workspace")
		shutil.copytree(os.path.join(REPO, "tests", "e2e", "fixture", "workspace"), workspace)
		argv = [executable, "--host", "127.0.0.1", "--port", "0", "--data", os.path.join(self.root, "data"), "--control", "ied-soak-%d" % os.getpid(),
			"--home", REPO, "--static", os.path.join(REPO, "frontend"), "--quiet", "--workspace", workspace]
		self.process = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
		self.job = bind_to_this_process(self.process)
		listening = self.process.stdout.readline().strip()
		opening = self.process.stdout.readline().strip()
		if not listening.startswith("LISTENING ") or not opening.startswith("OPEN "):
			raise SystemExit("ied-server did not start: %s %s" % (listening, opening))
		url = urllib.parse.urlsplit(listening.split(" ", 1)[1])
		self.host, self.port = url.hostname, url.port
		self.cookie = sign_in(self.host, self.port, opening.split(" ", 1)[1])

	async def connect(self):
		for device_class in DEVICE_CLASSES:
			client = await Client.connect("ws://%s:%d/ws" % (self.host, self.port), FrameChecker(), device_class=device_class, headers={"Cookie": self.cookie})
			await client.sub(TOPICS + ["layout:w1:" + device_class])
			self.clients.append(client)

	async def tick(self, n):
		for client in self.clients:
			if client.closed.is_set():
				self.fail("client %s was dropped (close %s)" % (client.client_id, client.close_code))
				continue
			await client.send({"t": "ping", "ts": int(time.time() * 1000)})
			await asyncio.wait_for(client.pongs.get(), 30)
			for state in client.topics.values():
				state.frames.clear()
				state.applied.clear()
				while not state.queue.empty():
					state.queue.get_nowait()
			if client.violations:
				self.fail("protocol violations: %s" % client.violations[:3])
				client.violations.clear()
		first = self.clients[0]
		await first.request("server.info")
		await first.request("fs.list", {"wsId": "w1"})
		if n % 10 == 0:
			second = self.clients[1]
			snapshot = await second.request("ws.list")
			if not snapshot["workspaces"]:
				self.fail("the workspace is gone")

	def sample(self, elapsed):
		if self.process.poll() is not None:
			self.fail("the server exited with %s" % self.process.returncode)
			return
		working, private, handles = process_sample(self.process.pid)
		self.samples.append({"s": round(elapsed), "workingMB": round(working / MB, 1), "privateMB": round(private / MB, 1), "handles": handles})
		print("%8.0f s  working %7.1f MB  private %7.1f MB  handles %5d" % (elapsed, working / MB, private / MB, handles), flush=True)

	def fail(self, message):
		print("FAIL " + message, flush=True)
		self.failures.append(message)

	def judge(self):
		warm = [s for s in self.samples if s["s"] >= self.args.warmup]
		if len(warm) < 2:
			self.fail("too few samples after the warm-up to judge growth")
			return
		base = warm[0]
		last = warm[-1]
		limit = base["privateMB"] * (1 + self.args.growth / 100) + 16
		if last["privateMB"] > limit:
			self.fail("private memory grew from %.1f MB to %.1f MB (limit %.1f)" % (base["privateMB"], last["privateMB"], limit))
		if last["workingMB"] > BUDGET_MB:
			self.fail("working set %.1f MB is over the %d MB budget" % (last["workingMB"], BUDGET_MB))
		if last["handles"] > base["handles"] + 200:
			self.fail("handles grew from %d to %d" % (base["handles"], last["handles"]))

	async def run(self):
		self.start_server()
		try:
			await self.connect()
			await self.idle()
		finally:
			for client in self.clients:
				client.abort()
			await asyncio.sleep(0)
		if not self.failures:
			self.judge()

	async def idle(self):
		started = time.monotonic()
		end = started + self.args.hours * 3600
		n = 0
		while time.monotonic() < end and not self.failures:
			n += 1
			try:
				await self.tick(n)
			except Exception as e:
				self.fail("a request failed: %r" % e)
			self.sample(time.monotonic() - started)
			await asyncio.sleep(self.args.interval)

	def stop(self):
		if self.process and self.process.poll() is None:
			self.process.terminate()
			self.process.wait(10)
		shutil.rmtree(self.root, ignore_errors=True)

	def report(self):
		folder = os.path.join(REPO, "build", "soak")
		os.makedirs(folder, exist_ok=True)
		path = os.path.join(folder, "idle-%s.json" % datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
		with open(path, "w", encoding="utf-8") as f:
			json.dump({"hours": self.args.hours, "interval": self.args.interval, "failures": self.failures, "samples": self.samples}, f, indent="\t")
		return path


def sign_in(host, port, launch_url):
	"""The session cookie the launch token buys (docs/11)."""
	import http.client
	query = urllib.parse.urlsplit(launch_url).query
	connection = http.client.HTTPConnection(host, port, timeout=30)
	connection.request("GET", "/?" + query, headers={"Host": "%s:%d" % (host, port)})
	response = connection.getresponse()
	cookie = response.getheader("Set-Cookie", "")
	connection.close()
	if response.status != 303 or not cookie:
		raise SystemExit("the launch token was refused: %d" % response.status)
	return cookie.split(";", 1)[0]


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--server", required=True, help="the ied-server executable")
	parser.add_argument("--hours", type=float, default=24.0)
	parser.add_argument("--interval", type=float, default=60.0, help="seconds between samples")
	parser.add_argument("--warmup", type=float, default=600.0, help="seconds before the growth baseline")
	parser.add_argument("--growth", type=float, default=20.0, help="allowed growth of private memory after the warm-up, percent")
	args = parser.parse_args(argv)
	soak = Soak(args)
	try:
		asyncio.run(soak.run())
	finally:
		soak.stop()
	path = soak.report()
	print("%s: %d sample(s), %d failure(s); report %s" % ("PASS" if not soak.failures else "FAIL", len(soak.samples), len(soak.failures), path))
	return 1 if soak.failures else 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
