"""The desktop app (docs/27): ied-desktop with no server running starts one, signs its window in and shows the folder
it was given; a server that dies while the window is open is started again and the page reconnects; File > Open Folder
shows Windows' folder picker and opens the picked folder in the window. Each check gives the app and the server it
starts their own app-data folders and control pipe, and stops both however it ends."""

import asyncio
import ctypes
import ctypes.wintypes
import http.client
import json
import os
import secrets
import socket
import subprocess
import tempfile
import time
import unittest

import ws
from server import CLI_ENV, DESKTOP_ENV, ServerTestCase, remove_tree

START_SECONDS = 40
WM_SETTEXT, WM_COMMAND, IDOK, IDCANCEL = 0x000C, 0x0111, 1, 2
FOLDER_BOX, NAME_BOX = 1152, 1148   # the folder picker's "Folder:" edit (edt1); a file dialog's name combo (cmb13)


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


def evaluate(page, expression):
	"""Runs `expression` in the page over its DevTools protocol; its value."""
	async def run():
		socket_ = await ws.connect(page["webSocketDebuggerUrl"])
		try:
			await socket_.send_text(json.dumps({"id": 1, "method": "Runtime.evaluate", "params": {"expression": expression, "returnByValue": True}}))
			while True:
				answer = json.loads(await socket_.recv())
				if answer.get("id") == 1:
					return answer["result"]["result"].get("value")
		finally:
			await socket_.close()
	return asyncio.run(run())


def dialog_of(pid):
	"""The visible dialog box (#32770) of process `pid`, or None."""
	user32 = ctypes.WinDLL("user32")
	found = []

	def visit(hwnd, _):
		owner = ctypes.wintypes.DWORD()
		user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
		name = ctypes.create_unicode_buffer(64)
		user32.GetClassNameW(hwnd, name, 64)
		if owner.value == pid and name.value == "#32770" and user32.IsWindowVisible(hwnd):
			found.append(hwnd)
		return True
	user32.EnumWindows(ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.wintypes.HWND, ctypes.wintypes.LPARAM)(visit), 0)
	return found[0] if found else None


def resize_window(pid, width, height):
	"""Sizes the app's IED window (its class is IedDesktopWindow); false when it has none."""
	user32 = ctypes.WinDLL("user32")
	found = []

	def visit(hwnd, _):
		owner = ctypes.wintypes.DWORD()
		user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
		name = ctypes.create_unicode_buffer(64)
		user32.GetClassNameW(hwnd, name, 64)
		if owner.value == pid and name.value == "IedDesktopWindow" and user32.IsWindowVisible(hwnd):
			found.append(hwnd)
		return True
	user32.EnumWindows(ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.wintypes.HWND, ctypes.wintypes.LPARAM)(visit), 0)
	if found:
		user32.SetWindowPos(ctypes.wintypes.HWND(found[0]), None, 0, 0, width, height, 0x0002 | 0x0004)   # SWP_NOMOVE | SWP_NOZORDER
	return bool(found)


def controls_of(dialog):
	"""The dialog's controls as "class#id" under their parents, to say what a picker looked like."""
	user32 = ctypes.WinDLL("user32")
	user32.GetParent.restype = ctypes.wintypes.HWND
	user32.GetDlgCtrlID.argtypes = (ctypes.wintypes.HWND,)
	rows = []

	def visit(hwnd, _):
		name = ctypes.create_unicode_buffer(64)
		user32.GetClassNameW(hwnd, name, 64)
		parent = ctypes.create_unicode_buffer(64)
		user32.GetClassNameW(user32.GetParent(hwnd), parent, 64)
		rows.append("%s>%s#%d" % (parent.value, name.value, user32.GetDlgCtrlID(hwnd)))
		return True
	user32.EnumChildWindows(dialog, ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.wintypes.HWND, ctypes.wintypes.LPARAM)(visit), 0)
	return ", ".join(rows)


