"""What the file viewers read (docs/33): /w/<wsId>/files/<path> (docs/10) - every file of the workspace, as its type for
the viewers (pictures, SVG, sound, video, PDF, fonts) and as bytes to save for anything else, never anything that runs
script on IED's origin; one byte range at a time (206, Content-Range, an open-ended one capped at 4 MiB, a suffix, 416
past the end, several ranges answered whole), its ETag and 304, ?download=1. And doc.open of a binary file: refused
with reason "binary" and the size, unless binary: true, which shows its text read-only."""

import os
import shutil
import tempfile

from frames import FrameChecker
from iedclient import RpcFailure
from routed import RoutedClient
from server import IedServer, ServerTestCase

PNG = bytes.fromhex("89504e470d0a1a0a0000000d4948445200000001000000010806000000")
BIG = 5 * 1024 * 1024
MOST_RANGE = 4 * 1024 * 1024


def write(root, path, data):
	full = os.path.join(root, *path.split("/"))
	os.makedirs(os.path.dirname(full), exist_ok=True)
	with open(full, "wb") as f:
		f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


class WorkspaceFileTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-files-")
		self.workspace = os.path.join(self.root, "work")
		write(self.workspace, "docs/img/Shot.PNG", PNG)
		write(self.workspace, "docs/page.html", "<script>alert(1)</script>")
		write(self.workspace, "docs/logo.svg", "<svg xmlns='http://www.w3.org/2000/svg' onload='alert(1)'/>")
		write(self.workspace, "docs/notes.md", "# x")
		write(self.workspace, "media/clip.mp4", bytes(range(256)) * 4)
		write(self.workspace, "media/big.bin", bytes(i % 251 for i in range(BIG)))
		write(self.workspace, "docs/manual.pdf", b"%PDF-1.4\n%%EOF\n")
		write(self.workspace, "bin/a b's.dat", b"\x00\x01\x02")
		write(self.workspace, "empty.bin", b"")
		write(self.root, "outside.png", PNG)
		self.server = IedServer(extra=("--workspace", self.workspace))
		self.assertTrue(self.server.started, self.server.error)
		self.cookie = {"Cookie": self.server.sign_in()}

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	def get(self, path, signed_in=True, **headers):
		sent = dict(self.cookie) if signed_in else {}
		sent.update({name.replace("_", "-"): value for name, value in headers.items()})
		return self.server.request("GET", path, headers=sent)

	def test_files_as_their_types_and_nothing_that_runs_script(self):
		png = self.get("/w/w1/files/docs/img/Shot.PNG")
		self.assertEqual((200, "image/png", PNG), (png.status, png.header("Content-Type"), png.body))
		self.assertEqual(("nosniff", "bytes", "no-cache"), (png.header("X-Content-Type-Options"), png.header("Accept-Ranges"), png.header("Cache-Control")))
		self.assertIn("sandbox", png.header("Content-Security-Policy"))
		self.assertIsNone(png.header("Content-Disposition"), "a picture is shown, not saved")
		svg = self.get("/w/w1/files/docs/logo.svg")
		self.assertEqual("image/svg+xml", svg.header("Content-Type"))
		self.assertIn("sandbox", svg.header("Content-Security-Policy"), "an SVG opened by itself runs no script")
		self.assertNotIn("script-src", svg.header("Content-Security-Policy"))
		html = self.get("/w/w1/files/docs/page.html")
		self.assertEqual((200, "application/octet-stream"), (html.status, html.header("Content-Type")), "HTML is bytes to save, never a page")
		self.assertTrue(html.header("Content-Disposition").startswith("attachment"))
		self.assertIn("sandbox", html.header("Content-Security-Policy"))
		self.assertEqual("text/plain; charset=utf-8", self.get("/w/w1/files/docs/notes.md").header("Content-Type"))
		self.assertEqual("video/mp4", self.get("/w/w1/files/media/clip.mp4").header("Content-Type"))
		pdf = self.get("/w/w1/files/docs/manual.pdf")
		self.assertEqual(("application/pdf", None), (pdf.header("Content-Type"), pdf.header("Content-Security-Policy")), "a sandboxed PDF is refused by Chromium's viewer")
		odd = self.get("/w/w1/files/bin/a%20b's.dat")
		self.assertEqual("attachment; filename*=UTF-8''a%20b%27s.dat", odd.header("Content-Disposition"))
		saved = self.get("/w/w1/files/docs/img/Shot.PNG?download=1")
		self.assertEqual("attachment; filename*=UTF-8''Shot.PNG", saved.header("Content-Disposition"))
		for path in ("/w/w1/files/docs/img/missing.png", "/w/w9/files/docs/img/Shot.PNG", "/w/w1/files/", "/w/w1/files/docs"):
			self.assertEqual(404, self.get(path).status, path)
		for path in ("/w/w1/files/../outside.png", "/w/w1/files/%2e%2e/outside.png"):
			self.assertIn(self.get(path).status, (403, 404), path)
		self.assertEqual(401, self.get("/w/w1/files/docs/img/Shot.PNG", signed_in=False).status, "a session first")

	def test_byte_ranges(self):
		clip = bytes(range(256)) * 4
		part = self.get("/w/w1/files/media/clip.mp4", Range="bytes=10-19")
		self.assertEqual((206, "bytes 10-19/1024", clip[10:20]), (part.status, part.header("Content-Range"), part.body))
		self.assertEqual("bytes 1014-1023/1024", self.get("/w/w1/files/media/clip.mp4", Range="bytes=-10").header("Content-Range"))
		self.assertEqual("bytes 1000-1023/1024", self.get("/w/w1/files/media/clip.mp4", Range="bytes=1000-5000").header("Content-Range"), "an end past the file is the end")
		past = self.get("/w/w1/files/media/clip.mp4", Range="bytes=2000-")
		self.assertEqual((416, "bytes */1024"), (past.status, past.header("Content-Range")))
		several = self.get("/w/w1/files/media/clip.mp4", Range="bytes=0-1,5-6")
		self.assertEqual((200, clip), (several.status, several.body), "several ranges are answered whole")
		self.assertEqual(200, self.get("/w/w1/files/media/clip.mp4", Range="lines=1-2").status)
		opened = self.get("/w/w1/files/media/big.bin", Range="bytes=0-")
		self.assertEqual((206, "bytes 0-%d/%d" % (MOST_RANGE - 1, BIG), MOST_RANGE), (opened.status, opened.header("Content-Range"), len(opened.body)), "an open range is capped")
		tail = self.get("/w/w1/files/media/big.bin", Range="bytes=%d-" % (BIG - 3))
		self.assertEqual(bytes(i % 251 for i in range(BIG - 3, BIG)), tail.body)
		zipped = self.get("/w/w1/files/docs/logo.svg", Range="bytes=0-4", Accept_Encoding="gzip")
		self.assertEqual((206, None, b"<svg "), (zipped.status, zipped.header("Content-Encoding"), zipped.body), "a range is never coded")
		empty = self.get("/w/w1/files/empty.bin", Range="bytes=0-99")
		self.assertEqual((416, "bytes */0"), (empty.status, empty.header("Content-Range")))
		self.assertEqual((200, b""), (self.get("/w/w1/files/empty.bin").status, self.get("/w/w1/files/empty.bin").body))

	def test_entity_tags(self):
		first = self.get("/w/w1/files/docs/img/Shot.PNG")
		tag = first.header("ETag")
		self.assertTrue(tag and tag.startswith('"'))
		again = self.get("/w/w1/files/docs/img/Shot.PNG", If_None_Match=tag)
		self.assertEqual((304, b""), (again.status, again.body))
		write(self.workspace, "docs/img/Shot.PNG", PNG + b"\x00")
		changed = self.get("/w/w1/files/docs/img/Shot.PNG", If_None_Match=tag)
		self.assertEqual(200, changed.status, "a changed file is sent")
		stale = self.get("/w/w1/files/docs/img/Shot.PNG", Range="bytes=0-3", If_Range=tag)
		self.assertEqual(200, stale.status, "If-Range with an old tag sends the file whole")


