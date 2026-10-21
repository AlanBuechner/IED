"""The Python ITP codec (docs/16): varints, zig-zag deltas, chunk framing, interning, unknown events, the trace footer."""

import json
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import itp  # noqa: E402


class VarintTests(unittest.TestCase):
	def test_small_values_take_one_byte(self):
		self.assertEqual(b"\x00", itp.varint(0))
		self.assertEqual(b"\x7f", itp.varint(127))

	def test_larger_values_continue_in_seven_bit_groups(self):
		self.assertEqual(b"\x80\x01", itp.varint(128))
		self.assertEqual(b"\xff\xff\xff\xff\xff\xff\xff\xff\xff\x01", itp.varint(2 ** 64 - 1))

	def test_negative_values_are_refused(self):
		with self.assertRaises(ValueError):
			itp.varint(-1)

	def test_zigzag_round_trips_both_signs(self):
		for value in (0, 1, -1, 63, -64, 2 ** 40, -(2 ** 40)):
			self.assertEqual(value, itp.unzigzag(itp.zigzag(value)))
		self.assertEqual([0, 2, 1, 4, 3], [itp.zigzag(v) for v in (0, 1, -1, 2, -2)])


class StreamTests(unittest.TestCase):
	def build(self):
		writer = itp.Writer().hello(app="game", pid=42, freq=10_000_000, start_ticks=1000, token="tok")
		defs = writer.chunk(itp.GLOBAL_CHUNK, 0, 1000)
		frame = writer.srcloc("Frame", defs, function="Main", file="main.cf", line=7)
		writer.add(defs)
		thread = writer.chunk(itp.THREAD_CHUNK, 1, 1000)
		thread.event("ZoneBegin", 1010, srcLocId=frame).event("ZoneEnd", 1050).event("PlotValue", 1040, plotId=1, value=2.5)
		writer.add(thread)
		return writer.bytes()

	def test_events_read_back_with_absolute_times(self):
		events = itp.read(self.build())
		self.assertEqual("Hello", events[0]["name"])
		self.assertEqual(("game", 42, "tok"), (events[0]["appName"], events[0]["pid"], events[0]["token"]))
		zones = [(e["name"], e["t"]) for e in events if e["name"].startswith("Zone")]
		self.assertEqual([("ZoneBegin", 1010), ("ZoneEnd", 1050)], zones)

	def test_a_time_before_the_previous_one_is_a_negative_delta(self):
		events = itp.read(self.build())
		plot = [e for e in events if e["name"] == "PlotValue"][0]
		self.assertEqual((1040, 2.5), (plot["t"], plot["value"]))

	def test_strings_and_source_locations_are_interned_once(self):
		writer = itp.Writer()
		defs = writer.chunk(itp.GLOBAL_CHUNK)
		first = writer.srcloc("A", defs, file="x.cf")
		again = writer.srcloc("A", defs, file="x.cf")
		writer.add(defs)
		events = itp.read(writer.bytes())
		self.assertEqual(first, again)
		self.assertEqual({1: "A", 2: "x.cf"}, itp.strings_of(events))
		self.assertEqual(1, len([e for e in events if e["name"] == "SrcLocDef"]))

	def test_an_unknown_event_ends_its_chunk_only(self):
		writer = itp.Writer()
		writer.add(writer.chunk(itp.THREAD_CHUNK, 1).event("ZoneBegin", 5, srcLocId=1).raw(b"\xee\x01\x02").event("ZoneEnd", 9))
		writer.add(writer.chunk(itp.THREAD_CHUNK, 1, 9).event("ZoneEnd", 12))
		names = [e["name"] for e in itp.read(writer.bytes())]
		self.assertEqual(["ZoneBegin", "Unknown", "ZoneEnd"], names)

	def test_a_cut_off_chunk_is_left_out(self):
		data = self.build()
		self.assertEqual(itp.read(data)[:3], itp.read(data[:-3])[:3])
		self.assertNotIn("PlotValue", [e["name"] for e in itp.read(data[:-3])])

	def test_a_footer_hides_the_index_from_the_events(self):
		data = self.build()
		meta = json.dumps({"app": "game"}).encode()
		index = itp.INDEX_MAGIC + struct.pack("<I", len(meta)) + meta
		trace = data + index + struct.pack("<QHH", len(data), 1, 0) + itp.FOOTER_MAGIC
		self.assertEqual((len(data), {"app": "game"}), itp.stream_end(trace))
		self.assertEqual(itp.read(data), itp.read(trace))

	def test_a_stream_without_the_magic_is_refused(self):
		with self.assertRaises(ValueError):
			itp.read(b"NOPE\x01\x00\x00\x00")


if __name__ == "__main__":
	unittest.main()
