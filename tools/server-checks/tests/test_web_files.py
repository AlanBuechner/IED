"""The files the web client loads besides its own (docs/10 "Transport"): /ui-kit/ (shared/ui-kit), /codicons/<name>.svg
(third_party/codicons) and /vendor/<library>/ (vendor/web, installed by setup.bat): with a session only, their types,
cached where the content never changes, nothing outside their folders."""

import os
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

	@unittest.skipUnless(os.path.isfile(VENDOR_LOADER), "vendor/web is not installed (setup.bat)")
	def test_vendor(self):
		reply = self.get("/vendor/monaco-editor/min/vs/loader.js")
		self.assertEqual((200, "text/javascript; charset=utf-8", "max-age=86400"), (reply.status, reply.header("Content-Type"), reply.header("Cache-Control")))
		self.assertEqual(404, self.get("/vendor/monaco-editor/missing.js").status)
		self.assertIn(self.get("/vendor/%2e%2e/python/python.exe").status, (403, 404), "nothing outside vendor/web")
		self.assertEqual(401, self.get("/vendor/monaco-editor/min/vs/loader.js", signed_in=False).status)
