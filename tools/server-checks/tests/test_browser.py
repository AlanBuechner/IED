"""The browser (docs/27 "The browser"): pages on the server in a headless Edge - opened, titled, their console and
failed requests on page:<pageId>, frames pulled with browser.frame, a person's input forwarded, the AI's bindings
(an accessibility snapshot with refs, acting on them, a screenshot, the console, eval) with each action told to the
page's viewers, the URL rules, pages kept across a server restart, and closing."""

import asyncio
import base64
import functools
import http.server
import os
import re
import tempfile
import threading
import time
import unittest

from frames import FrameChecker
from iedclient import Client, RpcFailure
from server import IedServer, ServerTestCase, remove_tree
import test_chat as chat_checks

PAGE = """<!doctype html>
<html><head><title>Fixture</title></head>
<body style="margin:0">
<h1 style="position:absolute;left:20px;top:10px;margin:0">Browser fixture</h1>
<input id="name" aria-label="Name" style="position:absolute;left:20px;top:120px;width:200px;height:30px">
<button id="go" style="position:absolute;left:20px;top:200px;width:200px;height:60px"
	onclick="document.title = 'Clicked ' + document.getElementById('name').value; console.log('clicked', document.getElementById('name').value)">Go</button>
<a id="pop" href="/index.html?opened=1" target="_blank" style="position:absolute;left:20px;top:300px">Pop</a>
<img src="/missing.png" alt="">
<script>console.warn('fixture ready'); setTimeout(() => { throw new Error('fixture boom'); }, 0);</script>
</body></html>
"""

LOAD_SECONDS = 30


def browser_installed():
	for variable, rest in (("ProgramFiles(x86)", "Microsoft/Edge/Application/msedge.exe"), ("ProgramFiles", "Microsoft/Edge/Application/msedge.exe"),
		("LOCALAPPDATA", "Microsoft/Edge/Application/msedge.exe"), ("ProgramFiles", "Google/Chrome/Application/chrome.exe")):
		root = os.environ.get(variable)
		if root and os.path.isfile(os.path.join(root, rest)):
			return True
	return False


def remove_when_released(path):
	"""The browser's processes end with the server, a moment after it: their profile files are removed once released."""
	deadline = time.monotonic() + 10
	while True:
		try:
			remove_tree(path)
			return
		except PermissionError:
			if time.monotonic() > deadline:
				raise
			time.sleep(0.25)


class QuietHandler(http.server.SimpleHTTPRequestHandler):
	def log_message(self, *args):
		pass


