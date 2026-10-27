"""The c4 plugin and its toolchains (docs/26 "The c4 plugin and its toolchains"): the built-in copy (plugins/c4) gives a
workspace the language server, debug adapter and reference docs of the toolchain it uses - the app's c4.toolchain
setting, else the workspace's nuke.toml pin, else the newest installed one, from the store NUKE_TOOLCHAINS names - and
a toolchain's own copy (<toolchain>/ied, pinned by a solution's localPlugins) those of its toolchain."""

import asyncio
import json
import os
import shutil
import sys
import tempfile

from frames import FrameChecker
from routed import RoutedClient
from server import REPO, IedServer, ServerTestCase

sys.path.insert(0, os.path.join(REPO, "C4"))
import MakeToolchain   # noqa: E402 - what a toolchain's copy of the plugin is made with

PLUGIN = os.path.join(REPO, "plugins", "c4")
BUILT_IN = ("ied-plugin.json", "settings.schema.json", "toolchain-contributions.json", "grammars", "skills")


def write(path, text):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8") as f:
		f.write(text if isinstance(text, str) else json.dumps(text, indent="\t"))


def copy_built_in(to):
	"""The c4 plugin as IED ships it, but for its code (compiled into the server) and its Nuke package."""
	for name in BUILT_IN:
		source = os.path.join(PLUGIN, name)
		if os.path.isdir(source):
			shutil.copytree(source, os.path.join(to, name))
		else:
			os.makedirs(to, exist_ok=True)
			shutil.copy2(source, os.path.join(to, name))


def copy_toolchain_plugin(to):
	"""The c4 plugin as a toolchain ships it (C4/MakeToolchain.py): its files, and the declarative manifest written for
	the toolchain."""
	for rel in MakeToolchain.plugin_files(PLUGIN):
		os.makedirs(os.path.dirname(os.path.join(to, rel)), exist_ok=True)
		shutil.copy2(os.path.join(PLUGIN, rel), os.path.join(to, rel))
	with open(os.path.join(PLUGIN, "ied-plugin.json"), encoding="utf-8") as f, open(os.path.join(PLUGIN, "toolchain-contributions.json"), encoding="utf-8") as g:
		write(os.path.join(to, "ied-plugin.json"), MakeToolchain.toolchain_manifest(f.read(), g.read()))


def make_toolchain(folder, version):
	"""What the plugin looks for in a toolchain: its C4.exe and toolchain.json, the tools it starts and the reference docs
	of its own copy of the plugin."""
	write(os.path.join(folder, "toolchain.json"), {"name": "c4", "version": version})
	for path in ("C4.exe", "tools/C4LSP/C4LSP.exe", "tools/C4DAP/C4DAP.exe"):
		write(os.path.join(folder, *path.split("/")), "")
	write(os.path.join(folder, "ied", "docs", "01-what-is-c4.md"), "# What is C4 %s\n" % version)


def slashed(path):
	return path.replace("\\", "/")


