"""New projects (docs/03 "New projects"): project.templates lists the built-in plugins' projectTemplates as the server
does, and project.create makes a project in memory - the mock runs no template steps, so no `nuke init`: a new
workspace is a solution whose one project holds the template's files (${name} filled) beside a nuke.toml, and a
solution with a nuke.toml takes a project added the same way. Managing projects (docs/03 "Managing projects"):
project.add, project.remove and project.move change the solution model in memory as a hand-written solution changes
(no build system runs), a folder workspace becoming a solution in place. The refusals are the server's."""

import posixpath
import re

from rpc import RpcError
from services.workspace import File, normalize

NAME = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}$")
LOCATION = "C:/mock"


class Projects:
	def __init__(self, server):
		self.server = server
		server.rpc.register("project.templates", self.m_project_templates)
		server.rpc.register("project.create", self.m_project_create)
		server.rpc.register("project.add", self.m_project_add)
		server.rpc.register("project.remove", self.m_project_remove)
		server.rpc.register("project.move", self.m_project_move)

	def templates(self):
		"""[(plugin, template)] of the loaded plugins, each key once."""
		found, seen = [], set()
		for plugin in self.server.plugins.loaded():
			for template in plugin.manifest.get("contributes", {}).get("projectTemplates", []):
				key = plugin.id + "/" + template["id"]
				if key not in seen:
					seen.add(key)
					found.append((plugin, template))
		return found

	def can_add(self, template, ws):
		return bool(ws and ws.trusted and ws.solution and any(m in ws.files for m in template.get("into", [])))

	def language_name(self, plugin, language):
		for entry in plugin.manifest.get("contributes", {}).get("languages", []):
			if entry.get("id") == language:
				return (entry.get("aliases") or [language])[0]
		return language

	async def m_project_templates(self, call, p):
		ws = self.server.workspace.get(p.str("wsId")) if "wsId" in p.raw else None
		templates, icons, drawn = [], [], set()
		for plugin, t in self.templates():
			templates.append({"key": plugin.id + "/" + t["id"], "plugin": plugin.id, "id": t["id"], "title": t["title"], "description": t.get("description", ""),
				"language": t.get("language", ""), "languageName": self.language_name(plugin, t.get("language", "")), "tags": t.get("tags", []), "canAdd": self.can_add(t, ws)})
			if plugin.id not in drawn:
				drawn.add(plugin.id)
				icons += [{"plugin": {"id": plugin.id, "base": "/plugins/%s/" % plugin.id}, "value": icon} for icon in plugin.manifest.get("contributes", {}).get("projectIcons", [])]
		return {"templates": templates, "icons": icons, "location": LOCATION}

	def files_of(self, plugin, template, name):
		"""The template's files, ${name} filled in their paths and text."""
		folder = template.get("files")
		if not folder:
			return {}
		base = plugin.folder / folder
		return {str(f.relative_to(base)).replace("\\", "/").replace("${name}", name): f.read_text(encoding="utf-8").replace("${name}", name) for f in base.rglob("*") if f.is_file()}

	def project_model(self, root, folder, name, language, kind):
		file = "%s/build/ied/%s.iedproj" % (root, name)
		data = {"version": 1, "name": name, "root": "../../" + folder if folder else "../..", "kind": kind, "language": language, "presets": [language + ".nuke"]}
		return {"name": name, "file": file, "root": root + ("/" + folder if folder else ""), "kind": kind, "problems": [], "data": data}

	async def m_project_create(self, call, p):
		found = {plugin.id + "/" + t["id"]: (plugin, t) for plugin, t in self.templates()}
		key = p.str("template")
		if key not in found:
			raise RpcError("not_found", "no project template " + key)
		plugin, template = found[key]
		name = p.str("name")
		if not NAME.match(name):
			raise RpcError("bad_request", "'%s' cannot name a project: use letters, digits, '_', '.' and '-'" % name)
		ws = self.server.workspace.get(p.str("wsId")) if "wsId" in p.raw else None
		if ws and not ws.trusted:
			raise RpcError("forbidden", "a template runs commands in the workspace: trust it first")
		if ws and not self.can_add(template, ws):
			raise RpcError("bad_request", "the template %s cannot add a project to this solution" % key)
		language = "cpp" if template.get("language") == "c" else template.get("language", "")
		kind = "library" if "Library" in template.get("tags", []) else "executable"
		if ws:
			if any(project["name"].lower() == name.lower() for project in ws.solution["projects"]) or any(path.startswith(name + "/") for path in ws.files):
				raise RpcError("conflict", "%s/%s is there already: pick another name or folder" % (ws.root, name))
			for path, text in self.files_of(plugin, template, name).items():
				ws.files.setdefault(name + "/" + path, File(text))
			ws.files.setdefault(name + "/nuke.toml", File("[project]\nname = \"%s\"\n" % name))
			ws.solution["projects"].append(self.project_model(ws.root, name, name, language, kind))
			self.solution_changed(ws)
			return {"workspace": ws.summary(), "path": ws.root + "/" + name, "log": ""}
		location = p.str("location", "", required=False).replace("\\", "/").rstrip("/")
		if not re.match(r"^([A-Za-z]:/|/)", location):
			raise RpcError("bad_request", "a new project needs the folder it goes in, as a full path")
		root = location + "/" + name
		if any(w.root.lower() == root.lower() for w in self.server.workspace.items.values()):
			raise RpcError("conflict", "%s is there already: pick another name or folder" % root)
		files = dict(self.files_of(plugin, template, name))
		files["nuke.toml"] = "[workspace]\nname = \"%s\"\n\n[project]\nname = \"%s\"\nkind = \"%s\"\n" % (name, name, kind)
		files[name + ".iedsln"] = '{ "version": 1, "name": "%s", "projects": [{ "file": "build/ied/*.iedproj", "optional": true }], "items": ["nuke.toml"] }\n' % name
		solution = {"name": name, "file": root + "/" + name + ".iedsln", "problems": [], "groups": [], "items": ["nuke.toml"], "generator": None, "localPlugins": [],
			"projects": [self.project_model(root, "", name, language, kind)]}
		made = self.server.workspace.add(name, root, files, "solution", solution)
		self.server.workspace.opened(made, root + "/" + name + ".iedsln", "solution")
		return {"workspace": made.summary(), "path": root, "log": "nuke: created workspace %s (mock)" % name}

	# --- Managing projects ------------------------------------------------------------------------------------------

	def solution_changed(self, ws):
		self.server.hub.publish("ws:" + ws.ws_id, "solution.changed", {"solution": ws.solution, "kind": ws.kind, "name": ws.name})

	def changed(self, ws, path, name):
		self.solution_changed(ws)
		return {"workspace": ws.summary(), "path": path, "project": name, "notes": [], "log": ""}

	@staticmethod
	def folder_of(ws, project):
		"""The project's folder relative to the workspace: "" for its root, None outside it."""
		root = project["root"].replace("\\", "/")
		if root.lower() == ws.root.lower():
			return ""
		return root[len(ws.root) + 1:] if root.lower().startswith(ws.root.lower() + "/") else None

	@staticmethod
	def named(ws, name):
		if not ws.solution:
			raise RpcError("bad_request", "the workspace is a folder, not a solution: it has no projects (import it to make it one)")
		for project in ws.solution["projects"]:
			if project["name"] == name:
				return project
		raise RpcError("not_found", "no project %s in the solution; its projects: %s" % (name, ", ".join(p["name"] for p in ws.solution["projects"]) or "none"))

	async def m_project_add(self, call, p):
		ws = self.server.workspace.get(p.str("wsId"))
		folder = normalize(p.str("folder"))
		if not folder:
			raise RpcError("bad_request", "that is the workspace's own folder: import it (project.import) to make the workspace a solution")
		if folder in ws.files or not ws.is_dir(folder):
			raise RpcError("not_found", "no folder " + folder)
		for project in (ws.solution or {}).get("projects", []):
			if self.folder_of(ws, project) == folder:
				raise RpcError("conflict", "%s is project %s already" % (folder, project["name"]))
		name = p.str("name", "", required=False) or posixpath.basename(folder)
		if not NAME.match(name):
			raise RpcError("bad_request", "'%s' cannot name a project: give a name of letters, digits, '_', '.' and '-'" % name)
		if ws.solution and any(project["name"] == name for project in ws.solution["projects"]):
			raise RpcError("conflict", "the solution has a project named %s already: give another name" % name)
		if not ws.solution:
			ws.kind = "solution"
			ws.solution = {"name": ws.name, "file": "%s/%s.iedsln" % (ws.root, ws.name), "problems": [], "groups": [], "items": [], "generator": None, "localPlugins": [], "projects": []}
			self.server.hub.publish("server", "workspace.changed", ws.summary())
		kind = p.str("kind", "", required=False) or "other"
		data = {"version": 1, "name": name, "root": ".", "kind": kind}
		ws.files["%s/%s.iedproj" % (folder, name)] = File('{ "version": 1, "name": "%s", "root": ".", "kind": "%s" }\n' % (name, kind))
		ws.solution["projects"].append({"name": name, "file": "%s/%s/%s.iedproj" % (ws.root, folder, name), "root": ws.root + "/" + folder, "kind": kind, "problems": [], "data": data})
		return self.changed(ws, ws.root + "/" + folder, name)

	async def m_project_remove(self, call, p):
		ws = self.server.workspace.get(p.str("wsId"))
		name = p.str("project")
		project = self.named(ws, name)
		referrers = [other["name"] for other in ws.solution["projects"] if name in other["data"].get("references", []) and other["name"] != name]
		if referrers:
			raise RpcError("conflict", "%s depend on %s (their references): take it out of their dependencies first, or remove them too" % (", ".join(referrers), name))
		folder = self.folder_of(ws, project)
		deleting = p.bool("deleteFiles", False, required=False)
		if deleting and not folder:
			raise RpcError("bad_request", "%s's folder is %s: remove it without deleting its files" % (name, "the workspace's own" if folder == "" else "outside the workspace"))
		ws.solution["projects"].remove(project)
		result = self.changed(ws, project["root"], name)
		if deleting:
			result["trashId"] = self.server.workspace._delete_folder(ws, folder, True)["trashId"]
		return result

	async def m_project_move(self, call, p):
		ws = self.server.workspace.get(p.str("wsId"))
		name = p.str("project")
		project = self.named(ws, name)
		folder = self.folder_of(ws, project)
		if not folder:
			raise RpcError("bad_request", "%s's folder is %s: it cannot move" % (name, "the workspace's own" if folder == "" else "outside the workspace"))
		target = normalize(p.str("to"))
		if not target or target == folder:
			raise RpcError("bad_request", "give the folder to move %s to, relative to the workspace" % name)
		if target.startswith(folder + "/"):
			raise RpcError("bad_request", "a project cannot move into its own folder")
		if target in ws.files or ws.is_dir(target):
			raise RpcError("conflict", target + " is there already")
		self.server.workspace._rename_folder(ws, folder, target)
		project["root"] = ws.root + "/" + target
		project["file"] = project["file"].replace("/%s/" % folder, "/%s/" % target, 1)
		return self.changed(ws, project["root"], name)
