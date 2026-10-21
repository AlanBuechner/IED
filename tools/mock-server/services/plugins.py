"""Built-in plugins, read from the repository's plugins/ folder as the real host reads them (docs/02): the 'plugins'
topic, plugins.list, plugins.contributions with each UI module's URL, and the files /plugins/<id>/<path> serves.

The mock has no app or project-local plugins: a valid built-in manifest is loaded unless disabled (plugins.setEnabled,
kept in memory), an invalid one is 'failed' with its problems; plugins.approve has no app plugin to approve. It runs
no plugin code (docs/26 section 2): plugins.call finds no server binding.
"""

import pathlib
import re

import jsonc
import minischema
from rpc import RpcError

REPO = pathlib.Path(__file__).resolve().parents[3]
PLUGINS_DIR = REPO / "plugins"
MANIFEST_SCHEMA = REPO / "shared" / "host-api" / "schemas" / "plugin-manifest.v1.schema.json"
MANIFEST_FILE = "ied-plugin.json"
UI_PATH = re.compile(r"^[A-Za-z0-9._/-]+$")


class Plugin:
	def __init__(self, folder, manifest, problems):
		self.folder = folder
		self.manifest = manifest
		self.problems = problems

	@property
	def id(self):
		return self.manifest.get("id", self.folder.name)

	@property
	def ui(self):
		return self.manifest.get("runtime", {}).get("ui", "")

	def summary(self, disabled):
		runtime = self.manifest.get("runtime", {})
		state = "failed" if self.problems else "disabled" if self.id in disabled else "loaded"
		return {
			"id": self.id, "name": self.manifest.get("name", ""), "version": self.manifest.get("version", ""),
			"kind": runtime.get("kind", ""), "source": "builtin", "state": state,
			"problems": self.problems, "permissions": self.manifest.get("permissions", []),
			"contributes": list(self.manifest.get("contributes", {})),
		}


def read_plugin(folder, validator):
	try:
		manifest = jsonc.load_file(str(folder / MANIFEST_FILE))
	except (OSError, ValueError) as e:
		return Plugin(folder, {}, ["cannot read %s: %s" % (MANIFEST_FILE, e)])
	problems = [str(problem) for problem in validator.validate(manifest)]
	ui = manifest.get("runtime", {}).get("ui", "") if isinstance(manifest.get("runtime"), dict) else ""
	if ui and not is_ui_path(folder, ui):
		problems.append("runtime.ui '%s' is not a file in the plugin's folder named with letters, digits, '.', '_', '-' and '/'" % ui)
	return Plugin(folder, manifest, problems)


def is_ui_path(folder, path):
	if not UI_PATH.match(path) or ".." in path or path.startswith("/"):
		return False
	return (folder / path).is_file()


class Plugins:
	def __init__(self, server):
		self.server = server
		self.disabled = set()
		self.items = self._discover()
		server.hub.add("plugins", self.snapshot)
		server.rpc.register("plugins.list", self.m_list)
		server.rpc.register("plugins.contributions", self.m_contributions)
		server.rpc.register("plugins.setEnabled", self.m_set_enabled)
		server.rpc.register("plugins.approve", self.m_approve)
		server.rpc.register("plugins.reload", self.m_reload)
		server.rpc.register("plugins.call", self.m_call)

	def _discover(self):
		registry = minischema.Registry()
		validator = minischema.Validator(registry, str(MANIFEST_SCHEMA))
		folders = sorted(p for p in PLUGINS_DIR.iterdir() if (p / MANIFEST_FILE).is_file()) if PLUGINS_DIR.is_dir() else []
		return [read_plugin(folder, validator) for folder in folders]

	def loaded(self):
		return [p for p in self.items if not p.problems and p.id not in self.disabled]

	def snapshot(self):
		return {"plugins": [p.summary(self.disabled) for p in self.items]}

	def changed(self):
		self.server.hub.publish("plugins", "plugins.changed", self.snapshot())

	def language_of(self, path):
		"""The language the loaded plugins give a file (as src/plugins/languages.ch): a file name wins over an extension,
		the plugin loaded last wins, extensions fold case."""
		name = path.replace("\\", "/").rsplit("/", 1)[-1]
		dot = name.rfind(".")
		extension = name[dot:].lower() if dot > 0 else ""
		by_name = by_extension = None
		for plugin in self.loaded():
			for language in plugin.manifest.get("contributes", {}).get("languages", []):
				if name in language.get("fileNames", []):
					by_name = language["id"]
				if extension and extension in [e.lower() for e in language.get("extensions", [])]:
					by_extension = language["id"]
		return by_name or by_extension or "plaintext"

	def file_of(self, plugin_id, rel):
		plugin = next((p for p in self.loaded() if p.id == plugin_id), None)
		if plugin is None:
			return None
		target = (plugin.folder / rel).resolve()
		if plugin.folder.resolve() not in target.parents or not target.is_file():
			return None
		return target

	def _workspace(self, p):
		ws_id = p.str("wsId", required=False)
		if ws_id is not None and ws_id not in self.server.workspace.items:
			raise RpcError("not_found", "no workspace '%s'" % ws_id)

	async def m_list(self, call, p):
		self._workspace(p)
		return self.snapshot()

	async def m_contributions(self, call, p):
		self._workspace(p)
		plugins = []
		for plugin in self.loaded():
			entry = {"id": plugin.id, "source": "builtin", "contributes": plugin.manifest.get("contributes", {})}
			if plugin.ui:
				entry["ui"] = "/plugins/%s/%s" % (plugin.id, plugin.ui)
			plugins.append(entry)
		return {"plugins": plugins}

	async def m_set_enabled(self, call, p):
		plugin_id = p.str("id")
		plugin = next((x for x in self.items if x.id == plugin_id), None)
		if plugin is None:
			raise RpcError("not_found", "no plugin %s" % plugin_id)
		if p.bool("enabled", True, required=False):
			self.disabled.discard(plugin_id)
		else:
			self.disabled.add(plugin_id)
		self.changed()
		return plugin.summary(self.disabled)

	async def m_approve(self, call, p):
		raise RpcError("not_found", "no app plugin %s" % p.str("id"))

	async def m_reload(self, call, p):
		self.items = self._discover()
		self.changed()
		return self.snapshot()

	async def m_call(self, call, p):
		raise RpcError("not_found", "no plugin provides the server binding '%s' (the mock runs no plugin code)" % p.str("name"))