class BinaryDocumentTests(ServerTestCase):
	def setUp(self):
		self.folder = tempfile.mkdtemp(prefix="ied-binary-")
		write(self.folder, "art/hero.png", PNG + bytes(64))
		write(self.folder, "data/blob.xyz", b"head\x00tail")
		write(self.folder, "src/main.c", "int main;\n")
		self.server = IedServer(extra=("--workspace", self.folder))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.folder, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def test_a_binary_file_opens_only_on_request_and_only_shown(self):
		for path, size in (("art/hero.png", len(PNG) + 64), ("data/blob.xyz", 9)):
			with self.assertRaises(RpcFailure) as refused:
				await self.client.request("doc.open", {"wsId": "w1", "path": path})
			self.assertEqual(("bad_request", "binary", size), (refused.exception.code, refused.exception.data["reason"], refused.exception.data["size"]), path)
		opened = await self.client.request("doc.open", {"wsId": "w1", "path": "data/blob.xyz", "binary": True})
		self.assertEqual((True, None), (opened["readOnly"], opened["holder"]))
		self.assertIn("binary", opened["readOnlyReason"])
		with self.assertRaises(RpcFailure) as edit:
			await self.client.request("doc.takeLease", {"docId": opened["docId"]})
		self.assertEqual("forbidden", edit.exception.code)
		with self.assertRaises(RpcFailure):
			await self.client.request("doc.open", {"wsId": "w1", "path": "data/blob.xyz"})
		text = await self.client.request("doc.open", {"wsId": "w1", "path": "src/main.c"})
		self.assertFalse(text["readOnly"], "a text file opens as before")
