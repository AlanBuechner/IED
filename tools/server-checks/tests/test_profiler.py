"""The profiler (docs/16, docs/features/12-profiler.md): a program connects to the plugin's listener with the
workspace's token and streams ITP; the capture is live on profs:<wsId> and prof:<captureId>, written to
.ied/local/captures as it arrives and finished with its index block; the prof.* queries answer from it, live and after
a restart; a wrong token or a broken stream never stops the server; Chrome traces and pprof profiles import; the
Profile task gets ${profiler.connect}; the AI's answers come through the profiler.query binding."""

import asyncio
import gzip
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "pylib"))

import itp  # noqa: E402
from frames import FrameChecker  # noqa: E402
from iedclient import Client, RpcFailure  # noqa: E402
from routed import RoutedClient
from server import REPO, IedServer, ServerTestCase, local_state, remove_tree, workspace_file  # noqa: E402

PY = sys.executable
MS = 1_000_000


def write(path, value):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8", newline="") as f:
		f.write(value if isinstance(value, str) else json.dumps(value, indent="\t"))


def profiler_manifest():
	"""The plugin's manifest as shipped, without its UI module and skills (the checks need its server side only)."""
	with open(os.path.join(REPO, "plugins", "profiler", "ied-plugin.json"), encoding="utf-8") as f:
		text = "\n".join(line for line in f.read().splitlines() if not line.lstrip().startswith("//"))
	manifest = json.loads(text)
	manifest["runtime"].pop("ui", None)
	manifest["contributes"].pop("skills", None)
	return manifest


def make_home(home):
	shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(home, "shared", "host-api", "schemas"))
	plugin = os.path.join(home, "plugins", "profiler")
	os.makedirs(plugin)
	shutil.copy(os.path.join(REPO, "plugins", "profiler", "settings.schema.json"), plugin)
	write(os.path.join(plugin, "ied-plugin.json"), profiler_manifest())
	return home


def game_stream(token, frames=3, bye=True):
	"""A small capture: two threads, nested zones, frames, a message, a plot, an allocation that leaks, a collection."""
	w = itp.Writer().hello(app="Game", pid=4242, freq=1_000_000_000, start_ticks=1000, start_unix_ns=1_700_000_000_000_000_000, token=token)
	defs = w.chunk(itp.GLOBAL_CHUNK, 0, 1000)
	update = w.srcloc("Update", defs, function="Game::Update", file="src/game.cf", line=12)
	physics = w.srcloc("Physics::Step", defs, function="Physics::Step", file="src/physics.cf", line=40)
	render = w.srcloc("Render", defs, function="Game::Render", file="src/render.cf", line=7)
	main = w.string("Main Thread", defs)
	worker = w.string("Job Worker 0", defs)
	loaded = w.string("arena loaded", defs)
	defs.event("ThreadDef", threadId=1, osThreadId=100, groupHint=0)
	defs.event("ThreadDef", threadId=2, osThreadId=200, groupHint=0)
	defs.event("PlotDef", plotId=1, nameStr=w.string("Entities", defs), kind=0, flags=0)
	w.add(defs)
	t = 1000
	chunk = w.chunk(itp.THREAD_CHUNK, 1, t).event("ThreadName", t, nameStr=main)
	for i in range(frames):
		start = t + i * 16 * MS
		chunk.event("FrameMark", start, frameSetStr=0)
		chunk.event("ZoneBegin", start + 1 * MS, srcLocId=update)
		chunk.event("ZoneBegin", start + 2 * MS, srcLocId=physics).event("ZoneEnd", start + (5 + i) * MS)
		chunk.event("ZoneEnd", start + 9 * MS)
		chunk.event("ZoneBegin", start + 10 * MS, srcLocId=render).event("ZoneEnd", start + 14 * MS)
		chunk.event("PlotValue", start + 15 * MS, plotId=1, value=100.0 + i)
	chunk.event("Message", t + 3 * MS, severity=2, strId=loaded, color=0)
	chunk.event("FrameMark", t + frames * 16 * MS, frameSetStr=0)
	w.add(chunk)
	work = w.chunk(itp.THREAD_CHUNK, 2, t).event("ThreadName", t, nameStr=worker)
	work.event("ZoneBegin", t + 4 * MS, srcLocId=physics).event("ZoneEnd", t + 6 * MS)
	w.add(work)
	memory = w.chunk(itp.GLOBAL_CHUNK, 0, t)
	memory.event("Alloc", t + 2 * MS, threadId=1, poolId=0, address=0x1000, size=4096, callstackId=0)
	memory.event("Alloc", t + 3 * MS, threadId=1, poolId=0, address=0x2000, size=64, callstackId=0)
	memory.event("Free", t + 4 * MS, threadId=1, poolId=0, address=0x2000)
	memory.event("GcBegin", t + 20 * MS, kind=1, generation=1).event("GcEnd", t + 21 * MS, bytesBefore=10000, bytesAfter=4000, objectsFreed=12, pinned=0, roots=3)
	w.add(memory)
	if bye:
		w.add(w.chunk(itp.GLOBAL_CHUNK, 0, t).event("Bye", reason=0))
	return w.bytes()


