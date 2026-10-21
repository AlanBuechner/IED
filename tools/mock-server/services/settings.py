"""Settings (docs/07 "Settings on the wire"): the settings the loaded plugins declare, an app tier and the workspace's
solution tier kept in memory, effective values by merge kind, settings.schema / get / set / reset and the
settings:app and settings:<wsId> topics. Values are checked against their schema as the real server does."""

import os

import minischema
from rpc import RpcError

TIERS = ("app", "solution", "project")
HOST_SCHEMA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "shared", "host-api", "schemas", "host-settings.v1.schema.json")
HOST_OWNER, HOST_PAGE = "ied", "Environment"


class Entry:
	def __init__(self, plugin, key, schema, validator, page):
		self.plugin = plugin
		self.key = key
		self.schema = schema
		self.validator = validator
		self.page = page
		self.default = schema.get("default")
		self.tiers = schema.get("x-ied-tiers", list(TIERS))
		self.merge = schema.get("x-ied-merge", "replace")

	def to_dict(self):
		out = {"plugin": self.plugin, "schema": self.schema, "default": self.default, "tiers": self.tiers, "merge": self.merge}
		if self.page:
			out["page"] = self.page
		return out


class Tier:
	def __init__(self, file):
		self.file = file
		self.revision = 1
		self.values = {}

	def state(self):
		return {"file": self.file, "revision": self.revision, "problems": []}


def merged(entry, below, above):
	if entry.merge == "merge" and isinstance(below, dict) and isinstance(above, dict):
		return dict(below, **above)
	if entry.merge == "append" and isinstance(below, list) and isinstance(above, list):
		return below + above
	return above


class Settings:
	def __init__(self, server):
		self.server = server
		self.app = Tier("<mock>/settings.json")
		self.solutions = {}
		self.catalog = None
		server.hub.add("settings:app", lambda: self.snapshot(None))
		for ws_id in server.workspace.items:
			self.opened(ws_id)
		for name in ("settings.schema", "settings.get", "settings.set", "settings.reset"):
			server.rpc.register(name, getattr(self, "m_" + name.replace(".", "_")))

	def entries(self):
		"""The host's own settings (owner 'ied'), then the loaded plugins'."""
		if self.catalog is None:
			registry = minischema.Registry()
			self.catalog = {}
			self.declare(registry, HOST_OWNER, os.path.normpath(HOST_SCHEMA), HOST_PAGE)
			for plugin in self.server.plugins.loaded():
				declared = plugin.manifest.get("contributes", {}).get("settings")
				if declared:
					self.declare(registry, plugin.id, str(plugin.folder / declared["schema"]), declared.get("page"))
		return self.catalog

	def declare(self, registry, owner, path, page):
		for key, schema in registry.load(path).get("properties", {}).items():
			if key not in self.catalog:
				self.catalog[key] = Entry(owner, key, schema, minischema.Validator(registry, path, "/properties/" + key), page)

	def values(self, ws_id):
		out = {}
		for key, entry in self.entries().items():
			value, source = entry.default, "default"
			for tier, name in ((self.app, "app"), (self.solutions.get(ws_id), "solution")):
				if tier is not None and key in tier.values:
					value, source = merged(entry, value, tier.values[key]), name
			out[key] = {"value": value, "source": source}
		return out

	def opened(self, ws_id):
		"""A workspace's topics and state; also for one opened after the start (ws.openRecent)."""
		self.solutions[ws_id] = Tier("<mock>/%s/.ied/solution.json" % ws_id)
		self.server.hub.add("settings:" + ws_id, lambda: self.snapshot(ws_id))

	def snapshot(self, ws_id):
		tiers = {"app": self.app.state()}
		if ws_id is not None:
			tiers["solution"] = self.solutions[ws_id].state()
		return {"tiers": tiers, "projects": {}, "values": self.values(ws_id)}

	def publish(self):
		self.server.hub.publish("settings:app", "settings.changed", self.snapshot(None))
		for ws_id in self.solutions:
			self.server.hub.publish("settings:" + ws_id, "settings.changed", self.snapshot(ws_id))

	def tier_of(self, p):
		tier = p.str("tier")
		if tier == "app":
			return self.app
		if tier == "solution":
			ws_id = p.str("wsId")
			if ws_id not in self.solutions:
				raise RpcError("not_found", "no workspace %r" % ws_id)
			return self.solutions[ws_id]
		raise RpcError("bad_request", "the mock has no %s tier" % tier)

	async def m_settings_schema(self, call, p):
		return {"settings": {key: entry.to_dict() for key, entry in self.entries().items()}}

	async def m_settings_get(self, call, p):
		return self.snapshot(p.str("wsId", None, required=False))

	async def m_settings_set(self, call, p):
		tier = self.tier_of(p)
		key = p.str("key")
		entry = self.entries().get(key)
		if entry is None:
			raise RpcError("bad_request", "unknown setting " + key, {"problems": []})
		if p.str("tier") not in entry.tiers:
			raise RpcError("bad_request", "%s cannot be set in the %s tier" % (key, p.str("tier")), {"problems": []})
		if "value" not in p.raw:
			raise RpcError("bad_request", "set needs a value (reset removes one)")
		problems = [str(problem) for problem in entry.validator.validate(p.raw["value"])]
		if problems:
			raise RpcError("bad_request", "the value does not fit " + key, {"problems": problems})
		tier.values[key] = p.raw["value"]
		tier.revision += 1
		self.publish()
		return {"revision": tier.revision}

	async def m_settings_reset(self, call, p):
		tier = self.tier_of(p)
		tier.values.pop(p.str("key"), None)
		tier.revision += 1
		self.publish()
		return {"revision": tier.revision}
