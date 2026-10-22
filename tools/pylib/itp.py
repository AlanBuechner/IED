"""The IED Trace Protocol v1 (docs/16 "ITP v1 wire format") in Python: a writer that builds a stream event by event,
and a reader that turns a stream (or an .iedtrace file) back into events. The server checks feed captures with the
writer; the client libraries' tests check what they wrote with the reader."""

import json
import struct

MAGIC = b"ITP1"
VERSION = 1
THREAD_CHUNK = 0
GLOBAL_CHUNK = 1
INDEX_MAGIC = b"ITPI"
FOOTER_MAGIC = b"ITPX"
FOOTER_SIZE = 16

# type -> (name, fields). Field kinds: "dt" a zig-zag time delta, "v" a varint, "s" an inline string, "f" an 8-byte
# double, "list" a count then that many varints.
EVENTS = {
	0x01: ("Hello", [("version", "v"), ("appName", "s"), ("pid", "v"), ("clockFreqHz", "v"), ("startTicks", "v"), ("startUnixNs", "v"), ("flags", "v"), ("token", "s")]),
	0x02: ("Calibration", [("ticks", "v"), ("unixNs", "v")]),
	0x03: ("Bye", [("reason", "v")]),
	0x04: ("Warning", [("code", "v"), ("strId", "v")]),
	0x05: ("Stats", [("eventsSent", "v"), ("eventsDropped", "v"), ("bytesBuffered", "v")]),
	0x10: ("StringDef", [("id", "v"), ("utf8", "s")]),
	0x11: ("SrcLocDef", [("id", "v"), ("nameStr", "v"), ("functionStr", "v"), ("fileStr", "v"), ("line", "v"), ("color", "v")]),
	0x12: ("ThreadDef", [("threadId", "v"), ("osThreadId", "v"), ("groupHint", "v")]),
	0x13: ("ThreadName", [("dt", "dt"), ("nameStr", "v")]),
	0x20: ("ZoneBegin", [("dt", "dt"), ("srcLocId", "v")]),
	0x21: ("ZoneBeginNamed", [("dt", "dt"), ("srcLocId", "v"), ("nameStr", "v")]),
	0x22: ("ZoneEnd", [("dt", "dt")]),
	0x23: ("ZoneText", [("strId", "v")]),
	0x24: ("ZoneValue", [("value", "v")]),
	0x25: ("ZoneColor", [("color", "v")]),
	0x26: ("ZoneCallstack", [("callstackId", "v")]),
	0x27: ("ZoneAllocs", [("objects", "v"), ("bytes", "v")]),
	0x28: ("ZoneNative", [("bytes", "v")]),
	0x29: ("ZoneNativeFreed", [("bytes", "v")]),
	0x30: ("TaskBegin", [("dt", "dt"), ("taskId", "v"), ("nameStr", "v"), ("parentTaskId", "v")]),
	0x31: ("TaskEnd", [("dt", "dt"), ("taskId", "v")]),
	0x32: ("TaskSuspend", [("dt", "dt"), ("taskId", "v")]),
	0x33: ("TaskResume", [("dt", "dt"), ("taskId", "v")]),
	0x40: ("FrameMark", [("dt", "dt"), ("frameSetStr", "v")]),
	0x41: ("FrameBegin", [("dt", "dt"), ("frameSetStr", "v")]),
	0x42: ("FrameEnd", [("dt", "dt"), ("frameSetStr", "v")]),
	0x50: ("PlotDef", [("plotId", "v"), ("nameStr", "v"), ("kind", "v"), ("flags", "v")]),
	0x51: ("PlotValue", [("dt", "dt"), ("plotId", "v"), ("value", "f")]),
	0x52: ("Message", [("dt", "dt"), ("severity", "v"), ("strId", "v"), ("color", "v")]),
	0x60: ("PoolDef", [("poolId", "v"), ("nameStr", "v")]),
	0x61: ("Alloc", [("dt", "dt"), ("threadId", "v"), ("poolId", "v"), ("address", "v"), ("size", "v"), ("callstackId", "v")]),
	0x62: ("Free", [("dt", "dt"), ("threadId", "v"), ("poolId", "v"), ("address", "v")]),
	0x63: ("PoolReset", [("dt", "dt"), ("poolId", "v")]),
	0x68: ("GcBegin", [("dt", "dt"), ("kind", "v"), ("generation", "v")]),
	0x69: ("GcEnd", [("dt", "dt"), ("bytesBefore", "v"), ("bytesAfter", "v"), ("objectsFreed", "v"), ("pinned", "v"), ("roots", "v")]),
	0x6A: ("AllocCount", [("dt", "dt"), ("objects", "v"), ("bytes", "v")]),
	0x6B: ("HeapCensus", [("dt", "dt"), ("types", "v")]),
	0x6C: ("HeapCensusType", [("nameStr", "v"), ("objects", "v"), ("bytes", "v")]),
	0x6D: ("HeapCensusTypeNative", [("nameStr", "v"), ("objects", "v"), ("bytes", "v"), ("nativeBytes", "v")]),
	0x70: ("LockDef", [("lockId", "v"), ("nameStr", "v"), ("srcLocId", "v")]),
	0x71: ("LockWait", [("dt", "dt"), ("lock", "v")]),
	0x72: ("LockObtain", [("dt", "dt"), ("lock", "v")]),
	0x73: ("LockRelease", [("dt", "dt"), ("lock", "v")]),
	0x80: ("ModuleDef", [("base", "v"), ("size", "v"), ("pathStr", "v"), ("debugIdStr", "v")]),
	0x81: ("CallstackDef", [("callstackId", "v"), ("addresses", "list")]),
	0x82: ("Sample", [("dt", "dt"), ("threadId", "v"), ("callstackId", "v"), ("kind", "v"), ("weight", "v")]),
	0x83: ("SymbolDef", [("address", "v"), ("functionStr", "v"), ("fileStr", "v"), ("line", "v")]),
	0x90: ("GpuContextDef", [("ctxId", "v"), ("api", "v"), ("queueNameStr", "v"), ("periodPicoseconds", "v")]),
	0x91: ("GpuCalibration", [("ctxId", "v"), ("cpuTicks", "v"), ("gpuTicks", "v")]),
	0x92: ("GpuZoneBegin", [("dt", "dt"), ("ctxId", "v"), ("queryId", "v"), ("srcLocId", "v")]),
	0x93: ("GpuZoneEnd", [("dt", "dt"), ("ctxId", "v"), ("queryId", "v")]),
	0x94: ("GpuTime", [("ctxId", "v"), ("queryId", "v"), ("gpuTicks", "v")]),
}
BY_NAME = {name: (code, fields) for code, (name, fields) in EVENTS.items()}


