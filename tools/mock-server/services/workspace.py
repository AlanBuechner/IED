"""The fake workspace: in-memory file tree, 'ws:<wsId>' topic, fs.* methods with revision checks."""

import posixpath
import re
import time

from fixtures import workspace_files
from rpc import RpcError

SEARCH_DEFAULT_RESULTS = 2000
SEARCH_MOST_RESULTS = 20000
# The folders fs.search skips when a query names no exclusions (src/fs/search.cf DefaultSearchExcludes).
SEARCH_EXCLUDES = ["**/.git", "**/node_modules", "**/build", "**/bin", "**/bin-int", "**/toolchains", "**/vendor", "**/third_party", "**/__pycache__"]


class File:
	def __init__(self, text, revision=1):
		self.text = text
		self.revision = revision


class Workspace:
	def __init__(self, ws_id, name, root, files, directories):
		self.ws_id = ws_id
		self.name = name
		self.root = root
		self.trusted = True
		self.files = {path: File(text) for path, text in files.items()}
		self.directories = set(directories)

	def summary(self):
		return {"wsId": self.ws_id, "name": self.name, "root": self.root, "trusted": self.trusted}

	def is_dir(self, path):
		if path == "" or path in self.directories:
			return True
		prefix = path + "/"
		return any(p.startswith(prefix) for p in self.files) or any(d.startswith(prefix) for d in self.directories)

	def entries(self, path):
		prefix = path + "/" if path else ""
		names = {}
		for candidate in list(self.files) + list(self.directories):
			if not candidate.startswith(prefix):
				continue
			head, sep, _ = candidate[len(prefix):].partition("/")
			if sep or candidate in self.directories:
				names[head] = "dir"
			else:
				names.setdefault(head, "file")
		return sorted(names.items(), key=lambda item: (item[1] != "dir", item[0].lower()))


def normalize(path):
	clean = path.replace("\\", "/").strip("/")
	if re.match(r"^[A-Za-z]:", clean) or any(part in ("..", ".") for part in clean.split("/") if part):
		raise RpcError("bad_request", "path must be workspace-relative without '.' or '..': %r" % path)
	return posixpath.normpath(clean) if clean else ""


