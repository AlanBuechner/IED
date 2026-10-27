"""Language tools (docs/26 "Language tools"): 'tools:<wsId>' with the tools the loaded C/C++ and Go plugins name, each in a
scripted state - clangd found (it comes with IED), CodeLLDB and Delve missing, Go found with go.mod's pin, gopls found -
and tools.status / tools.detect / tools.install / tools.cancel / tools.dismiss. An install runs its tools one by one on
the mock's clock, writing to the workspace's 'tools' output source, and leaves each found in IED's tools folder. Nothing
is run or downloaded."""

import asyncio

from rpc import RpcError

PLUGINS = ("ied.cpp", "ied.go")
SCRIPT = {
	"clangd": {"state": "found", "version": "22.1.0", "source": "bundled", "path": "C:\\Program Files\\IED\\plugins\\cpp\\clangd\\bin\\clangd.exe"},
	"codelldb": {"state": "missing", "problem": "codelldb was not found in C:\\Program Files\\IED\\plugins\\cpp\\codelldb\\adapter and PATH"},
	"go": {"state": "found", "version": "1.27.3", "source": "path", "path": "C:\\Program Files\\Go\\bin\\go.exe", "pin": {"version": "1.22", "match": "minimum", "file": "go.mod"}},
	"gopls": {"state": "found", "version": "0.24.0", "source": "searchPath", "path": "C:\\Users\\me\\go\\bin\\gopls.exe"},
	"dlv": {"state": "missing", "problem": "dlv was not found in C:\\Users\\me\\go\\bin and PATH"},
}
INSTALL_SECONDS = 0.6


