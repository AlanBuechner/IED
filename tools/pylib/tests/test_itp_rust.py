"""The Rust ITP client (plugins/rust/profiler, the ied-profiler crate) run through its examples: what `demo` writes to
a trace file and streams to a listener, and what `spans` (the `tracing` feature) writes, read back as docs/16 says.
Skipped without cargo."""

import os
import pathlib
import shutil
import subprocess
import unittest

import itp_clients as clients
from itp_clients import of

ROOT = pathlib.Path(__file__).resolve().parents[3]
CRATE = ROOT / "plugins" / "rust" / "profiler"
TARGET = ROOT / "build" / "int" / "test-itp-rust"
EXAMPLES = TARGET / "release" / "examples"
BUILT = {}


def setUpModule():
	cargo = shutil.which("cargo")
	if not cargo:
		raise unittest.SkipTest("no cargo")
	env = dict(os.environ, CARGO_TARGET_DIR=str(TARGET))
	result = subprocess.run([cargo, "build", "--release", "--examples", "--features", "tracing"], cwd=CRATE, env=env,
		capture_output=True, text=True, timeout=600)
	if result.returncode != 0:
		raise AssertionError("the examples do not build:\n" + result.stdout + result.stderr)
	suffix = ".exe" if os.name == "nt" else ""
	BUILT["demo"] = EXAMPLES / ("demo" + suffix)
	BUILT["spans"] = EXAMPLES / ("spans" + suffix)


def names_by_track(events):
	strings, _ = clients.names(events)
	tracks = {}
	for event in of(events, "ThreadName"):
		tracks.setdefault(event["threadId"], []).append(strings[event["nameStr"]])
	return sorted(tracks.values())


class DemoTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.events = clients.run_to_file([str(BUILT["demo"])], TARGET / "demo.iedtrace")
		cls.strings, cls.sites = clients.names(cls.events)

	def test_the_stream_opens_with_hello_and_ends_with_bye(self):
		clients.check_framing(self, self.events, "itp-rust-test")

	def test_every_id_is_defined_before_its_first_use(self):
		clients.check_definitions_precede_use(self, self.events)

	def test_the_clock_is_finer_than_the_work(self):
		clients.check_zones_take_time(self, self.events, "Spin")

	def test_threads_keep_their_std_names_and_renames(self):
		self.assertEqual([["Worker"]] * 5 + [["main", "Main loop done"]], names_by_track(self.events))

	def test_zones_nest_and_name_where_they_are(self):
		zones = list(clients.zones_by_track(self, self.events).values())
		self.assertIn(["Spin"] + ["Update", "level.bin", "Spin"] * 5, zones)
		self.assertEqual(5, zones.count(["Physics::Step", "Spin"]))
		sites = {self.strings[e["nameStr"]]: e for e in of(self.events, "SrcLocDef")}
		functions = {name: self.strings[site["functionStr"]] for name, site in sites.items()}
		self.assertEqual({"Spin": "demo::spin", "Update": "demo::update", "Load": "demo::update", "Physics::Step": "demo::update"}, functions)
		self.assertTrue(all(self.strings[site["fileStr"]].replace("\\", "/").endswith("examples/demo.rs") for site in sites.values()))

	def test_text_values_frames_plots_messages_and_tasks(self):
		self.assertEqual(["bytes=4096"] * 5, [self.strings[e["strId"]] for e in of(self.events, "ZoneText")])
		self.assertEqual([4096] * 5, [e["value"] for e in of(self.events, "ZoneValue")])
		self.assertEqual(5, len(of(self.events, "FrameMark")))
		self.assertEqual(["Loading"], [self.strings[e["frameSetStr"]] for e in of(self.events, "FrameEnd")])
		self.assertEqual([0.0, 1.0, 2.0, 3.0, 4.0], [e["value"] for e in of(self.events, "PlotValue")])
		self.assertEqual([(clients.WARNING, "done")], [(e["severity"], self.strings[e["strId"]]) for e in of(self.events, "Message")])
		begun = {e["taskId"]: self.strings[e["nameStr"]] for e in of(self.events, "TaskBegin")}
		self.assertEqual(["Physics"] * 5, list(begun.values()))
		self.assertEqual(set(begun), {e["taskId"] for e in of(self.events, "TaskEnd")})

	def test_the_global_allocator_reports_every_block(self):
		heap = [e for e in of(self.events, "Alloc") if e["poolId"] == 0]
		self.assertGreaterEqual(len(heap), 5 * 16)
		freed = {e["address"] for e in of(self.events, "Free") if e["poolId"] == 0}
		self.assertTrue(any(e["address"] in freed and e["size"] == 64 for e in heap), "the 64-byte strings are freed")
		arena = [(e["name"], e.get("size")) for e in self.events if e["name"] in ("Alloc", "Free") and e["poolId"] == 2]
		self.assertEqual([("Alloc", 64), ("Free", None)], arena)


class TracingTests(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.events = clients.run_to_file([str(BUILT["spans"])], TARGET / "spans.iedtrace")
		cls.strings, _ = clients.names(cls.events)

	def test_spans_are_zones_with_their_fields_as_text(self):
		clients.check_framing(self, self.events, "itp-rust-spans")
		clients.check_definitions_precede_use(self, self.events)
		self.assertEqual([["frame", "load"] * 3], list(clients.zones_by_track(self, self.events).values()))
		self.assertEqual(["number=0", 'path="level.bin"', "number=1", 'path="level.bin"', "number=2", 'path="level.bin"'],
			[self.strings[e["strId"]] for e in of(self.events, "ZoneText")])
		functions = {self.strings[e["nameStr"]]: self.strings[e["functionStr"]] for e in of(self.events, "SrcLocDef")}
		self.assertEqual({"frame": "spans", "load": "spans"}, functions)

	def test_events_are_messages_of_their_level(self):
		messages = [(e["severity"], self.strings[e["strId"]]) for e in of(self.events, "Message")]
		self.assertEqual([(2, "loaded"), (3, "slow frame frame=0"), (2, "loaded"), (3, "slow frame frame=1"), (2, "loaded"), (3, "slow frame frame=2")], messages)


class StartTests(unittest.TestCase):
	def test_a_program_connects_from_its_environment_and_ends_at_exit(self):
		events = clients.run_from_environment([str(BUILT["demo"])], "secret")
		self.assertEqual(("Hello", "secret"), (events[0]["name"], events[0]["token"]))
		self.assertEqual("Bye", events[-1]["name"])
		self.assertEqual(5, len(of(events, "FrameMark")))

	def test_an_unreachable_or_malformed_target_is_refused(self):
		for target in ("not-an-address", "127.0.0.1:1", "file:" + str(TARGET / "missing" / "x.iedtrace")):
			result = subprocess.run([str(BUILT["demo"]), target], capture_output=True, text=True, timeout=60)
			self.assertEqual(2, result.returncode, target)
			self.assertIn("start failed:", result.stdout)


if __name__ == "__main__":
	unittest.main()
