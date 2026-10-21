"""Recent and favorite projects (docs/10 "Workspaces"): every opened workspace goes first in the server's recent list,
kept in its data folder across restarts; the owner's favorites stay; a client opens again only a listed project, never
any path; and the server topic carries the list and its changes."""

import os
import shutil
import tempfile

from frames import FrameChecker
from iedclient import Client, RpcFailure
from server import IedServer, ServerTestCase


class RecentTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-recent-")
		self.first = os.path.join(self.root, "First")
		self.second = os.path.join(self.root, "Second")
		for folder in (self.first, self.second):
			os.makedirs(folder)
		self.server = None

	def tearDown(self):
		if self.server:
			self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def connect(self, *workspaces, data=None):
		if self.server:
			self.server.stop(remove_data=False)
		extra = []
		for folder in workspaces:
			extra += ["--workspace", folder]
		self.server = IedServer(data=data, extra=tuple(extra))
		self.assertTrue(self.server.started, self.server.error)
		client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		await client.sub(["server"])
		return client

	async def open_roots(self, client):
		return [self.path(w["root"]) for w in (await client.request("ws.list"))["workspaces"]]

	async def snapshot(self, client):
		frames = await client.wait_for("server", lambda f: f["t"] == "snap")
		return frames[-1]["data"]

	def paths(self, recent):
		return [os.path.normcase(os.path.normpath(p["path"])) for p in recent]

	def path(self, folder):
		return os.path.normcase(os.path.normpath(folder))

	async def test_opened_workspaces_are_listed_newest_first_and_kept(self):
		client = await self.connect(self.first, self.second)
		recent = (await self.snapshot(client))["recent"]
		self.assertEqual([self.path(self.second), self.path(self.first)], self.paths(recent), "both, the last opened first")
		self.assertTrue(all(p["exists"] and p["kind"] == "folder" and not p["favorite"] for p in recent))
		self.assertEqual({"First", "Second"}, {p["name"] for p in recent})
		first_path = next(p["path"] for p in recent if p["name"] == "First")
		self.assertEqual({}, await client.request("ws.setFavorite", {"path": first_path, "favorite": True}))
		changed = await client.wait_for("server", lambda f: f.get("kind") == "recent.changed")
		self.assertTrue(next(p for p in changed[-1]["data"]["recent"] if p["name"] == "First")["favorite"], "recent.changed carries the list")
		client.abort()
		data = self.server.data
		client = await self.connect(data=data)
		recent = (await self.snapshot(client))["recent"]
		self.assertEqual([self.path(self.second), self.path(self.first)], self.paths(recent), "kept across a restart")
		self.assertTrue(next(p for p in recent if p["name"] == "First")["favorite"], "and the favorite")
		self.assertNotIn(self.path(self.first), await self.open_roots(client), "listed, not open")
		opened = await client.request("ws.openRecent", {"path": first_path})
		self.assertEqual(self.path(self.first), self.path(opened["workspace"]["root"]), "opened again from the list")
		self.assertIn(self.path(self.first), await self.open_roots(client))
		frames = await client.wait_for("server", lambda f: f.get("kind") == "recent.changed")
		self.assertEqual(self.path(self.first), self.paths(frames[-1]["data"]["recent"])[0], "and first again")
		client.abort()

	async def test_only_listed_projects_open_and_a_gone_one_says_so(self):
		client = await self.connect(self.first, self.second)
		recent = (await self.snapshot(client))["recent"]
		with self.assertRaises(RpcFailure) as never:
			await client.request("ws.openRecent", {"path": self.root})
		self.assertEqual("not_found", never.exception.code, "a path the owner never opened")
		second_path = next(p["path"] for p in recent if p["name"] == "Second")
		client.abort()
		data = self.server.data
		shutil.rmtree(self.second)
		client = await self.connect(data=data)
		recent = (await self.snapshot(client))["recent"]
		self.assertFalse(next(p for p in recent if p["name"] == "Second")["exists"], "a folder that went is marked")
		with self.assertRaises(RpcFailure) as gone:
			await client.request("ws.openRecent", {"path": second_path})
		self.assertEqual("not_found", gone.exception.code)
		self.assertEqual({}, await client.request("ws.forgetRecent", {"path": second_path}))
		frames = await client.wait_for("server", lambda f: f.get("kind") == "recent.changed")
		self.assertEqual([self.path(self.first)], self.paths(frames[-1]["data"]["recent"]), "forgotten")
		with self.assertRaises(RpcFailure):
			await client.request("ws.setFavorite", {"path": second_path, "favorite": True})
		client.abort()