class Tools:
	def __init__(self, server):
		self.server = server
		self.states = {}
		self.dismissed = {}
		self.install = {}
		for name in ("tools.status", "tools.detect", "tools.install", "tools.cancel", "tools.dismiss"):
			server.rpc.register(name, getattr(self, "m_" + name.replace(".", "_")))
		for ws_id in server.workspace.items:
			self.opened(ws_id)

	def opened(self, ws_id):
		self.states[ws_id] = {tool["id"]: dict(SCRIPT.get(tool["id"], {"state": "missing"})) for _, tool in self.specs()}
		self.server.hub.add("tools:" + ws_id, lambda: self.snapshot(ws_id))

	def specs(self):
		"""[(plugin, tool entry)] of the loaded plugins the mock scripts."""
		found = []
		for plugin in self.server.plugins.loaded():
			if plugin.id in PLUGINS:
				for tool in plugin.manifest.get("contributes", {}).get("tools", []):
					found.append((plugin, tool))
		return found

	def status(self, ws_id, plugin, spec):
		state = self.states[ws_id][spec["id"]]
		install = spec.get("install", {})
		steps = install.get("windows", [])
		running = self.install.get(ws_id)
		shown = state["state"]
		if running and running["current"] == spec["id"]:
			shown = "installing"
		elif running and spec["id"] in running["tools"] and shown != "found":
			shown = "queued"
		return {
			"id": spec["id"], "title": spec["title"], "kind": spec.get("kind", "other"), "plugin": plugin.id, "pluginName": plugin.manifest.get("name", plugin.id),
			"state": shown, "path": state.get("path", ""), "version": state.get("version", ""), "source": state.get("source", ""), "pin": state.get("pin"),
			"problem": state.get("problem", ""), "canInstall": bool(steps), "installs": install.get("version", "latest") if steps else "",
			"installsForUser": any(step.get("scope") == "user" for step in steps), "requires": spec.get("requires", []), "homepage": spec.get("homepage", ""),
		}

	def lacking(self, ws_id):
		return ",".join(tool_id for tool_id, state in self.states[ws_id].items() if state["state"] in ("missing", "wrongVersion", "failed"))

	def snapshot(self, ws_id):
		ws = self.server.workspace.get(ws_id)
		running = self.install.get(ws_id)
		lacking = self.lacking(ws_id)
		tools = [self.status(ws_id, plugin, spec) for plugin, spec in self.specs()]
		if not ws.trusted:
			for tool in tools:
				tool.update({"state": "unchecked", "path": "", "version": "", "source": "", "problem": ""})
		return {
			"trusted": ws.trusted, "detected": True, "detecting": False,
			"install": {"tools": running["tools"], "current": running["current"]} if running else None,
			"busy": any(w != ws_id for w in self.install), "dismissed": bool(lacking) and self.dismissed.get(ws_id) == lacking, "tools": tools,
		}

	def publish(self, ws_id):
		self.server.hub.publish("tools:" + ws_id, "tools.changed", self.snapshot(ws_id))

	def say(self, ws_id, text):
		self.server.outputs.append(ws_id, "tools", text)

	async def m_tools_status(self, call, p):
		return self.snapshot(self.server.workspace.get(p.str("wsId")).ws_id)

	async def m_tools_detect(self, call, p):
		"""The mock's detection is its script again: what an install left found is missing once more."""
		ws = self.server.workspace.get(p.str("wsId"))
		if not ws.trusted:
			raise RpcError("forbidden", "the workspace is not trusted: no program is run to find its tools")
		if ws.ws_id not in self.install:
			self.states[ws.ws_id] = {tool["id"]: dict(SCRIPT.get(tool["id"], {"state": "missing"})) for _, tool in self.specs()}
			self.dismissed.pop(ws.ws_id, None)
		self.publish(ws.ws_id)
		return {}

	async def m_tools_install(self, call, p):
		ws = self.server.workspace.get(p.str("wsId"))
		if not ws.trusted:
			raise RpcError("forbidden", "the workspace is not trusted: nothing is installed for it")
		if self.install:
			raise RpcError("conflict", "an install of language tools runs already")
		specs = {spec["id"]: spec for _, spec in self.specs()}
		named = p.raw.get("tools") or [tool_id for tool_id, state in self.states[ws.ws_id].items() if state["state"] != "found" and specs[tool_id].get("install")]
		for tool_id in named:
			if tool_id not in specs:
				raise RpcError("not_found", "the workspace needs no tool " + tool_id)
		if not named:
			raise RpcError("bad_request", "nothing to install: no tool the workspace lacks has an install")
		for_user = [tool_id for tool_id in named if any(step.get("scope") == "user" for step in specs[tool_id].get("install", {}).get("windows", []))]
		if for_user and not p.raw.get("allowUserScope"):
			raise RpcError("forbidden", "an install writes outside IED's tools folder, for your user: ask, then pass allowUserScope", {"reason": "userScope", "tools": for_user})
		self.install[ws.ws_id] = {"tools": list(named), "current": named[0], "cancelled": False}
		asyncio.ensure_future(self.run(ws.ws_id, specs))
		return {"tools": list(named), "output": "tools"}

	async def run(self, ws_id, specs):
		running = self.install[ws_id]
		done = []
		for tool_id in running["tools"]:
			if running["cancelled"]:
				break
			running["current"] = tool_id
			self.publish(ws_id)
			version = specs[tool_id]["install"].get("version", "latest")
			self.say(ws_id, "\r\n== Installing %s %s\r\n-- (mock) nothing is downloaded\r\n" % (specs[tool_id]["title"], version))
			await self.server.clock.sleep(INSTALL_SECONDS)
			folder = "C:\\Users\\me\\AppData\\Local\\IED\\tools\\%s\\%s" % (tool_id, version)
			self.states[ws_id][tool_id] = {"state": "found", "version": version, "source": "installed", "path": folder + "\\" + specs[tool_id]["program"] + ".exe"}
			done.append("%s %s" % (specs[tool_id]["title"], version))
			self.say(ws_id, "\x1b[32mInstalled %s\x1b[0m\r\n" % done[-1])
		del self.install[ws_id]
		self.publish(ws_id)
		if done:
			self.server.presence.post("info", "Installed " + ", ".join(done))

	async def m_tools_cancel(self, call, p):
		ws = self.server.workspace.get(p.str("wsId"))
		if ws.ws_id not in self.install:
			raise RpcError("not_found", "no install runs for this workspace")
		self.install[ws.ws_id]["cancelled"] = True
		return {}

	async def m_tools_dismiss(self, call, p):
		ws_id = self.server.workspace.get(p.str("wsId")).ws_id
		self.dismissed[ws_id] = self.lacking(ws_id)
		self.publish(ws_id)
		return {}
