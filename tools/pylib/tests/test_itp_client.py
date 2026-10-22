"""The C/C++ ITP client (C4/Frost/Projects/Profiling/src/Profiling/Itp.cpp) built and run: what it writes to a trace
file and streams to a listener reads back as docs/16 says. Skipped without a C++ compiler (the vendored clang, else
clang++ on the PATH)."""

import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

import itp_clients as clients
from itp_clients import of

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
CLIENT = ROOT / "C4" / "Frost" / "Projects" / "Profiling" / "src"
VENDORED = ROOT / "C4" / "Frost" / "vendor" / "Compiler" / "bin" / ("clang++.exe" if os.name == "nt" else "clang++")
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


class FileTraceTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.events = clients.run_to_file([str(BUILT["exe"])], BUILT["dir"] / "run.iedtrace")
		cls.strings, cls.sites = clients.names(cls.events)

	def test_the_stream_opens_with_hello_and_ends_with_bye(self):
		clients.check_framing(self, self.events, "itp-client-test")

	def test_every_id_is_defined_before_its_first_use(self):
		clients.check_definitions_precede_use(self, self.events)

	def test_the_clock_is_finer_than_the_work(self):
		clients.check_zones_take_time(self, self.events, "Spin")

	def test_zones_nest_and_times_only_grow_on_each_thread(self):
		zones = clients.zones_by_track(self, self.events)
		main = [names for names in zones.values() if "Update" in names]
		self.assertEqual(1, len(main))
		self.assertEqual(["Spin"] + ["Update", "level.bin", "Spin"] * 5, main[0])
		workers = [names for names in zones.values() if "Physics::Step" in names]
		self.assertEqual(5, len(workers))
		for names in workers:
			self.assertEqual(["Physics::Step", "Spin"], names)

	def test_a_named_zone_carries_its_text_and_value(self):
		self.assertEqual(["bytes=4096"] * 5, [self.strings[e["strId"]] for e in of(self.events, "ZoneText")])
		self.assertEqual([4096] * 5, [e["value"] for e in of(self.events, "ZoneValue")])

	def test_threads_frames_plots_messages_and_tasks(self):
		self.assertEqual(["Main"] + ["Worker"] * 5, clients.thread_names(self.events))
		self.assertEqual([0] * 5, [e["frameSetStr"] for e in of(self.events, "FrameMark")])
		self.assertEqual(["Loading"], [self.strings[e["frameSetStr"]] for e in of(self.events, "FrameBegin")])
		self.assertEqual(["Loading"], [self.strings[e["frameSetStr"]] for e in of(self.events, "FrameEnd")])
		self.assertEqual([0.0, 1.0, 2.0, 3.0, 4.0], [e["value"] for e in of(self.events, "PlotValue")])
		self.assertEqual([(clients.WARNING, "done")], [(e["severity"], self.strings[e["strId"]]) for e in of(self.events, "Message")])
		begun = {e["taskId"]: self.strings[e["nameStr"]] for e in of(self.events, "TaskBegin")}
		self.assertEqual(["Physics"] * 5, list(begun.values()))
		self.assertEqual(set(begun), {e["taskId"] for e in of(self.events, "TaskEnd")})

	def test_a_lockable_mutex_records_its_waits_and_holds(self):
		clients.check_lock(self, self.events, "Queue", 1)

	def test_operator_new_is_reported_without_recursing(self):
		heap = [e for e in of(self.events, "Alloc") if e["poolId"] == 0]
		self.assertGreaterEqual(len(heap), 5 * 16)
		arena = [e for e in self.events if e["name"] in ("Alloc", "Free") and e["poolId"] == 2]
		self.assertEqual(["Alloc", "Free"], [e["name"] for e in arena])
		self.assertEqual(arena[0]["address"], arena[1]["address"])
		self.assertEqual(64, arena[0]["size"])
		self.assertEqual(["Arena"], [self.strings[e["nameStr"]] for e in of(self.events, "PoolDef") if e["poolId"] == 2])


class StartTests(unittest.TestCase):
	def test_a_program_connects_from_its_environment_and_ends_at_exit(self):
		events = clients.run_from_environment([str(BUILT["exe"])], "secret")
		self.assertEqual(("Hello", "secret"), (events[0]["name"], events[0]["token"]))
		self.assertEqual("Bye", events[-1]["name"])
		self.assertEqual(5, len(of(events, "FrameMark")))

	def test_an_unreachable_or_malformed_target_is_refused(self):
		for target in ("not-an-address", "file:" + str(BUILT["dir"] / "missing" / "x.iedtrace")):
			result = subprocess.run([str(BUILT["exe"]), target], capture_output=True, text=True, timeout=60)
			self.assertEqual(2, result.returncode, target)
			self.assertIn("start failed:", result.stdout)


if __name__ == "__main__":
	unittest.main()
