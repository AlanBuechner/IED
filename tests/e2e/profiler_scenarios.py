"""The profiler end to end (docs/16, mockup 06): a program streams a live capture to the server; the Captures window and
the capture's toolbar say it is recording; the capture opens as a document with its frame graph, lanes and statistics;
a frame zooms the timeline, a zone's details name its source; and the capture is kept when the program says Bye."""

import os
import socket
import sys
import threading
import time

from harness import check
from scenarios import scenario

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools", "pylib"))
import itp  # noqa: E402

MS = 1_000_000
FRAME = 16 * MS


class LiveProgram(threading.Thread):
	"""A program recording frames in real time: Update and Render on its main thread, Physics on a worker it renames."""

	def __init__(self, env):
		super().__init__(daemon=True)
		address, self.token = env.split(";token=")
		host, port = address.split(":")
		self.sock = socket.create_connection((host, int(port)))
		self.stopping = threading.Event()

	def run(self):
		w = itp.Writer().hello(app="E2E Game", pid=777, freq=1_000_000_000, start_ticks=0, token=self.token)
		defs = w.chunk(itp.GLOBAL_CHUNK)
		update = w.srcloc("Update", defs, function="Game::Update", file="src/game.c", line=12)
		physics = w.srcloc("Physics::Step", defs, file="src/physics.c", line=40)
		render = w.srcloc("Render", defs, file="src/render.c", line=7)
		names = [w.string(name, defs) for name in ("Main", "Worker", "Physics")]
		w.add(defs)
		self.sock.sendall(w.bytes())
		start = time.monotonic()
		frame = 0
		while not self.stopping.is_set():
			t = frame * FRAME
			main = w.chunk(itp.THREAD_CHUNK, 1, t)
			if frame == 0:
				main.event("ThreadName", t, nameStr=names[0])
			main.event("FrameMark", t, frameSetStr=0)
			main.event("ZoneBegin", t + 1 * MS, srcLocId=update).event("ZoneEnd", t + 9 * MS)
			main.event("ZoneBegin", t + 10 * MS, srcLocId=render).event("ZoneEnd", t + 15 * MS)
			worker = w.chunk(itp.THREAD_CHUNK, 2, t).event("ThreadName", t + 2 * MS, nameStr=names[2])
			worker.event("TaskBegin", t + 2 * MS, taskId=frame + 1, nameStr=names[2], parentTaskId=0)
			worker.event("ZoneBegin", t + 2 * MS, srcLocId=physics).event("ZoneEnd", t + 6 * MS)
			worker.event("TaskEnd", t + 6 * MS, taskId=frame + 1).event("ThreadName", t + 7 * MS, nameStr=names[1])
			self.sock.sendall(main.encode() + worker.encode())
			frame += 1
			time.sleep(max(0.0, start + frame * FRAME / 1e9 - time.monotonic()))
		self.sock.sendall(itp.Chunk(itp.GLOBAL_CHUNK, 0, 0).event("Bye", reason=0).encode())
		self.sock.close()

	def stop(self):
		self.stopping.set()
		self.join(10)


def canvas_point(page, selector, fx, y):
	box = page.query_selector(selector).bounding_box()
	return box["x"] + box["width"] * fx, box["y"] + y


