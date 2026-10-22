"""What the ITP client tests share (test_itp_client.py for C/C++, test_itp_go.py, test_itp_rust.py): running a client's
program to a trace file or to a listener from its environment, and the checks every client's stream must pass."""

import os
import pathlib
import socket
import subprocess
import sys
import threading

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import itp  # noqa: E402

WARNING = 3  # the protocol's Message severity for a warning

# Where each kind of id is defined, and the fields that use one.
USES = {
	"ZoneBegin": [("srcLocId", "site")], "ZoneBeginNamed": [("srcLocId", "site"), ("nameStr", "string")],
	"ZoneText": [("strId", "string")], "ThreadName": [("nameStr", "string")], "Message": [("strId", "string")],
	"TaskBegin": [("nameStr", "string")], "PlotValue": [("plotId", "plot")], "PlotDef": [("nameStr", "string")],
	"PoolDef": [("nameStr", "string")], "SrcLocDef": [("nameStr", "string"), ("fileStr", "string"), ("functionStr", "string")],
	"FrameMark": [("frameSetStr", "string")], "FrameBegin": [("frameSetStr", "string")], "FrameEnd": [("frameSetStr", "string")],
	"Alloc": [("poolId", "pool")], "Free": [("poolId", "pool")], "HeapCensusType": [("nameStr", "string")],
}
DEFINES = {"StringDef": ("id", "string"), "SrcLocDef": ("id", "site"), "PlotDef": ("plotId", "plot"), "PoolDef": ("poolId", "pool")}


def run_to_file(argv, path, cwd=None):
	"""Runs a client's program with `file:<path>` as its target and reads the trace back."""
	subprocess.run(argv + ["file:" + str(path)], check=True, capture_output=True, timeout=120, cwd=cwd)
	return itp.read(pathlib.Path(path).read_bytes())


def run_from_environment(argv, token, cwd=None):
	"""Runs a client's program with IED_PROFILER naming a listener (and `token`) and reads what it streamed."""
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
	with server:
		env = dict(os.environ, IED_PROFILER="127.0.0.1:%d;token=%s" % (server.getsockname()[1], token))
		subprocess.run(argv, check=True, capture_output=True, timeout=120, env=env, cwd=cwd)
		thread.join(60)
	return itp.read(bytes(received))


def of(events, name):
	return [e for e in events if e["name"] == name]


def names(events):
	"""The stream's strings by id, and each source location's name by id."""
	strings = itp.strings_of(events)
	return strings, {e["id"]: strings.get(e["nameStr"]) for e in events if e["name"] == "SrcLocDef"}


def check_framing(test, events, app):
	"""Hello first (as `app`, with a clock), Bye last, nothing the reader does not know."""
	test.assertEqual(("Hello", app), (events[0]["name"], events[0]["appName"]))
	test.assertGreater(events[0]["clockFreqHz"], 1_000_000)
	test.assertEqual("Bye", events[-1]["name"])
	test.assertEqual([], of(events, "Unknown"))


def check_definitions_precede_use(test, events):
	defined = {"string": set(), "site": set(), "plot": set(), "pool": set()}
	for event in events:
		for field, kind in USES.get(event["name"], []):
			if event[field] != 0:
				test.assertIn(event[field], defined[kind], "%s uses %s %d before its definition" % (event["name"], field, event[field]))
		if event["name"] in DEFINES:
			field, kind = DEFINES[event["name"]]
			defined[kind].add(event[field])


def zones_by_track(test, events):
	"""Each track's zone names in the order they opened, checking on the way that zones nest, every zone closes and
	times only grow on each track."""
	strings, sites = names(events)
	depth, last, zones = {}, {}, {}
	for event in events:
		if event["kind"] != itp.THREAD_CHUNK or "t" not in event:
			continue
		thread = event["threadId"]
		test.assertGreaterEqual(event["t"], last.get(thread, 0), "time went back on track %d" % thread)
		last[thread] = event["t"]
		if event["name"] == "ZoneBegin":
			zones.setdefault(thread, []).append(sites[event["srcLocId"]])
		elif event["name"] == "ZoneBeginNamed":
			zones.setdefault(thread, []).append(strings[event["nameStr"]])
		if event["name"].startswith("ZoneBegin"):
			depth[thread] = depth.get(thread, 0) + 1
		elif event["name"] == "ZoneEnd":
			depth[thread] = depth.get(thread, 0) - 1
			test.assertGreaterEqual(depth[thread], 0, "a zone ended that never began on track %d" % thread)
	test.assertEqual({0}, set(depth.values()) or {0}, "a zone never ended")
	return zones


def zone_durations(events):
	"""Each closed zone's name and duration in ticks, in the order they closed."""
	strings, sites = names(events)
	open_zones, closed = {}, []
	for event in events:
		if event["kind"] != itp.THREAD_CHUNK:
			continue
		thread = event["threadId"]
		if event["name"] == "ZoneBegin":
			open_zones.setdefault(thread, []).append((sites[event["srcLocId"]], event["t"]))
		elif event["name"] == "ZoneBeginNamed":
			open_zones.setdefault(thread, []).append((strings[event["nameStr"]], event["t"]))
		elif event["name"] == "ZoneEnd" and open_zones.get(thread):
			name, begin = open_zones[thread].pop()
			closed.append((name, event["t"] - begin))
	return closed


def check_zones_take_time(test, events, name):
	"""Every zone `name` lasted at least a tick: the clock is finer than the work (a spin of microseconds)."""
	durations = [ticks for zone, ticks in zone_durations(events) if zone == name]
	test.assertTrue(durations, "no %s zones" % name)
	test.assertEqual([], [ticks for ticks in durations if ticks <= 0], "%s zones that took no time" % name)


def check_lock(test, events, name, waits):
	"""The lock `name` defined once with its site, `waits` waits for it, and as many releases as holds, each event naming
	the lock's id (times two: an exclusive hold)."""
	strings, _ = names(events)
	defs = [e for e in of(events, "LockDef") if strings.get(e["nameStr"]) == name]
	test.assertEqual(1, len(defs), "the lock is defined once")
	test.assertNotEqual(0, defs[0]["srcLocId"], "with the place it is declared")
	lock = defs[0]["lockId"] * 2
	mine = lambda kind: [e for e in of(events, kind) if e["lock"] == lock]
	test.assertEqual(waits, len(mine("LockWait")))
	test.assertGreater(len(mine("LockObtain")), waits)
	test.assertEqual(len(mine("LockObtain")), len(mine("LockRelease")))
	wait = mine("LockWait")[0]
	obtain = [e for e in mine("LockObtain") if e["threadId"] == wait["threadId"] and e["t"] >= wait["t"]][0]
	other = [e for e in mine("LockRelease") if e["threadId"] != wait["threadId"] and e["t"] <= obtain["t"]]
	test.assertTrue(other, "the waiter got the lock only after another thread let it go")


def thread_names(events):
	strings, _ = names(events)
	return [strings[e["nameStr"]] for e in of(events, "ThreadName")]
