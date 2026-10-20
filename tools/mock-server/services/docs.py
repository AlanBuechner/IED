"""Server-owned documents: 'doc:<docId>' topics, lease, versioned edits with rebase, save, disk reload."""

import collections
import posixpath

from rpc import RpcError
from services import textedit
from services.workspace import normalize

HISTORY_VERSIONS = 1000
LANGUAGES = {".c": "c", ".h": "c", ".md": "markdown", ".json": "json", ".txt": "plaintext"}


class Doc:
	def __init__(self, doc_id, ws_id, path, text):
		self.doc_id = doc_id
		self.ws_id = ws_id
		self.path = path
		self.text = text
		self.version = 1
		self.holder = None
		self.dirty = False
		self.history = collections.deque(maxlen=HISTORY_VERSIONS)

	@property
	def topic(self):
		return "doc:" + self.doc_id


class Documents:
	def __init__(self, server):
		self.server = server
		self.by_id = {}
		self.by_path = {}
		for name in ("doc.open", "doc.edit", "doc.takeLease", "doc.save"):
			server.rpc.register(name, getattr(self, "m_" + name.replace(".", "_")))

	def snapshot(self, doc):
		return {
			"docId": doc.doc_id, "wsId": doc.ws_id, "path": doc.path, "text": doc.text, "version": doc.version,
			"lease": {"holder": doc.holder}, "dirty": doc.dirty,
			"languageId": LANGUAGES.get(posixpath.splitext(doc.path)[1], "plaintext"),
		}

	def get(self, doc_id):
		doc = self.by_id.get(doc_id)
		if doc is None:
			raise RpcError("not_found", "no document %r" % doc_id)
		return doc

	def _open(self, ws_id, path):
		key = (ws_id, path)
		if key not in self.by_path:
			ws = self.server.workspace.get(ws_id)
			text = self.server.workspace.file(ws, path).text
			doc = Doc("d%d" % (len(self.by_id) + 1), ws_id, path, text)
			self.by_id[doc.doc_id] = doc
			self.by_path[key] = doc
			self.server.hub.add(doc.topic, lambda: self.snapshot(doc))
		return self.by_path[key]

	def apply_edit(self, doc, base_version, changes, origin, op_id=None):
		if base_version > doc.version or base_version < 1:
			raise RpcError("bad_request", "baseVersion %d is not a version of this document" % base_version)
		rebased = self._rebase(doc, base_version, changes)
		try:
			doc.text = textedit.apply(doc.text, rebased)
		except textedit.BadEdit as e:
			raise RpcError("bad_request", str(e))
		doc.version += 1
		doc.history.append((doc.version, rebased))
		doc.dirty = origin != "disk"
		event = {"docId": doc.doc_id, "version": doc.version, "changes": textedit.to_wire(rebased), "origin": origin}
		if base_version != doc.version - 1:
			event["rebasedFrom"] = base_version
		if op_id:
			event["opId"] = op_id
		self.server.hub.publish(doc.topic, "doc.edit", event)
		return {"version": doc.version, "changes": event["changes"], "rebased": "rebasedFrom" in event}

	def _rebase(self, doc, base_version, changes):
		if base_version == doc.version:
			return changes
		missing = doc.version - base_version
		if missing > len(doc.history):
			raise RpcError("conflict", "baseVersion is too old to rebase", {"version": doc.version, "reason": "too-old"})
		sets = [change_set for version, change_set in doc.history if version > base_version]
		try:
			return textedit.rebase(changes, sets)
		except textedit.Conflict:
			raise RpcError("conflict", "edit overlaps a change made since version %d" % base_version, {"version": doc.version, "reason": "overlap"})

	def on_disk_write(self, ws_id, path, text, revision):
		doc = self.by_path.get((ws_id, path))
		if doc is None or doc.text == text:
			return
		if doc.dirty:
			self.server.hub.publish(doc.topic, "doc.diskChanged", {"docId": doc.doc_id, "revision": revision})
			return
		replace_all = textedit.Change(textedit.Pos(0, 0), textedit.end_position(doc.text), text)
		self.apply_edit(doc, doc.version, [replace_all], "disk")

	async def m_doc_open(self, call, p):
		doc = self._open(p.str("wsId"), normalize(p.str("path")))
		if doc.holder is None:
			self._grant(doc, call.client_id)
		return {"docId": doc.doc_id, "version": doc.version, "holder": doc.holder}

	async def m_doc_edit(self, call, p):
		doc = self.get(p.str("docId"))
		if doc.holder != call.client_id:
			raise RpcError("forbidden", "only the lease holder may edit", {"reason": "lease", "holder": doc.holder})
		try:
			changes = textedit.parse_changes(p.list("changes"))
		except textedit.BadEdit as e:
			raise RpcError("bad_request", str(e))
		return self.apply_edit(doc, p.int("baseVersion"), changes, "user:" + call.client_id, call.op_id)

	async def m_doc_takeLease(self, call, p):
		doc = self.get(p.str("docId"))
		if doc.holder != call.client_id:
			self._grant(doc, call.client_id)
		return {"holder": doc.holder, "version": doc.version}

	def _grant(self, doc, client_id):
		doc.holder = client_id
		self.server.hub.publish(doc.topic, "doc.lease", {"docId": doc.doc_id, "holder": client_id})

	async def m_doc_save(self, call, p):
		doc = self.get(p.str("docId"))
		ws = self.server.workspace.get(doc.ws_id)
		revision = self.server.workspace.write(ws, doc.path, doc.text)
		doc.dirty = False
		self.server.hub.publish(doc.topic, "doc.saved", {"docId": doc.doc_id, "version": doc.version, "revision": revision})
		return {"version": doc.version, "revision": revision}
