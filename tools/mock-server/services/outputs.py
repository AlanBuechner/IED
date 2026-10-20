"""Output window sources ('out:<wsId>:<sourceId>') and diagnostics ('diag:<wsId>')."""

from rpc import RpcError

SOURCES = {"build": "Build", "test": "Tests", "run": "Run", "ied": "IED"}
TAIL_CHARS = 256 * 1024


class Source:
	def __init__(self, source_id, label):
		self.source_id = source_id
		self.label = label
		self.tail = ""


class Outputs:
	def __init__(self, server):
		self.server = server
		self.sources = {}
		self.diagnostics = {}
		for ws_id in server.workspace.items:
			for source_id, label in SOURCES.items():
				self._add_source(ws_id, source_id, label)
			self._add_diagnostics(ws_id)
		server.rpc.register("out.sources", self.m_out_sources)
		server.rpc.register("out.clear", self.m_out_clear)
		self.append(next(iter(server.workspace.items)), "ied", "mock server %s started\r\n" % server.version)

	def _add_source(self, ws_id, source_id, label):
		key = "out:%s:%s" % (ws_id, source_id)
		source = Source(source_id, label)
		self.sources[key] = source
		self.server.hub.add(key, lambda: {"sourceId": source.source_id, "label": source.label, "tail": source.tail})

	def _add_diagnostics(self, ws_id):
		files = {"src/engine/render.c": {"mock.language": [{
			"range": {"start": {"line": 8, "col": 7}, "end": {"line": 8, "col": 12}},
			"severity": "hint", "message": "'scale' is assigned but never used", "code": "unused",
		}]}}
		self.diagnostics[ws_id] = files
		self.server.hub.add("diag:" + ws_id, lambda: {"files": files})

	def append(self, ws_id, source_id, text):
		key = "out:%s:%s" % (ws_id, source_id)
		source = self.sources[key]
		source.tail = (source.tail + text)[-TAIL_CHARS:]
		self.server.hub.publish(key, "append", {"text": text})

	def clear(self, ws_id, source_id):
		key = "out:%s:%s" % (ws_id, source_id)
		self.sources[key].tail = ""
		self.server.hub.publish(key, "clear", {})

	def set_diagnostics(self, ws_id, path, source, items):
		files = self.diagnostics[ws_id]
		if items:
			files.setdefault(path, {})[source] = items
		elif path in files:
			files[path].pop(source, None)
			if not files[path]:
				del files[path]
		self.server.hub.publish("diag:" + ws_id, "diag.set", {"path": path, "source": source, "items": items})

	def clear_diagnostics_source(self, ws_id, source):
		for path in [p for p, by_source in self.diagnostics[ws_id].items() if source in by_source]:
			self.set_diagnostics(ws_id, path, source, [])

	async def m_out_sources(self, call, p):
		ws_id = p.str("wsId")
		found = [{"sourceId": s.source_id, "label": s.label, "topic": key} for key, s in self.sources.items() if key.startswith("out:%s:" % ws_id)]
		if not found:
			raise RpcError("not_found", "no workspace %r" % ws_id)
		return {"sources": found}

	async def m_out_clear(self, call, p):
		key = "out:%s:%s" % (p.str("wsId"), p.str("sourceId"))
		if key not in self.sources:
			raise RpcError("not_found", "no output source %r" % key)
		ws_id, source_id = key.split(":")[1:]
		self.clear(ws_id, source_id)
		return {}