class C4PluginTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-c4-plugin-")
		self.home = os.path.join(self.root, "home")
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		copy_built_in(os.path.join(self.home, "plugins", "c4"))
		self.store = os.path.join(self.root, "store")
		for version in ("0.2.0", "0.10.0", "dev"):
			make_toolchain(os.path.join(self.store, "c4", version), version)
		self.game = os.path.join(self.root, "game")
		write(os.path.join(self.game, "nuke.toml"), '[workspace]\nname = "Game"\ntoolchain = "0.2.0"   # pinned\n')
		self.saved = os.environ.get("NUKE_TOOLCHAINS")
		os.environ["NUKE_TOOLCHAINS"] = self.store
		self.server = None

	def start(self, workspace):
		self.server = IedServer(extra=("--home", self.home, "--workspace", workspace))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		if self.saved is None:
			os.environ.pop("NUKE_TOOLCHAINS", None)
		else:
			os.environ["NUKE_TOOLCHAINS"] = self.saved
		if self.server:
			self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def connect(self):
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})

	async def asyncTearDown(self):
		if getattr(self, "client", None):
			self.assertEqual([], self.client.violations)
			self.client.abort()

	async def c4(self, source):
		plugins = (await self.client.request("plugins.contributions", {"wsId": "w1"}))["plugins"]
		found = [p for p in plugins if p["id"] == "c4"]
		self.assertEqual([source], [p["source"] for p in found], "one c4 plugin serves the workspace")
		return found[0]["contributes"]

	async def until(self, source, test, what):
		"""The c4 plugin's contributions once `test` holds for them (its code gives them on its own thread)."""
		for _ in range(100):
			contributes = await self.c4(source)
			if test(contributes):
				return contributes
			await asyncio.sleep(0.1)
		self.fail("%s; the plugin gives %s" % (what, json.dumps(contributes)[:2000]))

	def tool(self, contributes, point):
		entries = contributes.get(point, [])
		return entries[0]["command"] if entries else None

	def tool_is(self, point, path):
		return lambda contributes: self.tool(contributes, point) == slashed(path)

	async def test_the_built_in_uses_the_pinned_toolchain_then_follows_the_pin(self):
		self.start(self.game)
		await self.connect()
		pinned = os.path.join(self.store, "c4", "0.2.0")
		given = await self.until("builtin", self.tool_is("lspProfiles", os.path.join(pinned, "tools", "C4LSP", "C4LSP.exe")), "the pinned toolchain's language server")
		self.assertEqual(slashed(pinned), given["lspProfiles"][0]["env"]["C4_HOME"])
		self.assertEqual(slashed(os.path.join(pinned, "tools", "C4DAP", "C4DAP.exe")), self.tool(given, "dapProviders"))
		self.assertIn("workspaceTrusted", given["lspProfiles"][0]["when"], "C4LSP runs only in a trusted workspace, as when only a toolchain's copy brought it")
		self.assertEqual([{"id": "c4", "folder": slashed(os.path.join(pinned, "ied", "docs"))}], [{"id": d["id"], "folder": d["folder"]} for d in given["aiDocs"]],
			"the reference docs of the toolchain it uses, its own copy's")
		self.assertIn("skills", given, "its own contributions stay")
		write(os.path.join(self.game, "nuke.toml"), '[workspace]\nname = "Game"\ntoolchain = "latest"\n')
		newest = os.path.join(self.store, "c4", "0.10.0", "tools", "C4LSP", "C4LSP.exe")
		await self.until("builtin", self.tool_is("lspProfiles", newest), "latest: the newest numbered version, after the edit")
		await self.client.request("settings.set", {"tier": "app", "key": "c4.toolchain", "value": "dev"})
		await self.until("builtin", self.tool_is("lspProfiles", os.path.join(self.store, "c4", "dev", "tools", "C4LSP", "C4LSP.exe")), "the app's setting wins")
		await self.client.request("settings.set", {"tier": "app", "key": "c4.toolchain", "value": "9.9.9"})
		await self.until("builtin", lambda contributes: "lspProfiles" not in contributes, "a setting naming no installed toolchain gives no tools")

	async def test_a_folder_without_a_pin_gets_the_newest_and_none_without_a_store(self):
		folder = os.path.join(self.root, "loose")
		write(os.path.join(folder, "main.cf"), "int32 main() { return 0; }\n")
		self.start(folder)
		await self.connect()
		await self.until("builtin", self.tool_is("lspProfiles", os.path.join(self.store, "c4", "0.10.0", "tools", "C4LSP", "C4LSP.exe")), "the newest installed toolchain")
		self.server.stop()
		self.client.abort()
		self.client = None
		shutil.rmtree(self.store)
		self.start(folder)
		await self.connect()
		await asyncio.sleep(1)
		given = await self.c4("builtin")
		self.assertNotIn("lspProfiles", given, "no toolchain, no language server")
		self.assertNotIn("aiDocs", given, "nor its docs")
		self.assertIn("languages", given, "the language itself is still the plugin's")

	async def test_a_toolchain_copy_uses_its_toolchain_and_ships_its_docs(self):
		toolchain = os.path.join(self.root, "pinned-toolchain")
		make_toolchain(toolchain, "0.4.0")
		copy_toolchain_plugin(os.path.join(toolchain, "ied"))
		write(os.path.join(toolchain, "ied", "docs", "01-what-is-c4.md"), "# What is C4\n")
		write(os.path.join(self.game, "build", "ied", "Game.iedproj"), {"version": 1, "name": "Game", "root": "../..", "kind": "executable", "language": "c4"})
		solution = os.path.join(self.game, "Game.iedsln")
		write(solution, {"version": 1, "name": "Game", "projects": [{"file": "build/ied/*.iedproj"}], "localPlugins": [{"id": "c4", "path": slashed(os.path.join(toolchain, "ied"))}]})
		self.start(solution)
		await self.connect()
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		given = await self.c4("project")
		self.assertEqual("${pluginDir}/../tools/C4LSP/C4LSP.exe", self.tool(given, "lspProfiles"), "the toolchain's copy names its own toolchain's server")
		self.assertEqual("${pluginDir}/../tools/C4DAP/C4DAP.exe", self.tool(given, "dapProviders"))
		self.assertEqual("docs", given["aiDocs"][0]["folder"], "and the reference docs it holds")
		listed = {(p["id"], p["source"]): p for p in (await self.client.request("plugins.list", {"wsId": "w1"}))["plugins"]}
		self.assertEqual(("loaded", [], "declarative"), (listed[("c4", "project")]["state"], listed[("c4", "project")]["problems"], listed[("c4", "project")]["kind"]))
