"""Echo terminals (docs/10 "Terminals"): 'term:<termId>' topics with a tiny line discipline and a few built-in commands,
and each workspace's list on 'terms:<wsId>'."""

import asyncio

from rpc import RpcError

PROMPT = "\x1b[32mmock\x1b[0m:\x1b[34m~/demo\x1b[0m$ "
BANNER = "IED mock terminal (echo). Commands: help, clear, colors, echo <text>, flood <lines>, exit\r\n"
SCROLLBACK_CHARS = 64 * 1024
FLOOD_YIELD_EVERY = 100


class Terminal:
	def __init__(self, term_id, ws_id, cols, rows, holder, title):
		self.term_id = term_id
		self.ws_id = ws_id
		self.title = title
		self.cols = cols
		self.rows = rows
		self.holder = holder
		self.alive = True
		self.exit_code = None
		self.line = ""
		self.scrollback = ""

	@property
	def topic(self):
		return "term:" + self.term_id

	def summary(self):
		return {"termId": self.term_id, "wsId": self.ws_id, "title": self.title, "kind": "shell", "alive": self.alive, "exitCode": self.exit_code, "holder": self.holder}


class Terminals:
	def __init__(self, server):
		self.server = server
		self.items = {}
		self.made = 0
		for name in ("term.create", "term.input", "term.resize", "term.takeLease", "term.rename", "term.close", "term.list"):
			server.rpc.register(name, getattr(self, "m_" + name.replace(".", "_")))
		for ws_id in server.workspace.items:
			server.hub.add("terms:" + ws_id, lambda ws_id=ws_id: {"terminals": [t.summary() for t in self.items.values() if t.ws_id == ws_id]})

	def snapshot(self, term):
		return {"terminal": term.summary(), "cols": term.cols, "rows": term.rows, "scrollback": term.scrollback}

	def changed(self, term, kind):
		self.server.hub.publish("terms:" + term.ws_id, kind, term.summary())

	def get(self, term_id, alive=True):
		term = self.items.get(term_id)
		if term is None:
			raise RpcError("not_found", "no terminal %r" % term_id)
		if alive and not term.alive:
			raise RpcError("bad_request", "terminal %r has exited" % term_id)
		return term

	def _out(self, term, text):
		term.scrollback = (term.scrollback + text)[-SCROLLBACK_CHARS:]
		self.server.hub.publish(term.topic, "data", {"data": text})

	def _exit(self, term, code, reason):
		term.alive = False
		term.exit_code = code
		self.server.hub.publish(term.topic, "exit", {"code": code, "reason": reason})
		self.changed(term, "term.changed")

	def on_restart(self):
		for term in self.items.values():
			if term.alive:
				self._exit(term, None, "server-restart")

	async def m_term_create(self, call, p):
		ws_id = self.server.workspace.get(p.str("wsId")).ws_id
		self.made += 1
		term = Terminal("term%d" % self.made, ws_id, p.int("cols", 120, required=False), p.int("rows", 30, required=False), call.client_id, p.str("title", "", required=False) or "mock")
		self.items[term.term_id] = term
		self.server.hub.add(term.topic, lambda: self.snapshot(term))
		self.changed(term, "term.added")
		self._out(term, BANNER + PROMPT)
		return term.summary()

	async def m_term_list(self, call, p):
		ws_id = p.str("wsId")
		return {"terminals": [t.summary() for t in self.items.values() if t.ws_id == ws_id]}

	async def m_term_input(self, call, p):
		term = self.get(p.str("termId"))
		if term.holder != call.client_id:
			raise RpcError("forbidden", "another window is typing in this terminal", {"reason": "lease", "holder": term.holder})
		data = p.str("data")
		if data.startswith("\x1b"):
			return {}
		for ch in data:
			self._key(term, ch)
		return {}

	def _key(self, term, ch):
		if ch in "\r\n":
			self._out(term, "\r\n")
			command, term.line = term.line.strip(), ""
			self._command(term, command)
		elif ch in "\x7f\b":
			if term.line:
				term.line = term.line[:-1]
				self._out(term, "\b \b")
		elif ch == "\x03":
			term.line = ""
			self._out(term, "^C\r\n" + PROMPT)
		elif ch >= " ":
			term.line += ch
			self._out(term, ch)

	def _command(self, term, command):
		name, _, rest = command.partition(" ")
		if name == "exit":
			self._exit(term, 0, "exit")
			return
		if name == "flood":
			count = int(rest) if rest.strip().isdigit() else 1000
			asyncio.ensure_future(self._flood(term, count))
			return
		self._out(term, self._reply(name, rest) + PROMPT)

	def _reply(self, name, rest):
		if name == "":
			return ""
		if name == "help":
			return BANNER
		if name == "clear":
			return "\x1b[2J\x1b[H"
		if name == "echo":
			return rest + "\r\n"
		if name == "colors":
			return "".join("\x1b[%dm %3d \x1b[0m" % (code, code) for code in list(range(30, 38)) + list(range(90, 98))) + "\r\n"
		return "mock: command not found: %s\r\n" % name

	async def _flood(self, term, count):
		for i in range(1, count + 1):
			if not term.alive:
				return
			self._out(term, "terminal flood %06d the quick brown fox jumps over the lazy dog\r\n" % i)
			if i % FLOOD_YIELD_EVERY == 0:
				await asyncio.sleep(0)
		self._out(term, PROMPT)

	async def m_term_resize(self, call, p):
		term = self.get(p.str("termId"))
		if term.holder != call.client_id or (term.cols, term.rows) == (p.int("cols"), p.int("rows")):
			return {}
		term.cols, term.rows = p.int("cols"), p.int("rows")
		self.server.hub.publish(term.topic, "resize", {"cols": term.cols, "rows": term.rows})
		return {}

	async def m_term_takeLease(self, call, p):
		term = self.get(p.str("termId"), alive=False)
		term.holder = call.client_id
		self.server.hub.publish(term.topic, "lease", {"holder": term.holder})
		self.changed(term, "term.changed")
		return {"holder": term.holder}

	async def m_term_rename(self, call, p):
		term = self.get(p.str("termId"), alive=False)
		title = p.str("title").strip()
		if not title:
			raise RpcError("bad_request", "a terminal needs a title")
		term.title = title
		self.changed(term, "term.changed")
		return term.summary()

	async def m_term_close(self, call, p):
		term = self.get(p.str("termId"), alive=False)
		term.alive = False
		del self.items[term.term_id]
		self.changed(term, "term.removed")
		self.server.hub.remove(term.topic)
		return {}
