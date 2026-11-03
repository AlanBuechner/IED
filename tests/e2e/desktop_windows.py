"""Sidebar tabs between real windows (docs/15 "Windows"): drives ied-desktop on a scratch server over DevTools, with the real
mouse for the drags Playwright cannot make across windows. Not part of run_e2e.py: it opens windows on the person's screen
and moves their pointer for a few seconds, so its drags wait until the machine has been idle a while, or are skipped.

    python -I tests/e2e/desktop_windows.py --desktop build/bin/<...>/ied-desktop/ied-desktop.exe --server build/bin/<...>/ied-server/ied-server.exe
"""

import argparse
import ctypes
import json
import os
import secrets
import shutil
import subprocess
import sys
import tempfile
import time
from ctypes import wintypes

from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
FIXTURE = os.path.join(HERE, "fixture", "workspace")
FRONTEND = os.path.join(REPO, "frontend")
SHOTS = os.path.join(REPO, "build", "e2e", "desktop-windows")
PORT = 9377
IDLE_BEFORE_DRAGS_MS = 20000

ctypes.windll.shcore.SetProcessDpiAwareness(2)
user32 = ctypes.windll.user32
user32.WindowFromPoint.argtypes = [wintypes.POINT]
user32.WindowFromPoint.restype = wintypes.HWND
user32.GetAncestor.argtypes = [wintypes.HWND, wintypes.UINT]
user32.GetAncestor.restype = wintypes.HWND
user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
WM_CLOSE = 0x10
HWND_TOPMOST = wintypes.HWND(-1)
HWND_NOTOPMOST = wintypes.HWND(-2)
SWP_NOSIZE, SWP_NOMOVE, SWP_NOACTIVATE = 1, 2, 16
LEFTDOWN, LEFTUP = 2, 4
SM_XVIRTUALSCREEN, SM_YVIRTUALSCREEN, SM_CXVIRTUALSCREEN, SM_CYVIRTUALSCREEN = 76, 77, 78, 79
GA_ROOT = 2

results = []


def check(ok, what):
	results.append((bool(ok), what))
	print(("ok   " if ok else "FAIL ") + what, flush=True)


def class_of(hwnd):
	buffer = ctypes.create_unicode_buffer(256)
	user32.GetClassNameW(hwnd, buffer, 256)
	return buffer.value


def app_windows(pid):
	found = []

	@ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
	def visit(hwnd, _):
		owner = wintypes.DWORD()
		user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
		if owner.value == pid and class_of(hwnd) == "IedDesktopWindow" and user32.IsWindowVisible(hwnd):
			found.append(hwnd)
		return True
	user32.EnumWindows(visit, 0)
	return found


def title_of(hwnd):
	buffer = ctypes.create_unicode_buffer(512)
	user32.GetWindowTextW(hwnd, buffer, 512)
	return buffer.value


def rect_of(hwnd):
	r = wintypes.RECT()
	user32.GetWindowRect(hwnd, ctypes.byref(r))
	return (r.left, r.top, r.right - r.left, r.bottom - r.top)


def client_origin(hwnd):
	p = wintypes.POINT(0, 0)
	user32.ClientToScreen(hwnd, ctypes.byref(p))
	return p.x, p.y


def idle_ms():
	class LastInput(ctypes.Structure):
		_fields_ = [("cbSize", ctypes.c_uint), ("dwTime", ctypes.c_uint)]
	info = LastInput()
	info.cbSize = 8
	user32.GetLastInputInfo(ctypes.byref(info))
	return ctypes.windll.kernel32.GetTickCount() - info.dwTime


