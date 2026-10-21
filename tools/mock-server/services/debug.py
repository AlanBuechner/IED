"""The debugger (docs/25 "The server as built"): the workspace's breakpoints (bp.set/bp.list, 'bp:<wsId>') and scripted
sessions of the demo program (dbg.*, 'dbg:<wsId>' and 'dbg:<wsId>:<sessionId>'). The program is src/main.c: it stops
at each breakpoint in order (one on a line without code binds to the next line with some, one past the end is not
verified), steps a line with code at a time, prints "hero spawned" and exits with 0 when it runs off its last breakpoint.
Controls are revision-checked as on the real server."""

import asyncio

from rpc import RpcError

PROGRAM = "src/main.c"
CODE_LINES = [5, 6, 7, 9, 11, 12]
STEP_DELAY = 0.15
VARIABLES = {
	1000: [{"name": "player", "value": "{name=\"hero\" position={x=0 y=0}}", "type": "Player", "variablesReference": 1001},
		{"name": "renderer", "value": "0x000001f4a2b0e000", "type": "Renderer*", "variablesReference": 0}],
	1001: [{"name": "name", "value": "\"hero\"", "type": "const char*", "variablesReference": 0},
		{"name": "position", "value": "{x=0 y=0}", "type": "Vec2", "variablesReference": 1002}],
	1002: [{"name": "x", "value": "0", "type": "float", "variablesReference": 0}, {"name": "y", "value": "0", "type": "float", "variablesReference": 0}],
}


def bind(line):
	"""The line with code a breakpoint on `line` binds to, or None."""
	return next((code for code in CODE_LINES if code >= line), None)


class Session:
	def __init__(self, session_id, ws_id, name):
		self.session_id = session_id
		self.ws_id = ws_id
		self.name = name
		self.state = "starting"
		self.revision = 0
		self.line = None
		self.reason = ""
		self.exit_code = None
		self.output = []

	@property
	def topic(self):
		return "dbg:%s:%s" % (self.ws_id, self.session_id)

	def live(self):
		return self.state not in ("ended", "failed")

	def summary(self):
		stop = {"threadId": 1, "reason": self.reason, "description": ""} if self.state == "stopped" else None
		return {"id": self.session_id, "name": self.name, "project": "demo", "type": "mock", "state": self.state, "revision": self.revision,
			"problem": "", "exitCode": self.exit_code, "stop": stop}

	def frames(self):
		if self.state != "stopped":
			return []
		return [{"id": 1, "name": "main", "line": self.line, "column": 1, "source": {"name": "main.c", "path": PROGRAM}},
			{"id": 2, "name": "__scrt_common_main_seh", "line": 287, "column": 0, "source": {"name": "exe_common.inl"}}]