def troubled_stream(token):
	"""A game with problems to find (1 ns ticks): 60 frames of 10 ms on Main, frame 30 stalled 30 ms by Level::Stream;
	every frame Particles::Spawn allocates 50 blocks it frees within the frame and Cache::Remember 2 blocks of 32 KiB it
	never frees; an Entities plot."""
	w = itp.Writer().hello(app="Troubled", pid=7, freq=1_000_000_000, start_ticks=0, token=token)
	defs = w.chunk(itp.GLOBAL_CHUNK, 0, 0)
	frame = w.srcloc("Frame", defs, file="src/game.cf", line=5)
	spawn = w.srcloc("Particles::Spawn", defs, file="src/particles.cf", line=8)
	cache = w.srcloc("Cache::Remember", defs, file="src/cache.cf", line=20)
	stream = w.srcloc("Level::Stream", defs, file="src/level.cf", line=33)
	main = w.string("Main", defs)
	defs.event("PlotDef", plotId=1, nameStr=w.string("Entities", defs), kind=0, flags=0)
	w.add(defs)
	chunk = w.chunk(itp.THREAD_CHUNK, 1, 0).event("ThreadName", 0, nameStr=main)
	memory = w.chunk(itp.GLOBAL_CHUNK, 0, 0)
	address = 0x100000
	start = 0
	for i in range(60):
		length = 40 * MS if i == 30 else 10 * MS
		chunk.event("FrameMark", start, frameSetStr=0).event("ZoneBegin", start, srcLocId=frame)
		chunk.event("ZoneBegin", start + 1 * MS, srcLocId=spawn).event("ZoneEnd", start + 2 * MS)
		chunk.event("ZoneBegin", start + 3 * MS, srcLocId=cache).event("ZoneEnd", start + 4 * MS)
		if i == 30:
			chunk.event("ZoneBegin", start + 5 * MS, srcLocId=stream).event("ZoneEnd", start + 35 * MS)
		chunk.event("ZoneEnd", start + length - 2 * MS).event("PlotValue", start + length - 1 * MS, plotId=1, value=1000.0 + i)
		for k in range(50):
			memory.event("Alloc", start + 1 * MS + k, threadId=1, poolId=0, address=address + k * 64, size=64, callstackId=0)
		for k in range(50):
			memory.event("Free", start + 2 * MS + k, threadId=1, poolId=0, address=address + k * 64)
		address += 0x10000
		for k in range(2):
			memory.event("Alloc", start + 3 * MS + k, threadId=1, poolId=0, address=address + k * 32768, size=32768, callstackId=0)
		address += 0x10000
		start += length
	chunk.event("FrameMark", start, frameSetStr=0)
	w.add(chunk)
	w.add(memory)
	w.add(w.chunk(itp.GLOBAL_CHUNK, 0, 0).event("Bye", reason=0))
	return w.bytes()


def collected_stream(token):
	"""A collected program that counts its allocations (1 ns ticks): 60 frames of 10 ms on Main, each Frame with Spawn
	(300 objects) and Cache (one 4 KiB object) inside it; the process's count at each frame; the GC heap plotted every
	frame, its floor rising; a collection every 10 frames, each followed by a census in which CacheEntry grows."""
	w = itp.Writer().hello(app="Collected", pid=8, freq=1_000_000_000, start_ticks=0, token=token)
	defs = w.chunk(itp.GLOBAL_CHUNK, 0, 0)
	frame = w.srcloc("Frame", defs, file="src/game.cf", line=5)
	spawn = w.srcloc("Particles::Spawn", defs, file="src/particles.cf", line=8)
	cache = w.srcloc("Cache::Remember", defs, file="src/cache.cf", line=20)
	main = w.string("Main", defs)
	particle = w.string("Particle", defs)
	entry = w.string("CacheEntry", defs)
	defs.event("PlotDef", plotId=1, nameStr=w.string("GC heap", defs), kind=1, flags=0)
	w.add(defs)
	chunk = w.chunk(itp.THREAD_CHUNK, 1, 0).event("ThreadName", 0, nameStr=main)
	memory = w.chunk(itp.GLOBAL_CHUNK, 0, 0)
	for i in range(60):
		start = i * 10 * MS
		chunk.event("FrameMark", start, frameSetStr=0).event("ZoneBegin", start, srcLocId=frame)
		chunk.event("ZoneBegin", start + 1 * MS, srcLocId=spawn).event("ZoneAllocs", objects=300, bytes=43200).event("ZoneEnd", start + 2 * MS)
		chunk.event("ZoneBegin", start + 3 * MS, srcLocId=cache).event("ZoneAllocs", objects=1, bytes=4096).event("ZoneEnd", start + 4 * MS)
		chunk.event("ZoneAllocs", objects=306, bytes=47796).event("ZoneEnd", start + 8 * MS)
		memory.event("AllocCount", start, objects=316 * i, bytes=48296 * i)
		memory.event("PlotValue", start + 5 * MS, plotId=1, value=float(4_000_000 + i * 4096 + (i % 10) * 48296))
		if i % 10 == 9:
			memory.event("GcBegin", start + 9 * MS, kind=2, generation=0).event("GcEnd", start + 9 * MS + 200_000, bytesBefore=0, bytesAfter=4_000_000 + i * 4096, objectsFreed=0, pinned=0, roots=0)
			memory.event("HeapCensus", start + 9 * MS + 200_000, types=2)
			memory.event("HeapCensusType", nameStr=particle, objects=300, bytes=43200)
			memory.event("HeapCensusType", nameStr=entry, objects=i + 1, bytes=(i + 1) * 4096 * 4)
	chunk.event("FrameMark", 60 * 10 * MS, frameSetStr=0)
	memory.event("AllocCount", 60 * 10 * MS, objects=316 * 60, bytes=48296 * 60)
	w.add(chunk)
	w.add(memory)
	w.add(w.chunk(itp.GLOBAL_CHUNK, 0, 0).event("Bye", reason=0))
	return w.bytes()


def lock_stream(token):
	"""A lock, "Queue", used by two threads (1 ns ticks): thread 1 holds it 10..50 us while thread 2 waits from 20 us,
	then thread 2 holds it to 60 us."""
	w = itp.Writer().hello(app="Locks", freq=1_000_000_000, start_ticks=0, token=token)
	defs = w.chunk(itp.GLOBAL_CHUNK, 0, 0)
	work = w.srcloc("Work", defs, file="src/work.cf", line=3)
	queue = w.string("Queue", defs)
	site = w.srcloc("Queue", defs, file="src/queue.cf", line=12)
	defs.event("LockDef", lockId=1, nameStr=queue, srcLocId=site)
	w.add(defs)
	us = 1000
	first = w.chunk(itp.THREAD_CHUNK, 1, 0).event("ZoneBegin", 0, srcLocId=work)
	first.event("LockObtain", 10 * us, lock=2).event("LockRelease", 50 * us, lock=2).event("ZoneEnd", 100 * us)
	w.add(first)
	second = w.chunk(itp.THREAD_CHUNK, 2, 0).event("ZoneBegin", 0, srcLocId=work)
	second.event("LockWait", 20 * us, lock=2).event("LockObtain", 50 * us, lock=2).event("LockRelease", 60 * us, lock=2).event("ZoneEnd", 100 * us)
	w.add(second)
	w.add(w.chunk(itp.GLOBAL_CHUNK, 0, 0).event("Bye", reason=0))
	return w.bytes()


