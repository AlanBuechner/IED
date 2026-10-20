"""File watching (docs/10 tree.changed, doc.diskChanged): changes made behind the server's back reach clients and open
documents; the server's own writes are not reported twice and do not move revisions."""

import asyncio
import os
import shutil
import tempfile

from frames import FrameChecker
from iedclient import Client, RpcFailure
from server import IedServer, ServerTestCase

TREE = "ws:w1"
SETTLE_S = 0.6   # longer than the server's quiet spell, so a batch has gone out


def tree_changes(frames):
	return [(c["path"], c["change"]) for f in frames if f["t"] == "evt" and f["kind"] == "tree.changed" for c in f["data"]["changes"]]


class WatchingTests(ServerTestCase):
	def setUp(self):
		self.folder = tempfile.mkdtemp(prefix="ied-watch-")
		with open(os.path.join(self.folder, "known.txt"), "w", newline="") as f:
			f.write("one\n")
		self.server = IedServer(extra=("--workspace", self.folder))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.folder, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		await self.client.sub([TREE])
		await self.client.next_frame(TREE)   # the snapshot

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	def write(self, name, text):
		path = os.path.join(self.folder, *name.split("/"))
		os.makedirs(os.path.dirname(path), exist_ok=True)
		with open(path, "w", newline="") as f:
			f.write(text)

	async def changes_until(self, expected):
		"""Tree changes until every (path, change) in `expected` has come, then a quiet spell for stragglers."""
		seen = []
		while not all(e in seen for e in expected):
			seen += tree_changes([await self.client.next_frame(TREE, 10)])
		await asyncio.sleep(SETTLE_S)
		return seen + tree_changes(self.client.drain(TREE))

	async def test_created_changed_deleted_and_renamed(self):
		self.write("new.txt", "fresh\n")
		self.assertIn(("new.txt", "created"), await self.changes_until([("new.txt", "created")]))
		self.write("known.txt", "two\n")
		self.assertIn(("known.txt", "changed"), await self.changes_until([("known.txt", "changed")]))
		os.remove(os.path.join(self.folder, "new.txt"))
		self.assertIn(("new.txt", "deleted"), await self.changes_until([("new.txt", "deleted")]))
		os.rename(os.path.join(self.folder, "known.txt"), os.path.join(self.folder, "moved.txt"))
		seen = await self.changes_until([("known.txt", "deleted"), ("moved.txt", "created")])
		self.assertEqual(sorted(set(seen)), [("known.txt", "deleted"), ("moved.txt", "created")])

	async def test_an_outside_write_moves_the_revision(self):
		read = await self.client.request("fs.read", {"wsId": "w1", "path": "known.txt"})
		self.write("known.txt", "edited outside\n")
		await self.changes_until([("known.txt", "changed")])
		again = await self.client.request("fs.read", {"wsId": "w1", "path": "known.txt"})
		self.assertEqual("edited outside\n", again["text"])
		self.assertGreater(again["revision"], read["revision"])
		with self.assertRaises(RpcFailure) as stale:
			await self.client.request("fs.write", {"wsId": "w1", "path": "known.txt", "text": "mine", "expectedRevision": read["revision"]})
		self.assertEqual("conflict", stale.exception.code)

	async def test_the_servers_own_changes_are_reported_once(self):
		written = await self.client.request("fs.write", {"wsId": "w1", "path": "own/a.txt", "text": "a"})
		await self.client.request("fs.mkdir", {"wsId": "w1", "path": "own/dir"})
		await self.client.request("fs.rename", {"wsId": "w1", "from": "own/a.txt", "to": "own/b.txt"})
		await self.client.request("fs.delete", {"wsId": "w1", "path": "own/b.txt"})
		await asyncio.sleep(SETTLE_S * 2)
		seen = tree_changes(self.client.drain(TREE))
		self.assertEqual([("own/a.txt", "created"), ("own/dir", "created"), ("own/a.txt", "deleted"), ("own/b.txt", "created"), ("own/b.txt", "deleted")],
			[c for c in seen if c[0] != "own"], "each change once, as the methods reported it")
		self.assertEqual(1, written["revision"])

	async def test_an_open_clean_document_reloads(self):
		opened = await self.client.request("doc.open", {"wsId": "w1", "path": "known.txt"})
		topic = "doc:" + opened["docId"]
		await self.client.sub([topic])
		await self.client.next_frame(topic)
		self.write("known.txt", "one\nreloaded\n")
		frames = await self.client.wait_for(topic, lambda f: f["t"] == "evt" and f["kind"] == "doc.edit", 10)
		edit = frames[-1]["data"]
		self.assertEqual("disk", edit["origin"])
		self.assertEqual("reloaded\n", "".join(c["text"] for c in edit["changes"]).replace("one\n", ""))

	async def test_an_open_dirty_document_is_told(self):
		opened = await self.client.request("doc.open", {"wsId": "w1", "path": "known.txt"})
		topic = "doc:" + opened["docId"]
		await self.client.sub([topic])
		await self.client.next_frame(topic)
		await self.client.request("doc.edit", {"docId": opened["docId"], "baseVersion": opened["version"], "changes": [
			{"range": {"start": {"line": 0, "col": 0}, "end": {"line": 0, "col": 0}}, "text": "dirty "}]})
		self.write("known.txt", "outside\n")
		frames = await self.client.wait_for(topic, lambda f: f["t"] == "evt" and f["kind"] == "doc.diskChanged", 10)
		self.assertGreater(frames[-1]["data"]["revision"], 1)
		os.remove(os.path.join(self.folder, "known.txt"))
		frames = await self.client.wait_for(topic, lambda f: f["t"] == "evt" and f["kind"] == "doc.diskChanged", 10)
		self.assertEqual(0, frames[-1]["data"]["revision"], "revision 0: the file is gone")

	async def test_a_repositorys_own_folder_is_not_reported(self):
		self.write(".git/objects/ab/cdef", "blob")
		self.write(".git/HEAD", "ref: refs/heads/main\n")
		self.write("after.txt", "x")
		seen = await self.changes_until([("after.txt", "created")])
		self.assertEqual([], [c for c in seen if c[0] == ".git" or c[0].startswith(".git/")])

	async def test_a_burst_is_gathered(self):
		for i in range(300):
			self.write("burst/f%03d.txt" % i, "x")
		expected = [("burst/f%03d.txt" % i, "created") for i in range(300)]
		seen = []
		events = 0
		while not all(e in seen for e in expected):
			frame = await self.client.next_frame(TREE, 15)
			events += 1
			seen += tree_changes([frame])
		self.assertLess(events, 30, "300 files arrive in a few batches, not one event each")

	async def test_created_then_deleted_within_a_batch_is_nothing(self):
		path = os.path.join(self.folder, "blink.txt")
		with open(path, "w") as f:
			f.write("x")
		os.remove(path)
		self.write("marker.txt", "x")
		seen = await self.changes_until([("marker.txt", "created")])
		self.assertNotIn(("blink.txt", "deleted"), seen)