def real_drag(start, end, during=None):
	"""A press, a first small move and a pause as a person's drag has, then the way there, and the pointer back after."""
	saved = wintypes.POINT()
	user32.GetCursorPos(ctypes.byref(saved))
	try:
		user32.SetCursorPos(*start)
		time.sleep(0.15)
		user32.mouse_event(LEFTDOWN, 0, 0, 0, 0)
		time.sleep(0.15)
		user32.SetCursorPos(start[0] + 10, start[1] + 6)
		time.sleep(0.4)
		steps = 24
		for i in range(1, steps + 1):
			user32.SetCursorPos(start[0] + (end[0] - start[0]) * i // steps, start[1] + (end[1] - start[1]) * i // steps)
			time.sleep(0.02)
		user32.SetCursorPos(end[0] + 1, end[1] + 1)
		time.sleep(0.05)
		user32.SetCursorPos(*end)
		time.sleep(0.4)
		if during:
			during()
	finally:
		user32.mouse_event(LEFTUP, 0, 0, 0, 0)
		time.sleep(1.2)
		user32.SetCursorPos(saved.x, saved.y)


def sidebar(page):
	return page.eval_on_selector_all(".sidebar .sb-act[data-activity]", "els => els.map(e => e.dataset.activity)")


class App:
	"""The app on a scratch server: its pages over DevTools, its windows by their handles."""

	def __init__(self, program, control, data, workspace, connect):
		self.program, self.control, self.data, self.workspace = program, control, data, workspace
		self.process = None
		self.connect = connect
		self.browser = None

	def start(self):
		self.process = subprocess.Popen([self.program, self.workspace, "--control", self.control, "--data", self.data, "--debug-port", str(PORT)])
		deadline = time.time() + 40
		while time.time() < deadline:
			try:
				self.browser = self.connect()
				return
			except Exception:
				time.sleep(0.5)
		raise RuntimeError("the app's DevTools port did not open")

	def windows(self):
		return app_windows(self.process.pid)

	def pages(self):
		out = []
		for context in self.browser.contexts:
			for page in context.pages:
				try:
					if page.evaluate("Boolean(window.ied_shell)"):
						out.append(page)
				except Exception:
					pass
		return out

	def by_window(self, count, timeout=25):
		"""The `count` ready pages, by window id (None: the main window)."""
		deadline = time.time() + timeout
		while time.time() < deadline:
			pages = [p for p in self.pages() if p.evaluate("Boolean(ied_shell.layout && ied_shell.layout.doc && ied_shell.client.state === 'live')")]
			if len(pages) == count:
				return {p.evaluate("ied_shell.windows.id"): p for p in pages}
			time.sleep(0.3)
		raise RuntimeError("wanted %d pages, have %s" % (count, [p.url for p in self.pages()]))

	def hwnd_for(self, page):
		title = page.title()
		for hwnd in self.windows():
			if title_of(hwnd) == title:
				return hwnd
		raise RuntimeError("no window titled %r: %s" % (title, [title_of(h) for h in self.windows()]))

	def wait_exit(self):
		self.process.wait(20)
		try:
			self.browser.close()
		except Exception:
			pass


def center_of(app, page, selector):
	hwnd = app.hwnd_for(page)
	box = page.evaluate("(sel) => { const r = document.querySelector(sel).getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2, devicePixelRatio]; }", selector)
	x, y = client_origin(hwnd)
	return int(x + box[0] * box[2]), int(y + box[1] * box[2])


def topmost(hwnds, on):
	for hwnd in hwnds:
		user32.SetWindowPos(hwnd, HWND_TOPMOST if on else HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE)


def free_point():
	"""A point on some monitor over no IED window, for a drop no window takes."""
	left, top = user32.GetSystemMetrics(SM_XVIRTUALSCREEN), user32.GetSystemMetrics(SM_YVIRTUALSCREEN)
	width, height = user32.GetSystemMetrics(SM_CXVIRTUALSCREEN), user32.GetSystemMetrics(SM_CYVIRTUALSCREEN)
	for y in range(top + 200, top + height - 100, 160):
		for x in range(left + 200, left + width - 200, 240):
			hwnd = user32.WindowFromPoint(wintypes.POINT(x, y))
			root = user32.GetAncestor(hwnd, GA_ROOT) if hwnd else None
			if root and class_of(root) != "IedDesktopWindow":
				return x, y
	return None


def move_by_menu(page, activity, label):
	page.click('.sb-act[data-activity="%s"]' % activity, button="right")
	page.wait_for_selector(".popup.menu")
	page.click(".popup.menu [role=menuitem]:has-text('%s')" % label)


def appeared(page, activity, what):
	try:
		page.wait_for_selector('.sb-act[data-activity="%s"]' % activity, timeout=8000)
		check(True, what)
	except Exception:
		check(False, what)


def chat_out(app, main):
	"""The chat moved out by its menu: the main page and the chat's."""
	before = len(app.windows())
	move_by_menu(main, "ied.chat", "Move to New Window")
	pages = app.by_window(before + 1)
	pages["w-ied.chat"].wait_for_selector('.sb-act[data-activity="ied.chat"]', timeout=15000)
	return pages[None], pages["w-ied.chat"]


def real_drags(app, main_hwnd, chat_hwnd):
	user32.SetWindowPos(main_hwnd, None, 40, 60, 900, 700, SWP_NOACTIVATE)
	user32.SetWindowPos(chat_hwnd, None, 960, 60, 900, 700, SWP_NOACTIVATE)
	topmost([main_hwnd, chat_hwnd], True)
	time.sleep(0.8)
	pages = app.by_window(2)
	main, chat = pages[None], pages["w-ied.chat"]
	try:
		def overlay():
			check(chat.evaluate("!document.querySelector('.window-drop').hidden"), "mid-drag, the other window offers itself")
			chat.screenshot(path=os.path.join(SHOTS, "drop-overlay.png"), timeout=3000)
		real_drag(center_of(app, main, '.sb-act[data-activity="ied.git"]'), center_of(app, chat, ".activity-area"), overlay)
		appeared(chat, "ied.git", "a drag docked Git to the other window")
		check("ied.git" not in sidebar(main), "and it left the first: %s" % sidebar(main))
		check(chat.locator(".window-drop").first.is_hidden(), "the overlay is gone")
		chat.screenshot(path=os.path.join(SHOTS, "chat-with-git.png"))

		real_drag(center_of(app, chat, '.sb-act[data-activity="ied.git"]'), center_of(app, main, ".activity-area"))
		appeared(main, "ied.git", "dragged back to the first window")

		count = len(app.windows())
		real_drag(center_of(app, main, '.sb-act[data-activity="ied.git"]'), center_of(app, main, ".activity-area"))
		check(len(app.windows()) == count and "ied.git" in sidebar(main), "dropped on its own window, nothing happens")

		free = free_point()
		if not free:
			check(False, "no point over no IED window to tear a tab off onto")
			return
		real_drag(center_of(app, main, '.sb-act[data-activity="ied.profiler"]'), free)
		deadline = time.time() + 12
		while len(app.windows()) == count and time.time() < deadline:
			time.sleep(0.2)
		check(len(app.windows()) == count + 1, "dropped outside every window, a new window opened")
		torn = app.by_window(count + 1).get("w-ied.profiler")
		check(torn is not None, "for the Profiler")
		if not torn:
			return
		torn.wait_for_selector('.sb-act[data-activity="ied.profiler"]', timeout=15000)
		placed = rect_of(app.hwnd_for(torn))
		check(placed[0] <= free[0] <= placed[0] + placed[2] and placed[1] <= free[1] <= placed[1] + 60, "its caption under the pointer: %s at %s" % (placed, free))
		torn.screenshot(path=os.path.join(SHOTS, "torn-off.png"))
		user32.PostMessageW(app.hwnd_for(torn), WM_CLOSE, 0, 0)
		appeared(main, "ied.profiler", "closed, the Profiler is back in the main window")
	finally:
		topmost([main_hwnd, chat_hwnd], False)


def drive(app):
	app.start()
	main = app.by_window(1)[None]
	main.wait_for_selector(".sidebar .sb-act")
	check(main.evaluate("typeof window.iedDesktop.closeWindow") == "function", "the app's bridge has closeWindow")

	main, chat = chat_out(app, main)
	main.wait_for_selector('.sb-act[data-activity="ied.chat"]', state="detached", timeout=15000)
	check(len(app.windows()) == 2, "two native windows")
	check(sidebar(chat) == ["ied.chat"], "the new window's sidebar: %s" % sidebar(chat))
	check("ied.chat" not in sidebar(main), "the chat left the main window: %s" % sidebar(main))
	check("win=w-ied.chat" in chat.url, "its address: %s" % chat.url)
	main_hwnd, chat_hwnd = app.hwnd_for(main), app.hwnd_for(chat)
	check(title_of(chat_hwnd).startswith("Chat - "), "its caption says Chat: %s" % title_of(chat_hwnd))
	main.screenshot(path=os.path.join(SHOTS, "main.png"))
	chat.screenshot(path=os.path.join(SHOTS, "chat.png"))

	if idle_ms() < IDLE_BEFORE_DRAGS_MS:
		check(False, "the machine is in use (%d ms idle): the real drags were skipped" % idle_ms())
	else:
		real_drags(app, main_hwnd, chat_hwnd)
		check(main.evaluate("ied_shell.deviceClass") == "desktop", "narrowed to 900 px, the main window keeps the desktop's layout")

	# Closed by its X, what it held comes back, and its place is kept apart from the main window's.
	pages = app.by_window(len(app.windows()))
	main, chat = pages[None], pages["w-ied.chat"]
	chat_hwnd = app.hwnd_for(chat)
	placed_at = rect_of(chat_hwnd)
	user32.PostMessageW(chat_hwnd, WM_CLOSE, 0, 0)
	appeared(main, "ied.chat", "closing the window gave its tab back")
	check(os.path.exists(os.path.join(app.data, "windows", "w-ied.chat.json")), "its place is kept")
	check(not os.path.exists(os.path.join(app.data, "window.json")), "and not as the main window's")

	main, chat = chat_out(app, main)
	check(rect_of(app.hwnd_for(chat)) == placed_at, "moved out again, it opens where it was: %s, was %s" % (rect_of(app.hwnd_for(chat)), placed_at))

	count = len(app.windows())
	move_by_menu(chat, "ied.chat", "Move to Main Window")
	appeared(main, "ied.chat", "moved back by its menu")
	deadline = time.time() + 10
	while len(app.windows()) >= count and time.time() < deadline:
		time.sleep(0.2)
	check(len(app.windows()) == count - 1, "a window that gave away its last tab closed itself")

	# The session ends, the main window first; started again, both come back where they were.
	main, chat = chat_out(app, main)
	time.sleep(1.5)
	chat_hwnd, main_hwnd = app.hwnd_for(chat), app.hwnd_for(main)
	placed_at = rect_of(chat_hwnd)
	user32.SetWindowPos(main_hwnd, None, 60, 60, 1000, 700, SWP_NOACTIVATE)
	time.sleep(0.3)
	main_at = rect_of(main_hwnd)
	user32.PostMessageW(main_hwnd, WM_CLOSE, 0, 0)
	time.sleep(1.5)
	user32.PostMessageW(chat_hwnd, WM_CLOSE, 0, 0)
	app.wait_exit()
	app.start()
	pages = app.by_window(2)
	main, chat = pages[None], pages["w-ied.chat"]
	chat.wait_for_selector('.sb-act[data-activity="ied.chat"]', timeout=15000)
	check("ied.chat" not in sidebar(main), "started again, the main window lacks the chat: %s" % sidebar(main))
	check(sidebar(chat) == ["ied.chat"], "and the chat's window is back: %s" % sidebar(chat))
	check(rect_of(app.hwnd_for(chat)) == placed_at, "where it was: %s, was %s" % (rect_of(app.hwnd_for(chat)), placed_at))
	check(rect_of(app.hwnd_for(main)) == main_at, "the main window too: %s, was %s" % (rect_of(app.hwnd_for(main)), main_at))
	main.screenshot(path=os.path.join(SHOTS, "main-restored.png"))
	chat.screenshot(path=os.path.join(SHOTS, "chat-restored.png"))


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--desktop", required=True, help="the ied-desktop executable")
	parser.add_argument("--server", required=True, help="the ied-server executable, where it was built")
	args = parser.parse_args(argv)
	os.makedirs(SHOTS, exist_ok=True)
	temp = tempfile.mkdtemp(prefix="ied-windows-")
	workspace = os.path.join(temp, "e2e")
	shutil.copytree(FIXTURE, workspace)
	app_data = os.path.join(temp, "data", "app")
	os.makedirs(app_data)
	# The machine's own Claude Code CLI stays out, as in targets.py.
	with open(os.path.join(app_data, "settings.json"), "w", encoding="utf-8") as f:
		json.dump({"ai.claude.command": [os.path.join(temp, "no-claude", "claude.exe")]}, f)
	control = "ied-windows-" + secrets.token_hex(6)
	server = subprocess.Popen([os.path.abspath(args.server), "--host", "127.0.0.1", "--port", "0", "--data", os.path.join(temp, "data"), "--control", control, "--test-hooks", "--quiet", "--static", FRONTEND, "--workspace", workspace], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
	server.stdout.readline()
	server.stdout.readline()
	try:
		with sync_playwright() as p:
			app = App(os.path.abspath(args.desktop), control, os.path.join(temp, "desktop"), workspace, lambda: p.chromium.connect_over_cdp("http://127.0.0.1:%d" % PORT))
			try:
				drive(app)
			finally:
				for hwnd in app.windows() if app.process else []:
					user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
				time.sleep(1)
				if app.process and app.process.poll() is None:
					app.process.kill()
	finally:
		server.terminate()
		try:
			server.wait(10)
		except subprocess.TimeoutExpired:
			server.kill()
		shutil.rmtree(temp, ignore_errors=True)
	failed = [what for ok, what in results if not ok]
	print("%d checks, %d failed; screenshots in %s" % (len(results), len(failed), SHOTS))
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