class Workspaces:
	def __init__(self, server):
		self.server = server
		files = dict(workspace_files.FILES, **workspace_files.generated_files(server.config.tree_files))
		fixture = workspace_files.WORKSPACE
		ws = Workspace(fixture["wsId"], fixture["name"], fixture["root"], files, workspace_files.DIRECTORIES)
		self.items = {ws.ws_id: ws}
		server.hub.add("ws:" + ws.ws_id, lambda: self.snapshot(ws))
		self.trash = {}
		self.trash_made = 0
		for name in ("ws.list", "ws.setTrust", "fs.list", "fs.read", "fs.write", "fs.delete", "fs.mkdir", "fs.rename", "fs.search",
				"trash.list", "trash.restore", "trash.purge"):
			server.rpc.register(name, getattr(self, "m_" + name.replace(".", "_")))

	def summaries(self):
		return [ws.summary() for ws in self.items.values()]

	def snapshot(self, ws):
		chats = [c.summary() for c in self.server.chat.items.values() if c.ws_id == ws.ws_id]
		return dict(ws.summary(), kind="folder", roots=["."], plugins=[{"id": "mock.language", "state": "active"}], chats=chats)

	def get(self, ws_id):
		ws = self.items.get(ws_id)
		if ws is None:
			raise RpcError("not_found", "no workspace %r" % ws_id)
		return ws

	def file(self, ws, path):
		entry = ws.files.get(path)
		if entry is None:
			raise RpcError("not_found", "no file %r" % path)
		return entry

	def write(self, ws, path, text, expected_revision=None):
		entry = ws.files.get(path)
		current = entry.revision if entry else 0
		if expected_revision is not None and expected_revision != current:
			raise RpcError("conflict", "file changed since revision %d" % expected_revision, {"revision": current})
		if ws.is_dir(path) and entry is None:
			raise RpcError("bad_request", "%r is a directory" % path)
		revision = current + 1
		ws.files[path] = File(text, revision)
		self._tree_changed(ws, path, "changed" if entry else "created")
		return revision

	def _tree_changed(self, ws, path, change):
		self.server.hub.publish("ws:" + ws.ws_id, "tree.changed", {"changes": [{"path": path, "change": change}]})

	async def m_ws_list(self, call, p):
		return {"workspaces": self.summaries()}

	async def m_ws_setTrust(self, call, p):
		ws = self.get(p.str("wsId"))
		ws.trusted = p.bool("trusted")
		self.server.hub.publish("ws:" + ws.ws_id, "trust.changed", {"trusted": ws.trusted})
		return {"trusted": ws.trusted}

	async def m_fs_list(self, call, p):
		ws = self.get(p.str("wsId"))
		path = normalize(p.str("path", "", required=False))
		if not ws.is_dir(path):
			raise RpcError("not_found", "no directory %r" % path)
		entries = []
		for name, kind in ws.entries(path):
			full = posixpath.join(path, name) if path else name
			entry = {"name": name, "path": full, "kind": kind}
			if kind == "file":
				entry.update(size=len(ws.files[full].text.encode("utf-8")), revision=ws.files[full].revision)
			entries.append(entry)
		return {"path": path, "entries": entries}

	async def m_fs_read(self, call, p):
		ws = self.get(p.str("wsId"))
		path = normalize(p.str("path"))
		entry = self.file(ws, path)
		return {"path": path, "text": entry.text, "revision": entry.revision}

	async def m_fs_write(self, call, p):
		ws = self.get(p.str("wsId"))
		path = normalize(p.str("path"))
		text = p.str("text")
		revision = self.write(ws, path, text, p.int("expectedRevision", required=False))
		self.server.docs.on_disk_write(ws.ws_id, path, text, revision)
		return {"path": path, "revision": revision}

	async def m_fs_delete(self, call, p):
		ws = self.get(p.str("wsId"))
		path = normalize(p.str("path"))
		if path and path not in ws.files and ws.is_dir(path):
			return self._delete_folder(ws, path, p.bool("recursive", False, required=False))
		entry = self.file(ws, path)
		expected = p.int("expectedRevision", required=False)
		if expected is not None and expected != entry.revision:
			raise RpcError("conflict", "file changed since revision %d" % expected, {"revision": entry.revision})
		trash_id = self._to_trash(ws, path, "file", {path: ws.files.pop(path)}, set())
		self.server.docs.on_disk_delete(ws.ws_id, path)
		self._tree_changed(ws, path, "deleted")
		return {"trashId": trash_id}

	def _to_trash(self, ws, path, kind, files, directories):
		"""Keeps what fs.delete removed (docs/10 Trash): its files and folders, for trash.restore."""
		self.trash_made += 1
		trash_id = "%d-%d" % (int(time.time() * 1000), self.trash_made)
		size = sum(len(f.text.encode("utf-8")) for f in files.values())
		self.trash[trash_id] = {"id": trash_id, "wsId": ws.ws_id, "root": ws.root, "path": path, "kind": kind,
			"deletedMs": int(time.time() * 1000), "bytes": size, "files": files, "directories": directories}
		return trash_id

	def _delete_folder(self, ws, path, recursive):
		if not recursive:
			raise RpcError("bad_request", "the path is a folder: pass recursive to delete it with everything in it")
		prefix = path + "/"
		files = {f: ws.files.pop(f) for f in [f for f in ws.files if f.startswith(prefix)]}
		directories = {d for d in ws.directories if d == path or d.startswith(prefix)} | {path}
		ws.directories -= directories
		trash_id = self._to_trash(ws, path, "folder", files, directories)
		for gone in files:
			self.server.docs.on_disk_delete(ws.ws_id, gone)
		self._tree_changed(ws, path, "deleted")
		return {"trashId": trash_id}

	@staticmethod
	def _public(entry):
		return {k: entry[k] for k in ("id", "root", "path", "kind", "deletedMs", "bytes")}

	async def m_trash_list(self, call, p):
		ws_id = p.str("wsId", required=False)
		if ws_id:
			self.get(ws_id)
		entries = sorted(self.trash.values(), key=lambda e: e["deletedMs"], reverse=True)
		return {"entries": [self._public(e) for e in entries if not ws_id or e["wsId"] == ws_id], "bytes": sum(e["bytes"] for e in entries)}

	async def m_trash_restore(self, call, p):
		entry = self.trash.get(p.str("id"))
		if entry is None:
			raise RpcError("not_found", "no trash entry %r" % p.str("id"))
		ws = self.get(entry["wsId"])
		if entry["path"] in ws.files or ws.is_dir(entry["path"]):
			raise RpcError("conflict", "something is at %s now" % entry["path"])
		ws.files.update(entry["files"])
		ws.directories |= entry["directories"]
		del self.trash[entry["id"]]
		self._tree_changed(ws, entry["path"], "created")
		return {"wsId": ws.ws_id, "path": entry["path"]}

	async def m_trash_purge(self, call, p):
		if p.bool("all", False, required=False):
			self.trash.clear()
			return {}
		if self.trash.pop(p.str("id"), None) is None:
			raise RpcError("not_found", "no trash entry %r" % p.str("id"))
		return {}

	async def m_fs_mkdir(self, call, p):
		ws = self.get(p.str("wsId"))
		path = normalize(p.str("path"))
		if path in ws.files:
			raise RpcError("conflict", "%r is a file" % path)
		if not ws.is_dir(path):
			ws.directories.add(path)
			self._tree_changed(ws, path, "created")
		return {"path": path}

	async def m_fs_rename(self, call, p):
		ws = self.get(p.str("wsId"))
		source, target = normalize(p.str("from")), normalize(p.str("to"))
		if not source or not target:
			raise RpcError("bad_request", "the workspace's own folder cannot be renamed")
		if source not in ws.files and ws.is_dir(source):
			return self._rename_folder(ws, source, target)
		entry = self.file(ws, source)
		if target in ws.files or ws.is_dir(target):
			raise RpcError("conflict", "%r already exists" % target)
		ws.files[target] = ws.files.pop(source)
		self.server.docs.renamed(ws.ws_id, source, target)
		self._moved(ws, source, target)
		return {"path": target, "revision": entry.revision}

	def _rename_folder(self, ws, source, target):
		if target.startswith(source + "/"):
			raise RpcError("bad_request", "a folder cannot move into itself")
		if target in ws.files or ws.is_dir(target):
			raise RpcError("conflict", "%r already exists" % target)
		prefix = source + "/"
		for old in [f for f in ws.files if f.startswith(prefix)]:
			new = target + old[len(source):]
			ws.files[new] = ws.files.pop(old)
			self.server.docs.renamed(ws.ws_id, old, new)
		ws.directories = {(target + d[len(source):]) if d == source or d.startswith(prefix) else d for d in ws.directories}
		ws.directories.add(target)
		self._moved(ws, source, target)
		return {"path": target}

	def _moved(self, ws, source, target):
		self.server.hub.publish("ws:" + ws.ws_id, "tree.changed", {"changes": [
			{"path": source, "change": "deleted"}, {"path": target, "change": "created"},
		]})

	async def m_fs_search(self, call, p):
		"""docs/10 "Files": the first match of each line, open documents as they are, files in path order."""
		ws = self.get(p.str("wsId"))
		query = p.str("query")
		if not query:
			raise RpcError("bad_request", "the query is empty")
		matcher = _matcher(query, p.bool("regex", False, required=False), p.bool("caseSensitive", False, required=False))
		include = p.list("include", [], required=False)
		exclude = p.list("exclude", SEARCH_EXCLUDES, required=False)
		limit = max(1, min(SEARCH_MOST_RESULTS, p.int("maxResults", SEARCH_DEFAULT_RESULTS, required=False)))
		matches = []
		for path in sorted(ws.files):
			if not _searched(path, include, exclude):
				continue
			doc = self.server.docs.by_path.get((ws.ws_id, path))
			for number, line in enumerate((doc.text if doc else ws.files[path].text).split("\n")):
				hit = matcher(line.rstrip("\r"))
				if hit is None:
					continue
				if len(matches) >= limit:
					return {"matches": matches, "truncated": True}
				matches.append({"path": path, "line": number, "col": hit, "preview": line.strip()[:200]})
		return {"matches": matches, "truncated": False}


def _glob_regex(glob):
	"""src/fs/glob.cf's rule: '*' within a segment, '?' one character, a '**' segment any number of segments."""
	out = ""
	for i, part in enumerate(glob.split("/")):
		if part == "**":
			out += "(?:[^/]+/)*"
			continue
		out += "".join("[^/]*" if c == "*" else "[^/]" if c == "?" else re.escape(c) for c in part) + "/"
	return re.compile(out.rstrip("/") + "$", re.IGNORECASE)


def _searched(path, include, exclude):
	parts = path.split("/")
	prefixes = ["/".join(parts[:n]) for n in range(1, len(parts) + 1)]
	if any(_glob_regex(g).match(prefix) for g in exclude for prefix in prefixes):
		return False
	return not include or any(_glob_regex(g).match(path) for g in include)


def _matcher(query, is_regex, case_sensitive):
	flags = 0 if case_sensitive else re.IGNORECASE
	try:
		pattern = re.compile(query if is_regex else re.escape(query), flags)
	except re.error as e:
		raise RpcError("bad_request", "bad regex: %s" % e)

	def match(line):
		found = pattern.search(line)
		if not found or found.end() == found.start():
			return None
		return len(line[:found.start()].encode("utf-16-le")) // 2
	return match
