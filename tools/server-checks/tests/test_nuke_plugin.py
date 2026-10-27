"""The nuke plugin's package fetch (docs/31 section 6): in a trusted workspace that uses Nuke packages it runs
`nuke package status --fetch` when the workspace opens or is trusted and then every nuke.packages.fetchMinutes, so what
that command writes in .ied/local/readonly/nuke.json - a develop clone behind its remote - turns documents read-only
without a Nuke command run by hand. A fetch that fails is told in the workspace's IED output once, and that it works
again once. A fake nuke.command records each run."""

import asyncio
import json
import os
import shutil
import tempfile
import time

from frames import FrameChecker
from routed import RoutedClient
from server import REPO, IedServer, ServerTestCase

PLUGIN = os.path.join(REPO, "plugins", "nuke")
CLONE = "packages/claymore"
REASON = "claymore is 1 commit(s) behind origin/main: pull it"
MANIFEST = ('[workspace]\nname = "Game"\n\n[project]\nname = "Game"\n\n[dependencies]\n'
	'Windowing = { source = "https://example.invalid/claymore.git/Windowing", version = "^0.2" }\n')
FAKE_NUKE = """@echo off
echo %*>> "%~dp0calls.txt"
if exist "%~dp0fail.txt" (
	type "%~dp0fail.txt"
	exit /b 1
)
if exist "%~dp0readonly.json" (
	if not exist ".ied\\local\\readonly" mkdir ".ied\\local\\readonly"
	copy /y "%~dp0readonly.json" ".ied\\local\\readonly\\nuke.json" >nul
)
exit /b 0
"""
FAILED = "its Nuke packages could not be fetched"


def write(path, text):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8", newline="") as f:
		f.write(text)


class NukePluginFetchTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-nuke-plugin-")
		self.home = os.path.join(self.root, "home")
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		for name in ("ied-plugin.json", "settings.schema.json"):
			write(os.path.join(self.home, "plugins", "nuke", name), open(os.path.join(PLUGIN, name), encoding="utf-8").read())
		self.fake = os.path.join(self.root, "fake")
		write(os.path.join(self.fake, "nuke.cmd"), FAKE_NUKE)
		self.game = os.path.join(self.root, "game")
		write(os.path.join(self.game, "nuke.toml"), MANIFEST)
		write(os.path.join(self.game, *CLONE.split("/"), "Windowing", "win.c"), "int win;\n")
		self.data = os.path.join(self.root, "data")
		self.server = None
		self.client = None

	def tearDown(self):
		if self.server:
			self.server.stop(remove_data=False)
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncTearDown(self):
		if self.client:
			self.assertEqual([], self.client.violations)
			self.client.abort()

	async def start(self, minutes=0.05):
		"""The server on the game folder, Nuke being the fake, fetching every `minutes`; the workspace as it opens."""
		write(os.path.join(self.data, "app", "settings.json"), json.dumps({"nuke.command": os.path.join(self.fake, "nuke.cmd"), "nuke.packages.fetchMinutes": minutes}))
		self.server = IedServer(data=self.data, extra=("--home", self.home, "--workspace", self.game))
		self.assertTrue(self.server.started, self.server.error)
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})

	async def restart(self):
		if self.client:
			self.client.abort()
		self.server.stop(remove_data=False)
		await self.start()

	def calls(self):
		try:
			with open(os.path.join(self.fake, "calls.txt"), encoding="utf-8") as f:
				return [line.strip() for line in f if line.strip()]
		except FileNotFoundError:
			return []

	async def until_calls(self, count, seconds=15):
		deadline = time.monotonic() + seconds
		while len(self.calls()) < count:
			if time.monotonic() > deadline:
				self.fail("Nuke ran %d time(s), not %d: %s" % (len(self.calls()), count, self.calls()))
			await asyncio.sleep(0.1)
		return time.monotonic()

	async def trust(self):
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})

	async def log(self):
		await self.client.sub(["out:w1:ied"])
		tail = (await self.client.next_frame("out:w1:ied"))["data"]["tail"]
		await self.client.unsub(["out:w1:ied"])
		return tail

	async def test_it_fetches_when_trusted_then_on_its_timer_and_documents_follow(self):
		await self.start()
		doc = (await self.client.request("doc.open", {"wsId": "w1", "path": CLONE + "/Windowing/win.c"}))["docId"]
		await self.client.sub(["doc:" + doc])
		self.assertFalse((await self.client.next_frame("doc:" + doc))["data"]["readOnly"])
		await asyncio.sleep(2.5)
		self.assertEqual([], self.calls(), "nothing runs in an untrusted workspace")
		write(os.path.join(self.fake, "readonly.json"), json.dumps({"version": 1, "writer": "nuke", "paths": [{"path": CLONE, "reason": REASON}]}))
		await self.trust()
		first = await self.until_calls(1, seconds=5)
		self.assertEqual(["package status --fetch"], self.calls())
		flipped = await self.client.wait_for("doc:" + doc, lambda f: f["t"] == "evt" and f["kind"] == "doc.readOnly")
		self.assertEqual({"docId": doc, "readOnly": True, "reason": REASON}, flipped[-1]["data"], "what the fetch wrote reached the open document")
		second = await self.until_calls(2)
		self.assertGreater(second - first, 2.0, "the next fetch waits for nuke.packages.fetchMinutes (3 s)")
		os.remove(os.path.join(self.fake, "readonly.json"))
		write(os.path.join(self.game, ".ied", "local", "readonly", "nuke.json"), json.dumps({"version": 1, "writer": "nuke", "paths": []}))
		await self.client.wait_for("doc:" + doc, lambda f: f["t"] == "evt" and f["kind"] == "doc.readOnly" and not f["data"]["readOnly"])
		await self.restart()
		before = len(self.calls())
		await self.until_calls(before + 1, seconds=5)   # a trusted workspace fetches as it opens, with no one connected doing anything

	async def test_a_failed_fetch_is_told_once_and_its_recovery_once(self):
		write(os.path.join(self.fake, "fail.txt"), "nuke: error: git fetch in packages/claymore: fatal: unable to access 'https://example.invalid/claymore.git/': Could not resolve host\n")
		await self.start()
		await self.trust()
		await self.until_calls(3)
		log = await self.log()
		self.assertEqual(1, log.count(FAILED), log)
		self.assertIn("Could not resolve host", log)
		os.remove(os.path.join(self.fake, "fail.txt"))
		await self.until_calls(len(self.calls()) + 2)
		log = await self.log()
		self.assertEqual(1, log.count(FAILED), log)
		self.assertEqual(1, log.count("its Nuke packages are fetched again"), log)

	async def test_no_packages_or_fetching_off_runs_nothing(self):
		write(os.path.join(self.game, "nuke.toml"), '[workspace]\nname = "Game"\npackages-dir = "deps"\n')
		await self.start()
		await self.trust()
		await asyncio.sleep(4)
		self.assertEqual([], self.calls(), "a workspace without [dependencies] or a lock has nothing to fetch")
		write(os.path.join(self.game, "packages", "nuke.lock"), "version = 1\n")
		await self.until_calls(1, seconds=6)
		await self.client.request("settings.set", {"tier": "app", "key": "nuke.packages.fetchMinutes", "value": 0})
		await asyncio.sleep(1.5)
		count = len(self.calls())
		await asyncio.sleep(4)
		self.assertEqual(count, len(self.calls()), "0 turns fetching off")