class Debugger:
	def __init__(self, server):
		self.server = server
		self.sessions = {}
		self.breakpoints = {ws_id: {} for ws_id in server.workspace.items}
		self.watches = {ws_id: [] for ws_id in server.workspace.items}
		for ws_id in server.workspace.items:
			server.hub.add("dbg:" + ws_id, lambda ws_id=ws_id: {"sessions": [s.summary() for s in self.sessions.values() if s.ws_id == ws_id]})
			server.hub.add("bp:" + ws_id, lambda ws_id=ws_id: self.bp_snapshot(ws_id))
		for name in ("dbg.start", "dbg.stop", "dbg.restart", "dbg.sessions", "dbg.continue", "dbg.next", "dbg.stepIn", "dbg.stepOut", "dbg.pause",
				"dbg.stackTrace", "dbg.scopes", "dbg.variables", "dbg.evaluate", "bp.set", "bp.list", "watch.set"):
			server.rpc.register(name, getattr(self, "m_" + name.replace(".", "_")))

	# --- breakpoints ------------------------------------------------------------------------------------------------

	def bound(self, session):
		listed = self.breakpoints[session.ws_id].get(PROGRAM, [])
		return [{"line": b["line"], "verified": bind(b["line"]) is not None, "boundLine": bind(b["line"]) if bind(b["line"]) is not None else b["line"]}
			for b in listed if b.get("enabled", True)]

	def bp_snapshot(self, ws_id):
		files = [{"path": path, "breakpoints": list(items)} for path, items in self.breakpoints[ws_id].items()]
		bound = [{"sessionId": s.session_id, "path": PROGRAM, "breakpoints": self.bound(s)} for s in self.sessions.values() if s.ws_id == ws_id and s.live()]
		return {"files": files, "bound": bound, "watches": list(self.watches[ws_id])}

	def publish_bound(self, session):
		self.server.hub.publish("bp:" + session.ws_id, "bp.bound", {"sessionId": session.session_id, "path": PROGRAM, "breakpoints": self.bound(session)})

	async def m_bp_set(self, call, p):
		ws_id = self.server.workspace.get(p.str("wsId")).ws_id
		path = p.str("path")
		if path.startswith("/") or ".." in path.split("/"):
			raise RpcError("bad_request", "not a workspace path: %s" % path)
		items = [{"line": b["line"], "enabled": b.get("enabled", True)} for b in p.list("breakpoints")]
		if items:
			self.breakpoints[ws_id][path] = items
		else:
			self.breakpoints[ws_id].pop(path, None)
		self.server.hub.publish("bp:" + ws_id, "bp.changed", {"path": path, "breakpoints": items})
		for session in self.sessions.values():
			if session.ws_id == ws_id and session.live() and path == PROGRAM:
				self.publish_bound(session)
		return {"path": path, "breakpoints": items}

	async def m_watch_set(self, call, p):
		ws_id = self.server.workspace.get(p.str("wsId")).ws_id
		expressions = [str(e).strip() for e in p.list("expressions")]
		if any(not e for e in expressions):
			raise RpcError("bad_request", "a watch expression needs text")
		self.watches[ws_id] = expressions
		self.server.hub.publish("bp:" + ws_id, "watch.changed", {"expressions": expressions})
		return {"expressions": expressions}

	async def m_bp_list(self, call, p):
		return self.bp_snapshot(self.server.workspace.get(p.str("wsId")).ws_id)

	# --- sessions ---------------------------------------------------------------------------------------------------

	def changed(self, session):
		self.server.hub.publish("dbg:" + session.ws_id, "dbg.session", session.summary())
		self.server.hub.publish(session.topic, "dbg.state", session.summary())

	def stopped(self, session, line, reason):
		session.state, session.line, session.reason = "stopped", line, reason
		session.revision += 1
		self.server.hub.publish(session.topic, "dbg.stopped", {"session": session.summary(), "threads": [{"id": 1, "name": "main"}], "frames": session.frames()})
		self.server.hub.publish("dbg:" + session.ws_id, "dbg.session", session.summary())

	def ran(self, session):
		session.state = "running"
		session.revision += 1
		self.changed(session)

	def print(self, session, text):
		session.output.append(text)
		self.server.hub.publish(session.topic, "dbg.output", {"category": "stdout", "text": text})
		self.server.outputs.append(session.ws_id, "debug", text)

	async def run_on(self, session, after):
		"""Runs from line `after` to the next breakpoint, or to the end."""
		await asyncio.sleep(STEP_DELAY)
		if session.state != "running":
			return
		stops = sorted({b["boundLine"] for b in self.bound(session) if b["verified"] and b["boundLine"] > after})
		if stops:
			self.stopped(session, stops[0], "breakpoint")
			return
		self.print(session, "hero spawned\r\n")
		session.exit_code = 0
		session.state = "ended"
		self.changed(session)
		self.server.hub.publish("bp:" + session.ws_id, "bp.unbound", {"sessionId": session.session_id})

	async def launch(self, session):
		await asyncio.sleep(STEP_DELAY)
		self.publish_bound(session)
		self.ran(session)
		await self.run_on(session, -1)

	def session(self, p):
		session = self.sessions.get(p.str("sessionId"))
		if session is None:
			raise RpcError("not_found", "no debug session %s" % p.str("sessionId"))
		return session

	async def m_dbg_start(self, call, p):
		ws = self.server.workspace.get(p.str("wsId"))
		if not ws.trusted:
			raise RpcError("forbidden", "debugging runs the workspace's code: trust the workspace first")
		configuration = p.str("configuration", "", required=False) or "Launch"
		if configuration != "Launch":
			raise RpcError("not_found", "no debug configuration %s" % configuration)
		session = Session("d%d" % (len(self.sessions) + 1), ws.ws_id, configuration)
		self.sessions[session.session_id] = session
		self.server.hub.add(session.topic, lambda: {"session": session.summary(), "threads": [{"id": 1, "name": "main"}], "frames": session.frames(), "output": session.output})
		self.server.outputs.clear(ws.ws_id, "debug")
		self.changed(session)
		asyncio.ensure_future(self.launch(session))
		return {"sessionId": session.session_id, "topic": session.topic}

	async def m_dbg_stop(self, call, p):
		session = self.session(p)
		if session.live():
			session.state, session.exit_code = "ended", 1
			self.changed(session)
			self.server.hub.publish("bp:" + session.ws_id, "bp.unbound", {"sessionId": session.session_id})
		return {}

	async def m_dbg_restart(self, call, p):
		session = self.session(p)
		await self.m_dbg_stop(call, p)
		return await self.m_dbg_start(call, type(p)({"wsId": session.ws_id, "configuration": session.name}))

	async def m_dbg_sessions(self, call, p):
		ws_id = self.server.workspace.get(p.str("wsId")).ws_id
		return {"sessions": [s.summary() for s in self.sessions.values() if s.ws_id == ws_id]}

	def check(self, session, p, when_stopped):
		if not session.live():
			raise RpcError("conflict", "the debug session has ended")
		revision = p.int("revision", None, required=False)
		if revision is not None and revision != session.revision:
			raise RpcError("conflict", "the debug session moved on: refresh and try again")
		if when_stopped and session.state != "stopped":
			raise RpcError("conflict", "the program is not stopped")
		if not when_stopped and session.state != "running":
			raise RpcError("conflict", "the program is not running")

	async def step(self, p):
		session = self.session(p)
		self.check(session, p, True)
		line = session.line
		self.ran(session)
		at = session.revision

		async def land():
			await asyncio.sleep(STEP_DELAY)
			following = next((code for code in CODE_LINES if code > line), None)
			if session.revision != at or not session.live():
				return
			if following is None:
				await self.run_on(session, line)
			else:
				self.stopped(session, following, "step")
		asyncio.ensure_future(land())
		return {"revision": at}

	async def m_dbg_next(self, call, p):
		return await self.step(p)

	async def m_dbg_stepIn(self, call, p):
		return await self.step(p)

	async def m_dbg_stepOut(self, call, p):
		return await self.step(p)

	async def m_dbg_continue(self, call, p):
		session = self.session(p)
		self.check(session, p, True)
		line = session.line
		self.ran(session)
		asyncio.ensure_future(self.run_on(session, line))
		return {"revision": session.revision}

	async def m_dbg_pause(self, call, p):
		session = self.session(p)
		self.check(session, p, False)
		self.stopped(session, CODE_LINES[2], "pause")
		return {"revision": session.revision}

	async def m_dbg_stackTrace(self, call, p):
		frames = self.session(p).frames()
		return {"stackFrames": frames, "totalFrames": len(frames)}

	async def m_dbg_scopes(self, call, p):
		self.session(p)
		return {"scopes": [{"name": "Locals", "variablesReference": 1000, "expensive": False}]}

	async def m_dbg_variables(self, call, p):
		self.session(p)
		return {"variables": VARIABLES.get(p.int("variablesReference"), [])}

	async def m_dbg_evaluate(self, call, p):
		session = self.session(p)
		expression = p.str("expression")
		known = {v["name"]: v for v in VARIABLES[1000]}
		if expression not in known:
			raise RpcError("plugin_error", "no variable named %s" % expression)
		return {"result": known[expression]["value"], "type": known[expression]["type"], "variablesReference": known[expression]["variablesReference"]}