def varint(value):
	if value < 0:
		raise ValueError("varints are unsigned: %d" % value)
	out = bytearray()
	while True:
		byte = value & 0x7F
		value >>= 7
		if value:
			out.append(byte | 0x80)
		else:
			out.append(byte)
			return bytes(out)


def zigzag(value):
	return (value << 1) if value >= 0 else ((-value << 1) - 1)


def unzigzag(value):
	return (value >> 1) if not value & 1 else -((value + 1) >> 1)


def text(value):
	data = value.encode("utf-8")
	return varint(len(data)) + data


class Chunk:
	"""Events of one chunk; a timed event's `t` is absolute ticks, written as a delta from the one before."""

	def __init__(self, kind, thread_id, base_ticks):
		self.kind = kind
		self.thread_id = thread_id
		self.base = base_ticks
		self.cursor = base_ticks
		self.body = bytearray()

	def event(self, name, t=None, **fields):
		code, layout = BY_NAME[name]
		self.body.append(code)
		for field, kind in layout:
			if kind == "dt":
				self.body += varint(zigzag(t - self.cursor))
				self.cursor = t
			elif kind == "v":
				self.body += varint(fields.get(field, 0))
			elif kind == "s":
				self.body += text(fields.get(field, ""))
			elif kind == "f":
				self.body += struct.pack("<d", float(fields.get(field, 0.0)))
			elif kind == "list":
				values = fields.get(field, [])
				self.body += varint(len(values))
				for value in values:
					self.body += varint(value)
		return self

	def raw(self, data):
		"""Bytes appended as they are: for malformed or future events."""
		self.body += data
		return self

	def encode(self):
		payload = bytes([self.kind]) + varint(self.thread_id) + varint(self.base) + bytes(self.body)
		return struct.pack("<I", len(payload)) + payload