def pprof_profile():
	"""A pprof CPU profile, gzip-compressed: main -> work (3 samples) and main -> idle (1 sample)."""
	def varint(value):
		return itp.varint(value)

	def field(number, wire, payload):
		key = varint((number << 3) | wire)
		if wire == 0:
			return key + varint(payload)
		return key + varint(len(payload)) + payload

	strings = ["", "samples", "count", "cpu", "nanoseconds", "main", "work", "idle", "main.go"]
	sid = {s: i for i, s in enumerate(strings)}
	out = b""
	out += field(1, 2, field(1, 0, sid["samples"]) + field(2, 0, sid["count"]))
	out += field(1, 2, field(1, 0, sid["cpu"]) + field(2, 0, sid["nanoseconds"]))
	for fid, name in ((1, "main"), (2, "work"), (3, "idle")):
		out += field(5, 2, field(1, 0, fid) + field(2, 0, sid[name]) + field(4, 0, sid["main.go"]))
		out += field(4, 2, field(1, 0, fid) + field(4, 2, field(1, 0, fid) + field(2, 0, fid * 10)))
	packed = lambda values: b"".join(varint(v) for v in values)  # noqa: E731
	for stack, count in (([2, 1], 3), ([3, 1], 1)):
		out += field(2, 2, field(1, 2, packed(stack)) + field(2, 2, packed([count, count * 10_000_000])))
	for s in strings:
		out += field(6, 2, s.encode())
	out += field(9, 0, 1_700_000_000_000_000_000) + field(10, 0, 40_000_000)
	return gzip.compress(out)


async def require_profiler(test, client):
	"""Skips the test on a server built without the profiler's code (its plugin then fails to start)."""
	for plugin in (await client.request("plugins.list"))["plugins"]:
		if plugin["id"] == "ied.profiler" and plugin.get("code", {}).get("state") == "active":
			return
	test.skipTest("the server was built without plugins/profiler/code")


async def require_plugin(test, client, plugin_id):
	"""Fails with the plugin's state and problems unless it loaded: an importer's contribution comes only from a loaded
	plugin."""
	listed = {p["id"]: p for p in (await client.request("plugins.list", {"wsId": "w1"}))["plugins"]}
	plugin = listed.get(plugin_id)
	test.assertIsNotNone(plugin, "%s is not in the server's plugins: %s" % (plugin_id, sorted(listed)))
	test.assertEqual("loaded", plugin["state"], "%s did not load: %s" % (plugin_id, plugin.get("problems")))


class ProfilerServer(ServerTestCase):
	"""A server whose home has the profiler, with a workspace, connected and subscribed to its captures."""

	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-profiler-")
		self.addCleanup(remove_tree, self.root)
		self.home = make_home(os.path.join(self.root, "home"))
		self.ws = os.path.join(self.root, "Game")
		os.makedirs(self.ws)
		self.data = os.path.join(self.root, "data")
		self.start()

	def start(self):
		self.server = IedServer(data=self.data, extra=("--home", self.home, "--workspace", self.ws))
		self.addCleanup(self.server.stop, False)
		self.assertTrue(self.server.started, self.server.error)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		await self.connect()

	async def connect(self):
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		await require_profiler(self, self.client)
		self.library = await self.subscribed("profs:w1")

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def subscribed(self, topic):
		await self.client.sub([topic])
		return (await self.client.next_frame(topic))["data"]

	async def connect_info(self):
		for _ in range(100):
			info = (await self.client.request("prof.captures", {"wsId": "w1"}))["connect"]
			if info["port"]:
				return info
			await asyncio.sleep(0.05)
		self.fail("the profiler never listened")

	async def send(self, data, chunked=True):
		info = await self.connect_info()
		sock = socket.create_connection(("127.0.0.1", info["port"]), timeout=10)
		if chunked:
			for i in range(0, len(data), 97):
				sock.sendall(data[i:i + 97])
		else:
			sock.sendall(data)
		return sock

	def token(self):
		return self.library["connect"]["env"].split("token=")[1]

	async def capture_that_ends(self, data):
		sock = await self.send(data)
		frame = (await self.client.wait_for("profs:w1", lambda f: f.get("kind") == "captures.changed" and any(c["live"] for c in f["data"]["captures"]), timeout=15))[-1]
		capture = [c for c in frame["data"]["captures"] if c["live"]][0]
		topic = "prof:" + capture["captureId"]
		await self.client.sub([topic])
		await self.client.wait_for(topic, lambda f: f.get("kind") == "ended" or (f.get("t") == "snap" and not f["data"]["live"]), timeout=15)
		sock.close()
		return capture["captureId"]

	async def query(self, method, **params):
		return await self.client.request(method, params)


