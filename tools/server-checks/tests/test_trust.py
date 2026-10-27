"""Workspace trust (docs/11 'Trust layers'): a workspace opens untrusted, the owner's choice is kept in app data - never
in the repository - across restarts, and every change goes to the security log."""

import json
import os
import shutil
import tempfile

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
		self.assertFalse(os.path.exists(os.path.join(self.folder, ".ied")), "nothing is written into the repository")
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
