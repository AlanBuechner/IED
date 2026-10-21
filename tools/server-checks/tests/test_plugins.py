"""The plugin host (docs/02 'Lifecycle', docs/11 'Trust layers'): built-in and app plugins found, manifests validated
against the schema and the host, an app plugin loading only once its exact content is approved and overriding the
built-in of its id, enabling and disabling kept across restarts, dependencies, and plugin files served."""

import json
import os
import shutil
import tempfile

from frames import FrameChecker
from iedclient import Client
from server import REPO, IedServer, ServerTestCase


def manifest(plugin_id, **fields):
	body = {
		"manifestVersion": 1, "id": plugin_id, "name": plugin_id.title(), "version": "1.0.0",
		"ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
		"contributes": {"commands": [{"id": plugin_id + ".hello", "title": "Hello"}]},
	}
	body.update(fields)
	return body


class PluginTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-plugins-")
		self.home = os.path.join(self.root, "home")
		self.app = os.path.join(self.root, "app")
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		os.makedirs(os.path.join(self.home, "plugins"))
		os.makedirs(os.path.join(self.app, "plugins"))
		self.server = None

	def tearDown(self):
		if self.server:
			self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	def add(self, where, plugin_id, body, files=None):
		folder = os.path.join(self.home if where == "builtin" else self.app, "plugins", plugin_id)
		os.makedirs(folder, exist_ok=True)
		with open(os.path.join(folder, "ied-plugin.json"), "w", encoding="utf-8") as f:
			f.write("// written by test_plugins.py\n" + json.dumps(body, indent="\t"))
		for name, text in (files or {}).items():
			path = os.path.join(folder, *name.split("/"))
			os.makedirs(os.path.dirname(path), exist_ok=True)
			with open(path, "w", encoding="utf-8") as f:
				f.write(text)
		return folder

	def start(self):
		if self.server:
			self.server.stop(remove_data=False)
		self.server = IedServer(extra=("--home", self.home, "--app-data", self.app), data=getattr(self, "data", None))
		self.data = self.server.data
		self.assertTrue(self.server.started, self.server.error)
		self.cookie = self.server.sign_in()

	async def call(self, method, params=None):
		client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		try:
			return await client.request(method, params or {})
		finally:
			client.abort()

	async def states(self):
		return {(p["id"], p["source"]): p for p in (await self.call("plugins.list"))["plugins"]}

	async def test_declarative_plugins_load_and_contribute(self):
		self.add("builtin", "ied.sample", manifest("ied.sample"), {"grammars/x.json": "{\"tokenizer\": {}}"})
		self.start()
		plugin = (await self.states())[("ied.sample", "builtin")]
		self.assertEqual(("loaded", []), (plugin["state"], plugin["problems"]))
		self.assertEqual(["commands"], plugin["contributes"])
		contributions = await self.call("plugins.contributions")
		self.assertEqual([{"id": "ied.sample", "source": "builtin", "contributes": manifest("ied.sample")["contributes"]}], contributions["plugins"])

	async def test_broken_manifests_fail_with_reasons(self):
		self.add("builtin", "bad-schema", manifest("bad-schema", version="one"))
		self.add("builtin", "too-new", manifest("too-new", ied={"api": "^2"}))
		self.add("builtin", "scripted", manifest("scripted", runtime={"kind": "js", "entry": "main.js"}))
		self.add("app", "fake-builtin", manifest("fake-builtin", runtime={"kind": "builtin"}))
		self.add("builtin", "not-json", {})
		with open(os.path.join(self.home, "plugins", "not-json", "ied-plugin.json"), "w") as f:
			f.write("{ broken")
		os.makedirs(os.path.join(self.home, "plugins", "no-manifest"))
		self.start()
		states = await self.states()
		self.assertNotIn(("no-manifest", "builtin"), states, "a folder without a manifest is not a plugin")
		problems = {key[0]: " ".join(value["problems"]) for key, value in states.items() if value["state"] == "failed"}
		self.assertIn("/version", problems["bad-schema"])
		self.assertIn("needs host API ^2", problems["too-new"])
		self.assertIn("cannot load yet", problems["scripted"])
		self.assertIn("ships with IED itself", problems["fake-builtin"])
		self.assertIn("not JSON", problems["not-json"])

	async def test_app_plugins_need_approval_of_their_exact_content(self):
		folder = self.add("app", "acme", manifest("acme"), {"snippets/a.json": "[]"})
		self.start()
		self.assertEqual("needsApproval", (await self.states())[("acme", "app")]["state"])
		approved = await self.call("plugins.approve", {"id": "acme"})
		self.assertEqual("loaded", approved["state"])
		with open(os.path.join(folder, "snippets", "a.json"), "w") as f:
			f.write("[1]")
		await self.call("plugins.reload")
		self.assertEqual("needsApproval", (await self.states())[("acme", "app")]["state"], "a changed file needs approval again")
		log = open(os.path.join(self.server.data, "logs", "security.log")).read()
		self.assertIn("plugin-approved", log)

	async def test_an_app_plugin_overrides_the_built_in_one(self):
		self.add("builtin", "c4", manifest("c4", version="1.0.0"))
		self.add("app", "c4", manifest("c4", version="2.0.0"))
		self.start()
		states = await self.states()
		self.assertEqual("loaded", states[("c4", "builtin")]["state"], "until the app one is approved the built-in serves")
		await self.call("plugins.approve", {"id": "c4"})
		states = await self.states()
		self.assertEqual(("loaded", "2.0.0"), (states[("c4", "app")]["state"], states[("c4", "app")]["version"]))
		self.assertEqual(("overridden", "app"), (states[("c4", "builtin")]["state"], states[("c4", "builtin")]["overriddenBy"]))

	async def test_disabling_is_kept_across_restarts(self):
		self.add("builtin", "ied.sample", manifest("ied.sample"))
		self.start()
		self.assertEqual("disabled", (await self.call("plugins.setEnabled", {"id": "ied.sample", "enabled": False}))["state"])
		self.assertEqual([], (await self.call("plugins.contributions"))["plugins"])
		self.start()
		self.assertEqual("disabled", (await self.states())[("ied.sample", "builtin")]["state"])
		self.assertEqual("loaded", (await self.call("plugins.setEnabled", {"id": "ied.sample", "enabled": True}))["state"])

	async def test_dependencies(self):
		self.add("builtin", "base", manifest("base", version="1.4.0"))
		self.add("builtin", "uses-base", manifest("uses-base", dependencies=[{"id": "base", "version": "^1.2"}]))
		self.add("builtin", "wants-new-base", manifest("wants-new-base", dependencies=[{"id": "base", "version": "^2"}]))
		self.add("builtin", "wants-missing", manifest("wants-missing", dependencies=[{"id": "missing", "version": "^1"}]))
		self.add("builtin", "optional-missing", manifest("optional-missing", dependencies=[{"id": "missing", "version": "^1", "optional": True}]))
		self.add("builtin", "on-failed", manifest("on-failed", dependencies=[{"id": "wants-missing", "version": "^1"}]))
		self.add("builtin", "loop-a", manifest("loop-a", dependencies=[{"id": "loop-b", "version": "^1"}]))
		self.add("builtin", "loop-b", manifest("loop-b", dependencies=[{"id": "loop-a", "version": "^1"}]))
		self.start()
		states = {key[0]: value["state"] for key, value in (await self.states()).items()}
		self.assertEqual({"base": "loaded", "uses-base": "loaded", "wants-new-base": "failed", "wants-missing": "failed", "optional-missing": "loaded",
			"on-failed": "failed", "loop-a": "failed", "loop-b": "failed"}, states)

	async def test_plugin_files_are_served_from_inside_the_plugin(self):
		self.add("builtin", "ied.sample", manifest("ied.sample"), {"grammars/x.json": "{\"tokenizer\": {}}"})
		self.add("builtin", "off", manifest("off"), {"a.txt": "x"})
		self.start()
		await self.call("plugins.setEnabled", {"id": "off", "enabled": False})
		cookie = {"Cookie": self.cookie}
		reply = self.server.request("GET", "/plugins/ied.sample/grammars/x.json", headers=cookie)
		self.assertEqual((200, "application/json"), (reply.status, reply.header("Content-Type")))
		self.assertEqual(401, self.server.request("GET", "/plugins/ied.sample/grammars/x.json").status, "files need a session")
		for path in ("/plugins/ied.sample/../off/a.txt", "/plugins/ied.sample/%2e%2e/off/a.txt", "/plugins/off/a.txt", "/plugins/ied.sample/missing.json", "/plugins/ied.sample/"):
			self.assertEqual(404, self.server.request("GET", path, headers=cookie).status, path)
