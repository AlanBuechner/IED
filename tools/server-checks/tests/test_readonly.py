"""Read-only folders (docs/10 "Read-only folders"): a tool names workspace folders no one may write in
.ied/local/readonly/<writer>.json, each with a reason (Nuke's locked packages, docs/31). A document there opens read-only
with the reason and takes no lease, edit or save; fs.write, fs.rename, fs.mkdir and fs.delete in it - or of a folder
holding it - are refused with the reason; the workspace's snapshot lists the folders, and when the file changes
readonly.changed says so and each open document's doc.readOnly follows, a document that becomes read-only losing its
lease."""

import json
import os
import shutil
import tempfile

from frames import FrameChecker
from iedclient import RpcFailure
from routed import RoutedClient
from server import IedServer, ServerTestCase

LOCKED = "packages/lib/Lib@1.0.0"
REASON = "lib:Lib 1.0.0 is a locked package: develop it to change it"


class ReadOnlyFolderTests(ServerTestCase):
	def setUp(self):
		self.folder = tempfile.mkdtemp(prefix="ied-readonly-")
		for name, text in ((LOCKED + "/src/lib.c", "int lib;\n"), ("src/game.c", "int game;\n")):
			path = os.path.join(self.folder, *name.split("/"))
			os.makedirs(os.path.dirname(path), exist_ok=True)
			with open(path, "w", newline="") as f:
				f.write(text)
		self.rules([{"path": LOCKED, "reason": REASON}])
		self.server = IedServer(extra=("--workspace", self.folder))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.folder, ignore_errors=True)

	def rules(self, paths):
		path = os.path.join(self.folder, ".ied", "local", "readonly", "nuke.json")
		os.makedirs(os.path.dirname(path), exist_ok=True)
		with open(path, "w", newline="") as f:
			json.dump({"version": 1, "writer": "nuke", "paths": paths}, f)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def call(self, method, **params):
		return await self.client.request(method, dict(params, wsId="w1"))

	async def refused(self, method, params):
		with self.assertRaises(RpcFailure) as failure:
			await self.client.request(method, params)
		self.assertEqual("forbidden", failure.exception.code, method)
		return failure.exception

	async def test_a_read_only_folder_takes_no_writes_and_says_why(self):
		await self.client.sub(["ws:w1"])
		snapshot = await self.client.next_frame("ws:w1")
		self.assertEqual([{"path": LOCKED, "reason": REASON}], snapshot["data"]["readOnly"])
		opened = await self.call("doc.open", path=LOCKED + "/src/lib.c")
		self.assertEqual((True, None, REASON), (opened["readOnly"], opened["holder"], opened.get("readOnlyReason")))
		doc = opened["docId"]
		edit = {"docId": doc, "baseVersion": 1, "changes": [{"range": {"start": {"line": 0, "col": 0}, "end": {"line": 0, "col": 0}}, "text": "x"}]}
		for method, params in (("doc.takeLease", {"docId": doc}), ("doc.edit", edit), ("doc.save", {"docId": doc})):
			self.assertIn(REASON, str(await self.refused(method, params)))
		w1 = {"wsId": "w1"}
		for method, params in (("fs.write", {"path": LOCKED + "/src/new.c", "text": "x"}), ("fs.write", {"path": LOCKED + "/src/lib.c", "text": "x"}),
				("fs.mkdir", {"path": LOCKED + "/more"}), ("fs.rename", {"from": LOCKED + "/src/lib.c", "to": "src/moved.c"}),
				("fs.rename", {"from": "src/game.c", "to": LOCKED + "/src/game.c"}), ("fs.delete", {"path": LOCKED + "/src/lib.c"}),
				("fs.delete", {"path": "packages", "recursive": True}), ("fs.rename", {"from": "packages", "to": "elsewhere"})):
			self.assertIn(REASON, str(await self.refused(method, dict(params, **w1))), method + " " + json.dumps(params))
		with open(os.path.join(self.folder, *LOCKED.split("/"), "src", "lib.c"), newline="") as f:
			self.assertEqual("int lib;\n", f.read(), "nothing reached the file")
		game = await self.call("doc.open", path="src/game.c")
		self.assertEqual((False, None), (game["readOnly"], game.get("readOnlyReason")), "a file elsewhere is writable")
		self.assertEqual("src/other.c", (await self.call("fs.write", path="src/other.c", text="int other;\n"))["path"])

	async def test_documents_follow_when_the_folders_change(self):
		await self.client.sub(["ws:w1"])
		await self.client.next_frame("ws:w1")
		doc = (await self.call("doc.open", path=LOCKED + "/src/lib.c"))["docId"]
		await self.client.sub(["doc:" + doc])
		await self.client.next_frame("doc:" + doc)
		self.rules([])
		changed = await self.client.wait_for("ws:w1", lambda f: f["t"] == "evt" and f["kind"] == "readonly.changed")
		self.assertEqual([], changed[-1]["data"]["readOnly"])
		flipped = await self.client.wait_for("doc:" + doc, lambda f: f["t"] == "evt" and f["kind"] == "doc.readOnly")
		self.assertEqual({"docId": doc, "readOnly": False, "reason": ""}, flipped[-1]["data"])
		await self.client.request("doc.takeLease", {"docId": doc})
		await self.client.request("doc.edit", {"docId": doc, "baseVersion": 1, "changes": [{"range": {"start": {"line": 0, "col": 0}, "end": {"line": 0, "col": 0}}, "text": "// "}]})
		self.rules([{"path": "packages/lib", "reason": "lib is behind its remote: pull it to edit it"}])
		flipped = await self.client.wait_for("doc:" + doc, lambda f: f["t"] == "evt" and f["kind"] == "doc.readOnly")
		self.assertEqual({"docId": doc, "readOnly": True, "reason": "lib is behind its remote: pull it to edit it"}, flipped[-1]["data"])
		lease = [f for f in flipped if f["t"] == "evt" and f["kind"] == "doc.lease"]
		self.assertEqual(None, lease[-1]["data"]["holder"], "the lease went first")
		await self.refused("doc.save", {"docId": doc})