@scenario(viewports=("desktop", "phone"), targets=("real",))
def profiler_live_capture(run):
	page = run.open()
	ws = page.evaluate("() => ied_shell.wsId")
	program = LiveProgram(run.request("prof.captures", {"wsId": ws})["connect"]["env"])
	program.start()
	try:
		page.evaluate("() => ied_shell.commands.run('shell.showActivity', { id: 'ied.profiler' })")
		if run.phone:
			page.evaluate("() => ied_shell.commands.run('shell.showView', { id: 'ied.profiler.captures' })")
		page.wait_for_selector(".prof-library .prof-capture.live", timeout=20000)
		run.shot("library-live")
		page.click(".prof-library .prof-capture.live")
		page.wait_for_selector(".prof-doc .prof-canvas", timeout=20000)
		run.wait_text(".prof-bar .rec", "Capturing E2E Game", timeout=20000)
		time.sleep(1.5)
		run.shot("live")
	finally:
		program.stop()
	page.wait_for_function("() => !document.querySelector('.prof-bar .rec').textContent.includes('Capturing')", timeout=20000)
	check("E2E Game" in page.inner_text(".prof-bar .rec"), "the toolbar names the program once it said Bye, no longer recording")
	page.mouse.click(*canvas_point(page, ".prof-doc .fg", 0.004, 30))
	time.sleep(0.8)
	x, y = canvas_point(page, ".prof-doc .tl", 0.6, 52)
	page.mouse.click(x, y)
	run.wait_text(".prof-side .zname", "Update", timeout=10000)
	check("src/game.c:12" in page.inner_text(".prof-side"), "the zone's details name its source")
	run.shot("zone")
	if not run.phone:
		page.wait_for_selector(".prof-doc .stats tbody tr", timeout=10000)
		rows = run.texts(".prof-doc .stats tbody tr td:first-child")
		check({"Update", "Render", "Physics::Step"} <= set(rows), "statistics list the zones in view: %s" % rows)
		page.click(".prof-doc .prof-bar .seg span[data-mode='tasks']")
		time.sleep(1.0)
		run.shot("tasks")
	library = run.request("prof.captures", {"wsId": ws})["captures"]
	check(library and all(not c["live"] and c["metadata"]["app"] == "E2E Game" and c["metadata"]["frames"] > 10 for c in library), "each capture is kept with its metadata: %s" % library)


def send_lock_game(env):
	"""A finished capture of a lock two threads wanted (1 ns ticks): Main holds Queue from 10 to 50 ms while Loader waits
	for it from 20 ms, then Loader holds it to 60 ms."""
	address, token = env.split(";token=")
	host, port = address.split(":")
	w = itp.Writer().hello(app="Lock Game", pid=778, freq=1_000_000_000, start_ticks=0, token=token)
	defs = w.chunk(itp.GLOBAL_CHUNK)
	work = w.srcloc("Work", defs, file="src/work.c", line=3)
	queue = w.string("Queue", defs)
	site = w.srcloc("Queue", defs, file="src/queue.c", line=12)
	names = [w.string(name, defs) for name in ("Main", "Loader")]
	defs.event("LockDef", lockId=1, nameStr=queue, srcLocId=site)
	w.add(defs)
	main = w.chunk(itp.THREAD_CHUNK, 1, 0).event("ThreadName", 0, nameStr=names[0]).event("ZoneBegin", 0, srcLocId=work)
	w.add(main.event("LockObtain", 10 * MS, lock=2).event("LockRelease", 50 * MS, lock=2).event("ZoneEnd", 100 * MS))
	loader = w.chunk(itp.THREAD_CHUNK, 2, 0).event("ThreadName", 0, nameStr=names[1]).event("ZoneBegin", 0, srcLocId=work)
	loader.event("LockWait", 20 * MS, lock=2).event("LockObtain", 50 * MS, lock=2).event("LockRelease", 60 * MS, lock=2)
	w.add(loader.event("ZoneEnd", 100 * MS))
	w.add(w.chunk(itp.GLOBAL_CHUNK).event("Bye", reason=0))
	with socket.create_connection((host, int(port))) as sock:
		sock.sendall(w.bytes())


def new_capture(run, ws, known):
	deadline = time.monotonic() + 20
	while time.monotonic() < deadline:
		ended = [c["captureId"] for c in run.request("prof.captures", {"wsId": ws})["captures"] if c["captureId"] not in known and not c["live"]]
		if ended:
			return ended[0]
		time.sleep(0.3)
	raise AssertionError("the capture never ended")


