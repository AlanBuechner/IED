"""Task runs: task.list/run/cancel/runs, 'run:<runId>' topics, output mirrored to 'out:' and diagnostics to 'diag:'."""

import asyncio
import re
import time

from fixtures import task_output
from rpc import RpcError

DIAGNOSTIC = re.compile(r"^(?:\x1b\[\d+m)?(?P<path>[\w./-]+)\((?P<line>\d+),(?P<col>\d+)\): (?P<severity>error|warning) (?P<code>\w+): (?P<message>.*?)(?:\x1b\[0m)?\r?\n?$")
TAIL_CHARS = 64 * 1024
FLOOD_YIELD_EVERY = 100


class Run:
	def __init__(self, run_id, ws_id, task_id, label):
		self.run_id = run_id
		self.ws_id = ws_id
		self.task_id = task_id
		self.label = label
		self.status = "running"
		self.steps = []
		self.exit_code = None
		self.tail = ""
		self.started = time.time()
		self.runner = None
		self.stop_reason = "cancelled"

	@property
	def topic(self):
		return "run:" + self.run_id

	def summary(self):
		return {"runId": self.run_id, "task": self.task_id, "label": self.label, "status": self.status, "exitCode": self.exit_code}


class Tasks:
	def __init__(self, server):
		self.server = server
		self.runs = {}
		for name in ("task.list", "task.run", "task.cancel", "task.runs"):
			server.rpc.register(name, getattr(self, "m_" + name.replace(".", "_")))

	def snapshot(self, run):
		return dict(run.summary(), wsId=run.ws_id, steps=list(run.steps), tail=run.tail)

	async def m_task_list(self, call, p):
		self.server.workspace.get(p.str("wsId"))
		return {"tasks": [{"id": k, "label": t["label"], "slot": t["slot"]} for k, t in task_output.TASKS.items()]}

	async def m_task_runs(self, call, p):
		ws_id = p.str("wsId")
		return {"runs": [r.summary() for r in self.runs.values() if r.ws_id == ws_id]}

	async def m_task_run(self, call, p):
		ws_id = self.server.workspace.get(p.str("wsId")).ws_id
		task_id = p.str("task")
		task = task_output.TASKS.get(task_id)
		if task is None:
			raise RpcError("not_found", "no task %r" % task_id)
		params = p.obj("params", {}, required=False)
		lines = params.get("lines", 50000)
		if type(lines) is not int or not 1 <= lines <= 5000000:
			raise RpcError("bad_request", "params.lines must be an integer in 1..5000000")
		run = Run("r%d" % (len(self.runs) + 1), ws_id, task_id, task["label"])
		self.runs[run.run_id] = run
		self.server.hub.add(run.topic, lambda: self.snapshot(run))
		run.runner = asyncio.ensure_future(self._execute(run, task["steps"]({"lines": lines})))
		call.open_stream(lambda: self._cancel(run, "cancelled"))
		return {"runId": run.run_id, "topic": run.topic}

	async def m_task_cancel(self, call, p):
		run = self.runs.get(p.str("runId"))
		if run is None:
			raise RpcError("not_found", "no run %r" % p.str("runId"))
		self._cancel(run, "cancelled")
		return {"status": run.status}

	def _cancel(self, run, reason):
		if run.status == "running" and run.runner is not None:
			run.stop_reason = reason
			run.runner.cancel()

	def on_restart(self):
		for run in self.runs.values():
			self._cancel(run, "interrupted")

	async def _execute(self, run, steps):
		source = task_output.OUTPUT_SOURCE[run.task_id]
		self.server.outputs.clear(run.ws_id, source)
		self.server.outputs.clear_diagnostics_source(run.ws_id, "build")
		started = time.monotonic()
		try:
			for index, (label, command, lines) in enumerate(steps):
				run.exit_code = await self._step(run, index, label, command, lines, source)
				if run.exit_code != 0:
					break
			run.status = "succeeded" if run.exit_code == 0 else "failed"
		except asyncio.CancelledError:
			run.status = run.stop_reason
			if run.steps and run.steps[-1]["status"] == "running":
				run.steps[-1]["status"] = run.stop_reason
			self._emit(run, source, max(0, len(run.steps) - 1), "stderr", "\x1b[31m^C %s\x1b[0m\r\n" % run.stop_reason)
		self.server.hub.publish(run.topic, "run.end", dict(run.summary(), ms=int((time.monotonic() - started) * 1000)))

	async def _step(self, run, index, label, command, lines, source):
		run.steps.append({"step": index, "label": label, "command": command, "status": "running"})
		self.server.hub.publish(run.topic, "step.start", {"step": index, "label": label, "command": command})
		started = time.monotonic()
		emitted = 0
		while True:
			try:
				delay, stream, text = next(lines)
			except StopIteration as done:
				exit_code = done.value
				break
			if delay or emitted % FLOOD_YIELD_EVERY == 0:
				await self.server.clock.sleep(delay)
			self._emit(run, source, index, stream, text)
			emitted += 1
		run.steps[index].update(status="ok" if exit_code == 0 else "failed", exitCode=exit_code)
		self.server.hub.publish(run.topic, "step.end", {"step": index, "exitCode": exit_code, "ms": int((time.monotonic() - started) * 1000)})
		return exit_code

	def _emit(self, run, source, step, stream, text):
		run.tail = (run.tail + text)[-TAIL_CHARS:]
		self.server.hub.publish(run.topic, "output", {"step": step, "source": stream, "text": text})
		self.server.outputs.append(run.ws_id, source, text)
		match = DIAGNOSTIC.match(text)
		if match:
			self._diagnostic(run.ws_id, match)

	def _diagnostic(self, ws_id, match):
		line, col = int(match["line"]) - 1, int(match["col"]) - 1
		item = {
			"range": {"start": {"line": line, "col": col}, "end": {"line": line, "col": col + 1}},
			"severity": match["severity"], "message": match["message"], "code": match["code"],
		}
		files = self.server.outputs.diagnostics[ws_id]
		existing = files.get(match["path"], {}).get("build", [])
		self.server.outputs.set_diagnostics(ws_id, match["path"], "build", existing + [item])
