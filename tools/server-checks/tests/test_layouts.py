"""Layouts on disk (docs/10 "layout:<wsId>:<deviceClass>"): a folder's in the app data, keyed by the folder, so opening a
folder writes nothing into it; a solution's in its .ied/local/layout/; both kept across a restart; a file that is not a
layout is ignored with a log line."""

import json
import os
import shutil
import tempfile

from frames import FrameChecker
from iedclient import Client
from server import IedServer, ServerTestCase, local_state

DOCUMENT = {"v": 1, "activity": "ied.editor", "activities": {"ied.editor": {"kind": "tabs", "tabs": ["ied.output"], "active": "ied.output", "well": True}}}


class LayoutFileTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-layouts-")
		self.folder = os.path.join(self.root, "Notes")
		os.makedirs(self.folder)
		self.data = os.path.join(self.root, "data")
		os.makedirs(self.data)
		self.server = None

	def tearDown(self):
		if self.server:
			self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	def start(self, workspace):
		if self.server:
			self.server.stop()
		self.server = IedServer(data=self.data, extra=("--workspace", workspace))
		self.assertTrue(self.server.started, self.server.error)

	async def connect(self):
		client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		self.addCleanup(client.abort)
		return client

	async def stored(self, client, device_class):
		topic = "layout:w1:" + device_class
		await client.sub([topic])
		return (await client.wait_for(topic, lambda f: f["t"] == "snap"))[-1]["data"]

	async def store(self, client, device_class, layout):
		revision = (await self.stored(client, device_class))["revision"]
		return (await client.request("layout.set", {"wsId": "w1", "deviceClass": device_class, "layout": layout, "expectedRevision": revision}))["revision"]

	def folder_layouts(self, folder):
		return os.path.join(local_state(self.data, folder), "layout")

	async def test_a_folder_keeps_its_layouts_in_the_app_data(self):
		self.start(self.folder)
		client = await self.connect()
		for device_class in ("desktop", "tablet", "phone"):
			self.assertEqual({"revision": 0, "layout": None}, await self.stored(client, device_class), "nothing stored yet")
		self.assertEqual(1, await self.store(client, "desktop", DOCUMENT))
		with open(os.path.join(self.folder_layouts(self.folder), "desktop.json"), encoding="utf-8") as f:
			self.assertEqual({"revision": 1, "layout": DOCUMENT}, json.load(f))
		self.assertEqual([], os.listdir(self.folder), "opening a folder writes nothing into it")
		self.start(self.folder)
		client = await self.connect()
		self.assertEqual({"revision": 1, "layout": DOCUMENT}, await self.stored(client, "desktop"), "kept across a restart")
		self.assertEqual({"revision": 0, "layout": None}, await self.stored(client, "phone"))

	async def test_a_solution_keeps_its_layouts_in_its_local_folder(self):
		solution = os.path.join(self.folder, "Notes.iedsln")
		with open(solution, "w", encoding="utf-8") as f:
			json.dump({"version": 1, "name": "Notes", "projects": []}, f)
		self.start(solution)
		client = await self.connect()
		await self.store(client, "phone", DOCUMENT)
		with open(os.path.join(self.folder, ".ied", "local", "layout", "phone.json"), encoding="utf-8") as f:
			self.assertEqual({"revision": 1, "layout": DOCUMENT}, json.load(f))
		self.assertFalse(os.path.exists(os.path.join(self.data, "app", "workspaces")), "nothing in the app data")

	async def test_a_file_that_is_not_a_layout_is_ignored(self):
		layouts = self.folder_layouts(self.folder)
		os.makedirs(layouts)
		with open(os.path.join(layouts, "desktop.json"), "w", encoding="utf-8") as f:
			f.write("{\"revision\": \"three\"")
		self.start(self.folder)
		client = await self.connect()
		self.assertEqual({"revision": 0, "layout": None}, await self.stored(client, "desktop"))
		await client.sub(["out:w1:ied"])
		tail = (await client.wait_for("out:w1:ied", lambda f: f["t"] == "snap"))[-1]["data"]["tail"]
		self.assertIn("not a layout file", tail)
		self.assertEqual(1, await self.store(client, "desktop", DOCUMENT), "a layout can be stored over it")