class ProfilerTests(ProfilerServer):
	async def test_a_program_streams_a_live_capture_to_a_file(self):
		self.assertTrue(self.library["connect"]["env"].startswith("127.0.0.1:"), self.library)
		data = game_stream(self.token(), bye=False)
		sock = await self.send(data)
		frame = (await self.client.wait_for("profs:w1", lambda f: f.get("kind") == "captures.changed" and any(c["live"] for c in f["data"]["captures"]), timeout=15))[-1]
		live = [c for c in frame["data"]["captures"] if c["live"]][0]
		self.assertTrue(live["path"].startswith(".ied/local/captures/Game-"), live)
		topic = "prof:" + live["captureId"]
		await self.client.sub([topic])
		await self.client.wait_for(topic, lambda f: f["data"].get("zones", 0) == 10, timeout=15)
		timeline = await self.query("prof.timeline", captureId=live["captureId"], width=800)
		self.assertTrue(timeline["live"])
		self.assertEqual(["Main Thread", "Job Worker 0"], [lane["label"] for lane in timeline["lanes"]])
		sock.sendall(itp.Writer().chunk(itp.GLOBAL_CHUNK).event("Bye", reason=0).encode())
		ended = (await self.client.wait_for(topic, lambda f: f.get("kind") == "ended", timeout=15))[-1]["data"]
		self.assertFalse(ended["live"])
		sock.close()
		path = workspace_file(self.data, self.ws, live["path"])
		with open(path, "rb") as f:
			trace = f.read()
		end, meta = itp.stream_end(trace)
		self.assertEqual(data + itp.Writer().chunk(itp.GLOBAL_CHUNK).event("Bye", reason=0).encode(), trace[:end], "the stream is kept as it arrived")
		self.assertEqual(("Game", 10, 3, "bye"), (meta["app"], meta["zones"], meta["frames"], meta["ended"]))

	async def test_locks_answer_what_waited_for_what(self):
		capture = await self.capture_that_ends(lock_stream(self.token()))
		rows = (await self.query("prof.locks", captureId=capture))["rows"]
		self.assertEqual(1, len(rows))
		row = rows[0]
		self.assertEqual(("Queue", "src/queue.cf", 12), (row["name"], row["file"], row["line"]))
		self.assertEqual((2, 1, 1, 30_000), (row["acquisitions"], row["contended"], row["blocked"], row["waitNs"]))
		self.assertEqual({"track": 1, "index": 0, "fromNs": 20_000, "toNs": 50_000}, {k: row["worst"][k] for k in ("track", "index", "fromNs", "toNs")})
		hold = await self.query("prof.lockHold", captureId=capture, lock=1, track=0, index=0)
		self.assertEqual(("contended", 40_000), (hold["state"], hold["holdNs"]))
		self.assertEqual([(1, 20_000, 50_000)], [(w["track"], w["fromNs"], w["toNs"]) for w in hold["waitedBy"]])
		with self.assertRaises(RpcFailure) as missing:
			await self.query("prof.lockHold", captureId=capture, lock=1, track=0, index=7)
		self.assertEqual("not_found", missing.exception.code)
		lanes = (await self.query("prof.timeline", captureId=capture, width=1000))["lanes"]
		self.assertEqual([[[10_000, 50_000, 1, 0]], [[20_000, 50_000, 2, 0], [50_000, 60_000, 0, 0]]], [lane["locks"][0]["items"] for lane in lanes])
		text = (await self.client.request("plugins.call", {"name": "profiler.query", "wsId": "w1", "args": {"wsId": "w1", "tool": "locks", "args": {"capture": capture}}}))["result"]["text"]
		self.assertIn("Queue", text)
		self.assertIn("30.0 us", text)

	async def test_notes_are_kept_in_the_trace_file(self):
		capture = await self.capture_that_ends(game_stream(self.token()))
		topic = "prof:" + capture
		await self.client.sub([topic])
		added = (await self.query("prof.annotate", captureId=capture, **{"from": 5 * MS, "to": 2 * MS, "text": "  slow physics  "}))["annotation"]
		self.assertEqual((2 * MS, 5 * MS, "slow physics", "owner"), (added["fromNs"], added["toNs"], added["text"], added["by"]), "the range in order, the text trimmed")
		pushed = await self.client.wait_for(topic, lambda f: f.get("kind") == "annotations", timeout=10)
		self.assertEqual(["slow physics"], [n["text"] for n in pushed[-1]["data"]["annotations"]])
		with self.assertRaises(RpcFailure) as empty:
			await self.query("prof.annotate", captureId=capture, **{"from": 0, "text": "   "})
		self.assertEqual("bad_request", empty.exception.code)
		said = await self.client.request("plugins.call", {"name": "profiler.query", "wsId": "w1", "args": {"wsId": "w1", "tool": "annotate", "args": {"capture": capture, "from_ms": 20, "text": "GC pause"}}})
		self.assertIn("Added note", said["result"]["text"])
		self.client.abort()
		self.server.stop(False)
		self.start()
		await self.connect()
		notes = (await self.query("prof.annotations", captureId=capture))["annotations"]
		self.assertEqual([("slow physics", "owner"), ("GC pause", "ai")], [(n["text"], n["by"]) for n in notes], "both survive a restart, read from the file")
		await self.query("prof.updateAnnotation", captureId=capture, id=notes[0]["id"], text="physics spike")
		await self.query("prof.deleteAnnotation", captureId=capture, id=notes[1]["id"])
		self.assertEqual(["physics spike"], [n["text"] for n in (await self.query("prof.annotations", captureId=capture))["annotations"]])
		summary = await self.client.request("plugins.call", {"name": "profiler.query", "wsId": "w1", "args": {"wsId": "w1", "tool": "summary", "args": {"capture": capture}}})
		self.assertIn("Notes on the capture", summary["result"]["text"])
		self.assertIn("physics spike", summary["result"]["text"])
		with self.assertRaises(RpcFailure) as gone:
			await self.query("prof.deleteAnnotation", captureId=capture, id="n99")
		self.assertEqual("not_found", gone.exception.code)

	async def test_a_live_captures_notes_are_written_when_it_ends(self):
		sock = await self.send(game_stream(self.token(), bye=False))
		frame = (await self.client.wait_for("profs:w1", lambda f: f.get("kind") == "captures.changed" and any(c["live"] for c in f["data"]["captures"]), timeout=15))[-1]
		capture = [c for c in frame["data"]["captures"] if c["live"]][0]["captureId"]
		await self.query("prof.annotate", captureId=capture, **{"from": MS, "text": "while live"})
		sock.close()
		await self.client.sub(["prof:" + capture])
		await self.client.wait_for("prof:" + capture, lambda f: f.get("kind") == "ended" or (f.get("t") == "snap" and not f["data"]["live"]), timeout=15)
		listed = [c for c in (await self.query("prof.captures", wsId="w1"))["captures"] if c["captureId"] == capture][0]
		self.assertEqual(["while live"], [n["text"] for n in listed["metadata"]["annotations"]], "in the file's metadata")

	async def test_queries_answer_from_a_finished_capture(self):
		capture = await self.capture_that_ends(game_stream(self.token()))
		summary = await self.query("prof.summary", captureId=capture)
		self.assertEqual(("Game", 4242, 10, 3), (summary["app"], summary["pid"], summary["zones"], summary["frames"]["count"]))
		stats = await self.query("prof.stats", captureId=capture, order="total")
		self.assertEqual(["Update", "Physics::Step", "Render"], [row["name"] for row in stats["rows"]])
		self.assertEqual(("src/physics.cf", 40, 4, 14 * MS), (stats["rows"][1]["file"], stats["rows"][1]["line"], stats["rows"][1]["count"], stats["rows"][1]["totalNs"]))
		found = await self.query("prof.findZone", captureId=capture, name="Physics::Step", bins=8)
		self.assertEqual((4, 5 * MS), (found["count"], found["maxNs"]))
		zone = await self.query("prof.zone", captureId=capture, ref=found["instances"][0]["ref"])
		self.assertEqual(("Physics::Step", "Main Thread", "Update"), (zone["name"], zone["thread"], zone["parents"][0]["name"]))
		flame = await self.query("prof.flame", captureId=capture)
		self.assertEqual("Update", flame["root"]["children"][0]["name"])
		frames = await self.query("prof.frames", captureId=capture, width=100)
		self.assertEqual(3, len(frames["bars"]))
		worst = await self.query("prof.frame", captureId=capture)
		self.assertEqual(16 * MS, worst["durationNs"])
		messages = await self.query("prof.messages", captureId=capture)
		self.assertEqual(["arena loaded"], [m["text"] for m in messages["messages"]])
		leaks = await self.query("prof.leaks", captureId=capture)
		self.assertEqual((1, 4096), (leaks["count"], leaks["bytes"]))
		memory = await self.query("prof.memory", captureId=capture, width=50)
		self.assertEqual((1, 4160), (memory["gc"]["count"], memory["pools"][0]["peak"]))
		tasks = await self.query("prof.timeline", captureId=capture, mode="tasks", width=400)
		self.assertEqual(2, len(tasks["lanes"]), "threads that ran no task stay thread lanes")
		with self.assertRaises(RpcFailure) as missing:
			await self.query("prof.zone", captureId=capture, ref="9:0:0")
		self.assertEqual("not_found", missing.exception.code)
		with self.assertRaises(RpcFailure) as bad:
			await self.query("prof.zone", captureId=capture, ref="nonsense")
		self.assertEqual("bad_request", bad.exception.code)

	async def test_captures_survive_a_restart(self):
		capture = await self.capture_that_ends(game_stream(self.token()))
		self.client.abort()
		self.server.stop(False)
		self.start()
		await self.connect()
		listed = self.library["captures"]
		self.assertEqual([capture], [c["captureId"] for c in listed])
		self.assertEqual(("Game", 3), (listed[0]["metadata"]["app"], listed[0]["metadata"]["frames"]))
		timeline = await self.query("prof.timeline", captureId=capture, width=500)
		self.assertFalse(timeline["live"])
		self.assertEqual(3, len(timeline["frames"]["items"]))
		await self.query("prof.delete", captureId=capture)
		self.assertEqual([], (await self.query("prof.captures", wsId="w1"))["captures"], "a deleted capture goes to the trash")

	def closed_by_server(self, sock):
		"""Whether the server ended the connection: an orderly close, or a reset when it left bytes unread."""
		sock.settimeout(15)
		try:
			return sock.recv(16) == b""
		except ConnectionResetError:
			return True
		finally:
			sock.close()

	async def test_a_wrong_token_or_a_broken_stream_is_refused(self):
		sock = await self.send(game_stream("not-the-token"), chunked=False)
		self.assertTrue(self.closed_by_server(sock), "the server closes a connection whose Hello names no workspace")
		sock = await self.send(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n", chunked=False)
		self.assertTrue(self.closed_by_server(sock), "and one that is not ITP")
		self.assertEqual([], (await self.query("prof.captures", wsId="w1"))["captures"])

	async def test_instrumentation_errors_become_warnings(self):
		w = itp.Writer().hello(app="Broken", token=self.token())
		w.add(w.chunk(itp.THREAD_CHUNK, 1, 0).event("ZoneEnd", 5).event("ZoneBegin", 6, srcLocId=1).raw(b"\xee\x01"))
		w.add(w.chunk(itp.GLOBAL_CHUNK, 0, 0).event("Free", 7, threadId=1, poolId=0, address=1).event("Bye", reason=0))
		capture = await self.capture_that_ends(w.bytes())
		summary = await self.query("prof.summary", captureId=capture)
		self.assertEqual(3, summary["warningCount"], summary["warnings"])
		self.assertEqual(1, summary["zones"], "the open zone is closed at the end")
		pools = (await self.query("prof.memory", captureId=capture, width=100))["pools"]
		self.assertEqual([1], [p["earlierFrees"] for p in pools], "a free of a block from before the capture is counted, not warned of")

	async def test_chrome_traces_and_pprof_profiles_import(self):
		events = [
			{"name": "process_name", "ph": "M", "pid": 1, "tid": 1, "args": {"name": "Tool"}},
			{"name": "thread_name", "ph": "M", "pid": 1, "tid": 7, "args": {"name": "Loader"}},
			{"name": "Load", "ph": "X", "pid": 1, "tid": 7, "ts": 100.0, "dur": 50.0, "args": {"file": "a.png"}},
			{"name": "Decode", "ph": "B", "pid": 1, "tid": 7, "ts": 110.0},
			{"name": "Decode", "ph": "E", "pid": 1, "tid": 7, "ts": 130.0},
			{"name": "done", "ph": "i", "pid": 1, "tid": 7, "ts": 160.0},
			{"name": "queue", "ph": "C", "pid": 1, "tid": 7, "ts": 120.0, "args": {"depth": 3}}]
		write(os.path.join(self.ws, "trace.json"), {"traceEvents": events})
		chrome = (await self.query("prof.import", wsId="w1", path="trace.json"))["captureId"]
		timeline = await self.query("prof.timeline", captureId=chrome, width=200)
		lane = timeline["lanes"][0]
		self.assertEqual("Loader", lane["label"])
		self.assertEqual([[0, 0, 50_000], [1, 10_000, 30_000]], [z[:3] for z in lane["zones"]])
		self.assertEqual(["Load", "Decode"], [timeline["names"][z[3]] for z in lane["zones"]])
		self.assertEqual("queue", timeline["plots"][0]["name"])
		self.assertEqual("Tool", (await self.query("prof.summary", captureId=chrome))["app"])
		with open(os.path.join(self.ws, "cpu.pprof"), "wb") as f:
			f.write(pprof_profile())
		pprof = (await self.query("prof.import", wsId="w1", path="cpu.pprof"))["captureId"]
		flame = await self.query("prof.flame", captureId=pprof)
		self.assertEqual(("samples", "ns", 40_000_000), (flame["source"], flame["unit"], flame["root"]["total"]))
		main = flame["root"]["children"][0]
		self.assertEqual(("main", [("work", 30_000_000), ("idle", 10_000_000)]), (main["name"], [(c["name"], c["total"]) for c in main["children"]]))
		with self.assertRaises(RpcFailure) as refused:
			write(os.path.join(self.ws, "notes.json"), {"hello": 1})
			await self.query("prof.import", wsId="w1", path="notes.json")
		self.assertEqual("bad_request", refused.exception.code)

	async def test_the_investigation_tools_find_what_is_wrong(self):
		capture = await self.capture_that_ends(troubled_stream(self.token()))
		async def ask(tool, **args):
			return (await self.client.request("plugins.call", {"name": "profiler.query", "wsId": "w1", "args": {"wsId": "w1", "tool": tool, "args": args}}))["result"]["text"]
		diagnosis = await ask("diagnose")
		for found in ("Slow frames", "Level::Stream on Main", "Memory keeps growing", "Cache::Remember holds 120 allocations", "Allocations every frame", "Particles::Spawn: 50.0 a frame"):
			self.assertIn(found, diagnosis)
		self.assertNotIn("Lock contention", await ask("diagnose", focus="memory"), "focus keeps one area")
		allocations = await ask("allocations")
		self.assertIn("Particles::Spawn", allocations.splitlines()[5], "the most allocations first")
		self.assertIn("src/particles.cf:8", allocations)
		self.assertIn("Frame > Particles::Spawn", await ask("allocations", group_by="path"))
		leaks = await ask("leaks")
		self.assertIn("keeps accumulating", leaks)
		self.assertIn("src/cache.cf:20", leaks)
		self.assertIn("rose in", await ask("memory"))
		hitches = await ask("hitches")
		self.assertIn("Frame 30 at", hitches)
		self.assertIn("Level::Stream on Main: 30.0 ms of its own time", hitches)
		ref = re.search(r"\[(\d+:\d+:\d+)\]", hitches).group(1)
		self.assertIn("Zone Level::Stream [" + ref + "]", await ask("zone", ref=ref))
		self.assertIn("Particles::Spawn", await ask("timeline", from_ms=300, to_ms=340))
		self.assertIn("Main", await ask("threads"))
		self.assertIn("Plot Entities: min 1000", await ask("plots"))
		allocated = await self.query("prof.allocations", captureId=capture)
		self.assertEqual(("Particles::Spawn", 3000, 120), (allocated["groups"][0]["label"], allocated["groups"][0]["count"], allocated["live"]))
		self.assertEqual(1, (await self.query("prof.hitches", captureId=capture))["slow"])
		self.assertGreaterEqual((await self.query("prof.memoryTrend", captureId=capture))["series"][0]["floorRises"], 7)
		self.assertTrue((await self.query("prof.threads", captureId=capture))["threads"][0]["marksFrames"])
		self.assertEqual("Main", (await self.query("prof.window", captureId=capture, **{"from": 300 * MS, "to": 340 * MS}))["threads"][0]["name"])
		self.assertEqual("Entities", (await self.query("prof.plots", captureId=capture))["plots"][0]["name"])
		with self.assertRaises(RpcFailure):
			await ask("zone", ref="nonsense")

	async def test_a_collected_programs_counts_and_censuses_answer(self):
		capture = await self.capture_that_ends(collected_stream(self.token()))
		async def ask(tool, **args):
			return (await self.client.request("plugins.call", {"name": "profiler.query", "wsId": "w1", "args": {"wsId": "w1", "tool": tool, "args": args}}))["result"]["text"]
		allocations = await ask("allocations")
		self.assertIn("Counted by the program's collector", allocations)
		self.assertIn("Per frame (60 frames): mean 316.0", allocations)
		self.assertRegex(allocations, r"Particles::Spawn +18,000 +300\.0")
		leaks = await ask("leaks")
		self.assertIn("census after each of 6 collections", leaks)
		self.assertRegex(leaks, r"CacheEntry .* keeps accumulating")
		self.assertIn("a census of live objects by type after 6 collections", await ask("memory"))
		diagnosis = await ask("diagnose", focus="memory")
		self.assertIn("Allocations every frame: 316.0 a frame", diagnosis)
		self.assertIn("garbage for the collector", diagnosis)
		self.assertIn("CacheEntry: 10 -> 60 live objects", diagnosis)
		self.assertIn("its children's included", await ask("zone", ref="0:0:0"))
		census = await self.query("prof.census", captureId=capture)
		self.assertEqual(("CacheEntry", True, 6), (census["types"][0]["name"], census["types"][0]["accumulating"], census["censuses"]))
		self.assertEqual(316 * 60, (await self.query("prof.summary", captureId=capture))["memory"]["allocations"], "the summary counts what the process counted")

	async def test_the_ai_reads_captures_through_the_binding(self):
		await self.capture_that_ends(game_stream(self.token()))
		async def ask(tool, **args):
			return (await self.client.request("plugins.call", {"name": "profiler.query", "wsId": "w1", "args": {"wsId": "w1", "tool": tool, "args": args}}))["result"]["text"]
		listed = await ask("list")
		self.assertIn("Game", listed)
		summary = await ask("summary")
		self.assertIn("Frames: 3, mean 16.0 ms", summary)
		self.assertIn("Physics::Step", summary)
		top = await ask("top_zones", by="self", n=2)
		self.assertIn("Update", top)
		self.assertIn("src/game.cf:12", top)
		self.assertIn("Frame 0", await ask("frame"))
		self.assertIn("4 instances", await ask("find_zone", name="Physics::Step"))
		self.assertIn("Physics::Step", await ask("flame"))
		self.assertIn("arena loaded", await ask("messages"))
		self.assertIn("1 allocations (4.0 KiB) are live", await ask("leaks"))
		self.assertIn("A = ", await ask("compare", a="latest", b="latest"))
		path = (await self.query("prof.captures", wsId="w1"))["captures"][0]["path"]
		stem = os.path.splitext(os.path.basename(path))[0]
		self.assertIn("Frames: 3", await ask("summary", capture=stem), "a capture named as the Captures window shows it, without .iedtrace")
		with self.assertRaises(RpcFailure):
			await ask("summary", capture="no-such-capture")


CONVERTER = (
	"import json, sys\n"
	"source, output, scale = sys.argv[1], sys.argv[2], float(sys.argv[3])\n"
	"names = open(source, encoding='utf-8').read().split()[1:]\n"
	"if 'fail' in names:\n"
	"    sys.exit('cannot read ' + source)\n"
	"events = [{'name': n, 'ph': 'X', 'pid': 1, 'tid': 1, 'ts': i * 10 * scale, 'dur': 5 * scale} for i, n in enumerate(names)]\n"
	"json.dump({'traceEvents': events}, open(output, 'w'))\n")


def importer_plugin(home):
	"""A plugin whose profilerImporters entry turns `.up` files ("UP name name ...") into Chrome traces with a script."""
	plugin = os.path.join(home, "plugins", "importer")
	write(os.path.join(plugin, "convert.py"), CONVERTER)
	write(os.path.join(plugin, "settings.schema.json"), {"type": "object", "properties": {"importer.scale": {"type": "string", "default": "2"}}})
	write(os.path.join(plugin, "ied-plugin.json"), {
		"manifestVersion": 1, "id": "test.importer", "name": "Importer", "version": "0.1.0", "publisher": "IED", "ied": {"api": "^1"},
		"runtime": {"kind": "declarative"}, "permissions": [],
		"contributes": {
			"settings": {"schema": "settings.schema.json", "page": "Importer"},
			"profilerImporters": [{"id": "upper", "extensions": [".up"], "magic": "UP", "run": [PY, "-I", "${pluginFolder}/convert.py", "${input}", "${output}", "${config:importer.scale}"]}]}})


class ImporterTests(ProfilerServer):
	"""profilerImporters: another plugin's command converts its files, then the profiler reads what it wrote."""

	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-profiler-import-")
		self.addCleanup(remove_tree, self.root)
		self.home = make_home(os.path.join(self.root, "home"))
		importer_plugin(self.home)
		self.ws = os.path.join(self.root, "Game")
		os.makedirs(self.ws)
		self.data = os.path.join(self.root, "data")
		self.prepare()
		self.start()

	def prepare(self):
		pass

	async def import_file(self, name, text, trusted=True):
		await require_plugin(self, self.client, "test.importer")
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": trusted})
		if text is not None:
			write(os.path.join(self.ws, name), text)
		return await self.query("prof.import", wsId="w1", path=name)

	async def test_a_plugins_importer_converts_its_files(self):
		capture = (await self.import_file("steps.up", "UP load decode draw"))["captureId"]
		timeline = await self.query("prof.timeline", captureId=capture, width=200)
		zones = timeline["lanes"][0]["zones"]
		self.assertEqual([("load", 0, 10_000), ("decode", 20_000, 30_000), ("draw", 40_000, 50_000)],
			[(timeline["names"][z[3]], z[1], z[2]) for z in zones], "the importer's setting (a scale of 2) reached its command")
		entry = [c for c in (await self.query("prof.captures", wsId="w1"))["captures"] if c["captureId"] == capture][0]
		self.assertEqual("steps.iedtrace", os.path.basename(entry["path"]))
		self.assertEqual("upper", entry["metadata"]["format"])
		self.assertTrue(entry["metadata"]["importedFrom"].endswith("steps.up"), entry["metadata"])
		leftovers = [n for n in os.listdir(os.path.join(local_state(self.data, self.ws), "captures")) if n.startswith(".import-")]
		self.assertFalse(os.path.exists(os.path.join(self.ws, ".ied")), "a folder's captures live in the app data")
		self.assertEqual([], leftovers, "the converted file is removed")

	async def test_a_failing_importer_says_why(self):
		with self.assertRaises(RpcFailure) as refused:
			await self.import_file("broken.up", "UP fail")
		self.assertEqual("bad_request", refused.exception.code)
		self.assertIn("the test.importer importer upper failed", refused.exception.message)
		self.assertIn("cannot read", refused.exception.message)

	async def test_a_file_without_the_magic_goes_to_the_profilers_own_readers(self):
		with self.assertRaises(RpcFailure) as refused:
			await self.import_file("plain.up", "load decode")
		self.assertNotIn("importer", refused.exception.message)

	async def test_an_untrusted_workspace_runs_no_importer(self):
		with self.assertRaises(RpcFailure) as refused:
			await self.import_file("steps.up", "UP load", trusted=False)
		self.assertIn("not trusted", refused.exception.message)


GO_PROGRAM = """package main

import (
	"context"
	"os"
	"runtime/trace"
	"sync"
	"time"
)

func main() {
	out, _ := os.Create(os.Args[1])
	trace.Start(out)
	defer trace.Stop()
	ctx, task := trace.NewTask(context.Background(), "frame")
	var done sync.WaitGroup
	for i := 0; i < 3; i++ {
		done.Add(1)
		go func() {
			defer done.Done()
			trace.WithRegion(ctx, "work", func() { time.Sleep(2 * time.Millisecond) })
		}()
	}
	done.Wait()
	task.End()
}
"""


class GoTraceImportTests(ServerTestCase):
	"""The go plugin's importer: a Go execution trace imports with each goroutine's regions on a track of its own,
	beside each P's goroutines. Skipped without the go tool."""

	def setUp(self):
		if not shutil.which("go"):
			self.skipTest("no go tool")
		self.root = tempfile.mkdtemp(prefix="ied-profiler-go-")
		self.addCleanup(remove_tree, self.root)
		self.home = make_home(os.path.join(self.root, "home"))
		shutil.copytree(os.path.join(REPO, "plugins", "go"), os.path.join(self.home, "plugins", "go"), ignore=shutil.ignore_patterns("__pycache__", "profiler"))
		self.ws = os.path.join(self.root, "Game")
		program = os.path.join(self.root, "tracer")
		write(os.path.join(program, "go.mod"), "module tracer\n\ngo 1.22\n")
		write(os.path.join(program, "main.go"), GO_PROGRAM)
		os.makedirs(self.ws)
		subprocess.run(["go", "run", ".", os.path.join(self.ws, "trace.out")], cwd=program, check=True, capture_output=True, timeout=300)
		self.server = IedServer(data=os.path.join(self.root, "data"), extra=("--home", self.home, "--workspace", self.ws))
		self.addCleanup(self.server.stop, False)
		self.assertTrue(self.server.started, self.server.error)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		await require_profiler(self, self.client)

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def test_a_go_execution_trace_imports(self):
		await require_plugin(self, self.client, "ied.go")
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		# The importer builds itself with `go run` and runs `go tool trace`: seconds of work on a busy machine.
		capture = (await self.client.request("prof.import", {"wsId": "w1", "path": "trace.out"}, timeout=120))["captureId"]
		timeline = await self.client.request("prof.timeline", {"captureId": capture, "width": 400})
		labels = {lane["label"]: [timeline["names"][z[3]] for z in lane["zones"] if z[3] >= 0] for lane in timeline["lanes"]}
		workers = [names for label, names in labels.items() if label.endswith("main.main.func1")]
		self.assertEqual([["work"]] * 3, workers, labels)
		self.assertTrue(any(label.startswith("Proc ") for label in labels), labels)
		captures = (await self.client.request("prof.captures", {"wsId": "w1"}))["captures"]
		self.assertEqual("go.trace", [c for c in captures if c["captureId"] == capture][0]["metadata"]["format"])


class ProfileTaskTests(ServerTestCase):
	"""The Profile task: ${profiler.connect} names the listener and the workspace's token, and profile_capture runs the
	task, records while the program streams, and stops it."""

	PROGRAM = (
		"import os,socket,struct,sys,time\n"
		"sys.path.insert(0, %r)\n"
		"import itp\n"
		"address, token = os.environ['IED_PROFILER'].split(';token=')\n"
		"host, port = address.split(':')\n"
		"s = socket.create_connection((host, int(port)))\n"
		"w = itp.Writer().hello(app='Looper', token=token)\n"
		"defs = w.chunk(itp.GLOBAL_CHUNK)\n"
		"site = w.srcloc('Tick', defs)\n"
		"w.add(defs)\n"
		"s.sendall(w.bytes())\n"
		"t = 0\n"
		"while True:\n"
		"    c = itp.Chunk(itp.THREAD_CHUNK, 1, t)\n"
		"    c.event('ZoneBegin', t + 1000, srcLocId=site).event('ZoneEnd', t + 900000)\n"
		"    try:\n"
		"        s.sendall(c.encode())\n"
		"    except OSError:\n"
		"        pass\n"
		"    t += 1000000\n"
		"    time.sleep(0.01)\n") % os.path.join(REPO, "tools", "pylib")

	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-profile-task-")
		self.addCleanup(remove_tree, self.root)
		self.home = make_home(os.path.join(self.root, "home"))
		self.repo = os.path.join(self.root, "Game")
		write(os.path.join(self.repo, "loop.py"), self.PROGRAM)
		tasks = {
			"show": {"steps": [{"run": [PY, "-I", "-c", "import os; print('connect=' + os.environ['IED_PROFILER'])"], "env": {"IED_PROFILER": "${profiler.connect}"}}]},
			"profile": {"slot": "profile", "steps": [{"run": [PY, "-I", os.path.join(self.repo, "loop.py")], "env": {"IED_PROFILER": "${profiler.connect}"}}]}}
		write(os.path.join(self.repo, "Game.ied.proj"), {"version": 1, "name": "Game", "root": ".", "kind": "executable", "tasks": tasks})
		write(os.path.join(self.repo, "Game.ied.sln"), {"version": 1, "name": "Game", "projects": [{"file": "Game.ied.proj"}]})
		self.server = IedServer(extra=("--home", self.home, "--workspace", os.path.join(self.repo, "Game.ied.sln")))
		self.addCleanup(self.server.stop)
		self.assertTrue(self.server.started, self.server.error)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		await require_profiler(self, self.client)
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})

	async def asyncTearDown(self):
		self.client.abort()

	async def test_the_task_variable_names_the_listener(self):
		library = (await self.client.request("prof.captures", {"wsId": "w1"}))
		run = await self.client.request("task.run", {"wsId": "w1", "task": "show"})
		await self.client.sub([run["topic"]])
		frames = await self.client.wait_for(run["topic"], lambda f: f.get("kind") == "run.end" or (f["t"] == "snap" and f["data"]["status"] in ("succeeded", "failed", "cancelled")), timeout=30)
		text = "".join(f["data"]["text"] for f in frames if f.get("kind") == "output") + "".join(f["data"].get("tail", "") for f in frames if f["t"] == "snap")
		self.assertIn("connect=" + library["connect"]["env"], text)

	async def test_profile_capture_records_and_stops_the_program(self):
		started = time.monotonic()
		answer = (await self.client.request("plugins.call", {"name": "profiler.query", "wsId": "w1", "args": {"wsId": "w1", "tool": "capture", "args": {"seconds": 1}}}))["result"]["text"]
		self.assertIn("Looper", answer)
		self.assertIn("Tick", answer)
		self.assertLess(time.monotonic() - started, 60)
		runs = (await self.client.request("task.runs", {"wsId": "w1"}))["runs"]
		self.assertEqual("cancelled", [r for r in runs if r["task"] == "profile"][0]["status"], "the program is stopped after recording")
		captures = (await self.client.request("prof.captures", {"wsId": "w1"}))["captures"]
		self.assertEqual([False], [c["live"] for c in captures])