@scenario(viewports=("desktop", "phone"), targets=("real",))
def profiler_locks_and_notes(run):
	"""Locks (F-PRF-18) and notes (F-PRF-19): the Locks window lists a lock two threads wanted; its row shows the
	longest wait on the timeline with the hold's details; Note keeps a note on that wait, listed in the Notes window,
	kept by the server and deleted from the window."""
	page = run.open()
	ws = page.evaluate("() => ied_shell.wsId")
	listed = run.request("prof.captures", {"wsId": ws})
	send_lock_game(listed["connect"]["env"])
	capture = new_capture(run, ws, {c["captureId"] for c in listed["captures"]})
	page.evaluate("() => ied_shell.commands.run('shell.showActivity', { id: 'ied.profiler' })")
	if run.phone:
		page.evaluate("() => ied_shell.commands.run('shell.showView', { id: 'ied.profiler.captures' })")
	page.click(".prof-library .prof-capture[data-capture='%s']" % capture)
	page.wait_for_selector(".prof-doc .prof-canvas", timeout=20000)
	page.click(".prof-doc .tw-tab[data-panel='locks']")
	run.wait_text(".prof-doc .stats tbody tr td:first-child", "Queue")
	row = run.texts(".prof-doc .stats tbody tr td")
	check(row[:4] == ["Queue", "src/queue.c:12", "1", "30.0"], "one wait of 30 ms on Queue: %s" % row[:4])
	run.shot("locks")
	page.click(".prof-doc .stats tbody tr")
	run.wait_text(".prof-side", "Held alone")
	side = page.inner_text(".prof-side")
	check("Loader" in side and "30.0 ms" in side, "the longest wait's details: %s" % side[:200])
	run.shot("wait")
	page.click(".prof-doc .prof-bar button[title^='Add a note']")
	page.fill(".dialog-veil input", "Loader waits for Queue")
	page.keyboard.press("Enter")
	page.click(".prof-doc .tw-tab[data-panel='notes']")
	run.wait_text(".prof-doc .prof-note", "Loader waits for Queue")
	check("0.020 s to 0.050 s" in page.inner_text(".prof-doc .prof-note"), "the note covers the wait")
	kept = run.request("prof.annotations", {"captureId": capture})["annotations"]
	check([(n["fromNs"], n["toNs"], n["by"]) for n in kept] == [(20 * MS, 50 * MS, "owner")], "the server keeps the note: %s" % kept)
	run.shot("notes")
	page.click(".prof-doc .prof-note button[title='Delete the note']")
	page.click(".dialog-veil button:has-text('Delete')")
	run.wait_text(".prof-doc .prof-empty", "No notes yet")
	check(run.request("prof.annotations", {"captureId": capture})["annotations"] == [], "deleted on the server")


