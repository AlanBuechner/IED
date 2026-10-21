"""Settings tiers (docs/07 'Settings on the wire'): declared by plugins, read from the app and solution tier files,
merged per key, checked with problems on their lines, live when files change, written with comments kept and
revision-checked, and gone with the plugin that declared them."""

import asyncio
import json
import os
import shutil
import tempfile

from frames import FrameChecker
from iedclient import Client, RpcFailure
from server import REPO, IedServer, ServerTestCase

SCHEMA = {
	"properties": {
		"sample.tabSize": {"type": "integer", "minimum": 1, "maximum": 16, "default": 4},
		"sample.theme": {"enum": ["dark", "darker"], "default": "dark", "x-ied-tiers": ["app"]},
		"sample.colors": {"type": "object", "default": {"a": 1, "b": {"x": 1}}, "x-ied-merge": "merge"},
		"sample.paths": {"type": "array", "items": {"type": "string"}, "default": ["base"], "x-ied-merge": "append"},
	}
}
APP_FILE = '{\n\t// my own tab width\n\t"sample.tabSize": 8 // wide\n}\n'


class SettingsTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-settings-")
		self.home = os.path.join(self.root, "home")
		self.app = os.path.join(self.root, "app")
		self.folder = os.path.join(self.root, "ws")
		os.makedirs(self.folder)
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		plugin = os.path.join(self.home, "plugins", "sample")
		os.makedirs(plugin)
		manifest = {"manifestVersion": 1, "id": "sample", "name": "Sample", "version": "1.0.0", "ied": {"api": "^1"},
			"runtime": {"kind": "declarative"}, "contributes": {"settings": {"schema": "settings.schema.json", "page": "Sample"}}}
		with open(os.path.join(plugin, "ied-plugin.json"), "w") as f:
			json.dump(manifest, f)
		with open(os.path.join(plugin, "settings.schema.json"), "w") as f:
			json.dump(SCHEMA, f)
		os.makedirs(self.app)
		with open(os.path.join(self.app, "settings.json"), "w", newline="") as f:
			f.write(APP_FILE)
		self.server = IedServer(extra=("--home", self.home, "--app-data", self.app, "--workspace", self.folder))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def values(self, ws_id="w1"):
		state = await self.client.request("settings.get", {"wsId": ws_id} if ws_id else {})
		return {k: (v["value"], v["source"]) for k, v in state["values"].items()}, state

	async def until(self, topic, predicate, timeout=10):
		"""The first settings.changed on `topic` whose snapshot satisfies `predicate`."""
		while True:
			frame = await self.client.next_frame(topic, timeout)
			if frame["t"] in ("evt", "snap") and predicate(frame["data"]):
				return frame["data"]

	async def test_schema_defaults_and_tiers(self):
		schema = (await self.client.request("settings.schema"))["settings"]
		self.assertEqual(["sample.tabSize", "sample.theme", "sample.colors", "sample.paths"], list(schema))
		self.assertEqual((["app"], "replace", "Sample", "sample"), (schema["sample.theme"]["tiers"], schema["sample.theme"]["merge"], schema["sample.theme"]["page"], schema["sample.theme"]["plugin"]))
		values, state = await self.values()
		self.assertEqual((8, "app"), values["sample.tabSize"], "the app file's value")
		self.assertEqual(("dark", "default"), values["sample.theme"])
		self.assertEqual([], state["tiers"]["app"]["problems"])

	async def test_set_keeps_comments_and_layers_tiers(self):
		await self.client.sub(["settings:w1", "settings:app"])
		app = await self.client.request("settings.set", {"tier": "app", "key": "sample.tabSize", "value": 2})
		with open(os.path.join(self.app, "settings.json")) as f:
			self.assertEqual(APP_FILE.replace('": 8', '": 2'), f.read(), "comments and layout kept")
		await self.until("settings:w1", lambda d: d["values"]["sample.tabSize"]["value"] == 2)
		await self.client.request("settings.set", {"tier": "solution", "wsId": "w1", "key": "sample.tabSize", "value": 3})
		values, state = await self.values()
		self.assertEqual((3, "solution"), values["sample.tabSize"], "the solution tier wins")
		self.assertEqual((2, "app"), (await self.values(None))[0]["sample.tabSize"], "the app view is untouched")
		self.assertTrue(os.path.exists(os.path.join(self.folder, ".ied", "solution.json")))
		await self.client.request("settings.set", {"tier": "solution", "wsId": "w1", "key": "sample.colors", "value": {"b": {"y": 2}, "c": 3}})
		await self.client.request("settings.set", {"tier": "solution", "wsId": "w1", "key": "sample.paths", "value": ["mine"]})
		values, _ = await self.values()
		self.assertEqual({"a": 1, "b": {"x": 1, "y": 2}, "c": 3}, values["sample.colors"][0], "merged member by member")
		self.assertEqual(["base", "mine"], values["sample.paths"][0], "appended")
		reset = await self.client.request("settings.reset", {"tier": "solution", "wsId": "w1", "key": "sample.tabSize"})
		self.assertGreater(reset["revision"], 1)
		self.assertEqual((2, "app"), (await self.values())[0]["sample.tabSize"], "reset falls back to the app tier")
		self.assertGreater(app["revision"], 1)

	async def test_refusals(self):
		cases = [
			({"tier": "app", "key": "nope.key", "value": 1}, "bad_request"),
			({"tier": "solution", "wsId": "w1", "key": "sample.theme", "value": "darker"}, "bad_request"),
			({"tier": "app", "key": "sample.tabSize", "value": 99}, "bad_request"),
			({"tier": "app", "key": "sample.tabSize", "value": 4, "expectedRevision": 0}, "conflict"),
			({"tier": "bogus", "key": "sample.tabSize", "value": 4}, "bad_request"),
		]
		for params, code in cases:
			with self.assertRaises(RpcFailure) as refused:
				await self.client.request("settings.set", params)
			self.assertEqual(code, refused.exception.code, params)
		with self.assertRaises(RpcFailure) as bad:
			await self.client.request("settings.set", {"tier": "app", "key": "sample.tabSize", "value": 99})
		self.assertIn("maximum", bad.exception.data["problems"][0])

	async def test_hand_edits_are_live_and_checked(self):
		await self.client.sub(["settings:app"])
		await self.client.next_frame("settings:app")
		with open(os.path.join(self.app, "settings.json"), "w") as f:
			f.write('{\n\t"sample.tabSize": 99,\n\t"sample.theme": "darker",\n\t"other.key": 1\n}\n')
		state = await self.until("settings:app", lambda d: d["values"]["sample.theme"]["value"] == "darker")
		problems = {(p["line"], p.get("key")): p["message"] for p in state["tiers"]["app"]["problems"]}
		self.assertIn("maximum", problems[(2, "sample.tabSize")])
		self.assertEqual("unknown setting", problems[(4, "other.key")])
		self.assertEqual(8, state["values"]["sample.tabSize"]["value"], "the last valid value stays in effect")
		with open(os.path.join(self.folder, ".ied", "solution.json") if os.path.isdir(os.path.join(self.folder, ".ied")) else self.make_ied(), "w") as f:
			f.write('{ "sample.theme": "darker", "sample.tabSize": 5 }')
		await self.client.sub(["settings:w1"])
		state = await self.until("settings:w1", lambda d: d["values"]["sample.tabSize"]["value"] == 5)
		self.assertIn("cannot be set in the solution tier", state["tiers"]["solution"]["problems"][0]["message"])

	def make_ied(self):
		os.makedirs(os.path.join(self.folder, ".ied"))
		return os.path.join(self.folder, ".ied", "solution.json")

	async def test_settings_go_with_their_plugin(self):
		await self.client.request("plugins.setEnabled", {"id": "sample", "enabled": False})
		self.assertEqual({}, (await self.client.request("settings.schema"))["settings"])
		values, state = await self.values()
		self.assertEqual({}, values)
		self.assertEqual("unknown setting", state["tiers"]["app"]["problems"][0]["message"])
		await self.client.request("plugins.setEnabled", {"id": "sample", "enabled": True})
		self.assertEqual((8, "app"), (await self.values())[0]["sample.tabSize"])

	async def test_app_plugins_come_and_go_without_a_restart(self):
		folder = os.path.join(self.app, "plugins", "late")
		os.makedirs(folder)
		with open(os.path.join(folder, "ied-plugin.json"), "w") as f:
			json.dump({"manifestVersion": 1, "id": "late", "name": "Late", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"}}, f)
		for _ in range(50):
			plugins = {p["id"]: p["state"] for p in (await self.client.request("plugins.list"))["plugins"]}
			if "late" in plugins:
				break
			await asyncio.sleep(0.1)
		self.assertEqual("needsApproval", plugins.get("late"))