@unittest.skipUnless(browser_installed(), "needs Microsoft Edge or Google Chrome")
class BrowserTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-browser-")
		self.addCleanup(remove_when_released, self.root)
		self.site = os.path.join(self.root, "site")
		os.makedirs(self.site)
		with open(os.path.join(self.site, "index.html"), "w", encoding="utf-8") as f:
			f.write(PAGE)
		self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(QuietHandler, directory=self.site))
		threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
		self.addCleanup(self.httpd.server_close)
		self.addCleanup(self.httpd.shutdown)
		self.url = "http://127.0.0.1:%d/index.html" % self.httpd.server_address[1]
		self.data = os.path.join(self.root, "data")
		os.makedirs(self.data)
		self.server = self.start_server()

	def start_server(self):
		server = IedServer(data=self.data, extra=("--workspace", self.site))
		self.addCleanup(server.stop)
		self.assertTrue(server.started, server.error)
		return server

	async def asyncSetUp(self):
		await super().asyncSetUp()
		await self.connect()

	async def connect(self):
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		self.ws = (await self.client.request("ws.list"))["workspaces"][0]["wsId"]

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def open(self, url=None):
		page = (await self.client.request("browser.open", {"wsId": self.ws, "url": url or self.url, "width": 800, "height": 600}, timeout=LOAD_SECONDS))["pageId"]
		await self.client.sub(["page:" + page])
		snapshot = await self.client.next_frame("page:" + page)
		self.assertEqual("snap", snapshot["t"])
		self.snapshot = snapshot["data"]
		return page

	async def titled(self, page, title):
		"""Once the page has `title`: already (its topic is subscribed, so nothing after this check is missed), or by an event."""
		listed = (await self.client.request("browser.pages", {"wsId": self.ws}))["pages"]
		if any(p["pageId"] == page and p["title"] == title for p in listed):
			return []
		def done(frame):
			state = frame["data"].get("page", frame["data"]) if frame["t"] == "snap" else frame["data"]
			return frame["t"] in ("snap", "evt") and frame.get("kind", "changed") == "changed" and state.get("title") == title
		return await self.client.wait_for("page:" + page, done, timeout=LOAD_SECONDS)

	async def call(self, binding, **args):
		return (await self.client.request("plugins.call", {"name": binding, "args": {"wsId": self.ws, "args": args}}, timeout=LOAD_SECONDS))["result"]

	async def test_a_page_loads_with_its_title_console_and_failed_requests(self):
		page = await self.open()
		seen = [("console", e["level"], e["text"]) for e in self.snapshot["console"]] + [("network", e["status"], e["url"]) for e in self.snapshot["network"]]
		def logged(frame):
			if frame["t"] == "evt" and frame["kind"] in ("console", "network"):
				seen.extend((frame["kind"], e.get("level", e.get("status")), e.get("text", e.get("url"))) for e in frame["data"]["entries"])
			texts = " ".join(str(s[2]) for s in seen)
			return "fixture ready" in texts and "fixture boom" in texts and "missing.png" in texts
		if not logged({"t": "snap"}):
			await self.client.wait_for("page:" + page, logged, timeout=LOAD_SECONDS)
		self.assertIn(("console", "warning", "fixture ready"), seen)
		self.assertTrue(any(kind == "console" and level == "error" and "fixture boom" in text for kind, level, text in seen), seen)
		self.assertTrue(any(kind == "network" and level == 404 and text.endswith("/missing.png") for kind, level, text in seen), seen)
		await self.titled(page, "Fixture")
		listed = (await self.client.request("browser.pages", {"wsId": self.ws}))["pages"]
		self.assertEqual([(page, "user", True)], [(p["pageId"], p["opener"], p["live"]) for p in listed])

	async def test_frames_are_pulled_and_input_reaches_the_page(self):
		page = await self.open()
		await self.titled(page, "Fixture")
		frame = await self.client.request("browser.frame", {"pageId": page, "after": 0}, timeout=LOAD_SECONDS)
		self.assertGreater(frame["seq"], 0)
		self.assertEqual("image/jpeg", frame["mime"])
		self.assertEqual(b"\xff\xd8", base64.b64decode(frame["data"])[:2])
		self.assertEqual((800, 600), (frame["width"], frame["height"]))
		mouse = lambda action, x, y: {"type": "mouse", "action": action, "x": x, "y": y, "button": "left", "buttons": 1 if action == "down" else 0, "clickCount": 1}
		await self.client.request("browser.input", {"pageId": page, "events": [mouse("down", 60, 135), mouse("up", 60, 135)]})
		keys = []
		for letter in "hi":
			keys.append({"type": "key", "action": "down", "key": letter, "code": "Key" + letter.upper(), "keyCode": ord(letter.upper()), "text": letter})
			keys.append({"type": "key", "action": "up", "key": letter, "code": "Key" + letter.upper(), "keyCode": ord(letter.upper())})
		keys.append({"type": "text", "text": "!"})
		await self.client.request("browser.input", {"pageId": page, "events": keys})
		await self.client.request("browser.input", {"pageId": page, "events": [mouse("down", 120, 230), mouse("up", 120, 230)]})
		await self.titled(page, "Clicked hi!")
		later = await self.client.request("browser.frame", {"pageId": page, "after": frame["seq"]}, timeout=LOAD_SECONDS)
		self.assertGreater(later["seq"], frame["seq"], "the change painted a new frame")

	async def test_the_ai_reads_and_drives_a_page_through_its_bindings(self):
		opened = await self.call("browser.openPage", url=self.url)
		self.assertIn("Opened pg-", opened["text"])
		page = re.search(r"(pg-[0-9a-f]+)", opened["text"]).group(1)
		await self.client.sub(["page:" + page])
		await self.client.next_frame("page:" + page)
		tree = (await self.call("browser.snapshot", page=page))["text"]
		button = re.search(r'button "Go" \[ref=(e\d+)\]', tree)
		field = re.search(r'textbox "Name" \[ref=(e\d+)\]', tree)
		self.assertTrue(button and field, tree)
		typed = await self.call("browser.act", page=page, action="type", ref=field.group(1), text="AI")
		self.assertIn('typed "AI" into textbox "Name"', typed["text"])
		clicked = await self.call("browser.act", page=page, action="click", ref=button.group(1))
		self.assertIn('clicked button "Go"', clicked["text"])
		actions = await self.client.wait_for("page:" + page, lambda f: f.get("kind") == "action" and f["data"]["kind"] == "click", timeout=LOAD_SECONDS)
		click = actions[-1]["data"]
		self.assertEqual(("ai", 'click button "Go"'), (click["by"], click["label"]))
		self.assertEqual((20, 200, 200, 60), (click["box"]["x"], click["box"]["y"], click["box"]["width"], click["box"]["height"]))
		self.assertEqual({"text": 'Value: "Clicked AI"', "isError": False}, await self.call("browser.eval", page=page, js="document.title"))
		self.assertTrue((await self.call("browser.eval", page=page, js="throw new Error('nope')"))["isError"])
		shot = await self.call("browser.screenshot", page=page)
		path = os.path.join(self.site, shot["image"])
		self.assertTrue(shot["image"].startswith(".ied/local/browser/shots/" + page), shot)
		with open(path, "rb") as f:
			self.assertEqual(b"\x89PNG", f.read(4))
		console = (await self.call("browser.console", page=page))["text"]
		self.assertIn("fixture boom", console)
		self.assertIn("clicked AI", console)
		network = (await self.call("browser.console", page=page, network=True))["text"]
		self.assertIn("404", network)
		with self.assertRaises(RpcFailure) as stale:
			await self.call("browser.act", page=page, action="click", ref="e999")
		self.assertIn("take a new one", stale.exception.message)

	async def test_only_the_web_and_the_workspace_open(self):
		for url, code in (("javascript:alert(1)", "bad_request"), ("file:///C:/Windows/win.ini", "forbidden"), ("chrome://settings", "bad_request")):
			with self.subTest(url=url), self.assertRaises(RpcFailure) as refused:
				await self.client.request("browser.open", {"wsId": self.ws, "url": url})
			self.assertEqual(code, refused.exception.code)
		local = "file:///" + os.path.join(self.site, "index.html").replace("\\", "/")
		page = await self.open(local)
		await self.titled(page, "Fixture")

	async def test_pages_are_kept_across_a_restart(self):
		page = await self.open()
		await self.titled(page, "Fixture")
		self.client.abort()
		self.server.stop(remove_data=False)
		self.server = self.start_server()
		await self.connect()
		listed = (await self.client.request("browser.pages", {"wsId": self.ws}))["pages"]
		self.assertEqual([(page, self.url, False)], [(p["pageId"], p["url"], p["live"]) for p in listed])
		await self.client.sub(["page:" + page])
		await self.client.next_frame("page:" + page)
		frame = await self.client.request("browser.frame", {"pageId": page, "after": 0}, timeout=LOAD_SECONDS)
		self.assertGreater(frame["seq"], 0, "looking at it loads it again")
		await self.titled(page, "Fixture")

	async def test_an_idle_page_leaves_the_browser_and_comes_back(self):
		await self.client.request("settings.set", {"tier": "app", "key": "browser.unloadAfterSeconds", "value": 2})
		page = await self.open()
		await self.titled(page, "Fixture")
		await self.client.wait_for("page:" + page, lambda f: f.get("kind") == "changed" and not f["data"]["live"], timeout=20)
		listed = (await self.client.request("browser.pages", {"wsId": self.ws}))["pages"]
		self.assertEqual([(page, False)], [(p["pageId"], p["live"]) for p in listed], "still listed, not loaded")
		frame = await self.client.request("browser.frame", {"pageId": page, "after": 0}, timeout=LOAD_SECONDS)
		self.assertGreater(frame["seq"], 0, "looking at it loads it again")
		self.assertTrue((await self.client.request("browser.pages", {"wsId": self.ws}))["pages"][0]["live"])

	async def test_a_page_a_page_opens_is_a_page_of_its_own(self):
		page = await self.open()
		await self.titled(page, "Fixture")
		await self.client.sub(["browser:" + self.ws])
		await self.client.next_frame("browser:" + self.ws)
		await self.call("browser.act", page=page, action="click", selector="#pop")
		added = (await self.client.wait_for("browser:" + self.ws, lambda f: f.get("kind") == "page.added", timeout=LOAD_SECONDS))[-1]["data"]
		self.assertEqual((page, "user"), (added["parent"], added["opener"]), added)
		child = added["pageId"]
		await self.client.sub(["page:" + child])
		await self.client.next_frame("page:" + child)
		await self.titled(child, "Fixture")
		listed = (await self.client.request("browser.pages", {"wsId": self.ws}))["pages"]
		self.assertTrue([p for p in listed if p["pageId"] == child and p["url"].endswith("/index.html?opened=1") and p["live"]], listed)

	async def test_closing_removes_the_page(self):
		page = await self.open()
		await self.client.sub(["browser:" + self.ws])
		await self.client.next_frame("browser:" + self.ws)
		await self.client.request("browser.close", {"pageId": page})
		removed = await self.client.wait_for("browser:" + self.ws, lambda f: f.get("kind") == "page.removed", timeout=10)
		self.assertEqual(page, removed[-1]["data"]["pageId"])
		self.assertEqual([], (await self.client.request("browser.pages", {"wsId": self.ws}))["pages"])
		with self.assertRaises(RpcFailure) as gone:
			await self.client.request("browser.frame", {"pageId": page, "after": 0})
		self.assertEqual("not_found", gone.exception.code)


