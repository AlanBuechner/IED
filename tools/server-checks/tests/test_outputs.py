"""Output sources (docs/10 "out:<wsId>:<sourceId>"): every workspace has its tasks' sources and the "ied" source, the server's own log, with
what it logged before the workspace opened; problems the server finds (a broken solution) arrive as append events; a
source can be cleared."""

import json
import os
import shutil
import tempfile

from frames import FrameChecker
from iedclient import Client
from server import REPO, ControlPipe, IedServer, ServerTestCase

TOPIC = "out:w1:ied"


class StartupLogTests(ServerTestCase):
	"""Lines logged while the server starts reach the log's output source on its own thread while the workspaces
	open: an install without the AI prompts logs at every start. A race there corrupted the heap about once in a
	hundred starts."""

	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-startlog-")
		self.home = os.path.join(self.root, "home")
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		plugin = os.path.join(self.home, "plugins", "fake")
		os.makedirs(plugin)
		with open(os.path.join(plugin, "ied-plugin.json"), "w", encoding="utf-8") as f:
			json.dump({"manifestVersion": 1, "id": "fake", "name": "Fake", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
				"contributes": {"languages": [{"id": "fake", "extensions": [".fk"]}], "settings": {"schema": "settings.schema.json"}}}, f)
		with open(os.path.join(plugin, "settings.schema.json"), "w", encoding="utf-8") as f:
			json.dump({"type": "object", "properties": {"fake.flags": {"type": "array", "items": {"type": "string"}, "default": []}}}, f)
		os.makedirs(os.path.join(self.root, "work"))
		with open(os.path.join(self.root, "work", "a.fk"), "w", encoding="utf-8") as f:
			f.write("a\n")

	def tearDown(self):
		shutil.rmtree(self.root, ignore_errors=True)

	def test_starts_that_log_while_workspaces_open(self):
		for n in range(300):
			server = IedServer(data=os.path.join(self.root, "data%d" % n), extra=("--home", self.home, "--workspace", os.path.join(self.root, "work")))
			try:
				self.assertTrue(server.started, "start %d: %s" % (n, server.error))
			finally:
				server.stop()


class OutputTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-outputs-")
		self.server = IedServer()
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

	async def test_the_server_log_is_an_output_source(self):
		sources = (await self.client.request("out.sources", {"wsId": "w1"}))["sources"]
		self.assertEqual([("build", "Build"), ("run", "Run"), ("test", "Tests"), ("debug", "Debug"), ("ai", "AI"), ("git", "Git"), ("gpu", "GPU"), ("ied", "IED")], [(x["sourceId"], x["label"]) for x in sources], "the tasks' sources and the server's log")
		self.assertEqual(TOPIC, sources[-1]["topic"])
		await self.client.sub([TOPIC])
		snapshot = (await self.client.next_frame(TOPIC))["data"]
		self.assertEqual(("ied", "IED"), (snapshot["sourceId"], snapshot["label"]))
		self.assertRegex(snapshot["tail"], r"\[\d\d:\d\d:\d\d\] opened w1: ", "what was logged before, with its time")

	async def test_problems_are_logged_as_they_are_found(self):
		await self.client.sub([TOPIC])
		await self.client.next_frame(TOPIC)
		broken = os.path.join(self.root, "Broken.iedsln")
		with open(broken, "w") as f:
			json.dump({"version": 1, "name": "Broken", "projects": [{"file": "missing.iedproj"}]}, f)
		pipe = ControlPipe(self.server.control)
		try:
			pipe.call("open", {"path": broken})
		finally:
			pipe.close()
		frames = await self.client.wait_for(TOPIC, lambda f: f["t"] == "evt" and "no project file matches" in f["data"]["text"], 10)
		self.assertEqual("append", frames[-1]["kind"])
		self.assertIn("solution Broken: no project file matches missing.iedproj", frames[-1]["data"]["text"])

	async def test_clear(self):
		await self.client.sub([TOPIC])
		await self.client.next_frame(TOPIC)
		await self.client.request("out.clear", {"wsId": "w1", "sourceId": "ied"})
		frames = await self.client.wait_for(TOPIC, lambda f: f["t"] == "evt" and f["kind"] == "clear", 10)
		self.assertEqual({}, frames[-1]["data"])
		await self.client.sub([TOPIC])
		snapshot = await self.client.wait_for(TOPIC, lambda f: f["t"] == "snap", 10)
		self.assertEqual("", snapshot[-1]["data"]["tail"])
