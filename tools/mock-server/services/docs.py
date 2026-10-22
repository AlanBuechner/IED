"""Server-owned documents: 'doc:<docId>' topics, lease, versioned edits with rebase, save, disk reload."""

import collections

from rpc import RpcError
from services import textedit
from services.workspace import normalize

HISTORY_VERSIONS = 1000


class Doc:
	def __init__(self, doc_id, ws_id, path, text):
		self.doc_id = doc_id
		self.ws_id = ws_id
		self.path = path
		self.text = text
		self.version = 1
		self.holder = None
		self.dirty = False
		self.saved = text  # the file's text as last read, saved or seen changed on disk
		self.external = False  # a file outside the workspace, read-only; `path` is its file: URI (docs/10)
		self.opened = text  # the text when the server opened it: the track-changes margin's baseline
		self.language = None  # the language its clients were last told
		self.history = collections.deque(maxlen=HISTORY_VERSIONS)

	@property
	def topic(self):
		return "doc:" + self.doc_id


class Documents:
	def __init__(self, server):
		self.server = server
		self.by_id = {}
		self.by_path = {}
		self.external = {}  # (wsId, file URI) -> text: the files outside the workspace a language server named
		for name in ("doc.open", "doc.edit", "doc.takeLease", "doc.save", "doc.baseline"):
			server.rpc.register(name, getattr(self, "m_" + name.replace(".", "_")))

	def snapshot(self, doc):
		doc.language = self.server.plugins.language_of(doc.path)
		return {
			"docId": doc.doc_id, "wsId": doc.ws_id, "path": doc.path, "text": doc.text, "version": doc.version,
			"lease": {"holder": doc.holder}, "dirty": doc.dirty, "readOnly": doc.external,
			"languageId": doc.language,
		}

	def languages_changed(self):
		"""The loaded plugins changed: an open document whose language is another now is told (doc.language)."""
		for doc in self.by_id.values():
			language = self.server.plugins.language_of(doc.path)
			if language != doc.language:
				doc.language = language
				self.server.hub.publish(doc.topic, "doc.language", {"docId": doc.doc_id, "languageId": language})

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
		if origin == "disk":
			doc.saved = doc.text
		doc.dirty = doc.text != doc.saved
		event = {"docId": doc.doc_id, "version": doc.version, "changes": textedit.to_wire(rebased), "origin": origin, "dirty": doc.dirty}
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

	def renamed(self, ws_id, source, target):
		"""fs.rename moved a file: its open document follows (doc.renamed)."""
		doc = self.by_path.pop((ws_id, source), None)
		if doc is None:
			return
		doc.path = target
		self.by_path[(ws_id, target)] = doc
		self.server.hub.publish(doc.topic, "doc.renamed", {"docId": doc.doc_id, "path": target})

	def on_disk_delete(self, ws_id, path):
		doc = self.by_path.get((ws_id, path))
		if doc is not None:
			self.server.hub.publish(doc.topic, "doc.diskChanged", {"docId": doc.doc_id, "revision": 0})

	def on_disk_write(self, ws_id, path, text, revision):
		doc = self.by_path.get((ws_id, path))
		if doc is None or doc.text == text:
			return
		if doc.dirty:
			doc.saved = text
			self.server.hub.publish(doc.topic, "doc.diskChanged", {"docId": doc.doc_id, "revision": revision})
			return
		replace_all = textedit.Change(textedit.Pos(0, 0), textedit.end_position(doc.text), text)
		self.apply_edit(doc, doc.version, [replace_all], "disk")

	def _open_external(self, ws_id, uri):
		self.server.workspace.get(ws_id)
		key = (ws_id, uri)
		if key not in self.by_path:
			if key not in self.external:
				raise RpcError("forbidden", "only a file a language server of the workspace named opens from outside it")
			doc = Doc("d%d" % (len(self.by_id) + 1), ws_id, uri, self.external[key])
			doc.external = True
			self.by_id[doc.doc_id] = doc
			self.by_path[key] = doc
			self.server.hub.add(doc.topic, lambda: self.snapshot(doc))
		return self.by_path[key]

	async def m_doc_open(self, call, p):
		uri = p.str("uri", None, required=False)
		doc = self._open_external(p.str("wsId"), uri) if uri else self._open(p.str("wsId"), normalize(p.str("path")))
		if doc.holder is None and not doc.external:
			self._grant(doc, call.client_id)
		return {"docId": doc.doc_id, "path": doc.path, "version": doc.version, "holder": doc.holder, "readOnly": doc.external}

	@staticmethod
	def _refuse_external(doc):
		if doc.external:
			raise RpcError("forbidden", "the file is outside the workspace and read-only", {"reason": "read_only"})

	async def m_doc_edit(self, call, p):
		doc = self.get(p.str("docId"))
		self._refuse_external(doc)
		if doc.holder != call.client_id:
			raise RpcError("forbidden", "only the lease holder may edit", {"reason": "lease", "holder": doc.holder})
		try:
			changes = textedit.parse_changes(p.list("changes"))
		except textedit.BadEdit as e:
			raise RpcError("bad_request", str(e))
		return self.apply_edit(doc, p.int("baseVersion"), changes, "user:" + call.client_id, call.op_id)

	async def m_doc_takeLease(self, call, p):
		doc = self.get(p.str("docId"))
		self._refuse_external(doc)
		if doc.holder != call.client_id:
			self._grant(doc, call.client_id)
		return {"holder": doc.holder, "version": doc.version}

	def _grant(self, doc, client_id):
		doc.holder = client_id
		self.server.hub.publish(doc.topic, "doc.lease", {"docId": doc.doc_id, "holder": client_id})

	async def m_doc_baseline(self, call, p):
		doc = self.get(p.str("docId"))
		return {"version": doc.version, "opened": doc.opened, "saved": doc.saved}

	async def m_doc_save(self, call, p):
		doc = self.get(p.str("docId"))
		self._refuse_external(doc)
		ws = self.server.workspace.get(doc.ws_id)
		revision = self.server.workspace.write(ws, doc.path, doc.text)
		doc.saved = doc.text
		doc.dirty = False
		self.server.hub.publish(doc.topic, "doc.saved", {"docId": doc.doc_id, "version": doc.version, "revision": revision})
		return {"version": doc.version, "revision": revision}