def send_collected_game(env):
	"""A collected program's finished capture (1 ns ticks, ten 16 ms frames) that counts its allocations instead of
	sending each one: Frame holds Particles::Spawn (300 objects, 7200 bytes) and Cache::Remember (one of 32 bytes, and
	4 KiB of native memory its thread took); the
	process's count with each frame mark; its GC heap (a sawtooth) and process memory (climbing) plotted; a collection
	that did not say what the heap held before; a census each frame in which CacheEntry grows by 100 KB; an allocation
	sampled each frame with its call stack, in SpawnParticles but once in Remember."""
	address, token = env.split(";token=")
	host, port = address.split(":")
	w = itp.Writer().hello(app="Collected Game", pid=779, freq=1_000_000_000, start_ticks=0, token=token)
	defs = w.chunk(itp.GLOBAL_CHUNK)
	frame = w.srcloc("Frame", defs, file="src/main.cf", line=60)
	spawn = w.srcloc("Particles::Spawn", defs, file="src/main.cf", line=20)
	cache = w.srcloc("Cache::Remember", defs, file="src/main.cf", line=38)
	main_name, heap, process, particle, entry = (w.string(s, defs) for s in ("Main", "GC heap", "Process memory", "Particle", "CacheEntry"))
	defs.event("PlotDef", plotId=1, nameStr=heap, kind=1, flags=0).event("PlotDef", plotId=2, nameStr=process, kind=1, flags=0)
	for stack, (address, name) in enumerate(((0x1000, "SpawnParticles"), (0x3000, "Remember")), 1):
		defs.event("CallstackDef", callstackId=stack, addresses=[address, 0x9000])
		defs.event("SymbolDef", address=address, functionStr=w.string(name, defs), fileStr=0, line=0)
	w.add(defs)
	main = w.chunk(itp.THREAD_CHUNK, 1, 0).event("ThreadName", 0, nameStr=main_name)
	counts = w.chunk(itp.GLOBAL_CHUNK)
	for f in range(11):
		t = f * FRAME
		main.event("FrameMark", t, frameSetStr=0)
		counts.event("AllocCount", t, objects=301 * f, bytes=7232 * f)
		counts.event("PlotValue", t, plotId=1, value=float(100_000 + 50_000 * (f % 4)))
		counts.event("PlotValue", t, plotId=2, value=float(60_000_000 + 1_000_000 * f))
		if f == 10:
			break
		main.event("ZoneBegin", t + 1 * MS, srcLocId=frame)
		main.event("ZoneBegin", t + 2 * MS, srcLocId=spawn).event("ZoneAllocs", objects=300, bytes=7200).event("ZoneEnd", t + 4 * MS)
		main.event("ZoneBegin", t + 5 * MS, srcLocId=cache).event("ZoneAllocs", objects=1, bytes=32).event("ZoneNative", bytes=4096).event("ZoneEnd", t + 6 * MS)
		main.event("ZoneAllocs", objects=301, bytes=7232).event("ZoneNative", bytes=4096).event("ZoneEnd", t + 12 * MS)
		if f == 3:
			counts.event("GcBegin", t + 13 * MS, kind=0, generation=0)
			counts.event("GcEnd", t + 14 * MS, bytesBefore=0, bytesAfter=100_000, objectsFreed=0, pinned=0, roots=0)
		counts.event("Sample", t + 3 * MS, threadId=1, callstackId=2 if f == 5 else 1, kind=2, weight=65536)
		counts.event("HeapCensus", t + 14 * MS, types=2)
		counts.event("HeapCensusType", nameStr=particle, objects=300, bytes=7200)
		counts.event("HeapCensusType", nameStr=entry, objects=1000 * (f + 1), bytes=100_000 * (f + 1))
	w.add(main)
	w.add(counts)
	w.add(w.chunk(itp.GLOBAL_CHUNK).event("Bye", reason=0))
	with socket.create_connection((host, int(port))) as sock:
		sock.sendall(w.bytes())


def send_native_game(env):
	"""A finished capture that sends each allocation: three blocks from the heap, one of them freed."""
	address, token = env.split(";token=")
	host, port = address.split(":")
	w = itp.Writer().hello(app="Native Game", pid=780, freq=1_000_000_000, start_ticks=0, token=token)
	defs = w.chunk(itp.GLOBAL_CHUNK)
	defs.event("PoolDef", poolId=1, nameStr=w.string("heap", defs))
	w.add(defs)
	main = w.chunk(itp.THREAD_CHUNK, 1, 0).event("FrameMark", 0, frameSetStr=0)
	for k in range(3):
		main.event("Alloc", (k + 1) * MS, threadId=1, poolId=1, address=0x1000 * (k + 1), size=256, callstackId=0)
	main.event("Free", 5 * MS, threadId=1, poolId=1, address=0x1000).event("FrameMark", 16 * MS, frameSetStr=0)
	w.add(main)
	w.add(w.chunk(itp.GLOBAL_CHUNK).event("Bye", reason=0))
	with socket.create_connection((host, int(port))) as sock:
		sock.sendall(w.bytes())


def show_panel(page, doc, key):
	"""Shows a capture's tool window: a tab click toggles it, and a new capture opens with the last one shown."""
	if not page.query_selector(doc + " .tw-tab.on[data-panel='%s']" % key):
		page.click(doc + " .tw-tab[data-panel='%s']" % key)
	page.wait_for_selector(doc + " .prof-panel-body", state="visible", timeout=10000)


