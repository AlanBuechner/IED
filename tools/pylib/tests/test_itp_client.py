"""The C/C++ ITP client (C4/Frost/Projects/Profiling/src/Profiling/Itp.cpp) built and run: what it writes to a trace
file and streams to a listener reads back as docs/16 says. Skipped without a C++ compiler (the vendored clang, else
clang++ on the PATH)."""

import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import itp  # noqa: E402

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
CLIENT = ROOT / "C4" / "Frost" / "Projects" / "Profiling" / "src"
VENDORED = ROOT / "C4" / "Frost" / "vendor" / "Compiler" / "bin" / ("clang++.exe" if os.name == "nt" else "clang++")
WARNING = 3  # itp_severity's ITP_WARNING
BUILT = {}


def compiler():
	if VENDORED.exists():
		return str(VENDORED)
	return shutil.which("clang++")


def setUpModule():
	cxx = compiler()
	if not cxx:
		raise unittest.SkipTest("no C++ compiler (the vendored clang or clang++)")
	BUILT["dir"] = pathlib.Path(tempfile.mkdtemp(prefix="itp-client-"))
	BUILT["exe"] = build(cxx, BUILT["dir"])


def tearDownModule():
	shutil.rmtree(BUILT["dir"], ignore_errors=True)


def build(cxx, out_dir):
	exe = out_dir / ("itp_client.exe" if os.name == "nt" else "itp_client")
	args = [cxx, "-std=c++20", "-O2", "-I", str(CLIENT), str(HERE / "itp_client" / "client.cpp"),
		str(CLIENT / "Profiling" / "Itp.cpp"), "-o", str(exe)]
	if os.name != "nt":
		args.append("-pthread")
	result = subprocess.run(args, capture_output=True, text=True, timeout=300)
	if result.returncode != 0:
		raise AssertionError("the client does not build:\n" + result.stdout + result.stderr)
	return exe


def listen():
	server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	server.bind(("127.0.0.1", 0))
	server.listen(1)
	server.settimeout(60)
	received = bytearray()

	def serve():
		connection, _ = server.accept()
		with connection:
			while True:
				data = connection.recv(65536)
				if not data:
					return
				received.extend(data)

	thread = threading.Thread(target=serve, daemon=True)
	thread.start()
	return server, thread, received


def names(events):
	strings = itp.strings_of(events)
	sites = {e["id"]: strings.get(e["nameStr"]) for e in events if e["name"] == "SrcLocDef"}
	return strings, sites


class FileTraceTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		trace = BUILT["dir"] / "run.iedtrace"
		subprocess.run([str(BUILT["exe"]), "file:" + str(trace)], check=True, capture_output=True, timeout=120)
		cls.events = itp.read(trace.read_bytes())
		cls.strings, cls.sites = names(cls.events)

	def of(self, name):
		return [e for e in self.events if e["name"] == name]

	def test_the_stream_opens_with_hello_and_ends_with_bye(self):
		hello = self.events[0]
		self.assertEqual(("Hello", "itp-client-test"), (hello["name"], hello["appName"]))
		self.assertGreater(hello["clockFreqHz"], 1_000_000)
		self.assertEqual("Bye", self.events[-1]["name"])
		self.assertEqual([], self.of("Unknown"))

	def test_every_id_is_defined_before_its_first_use(self):
		strings, sites, plots, pools = set(), set(), set(), set()
		uses = {"ZoneBegin": [("srcLocId", sites)], "ZoneBeginNamed": [("srcLocId", sites), ("nameStr", strings)],
			"ZoneText": [("strId", strings)], "ThreadName": [("nameStr", strings)], "Message": [("strId", strings)],
			"TaskBegin": [("nameStr", strings)], "PlotValue": [("plotId", plots)], "PlotDef": [("nameStr", strings)],
			"PoolDef": [("nameStr", strings)], "SrcLocDef": [("nameStr", strings), ("fileStr", strings)],
			"FrameBegin": [("frameSetStr", strings)], "FrameEnd": [("frameSetStr", strings)]}
		for event in self.events:
			for field, defined in uses.get(event["name"], []):
				if event[field] != 0:
					self.assertIn(event[field], defined, "%s uses %s %d before its definition" % (event["name"], field, event[field]))
			if event["name"] == "StringDef":
				strings.add(event["id"])
			elif event["name"] == "SrcLocDef":
				sites.add(event["id"])
			elif event["name"] == "PlotDef":
				plots.add(event["plotId"])
			elif event["name"] == "PoolDef":
				pools.add(event["poolId"])
		for alloc in self.of("Alloc") + self.of("Free"):
			if alloc["poolId"] != 0:
				self.assertIn(alloc["poolId"], pools)

	def test_zones_nest_and_times_only_grow_on_each_thread(self):
		depth, last, zones = {}, {}, {}
		for event in self.events:
			if event["kind"] != itp.THREAD_CHUNK or "t" not in event:
				continue
			thread = event["threadId"]
			self.assertGreaterEqual(event["t"], last.get(thread, 0), "time went back on thread %d" % thread)
			last[thread] = event["t"]
			if event["name"] == "ZoneBegin":
				zones.setdefault(thread, []).append(self.sites[event["srcLocId"]])
			elif event["name"] == "ZoneBeginNamed":
				zones.setdefault(thread, []).append(self.strings[event["nameStr"]])
			if event["name"].startswith("ZoneBegin"):
				depth[thread] = depth.get(thread, 0) + 1
			elif event["name"] == "ZoneEnd":
				depth[thread] = depth.get(thread, 0) - 1
				self.assertGreaterEqual(depth[thread], 0)
		self.assertEqual({0}, set(depth.values()))
		main = [names for names in zones.values() if "Update" in names]
		self.assertEqual(1, len(main))
		self.assertEqual(["Spin"] + ["Update", "level.bin", "Spin"] * 5, main[0])
		workers = [names for names in zones.values() if "Physics::Step" in names]
		self.assertEqual(5, len(workers))
		for names in workers:
			self.assertEqual(["Physics::Step", "Spin"], names)

	def test_a_named_zone_carries_its_text_and_value(self):
		self.assertEqual(["bytes=4096"] * 5, [self.strings[e["strId"]] for e in self.of("ZoneText")])
		self.assertEqual([4096] * 5, [e["value"] for e in self.of("ZoneValue")])

	def test_threads_frames_plots_messages_and_tasks(self):
		self.assertEqual(["Main"] + ["Worker"] * 5, [self.strings[e["nameStr"]] for e in self.of("ThreadName")])
		self.assertEqual([0] * 5, [e["frameSetStr"] for e in self.of("FrameMark")])
		self.assertEqual(["Loading"], [self.strings[e["frameSetStr"]] for e in self.of("FrameBegin")])
		self.assertEqual(["Loading"], [self.strings[e["frameSetStr"]] for e in self.of("FrameEnd")])
		self.assertEqual([0.0, 1.0, 2.0, 3.0, 4.0], [e["value"] for e in self.of("PlotValue")])
		self.assertEqual([(WARNING, "done")], [(e["severity"], self.strings[e["strId"]]) for e in self.of("Message")])
		begun = {e["taskId"]: self.strings[e["nameStr"]] for e in self.of("TaskBegin")}
		self.assertEqual(["Physics"] * 5, list(begun.values()))
		self.assertEqual(set(begun), {e["taskId"] for e in self.of("TaskEnd")})

	def test_operator_new_is_reported_without_recursing(self):
		heap = [e for e in self.of("Alloc") if e["poolId"] == 0]
		self.assertGreaterEqual(len(heap), 5 * 16)
		arena = [e for e in self.events if e["name"] in ("Alloc", "Free") and e["poolId"] == 2]
		self.assertEqual(["Alloc", "Free"], [e["name"] for e in arena])
		self.assertEqual(arena[0]["address"], arena[1]["address"])
		self.assertEqual(64, arena[0]["size"])
		self.assertEqual(["Arena"], [self.strings[e["nameStr"]] for e in self.of("PoolDef") if e["poolId"] == 2])


class StartTests(unittest.TestCase):
	def test_a_program_connects_from_its_environment_and_ends_at_exit(self):
		server, thread, received = listen()
		with server:
			port = server.getsockname()[1]
			env = dict(os.environ, IED_PROFILER="127.0.0.1:%d;token=secret" % port)
			subprocess.run([str(BUILT["exe"])], check=True, capture_output=True, timeout=120, env=env)
			thread.join(60)
		events = itp.read(bytes(received))
		self.assertEqual(("Hello", "secret"), (events[0]["name"], events[0]["token"]))
		self.assertEqual("Bye", events[-1]["name"])
		self.assertEqual(5, len([e for e in events if e["name"] == "FrameMark"]))

	def test_an_unreachable_or_malformed_target_is_refused(self):
		for target in ("not-an-address", "file:" + str(BUILT["dir"] / "missing" / "x.iedtrace")):
			result = subprocess.run([str(BUILT["exe"]), target], capture_output=True, text=True, timeout=60)
			self.assertEqual(2, result.returncode, target)
			self.assertIn("start failed:", result.stdout)


if __name__ == "__main__":
	unittest.main()
