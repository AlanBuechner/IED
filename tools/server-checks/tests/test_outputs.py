"""Output sources (docs/10 "out:<wsId>:<sourceId>"): every workspace has the "ied" source, the server's own log, with
what it logged before the workspace opened; problems the server finds (a broken solution) arrive as append events; a
source can be cleared."""

import json
import os
import shutil
import tempfile

from frames import FrameChecker
from iedclient import Client
from server import ControlPipe, IedServer, ServerTestCase

TOPIC = "out:w1:ied"


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
		self.assertEqual([{"sourceId": "ied", "label": "IED", "topic": TOPIC}], sources)
		await self.client.sub([TOPIC])
		snapshot = (await self.client.next_frame(TOPIC))["data"]
		self.assertEqual(("ied", "IED"), (snapshot["sourceId"], snapshot["label"]))
		self.assertRegex(snapshot["tail"], r"\[\d\d:\d\d:\d\d\] opened w1: ", "what was logged before, with its time")

	async def test_problems_are_logged_as_they_are_found(self):
		await self.client.sub([TOPIC])
		await self.client.next_frame(TOPIC)
		broken = os.path.join(self.root, "Broken.ied.sln")
		with open(broken, "w") as f:
			json.dump({"version": 1, "name": "Broken", "projects": [{"file": "missing.ied.proj"}]}, f)
		pipe = ControlPipe(self.server.control)
		try:
			pipe.call("open", {"path": broken})
		finally:
			pipe.close()
		frames = await self.client.wait_for(TOPIC, lambda f: f["t"] == "evt" and "no project file matches" in f["data"]["text"], 10)
		self.assertEqual("append", frames[-1]["kind"])
		self.assertIn("solution Broken: no project file matches missing.ied.proj", frames[-1]["data"]["text"])

	async def test_clear(self):
		await self.client.sub([TOPIC])
		await self.client.next_frame(TOPIC)
		await self.client.request("out.clear", {"wsId": "w1", "sourceId": "ied"})
		frames = await self.client.wait_for(TOPIC, lambda f: f["t"] == "evt" and f["kind"] == "clear", 10)
		self.assertEqual({}, frames[-1]["data"])
		await self.client.sub([TOPIC])
		snapshot = await self.client.wait_for(TOPIC, lambda f: f["t"] == "snap", 10)
		self.assertEqual("", snapshot[-1]["data"]["tail"])
