"""The files the web client loads besides its own (docs/10 "Transport"): /ui-kit/ (shared/ui-kit), /codicons/<name>.svg
(third_party/codicons) and /vendor/<library>/ (vendor/web, installed by setup.bat): with a session only, their types,
cached where the content never changes, nothing outside their folders."""

import os
import shutil
import tempfile
import unittest

from server import REPO, IedServer, ServerTestCase

VENDOR_LOADER = os.path.join(REPO, "vendor", "web", "monaco-editor", "min", "vs", "loader.js")


class WebFileTests(ServerTestCase):
	def setUp(self):
		self.server = IedServer()
		self.assertTrue(self.server.started, self.server.error)
		self.cookie = {"Cookie": self.server.sign_in()}

	def tearDown(self):
		self.server.stop()

	def get(self, path, signed_in=True):
		return self.server.request("GET", path, headers=self.cookie if signed_in else None)

	def test_ui_kit(self):
		reply = self.get("/ui-kit/dom.js")
		self.assertEqual((200, "text/javascript; charset=utf-8"), (reply.status, reply.header("Content-Type")))
		self.assertIn(b"export function h(", reply.body)
		self.assertEqual("text/css; charset=utf-8", self.get("/ui-kit/ui-kit.css").header("Content-Type"))
		for path in ("/ui-kit/missing.js", "/ui-kit/%2e%2e/README.md", "/ui-kit/../host-api/README.md"):
			self.assertIn(self.get(path).status, (403, 404), path)
		self.assertEqual(401, self.get("/ui-kit/dom.js", signed_in=False).status, "a session first")

	def test_codicons(self):
		reply = self.get("/codicons/add.svg")
		self.assertEqual((200, "image/svg+xml"), (reply.status, reply.header("Content-Type")))
		self.assertEqual("max-age=86400", reply.header("Cache-Control"))
		self.assertIn(b"<svg", reply.body)
		for path in ("/codicons/Add.svg", "/codicons/add", "/codicons/no-such-icon.svg", "/codicons/a_b.svg", "/codicons/%2e%2e%2fREADME.md"):
			self.assertEqual(404, self.get(path).status, path)
		self.assertEqual(401, self.get("/codicons/add.svg", signed_in=False).status)

	def test_codicon_aliases(self):
		# An icon's other names (codicons' mapping.json) draw it, as VS Code's $(run) draws play.
		self.assertEqual(self.get("/codicons/play.svg").body, self.get("/codicons/run.svg").body)
		self.assertEqual(404, self.get("/codicons/RUN.svg").status)

	@unittest.skipUnless(os.path.isfile(VENDOR_LOADER), "vendor/web is not installed (setup.bat)")
	def test_vendor(self):
		reply = self.get("/vendor/monaco-editor/min/vs/loader.js")
		self.assertEqual((200, "text/javascript; charset=utf-8", "max-age=86400"), (reply.status, reply.header("Content-Type"), reply.header("Cache-Control")))
		self.assertEqual(404, self.get("/vendor/monaco-editor/missing.js").status)
		self.assertIn(self.get("/vendor/%2e%2e/python/python.exe").status, (403, 404), "nothing outside vendor/web")
		self.assertEqual(401, self.get("/vendor/monaco-editor/min/vs/loader.js", signed_in=False).status)


class WorkspaceImageTests(ServerTestCase):
	"""/w/<wsId>/files/<path> (docs/10): a workspace's images for previews, and never anything that could run script."""

	PNG = bytes.fromhex("89504e470d0a1a0a0000000d4948445200000001000000010806000000")

	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-images-")
		self.workspace = os.path.join(self.root, "work")
		os.makedirs(os.path.join(self.workspace, "docs", "img"))
		with open(os.path.join(self.workspace, "docs", "img", "Shot.PNG"), "wb") as f:
			f.write(self.PNG)
		for name, text in (("page.html", "<script>alert(1)</script>"), ("logo.svg", "<svg onload='alert(1)'/>"), ("notes.md", "# x")):
			with open(os.path.join(self.workspace, "docs", name), "w") as f:
				f.write(text)
		with open(os.path.join(self.root, "outside.png"), "wb") as f:
			f.write(self.PNG)
		self.server = IedServer(extra=("--workspace", self.workspace))
		self.assertTrue(self.server.started, self.server.error)
		self.cookie = {"Cookie": self.server.sign_in()}

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	def get(self, path, signed_in=True):
		return self.server.request("GET", path, headers=self.cookie if signed_in else None)

	def test_images_and_nothing_else(self):
		reply = self.get("/w/w1/files/docs/img/Shot.PNG")
		self.assertEqual((200, "image/png", self.PNG), (reply.status, reply.header("Content-Type"), reply.body))
		self.assertEqual("nosniff", reply.header("X-Content-Type-Options"))
		self.assertIn("sandbox", reply.header("Content-Security-Policy"))
		for path in ("/w/w1/files/docs/page.html", "/w/w1/files/docs/logo.svg", "/w/w1/files/docs/notes.md", "/w/w1/files/docs/img/missing.png", "/w/w9/files/docs/img/Shot.PNG", "/w/w1/files/"):
			self.assertEqual(404, self.get(path).status, path)
		for path in ("/w/w1/files/../outside.png", "/w/w1/files/%2e%2e/outside.png"):
			self.assertIn(self.get(path).status, (403, 404), path)
		self.assertEqual(401, self.get("/w/w1/files/docs/img/Shot.PNG", signed_in=False).status, "a session first")
