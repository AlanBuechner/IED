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