def open_capture(run, page, capture):
	page.evaluate("() => ied_shell.commands.run('shell.showActivity', { id: 'ied.profiler' })")
	if run.phone:
		page.evaluate("() => ied_shell.commands.run('shell.showView', { id: 'ied.profiler.captures' })")
	page.click(".prof-library .prof-capture[data-capture='%s']" % capture)
	page.wait_for_selector(".prof-doc[data-capture='%s'] .prof-canvas" % capture, timeout=20000)


@scenario(viewports=("desktop", "phone"), targets=("real",))
def profiler_collected_memory(run):
	"""Memory (F-PRF-30, F-PRF-33) for a collected program that counts its allocations: the Memory window draws its GC
	heap and process memory, lists what each zone allocated, the functions its collector's samples name and the types
	its censuses found accumulating, and says nothing of what an unsized collection freed; a zone's details say what it
	allocated, its own share too; a program that sends each allocation still gets those live at the end."""
	page = run.open()
	ws = page.evaluate("() => ied_shell.wsId")
	listed = run.request("prof.captures", {"wsId": ws})
	known = {c["captureId"] for c in listed["captures"]}
	send_collected_game(listed["connect"]["env"])
	capture = new_capture(run, ws, known)
	open_capture(run, page, capture)
	doc = ".prof-doc[data-capture='%s']" % capture
	show_panel(page, doc, "memory")
	run.wait_text(doc + " .prof-memory-right", "Particles::Spawn")
	memory = page.inner_text(doc + " .prof-memory")
	check("GC heap" in memory and "Process memory" in memory and "Collections" in memory, "both memory plots and the collection: %s" % memory[:300])
	check("freed" not in memory, "nothing said of what a collection freed when the program did not size it")
	rows = run.texts(doc + " .prof-memory-right .prof-leak summary")
	check("Particles::Spawn" in rows[0] and "3,000x" in rows[0] and "300 a frame" in rows[0] and "main.cf:20" in rows[0], "Spawn allocated the most, 300 a frame: %s" % rows)
	check("Cache::Remember" in rows[1] and "+40.0 KiB native" in rows[1], "then Cache::Remember, with the native memory it took: %s" % rows)
	check("CacheEntry" in memory and "keeps accumulating" in memory, "the census's growing type is marked")
	run.shot("memory")
	page.select_option(doc + " .prof-panel select", "function")
	run.wait_text(doc + " .prof-memory-right", "SpawnParticles")
	sampled = run.texts(doc + " .prof-memory-right .prof-leak summary")
	check("SpawnParticles" in sampled[0] and "9 samples" in sampled[0] and "Remember" in sampled[1], "the collector's samples by the function that allocated: %s" % sampled)
	run.shot("sampled")
	if run.phone:
		page.click(doc + " .prof-bar button.phone-only")
	page.mouse.click(*canvas_point(page, doc + " .fg", 0.05, 30))
	time.sleep(0.8)
	page.mouse.click(*canvas_point(page, doc + " .tl", 0.6, 52))
	run.wait_text(doc + " .prof-side .zname", "Frame", timeout=10000)
	side = page.inner_text(doc + " .prof-side")
	check("IN THIS ZONE" in side.upper(), "the details say what happened in the zone")
	check("301 (7.1 KiB)" in side and "self 0 (0 B)" in side, "it allocated 301, all in its children: %s" % side[:400])
	check("Native memory" in side and "4.0 KiB" in side, "and its children took 4 KiB of native memory: %s" % side[:400])
	run.shot("zone")
	known.add(capture)
	send_native_game(listed["connect"]["env"])
	native = new_capture(run, ws, known)
	open_capture(run, page, native)
	doc = ".prof-doc[data-capture='%s']" % native
	show_panel(page, doc, "memory")
	run.wait_text(doc + " .prof-memory-right .group-head", "Live allocations")
	page.wait_for_selector(doc + " .prof-memory-right .prof-leak summary", timeout=10000)
	live = run.texts(doc + " .prof-memory-right .prof-leak summary")
	check(len(live) == 1 and "2x" in live[0] and "512 B" in live[0], "the two blocks still live: %s" % live)
	run.shot("native")
