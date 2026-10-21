"""The desktop app (docs/27): ied-desktop with no server running starts one, signs its window in and shows the folder
it was given; a server that dies while the window is open is started again and the page reconnects. Each check gives
the app and the server it starts their own app-data folders and control pipe, and stops both however it ends."""

import http.client
import json
import os
import secrets
import socket
import subprocess
import tempfile
import time
import unittest

from server import CLI_ENV, DESKTOP_ENV, ServerTestCase, remove_tree

START_SECONDS = 40


def free_port():
	with socket.socket() as s:
		s.bind(("127.0.0.1", 0))
		return s.getsockname()[1]


def page_targets(port):
	"""The WebView2's pages (its DevTools protocol on loopback); none while it is not up."""
	try:
		connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
		connection.request("GET", "/json/list")
		targets = json.loads(connection.getresponse().read())
		connection.close()
		return [t for t in targets if t.get("type") == "page"]
	except OSError:
		return []


@unittest.skipUnless(os.environ.get(DESKTOP_ENV) and os.environ.get(CLI_ENV), "needs --desktop and --cli")
class DesktopTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-desktop-")
		self.folder = os.path.join(self.root, "project")
		os.makedirs(self.folder)
		with open(os.path.join(self.folder, "README.md"), "w", encoding="utf-8") as f:
			f.write("# Desktop check\n")
		self.control = "ied-desktop-check-" + secrets.token_hex(6)
		self.port = free_port()
		# The app and the server it starts keep their state here, not in the user's folders.
		self.env = dict(os.environ, LOCALAPPDATA=os.path.join(self.root, "local"), APPDATA=os.path.join(self.root, "roaming"))
		self.app = None
		self.addCleanup(self.stop_everything)

	def ied(self, *arguments):
		return subprocess.run([os.environ[CLI_ENV], "--control", self.control] + list(arguments), capture_output=True, text=True, timeout=60, env=self.env)

	def status(self):
		answer = self.ied("status")
		return json.loads(answer.stdout)["result"] if answer.returncode == 0 else None

	def start_app(self):
		argv = [os.environ[DESKTOP_ENV], self.folder, "--control", self.control, "--debug-port", str(self.port)]
		self.app = subprocess.Popen(argv, env=self.env)

	def wait(self, condition, what, seconds=START_SECONDS):
		deadline = time.monotonic() + seconds
		while time.monotonic() < deadline:
			value = condition()
			if value:
				return value
			time.sleep(0.25)
		self.fail("no %s within %d s" % (what, seconds))

	def signed_in_page(self):
		pages = [p for p in page_targets(self.port) if p["url"].startswith("http://127.0.0.1:") and "login" not in p["url"]]
		return pages[0] if pages and pages[0]["title"].endswith("- IED") else None

	def stop_everything(self):
		if self.app and self.app.poll() is None:
			self.app.kill()
			self.app.wait(10)
		# The window is gone, so nothing starts the server again once it stops.
		status = self.status()
		if status:
			self.ied("stop")
			try:
				os.kill(status["pid"], 9)
			except OSError:
				pass
		deadline = time.monotonic() + 10
		while time.monotonic() < deadline:
			try:
				remove_tree(self.root)
				return
			except PermissionError:
				time.sleep(0.25)

	def test_it_starts_the_server_and_signs_its_window_in(self):
		self.assertIsNone(self.status(), "no server on this pipe before the app")
		self.start_app()
		page = self.wait(self.signed_in_page, "signed-in window")
		self.assertEqual("project - IED", page["title"])
		status = self.wait(lambda: (lambda s: s if s and s["clients"] >= 1 else None)(self.status()), "connected window")
		self.assertEqual([os.path.normcase(self.folder)], [os.path.normcase(w["root"]) for w in status["workspaces"]])
		self.assertTrue(os.path.isfile(os.path.join(self.root, "local", "IED", "desktop", "webview", "EBWebView", "Local State")) or
			os.path.isdir(os.path.join(self.root, "local", "IED", "desktop", "webview")), "the WebView2 profile is the app's own")

	def test_a_server_that_dies_is_started_again(self):
		self.start_app()
		self.wait(self.signed_in_page, "signed-in window")
		first = self.wait(lambda: (lambda s: s if s and s["clients"] >= 1 else None)(self.status()), "connected window")
		os.kill(first["pid"], 9)
		again = self.wait(lambda: (lambda s: s if s and s["pid"] != first["pid"] and s["clients"] >= 1 else None)(self.status()), "server started again and the page back")
		self.assertNotEqual(first["pid"], again["pid"])
		self.assertIsNotNone(self.signed_in_page(), "the window still shows IED")