class Writer:
	"""A whole stream: the preamble, then chunks. Strings and source locations are interned on first use."""

	def __init__(self):
		self.out = bytearray(MAGIC + struct.pack("<HH", VERSION, 0))
		self.strings = {}
		self.srclocs = {}

	def preamble_only(self):
		return len(self.out) == 8

	def chunk(self, kind, thread_id=0, base_ticks=0):
		return Chunk(kind, thread_id, base_ticks)

	def add(self, chunk):
		self.out += chunk.encode()
		return self

	def hello(self, app="app", pid=1, freq=1_000_000_000, start_ticks=0, start_unix_ns=0, token="", flags=0):
		return self.add(self.chunk(GLOBAL_CHUNK, 0, start_ticks).event("Hello", version=VERSION, appName=app, pid=pid, clockFreqHz=freq, startTicks=start_ticks, startUnixNs=start_unix_ns, flags=flags, token=token))

	def string(self, value, defs):
		"""The id of `value`, adding its StringDef to the chunk `defs` the first time."""
		if value not in self.strings:
			self.strings[value] = len(self.strings) + 1
			defs.event("StringDef", id=self.strings[value], utf8=value)
		return self.strings[value]

	def srcloc(self, name, defs, function="", file="", line=0, color=0):
		key = (name, function, file, line)
		if key not in self.srclocs:
			ids = [self.string(part, defs) if part else 0 for part in (name, function, file)]
			self.srclocs[key] = len(self.srclocs) + 1
			defs.event("SrcLocDef", id=self.srclocs[key], nameStr=ids[0], functionStr=ids[1], fileStr=ids[2], line=line, color=color)
		return self.srclocs[key]

	def bytes(self):
		return bytes(self.out)


# --- Reading ----------------------------------------------------------------------------------------------------------

class Reader:
	def __init__(self, data, offset=0, end=None):
		self.data = data
		self.pos = offset
		self.end = len(data) if end is None else end

	def varint(self):
		value = 0
		shift = 0
		while True:
			if self.pos >= self.end:
				raise EOFError("a varint runs past the end")
			byte = self.data[self.pos]
			self.pos += 1
			value |= (byte & 0x7F) << shift
			if not byte & 0x80:
				return value
			shift += 7

	def text(self):
		length = self.varint()
		value = bytes(self.data[self.pos:self.pos + length]).decode("utf-8", "replace")
		self.pos += length
		return value


def stream_end(data):
	"""Where the event stream of an .iedtrace ends (before its index and footer), and its metadata or None."""
	if len(data) >= FOOTER_SIZE and data[-4:] == FOOTER_MAGIC:
		index_at = struct.unpack_from("<Q", data, len(data) - FOOTER_SIZE)[0]
		if data[index_at:index_at + 4] == INDEX_MAGIC:
			length = struct.unpack_from("<I", data, index_at + 4)[0]
			meta = json.loads(bytes(data[index_at + 8:index_at + 8 + length]).decode("utf-8"))
			return index_at, meta
	return len(data), None


def read(data):
	"""Every event of a stream or trace file as dicts: name, chunk kind, threadId, absolute `t` for timed events, and
	the fields. Unknown event types end their chunk with an `Unknown` entry, as the server skips them."""
	if bytes(data[:4]) != MAGIC:
		raise ValueError("not an ITP stream")
	end, _ = stream_end(data)
	pos = 8
	events = []
	while pos + 4 <= end:
		length = struct.unpack_from("<I", data, pos)[0]
		pos += 4
		if pos + length > end:
			break
		reader = Reader(data, pos, pos + length)
		kind = data[reader.pos]
		reader.pos += 1
		thread_id = reader.varint()
		cursor = reader.varint()
		while reader.pos < reader.end:
			code = data[reader.pos]
			reader.pos += 1
			if code not in EVENTS:
				events.append({"name": "Unknown", "type": code, "kind": kind, "threadId": thread_id})
				break
			name, layout = EVENTS[code]
			event = {"name": name, "kind": kind, "threadId": thread_id}
			for field, field_kind in layout:
				if field_kind == "dt":
					cursor += unzigzag(reader.varint())
					event["t"] = cursor
				elif field_kind == "v":
					event[field] = reader.varint()
				elif field_kind == "s":
					event[field] = reader.text()
				elif field_kind == "f":
					event[field] = struct.unpack_from("<d", data, reader.pos)[0]
					reader.pos += 8
				elif field_kind == "list":
					event[field] = [reader.varint() for _ in range(reader.varint())]
			events.append(event)
		pos += length
	return events


def strings_of(events):
	return {e["id"]: e["utf8"] for e in events if e["name"] == "StringDef"}
