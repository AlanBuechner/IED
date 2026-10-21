"""The Go ITP client (plugins/go/profiler) run through its example: what it writes to a trace file and streams to a
listener reads back as docs/16 says - goroutine tracks, zones at their call sites, the runtime's collections and
plots. Skipped without the go tool."""

import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

import itp_clients as clients
from itp_clients import of

ROOT = pathlib.Path(__file__).resolve().parents[3]
MODULE = ROOT / "plugins" / "go" / "profiler"
BUILT = {}


def setUpModule():
	go = shutil.which("go")
	if not go:
		raise unittest.SkipTest("no go tool")
	BUILT["dir"] = pathlib.Path(tempfile.mkdtemp(prefix="itp-go-"))
	BUILT["exe"] = BUILT["dir"] / ("example.exe" if os.name == "nt" else "example")
	result = subprocess.run([go, "build", "-o", str(BUILT["exe"]), "./example"], cwd=MODULE, capture_output=True, text=True, timeout=300)
	if result.returncode != 0:
		raise AssertionError("the example does not build:\n" + result.stdout + result.stderr)


def tearDownModule():
	shutil.rmtree(BUILT["dir"], ignore_errors=True)


class FileTraceTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.events = clients.run_to_file([str(BUILT["exe"])], BUILT["dir"] / "run.iedtrace")
		cls.strings, cls.sites = clients.names(cls.events)

	def test_the_stream_opens_with_hello_and_ends_with_bye(self):
		clients.check_framing(self, self.events, "itp-go-test")

	def test_every_id_is_defined_before_its_first_use(self):
		clients.check_definitions_precede_use(self, self.events)

	def test_the_clock_is_finer_than_the_work(self):
		clients.check_zones_take_time(self, self.events, "Spin")

	def test_each_goroutine_records_on_its_own_track(self):
		names = clients.thread_names(self.events)
		self.assertEqual(["Main"] + ["Worker"] * 5, names[:6])
		self.assertEqual(7, len(names))
		self.assertRegex(names[6], r"^goroutine \d+$")
		zones = list(clients.zones_by_track(self, self.events).values())
		self.assertIn(["Spin"] + ["Update", "Load", "a.bin", "Spin", "b.bin", "Spin"] * 5, zones)
		self.assertEqual(5, zones.count(["Physics.Step", "Spin"]))
		self.assertIn(["Background", "Spin"], zones)

	def test_zones_are_where_they_were_opened(self):
		sites = {self.strings[e["nameStr"]]: e for e in of(self.events, "SrcLocDef")}
		update = sites["Update"]
		self.assertTrue(self.strings[update["fileStr"]].endswith("example/main.go"))
		self.assertEqual("main.update", self.strings[update["functionStr"]])
		self.assertGreater(update["line"], 0)
		self.assertNotIn("b.bin", sites, "a second name at a site is a named zone, not a site")
		self.assertEqual(["b.bin"] * 5, [self.strings[e["nameStr"]] for e in of(self.events, "ZoneBeginNamed")])

	def test_text_values_frames_plots_messages_tasks_and_pools(self):
		self.assertEqual(["bytes=4096"] * 5, [self.strings[e["strId"]] for e in of(self.events, "ZoneText")])
		self.assertEqual([4096] * 5, [e["value"] for e in of(self.events, "ZoneValue")])
		self.assertEqual(5, len(of(self.events, "FrameMark")))
		self.assertEqual(["Loading"], [self.strings[e["frameSetStr"]] for e in of(self.events, "FrameBegin")])
		plots = {e["plotId"]: self.strings[e["nameStr"]] for e in of(self.events, "PlotDef")}
		frames = [e["value"] for e in of(self.events, "PlotValue") if plots[e["plotId"]] == "Frame"]
		self.assertEqual([0.0, 1.0, 2.0, 3.0, 4.0], frames)
		self.assertEqual([(clients.WARNING, "done")], [(e["severity"], self.strings[e["strId"]]) for e in of(self.events, "Message")])
		begun = {e["taskId"]: self.strings[e["nameStr"]] for e in of(self.events, "TaskBegin")}
		self.assertEqual(["Physics"] * 5, list(begun.values()))
		self.assertEqual(set(begun), {e["taskId"] for e in of(self.events, "TaskEnd")})
		arena = [e for e in self.events if e["name"] in ("Alloc", "Free")]
		self.assertEqual([("Alloc", 2, 64), ("Free", 2, None)], [(e["name"], e["poolId"], e.get("size")) for e in arena])

	def test_the_runtime_reports_collections_heap_and_goroutines(self):
		begins, ends = of(self.events, "GcBegin"), of(self.events, "GcEnd")
		self.assertGreaterEqual(len(begins), 1)
		self.assertEqual(len(begins), len(ends))
		for begin, end in zip(begins, ends):
			self.assertLessEqual(begin["t"], end["t"])
		self.assertGreater(ends[-1]["bytesAfter"], 0)
		kinds = {self.strings[e["nameStr"]]: e["kind"] for e in of(self.events, "PlotDef")}
		self.assertEqual({"Frame": 0, "Go heap": 1, "Goroutines": 0}, kinds)


class StartTests(unittest.TestCase):
	def test_a_program_connects_from_its_environment(self):
		events = clients.run_from_environment([str(BUILT["exe"])], "secret")
		self.assertEqual(("Hello", "secret"), (events[0]["name"], events[0]["token"]))
		self.assertEqual("Bye", events[-1]["name"])
		self.assertEqual(5, len(of(events, "FrameMark")))

	def test_an_unreachable_or_malformed_target_is_refused(self):
		for target in ("not-an-address", "127.0.0.1:1", "file:" + str(BUILT["dir"] / "missing" / "x.iedtrace")):
			result = subprocess.run([str(BUILT["exe"]), target], capture_output=True, text=True, timeout=60)
			self.assertEqual(2, result.returncode, target)
			self.assertIn("start failed:", result.stdout)


if __name__ == "__main__":
	unittest.main()
