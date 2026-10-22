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
