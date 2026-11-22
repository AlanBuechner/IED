"""Workspace trust (docs/11 'Trust layers'): a workspace opens untrusted, the owner's choice is kept in app data - never
in the repository - across restarts, and every change goes to the security log; on Linux what an older build kept under
a case-folded key (trust.json version 1, a folder's state) is found again."""

import hashlib
import json
import os
import shutil
import tempfile
import unittest

from frames import FrameChecker
from iedclient import Client
from routed import RoutedClient
from server import IedServer, ServerTestCase, read_lines


class TrustTests(ServerTestCase):
	def setUp(self):
		self.folder = tempfile.mkdtemp(prefix="ied-trust-")
		self.server = None

	def tearDown(self):
		if self.server:
			self.server.stop()
		shutil.rmtree(self.folder, ignore_errors=True)

	async def workspace(self, data=None):
		if self.server:
			self.server.stop(remove_data=False)
		self.server = IedServer(data=data, extra=("--workspace", self.folder))
		self.assertTrue(self.server.started, self.server.error)
		client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		workspaces = (await client.request("ws.list"))["workspaces"]
		return client, workspaces[0]

	async def test_untrusted_until_trusted_and_kept(self):
		client, ws = await self.workspace()
		self.assertFalse(ws["trusted"], "a folder opens untrusted")
		self.assertEqual({"trusted": True}, await client.request("ws.setTrust", {"wsId": ws["wsId"], "trusted": True}))
		client.abort()
		data = self.server.data
		self.assertEqual(["skills"], os.listdir(os.path.join(self.folder, ".ied")), "the choice is not written into the repository: its .ied/ has the skills folder alone")
		client, ws = await self.workspace(data)
		self.assertTrue(ws["trusted"], "the choice survives a restart")
		await client.request("ws.setTrust", {"wsId": ws["wsId"], "trusted": False})
		client.abort()
		client, ws = await self.workspace(data)
		self.assertFalse(ws["trusted"], "and so does taking it back")
		client.abort()
		events = [json.loads(line)["event"] for line in read_lines(os.path.join(data, "logs", "security.log"))]
		self.assertIn("workspace-trusted", events)
		self.assertIn("workspace-untrusted", events)

	@unittest.skipIf(os.name == "nt", "Windows' keys fold case as they always did")
	async def test_what_an_older_linux_build_kept_is_found_again(self):
		"""An older Linux build keyed a folder by its path with case folded: trust.json version 1 and the folder's state in
		the app data (src/host/Trust.cf, MoveCaseFoldedState). The folder is still trusted, and its state moves under the
		key of its path as spelled."""
		root = os.path.realpath(os.path.join(self.folder, "Game"))
		os.makedirs(root)
		data = tempfile.mkdtemp(prefix="ied-trust-data-")
		try:
			app = os.path.join(data, "app")
			state = lambda path: os.path.join(app, "workspaces", hashlib.sha256(path.encode("utf-8")).hexdigest()[:16])
			os.makedirs(state(root.lower()))
			with open(os.path.join(state(root.lower()), "selections.json"), "w") as f:
				f.write("{}")
			with open(os.path.join(app, "trust.json"), "w") as f:
				json.dump({"version": 1, "trusted": [root.lower()]}, f)
			self.server = IedServer(data=data, extra=("--workspace", root))
			self.assertTrue(self.server.started, self.server.error)
			client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
			ws = (await client.request("ws.list"))["workspaces"][0]
			self.assertTrue(ws["trusted"], "the folder the older key trusted")
			await client.request("fs.list", {"wsId": ws["wsId"], "path": ""})
			self.assertTrue(os.path.isfile(os.path.join(state(root), "selections.json")), "its state under its own key")
			self.assertFalse(os.path.exists(state(root.lower())), "nothing left under the older one")
			client.abort()
		finally:
			if self.server:
				self.server.stop()
				self.server = None
			shutil.rmtree(data, ignore_errors=True)