def answer_dialog(dialog, folder):
	"""Types `folder` into a folder picker and presses its button; None `folder` cancels it."""
	user32 = ctypes.WinDLL("user32")
	user32.GetDlgItem.restype = ctypes.wintypes.HWND
	user32.FindWindowExW.restype = ctypes.wintypes.HWND
	user32.FindWindowExW.argtypes = (ctypes.wintypes.HWND, ctypes.wintypes.HWND, ctypes.c_wchar_p, ctypes.c_wchar_p)
	user32.SendMessageW.argtypes = (ctypes.wintypes.HWND, ctypes.c_uint, ctypes.wintypes.WPARAM, ctypes.c_wchar_p)
	user32.PostMessageW.argtypes = (ctypes.wintypes.HWND, ctypes.c_uint, ctypes.wintypes.WPARAM, ctypes.wintypes.LPARAM)
	if folder is None:
		user32.PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0)
		return
	edit = user32.GetDlgItem(dialog, FOLDER_BOX)
	if not edit:
		combo = user32.FindWindowExW(user32.GetDlgItem(dialog, NAME_BOX), None, "ComboBox", None)
		edit = user32.FindWindowExW(combo, None, "Edit", None) if combo else None
	if not edit:
		raise AssertionError("the folder picker has no name box: " + controls_of(dialog))
	user32.SendMessageW(edit, WM_SETTEXT, 0, folder)
	# The first press goes into the typed folder, as Enter does; the next picks the folder it shows.
	for _ in range(3):
		user32.PostMessageW(dialog, WM_COMMAND, IDOK, 0)
		deadline = time.monotonic() + 3
		while time.monotonic() < deadline and user32.IsWindow(dialog):
			time.sleep(0.1)
		if not user32.IsWindow(dialog):
			return


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

	def start_app(self, home=False):
		argv = [os.environ[DESKTOP_ENV]] + ([] if home else [self.folder]) + ["--control", self.control, "--debug-port", str(self.port)]
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
		# On the same port the page reconnects by itself; on another (8765 taken) the window goes to the new address.
		origin = again["url"].rstrip("/")
		self.wait(lambda: [p for p in page_targets(self.port) if p["url"].startswith(origin + "/") and "login" not in p["url"]], "the window on the server again", 15)

	def home_page(self):
		pages = [p for p in page_targets(self.port) if p["url"].startswith("http://127.0.0.1:") and "login" not in p["url"]]
		if not pages:
			return None
		label = evaluate(pages[0], "(document.querySelector('.empty-area button') || {}).textContent || ''")
		return pages[0] if label == "Open Folder..." else None

	def open_folder(self, folder):
		"""Presses the home page's Open Folder button and answers the picker with `folder` (None: cancels it)."""
		evaluate(self.wait(self.home_page, "home page with Open Folder"), "document.querySelector('.empty-area button').click()")
		dialog = self.wait(lambda: dialog_of(self.app.pid), "folder picker", 15)
		answer_dialog(dialog, folder)
		self.wait(lambda: dialog_of(self.app.pid) is None, "picker closed", 15)

	def test_open_folder_opens_the_picked_folder_in_the_window(self):
		self.start_app(home=True)
		self.wait(self.home_page, "home page with Open Folder")
		self.assertEqual([], self.status()["workspaces"], "no workspace before")
		self.open_folder(None)
		time.sleep(1)
		self.assertIsNotNone(self.home_page(), "a cancelled picker leaves the page")
		self.assertEqual([], self.status()["workspaces"])
		for width, chrome in ((430, "phone"), (1200, "desktop")):
			self.assertTrue(resize_window(self.app.pid, width, 800))
			self.wait(lambda: evaluate(self.wait(self.home_page, "home page"), "!!document.querySelector('.app." + chrome + "')"), "the %s chrome" % chrome, 15)
			self.assertIsNotNone(self.wait(self.home_page, "Open Folder at %d px" % width, 15))
		self.open_folder(self.folder)
		page = self.wait(self.signed_in_page, "the picked folder in the window")
		self.assertEqual("project - IED", page["title"])
		self.assertEqual([os.path.normcase(self.folder)], [os.path.normcase(w["root"]) for w in self.status()["workspaces"]])
		self.assertEqual(1, len(page_targets(self.port)), "in the same window")
