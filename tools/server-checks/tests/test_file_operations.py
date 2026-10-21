"""File operations on folders and open documents (docs/10 "Files"): fs.rename moves a folder with its files' revisions and
the documents open in it, which keep their docId, text and version and get doc.renamed; fs.delete takes a folder only
with recursive, documents open in it are told, and what it deletes goes to the trash, which lists, restores and purges."""

import os
import shutil
import tempfile

from frames import FrameChecker
from iedclient import Client, RpcFailure
from server import IedServer, ServerTestCase


class FileOperationTests(ServerTestCase):
	def setUp(self):
		self.folder = tempfile.mkdtemp(prefix="ied-files-")
		for name, text in (("src/a.c", "int a;\n"), ("src/deep/b.c", "int b;\n"), ("top.txt", "top\n")):
			path = os.path.join(self.folder, *name.split("/"))
			os.makedirs(os.path.dirname(path), exist_ok=True)
			with open(path, "w", newline="") as f:
				f.write(text)
		self.server = IedServer(extra=("--workspace", self.folder))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.folder, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def call(self, method, **params):
		return await self.client.request(method, dict(params, wsId="w1"))

	async def open(self, path):
		doc = (await self.call("doc.open", path=path))["docId"]
		await self.client.sub(["doc:" + doc])
		await self.client.next_frame("doc:" + doc)
		return doc

	async def test_a_renamed_folder_takes_its_files_and_open_documents(self):
		doc = await self.open("src/deep/b.c")
		await self.client.request("doc.edit", {"docId": doc, "baseVersion": 1, "changes": [{"range": {"start": {"line": 1, "col": 0}, "end": {"line": 1, "col": 0}}, "text": "int c;"}]})
		revision = (await self.call("fs.read", path="src/a.c"))["revision"]
		self.assertEqual({"path": "lib"}, await self.call("fs.rename", **{"from": "src", "to": "lib"}))
		renamed = await self.client.wait_for("doc:" + doc, lambda f: f["t"] == "evt" and f["kind"] == "doc.renamed")
		self.assertEqual({"docId": doc, "path": "lib/deep/b.c"}, renamed[-1]["data"])
		self.assertEqual(revision, (await self.call("fs.read", path="lib/a.c"))["revision"], "the revision moved with the file")
		self.assertTrue(os.path.isfile(os.path.join(self.folder, "lib", "deep", "b.c")))
		self.assertEqual(doc, (await self.call("doc.open", path="lib/deep/b.c"))["docId"], "the same document under its new path")
		await self.client.request("doc.save", {"docId": doc})
		with open(os.path.join(self.folder, "lib", "deep", "b.c"), newline="") as f:
			self.assertEqual("int b;\nint c;", f.read(), "saved where the file is now, with the edit")
		self.assertFalse(os.path.exists(os.path.join(self.folder, "src")))
		with self.assertRaises(RpcFailure) as refused:
			await self.call("fs.rename", **{"from": "lib", "to": "lib/inner"})
		self.assertEqual("bad_request", refused.exception.code)
		with self.assertRaises(RpcFailure) as taken:
			await self.call("fs.rename", **{"from": "lib", "to": "top.txt"})
		self.assertEqual("conflict", taken.exception.code)

	async def test_a_renamed_file_takes_its_document(self):
		doc = await self.open("top.txt")
		await self.call("fs.rename", **{"from": "top.txt", "to": "docs/top.md"})
		renamed = await self.client.wait_for("doc:" + doc, lambda f: f["t"] == "evt" and f["kind"] == "doc.renamed")
		self.assertEqual("docs/top.md", renamed[-1]["data"]["path"])

	async def test_a_folder_is_deleted_only_with_recursive(self):
		doc = await self.open("src/a.c")
		with self.assertRaises(RpcFailure) as refused:
			await self.call("fs.delete", path="src")
		self.assertEqual("bad_request", refused.exception.code)
		self.assertIn("trashId", await self.call("fs.delete", path="src", recursive=True))
		self.assertFalse(os.path.exists(os.path.join(self.folder, "src")))
		told = await self.client.wait_for("doc:" + doc, lambda f: f["t"] == "evt" and f["kind"] == "doc.diskChanged")
		self.assertEqual(0, told[-1]["data"]["revision"])
		with self.assertRaises(RpcFailure) as gone:
			await self.call("fs.read", path="src/deep/b.c")
		self.assertEqual("not_found", gone.exception.code)
		with self.assertRaises(RpcFailure) as root:
			await self.call("fs.rename", **{"from": "", "to": "x"})
		self.assertEqual("bad_request", root.exception.code)

	async def test_deletes_go_to_the_trash_and_come_back(self):
		file_entry = (await self.call("fs.delete", path="top.txt"))["trashId"]
		folder_entry = (await self.call("fs.delete", path="src", recursive=True))["trashId"]
		self.assertFalse(os.path.exists(os.path.join(self.folder, "top.txt")))
		listed = await self.client.request("trash.list", {"wsId": "w1"})
		self.assertEqual([(folder_entry, "src", "folder"), (file_entry, "top.txt", "file")], [(e["id"], e["path"], e["kind"]) for e in listed["entries"]], "newest first")
		self.assertEqual(4 + 7 + 7, listed["bytes"])
		self.assertEqual({"wsId": "w1", "path": "src"}, await self.client.request("trash.restore", {"id": folder_entry}))
		with open(os.path.join(self.folder, "src", "deep", "b.c"), newline="") as f:
			self.assertEqual("int b;\n", f.read(), "the folder is back with what was in it")
		self.assertEqual("int a;\n", (await self.call("fs.read", path="src/a.c"))["text"])
		with open(os.path.join(self.folder, "top.txt"), "w") as f:
			f.write("new\n")
		with self.assertRaises(RpcFailure) as taken:
			await self.client.request("trash.restore", {"id": file_entry})
		self.assertEqual("conflict", taken.exception.code)
		self.assertEqual({}, await self.client.request("trash.purge", {"id": file_entry}))
		self.assertEqual([], (await self.client.request("trash.list", {}))["entries"])
		for crafted in ("..", "../..", "1-2\\..\\.."):
			with self.assertRaises(RpcFailure) as refused:
				await self.client.request("trash.purge", {"id": crafted})
			self.assertEqual("not_found", refused.exception.code, "an id never names a path outside the trash")
		self.assertTrue(os.path.isdir(self.folder))