@unittest.skipUnless(browser_installed(), "needs Microsoft Edge or Google Chrome")
class BrowserToolTests(ServerTestCase):
	"""The browser tools in a chat turn (docs/27 "AI tools"), against the fake model: open, act, and a screenshot that
	reaches a model that sees images."""

	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-browser-chat-")
		self.addCleanup(remove_when_released, self.root)
		self.site = os.path.join(self.root, "site")
		os.makedirs(self.site)
		with open(os.path.join(self.site, "index.html"), "w", encoding="utf-8") as f:
			f.write(PAGE)
		self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(QuietHandler, directory=self.site))
		threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
		self.addCleanup(self.httpd.server_close)
		self.addCleanup(self.httpd.shutdown)
		self.url = "http://127.0.0.1:%d/index.html" % self.httpd.server_address[1]
		self.model = chat_checks.FakeModel()
		self.addCleanup(self.model.stop)
		self.server = IedServer(data=os.path.join(self.root, "data"), extra=("--workspace", self.site))
		self.addCleanup(self.server.stop)
		self.assertTrue(self.server.started, self.server.error)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.cookie = self.server.sign_in()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		model = {"endpoint": self.model.url + "/v1", "vision": True, "watchdog": {"prefillBaseS": 2, "idleS": 1, "prefillRate": 1000000}}
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": {"fake": model}})

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def turn(self, chat_id):
		await self.client.request("chat.send", {"chatId": chat_id, "text": "Check the page"})
		frames = await self.client.wait_for("chat:" + chat_id, lambda f: f.get("kind") == "turn.end", timeout=LOAD_SECONDS * 3)
		self.assertEqual("finished", frames[-1]["data"]["stop"], frames[-1]["data"])
		turn = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"][-1]
		return [s for s in turn["segments"] if s["type"] == "tool"]

	async def listed(self, chat_id, wanted, what):
		"""The chat's "Open by this chat" list (docs/04) once `wanted(resources)` holds, from fresh snapshots: the browser
		tells the chat from its worker, so the change may come before or after the turn ends."""
		deadline = time.monotonic() + LOAD_SECONDS
		while True:
			probe = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
			await probe.sub(["chat:" + chat_id])
			resources = (await probe.wait_for("chat:" + chat_id, lambda f: f["t"] == "snap"))[-1]["data"]["resources"]
			probe.abort()
			if wanted(resources):
				return resources
			if time.monotonic() > deadline:
				self.fail("the chat's list is not %s: %r" % (what, resources))
			await asyncio.sleep(0.25)

	async def test_the_ai_opens_acts_on_and_sees_a_page(self):
		created = await self.client.request("chat.create", {"wsId": "w1", "mode": "ask", "permission": "auto"})
		chat_id = created["chatId"]
		await self.client.sub(["chat:" + chat_id])
		await self.client.next_frame("chat:" + chat_id)
		self.model.queue(chat_checks.call_step("browser_open", {"url": self.url}, "c1"), chat_checks.text_step("Opened."))
		opened = (await self.turn(chat_id))[0]
		self.assertIn("Opened pg-", opened["result"]["preview"], opened)
		page = re.search(r"(pg-[0-9a-f]+)", opened["result"]["preview"]).group(1)
		self.model.queue(chat_checks.call_step("browser_act", {"page": page, "action": "click", "selector": "#go"}, "c2"),
			chat_checks.call_step("browser_screenshot", {"page": page}, "c3"), chat_checks.text_step("Seen."))
		acted, shot = await self.turn(chat_id)
		self.assertIn('Done: clicked button "#go"', acted["result"]["preview"])
		self.assertIn("Clicked ", acted["result"]["preview"], "the page's new title")
		self.assertEqual(1, len(shot["result"]["images"]), shot)
		self.assertEqual("image/png", shot["result"]["images"][0]["mime"])
		self.assertTrue(shot["result"]["images"][0]["path"].startswith(".ied/local/browser/shots/" + page), shot)
		sent = self.model.requests()[-1]["body"]["messages"]
		parts = sent[-1]["content"]
		self.assertEqual("user", sent[-1]["role"], "the screenshot follows the step's results")
		self.assertIn("image_url", [p["type"] for p in parts])
		self.assertTrue([p for p in parts if p["type"] == "image_url"][0]["image_url"]["url"].startswith("data:image/png;base64,"))

	async def test_the_chat_lists_the_pages_its_ai_opened(self):
		"""docs/04 "What a chat has open": a page the AI opens is in its chat's list under the page's title, follows a new
		title, and leaves the list when the AI closes it; a page a person opens is in no chat's."""
		created = await self.client.request("chat.create", {"wsId": "w1", "mode": "ask", "permission": "auto"})
		chat_id = created["chatId"]
		await self.client.sub(["chat:" + chat_id])
		await self.client.next_frame("chat:" + chat_id)
		self.model.queue(chat_checks.call_step("browser_open", {"url": self.url}, "c1"), chat_checks.text_step("Opened."))
		opened = (await self.turn(chat_id))[0]
		page = re.search(r"(pg-[0-9a-f]+)", opened["result"]["preview"]).group(1)
		ref = "browser:" + page
		listed = await self.listed(chat_id, lambda r: [x["ref"] for x in r] == [ref], "the page")
		self.assertEqual(("page", "Fixture"), (listed[0]["kind"], listed[0]["title"]), "the page's kind and title")
		self.model.queue(chat_checks.call_step("browser_act", {"page": page, "action": "click", "selector": "#go"}, "c2"), chat_checks.text_step("Clicked."))
		await self.turn(chat_id)
		await self.listed(chat_id, lambda r: len(r) == 1 and r[0]["title"] == "Clicked", "under the page's new title")
		mine = await self.client.request("browser.open", {"wsId": "w1", "url": self.url}, timeout=LOAD_SECONDS)
		self.model.queue(chat_checks.call_step("browser_close", {"page": page}, "c3"), chat_checks.text_step("Closed."))
		await self.turn(chat_id)
		await self.listed(chat_id, lambda r: r == [], "empty once the page closed")
		await self.client.request("browser.close", {"pageId": mine["pageId"]})
