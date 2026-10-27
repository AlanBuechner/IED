"""Terminals (docs/10 "Terminals"): a program under a pseudo console on the server - its output on term:<termId>, the
keys of the lease holder only, its exit code; the workspace's list on terms:<wsId>; rename, resize and close; the default
shell runs a command typed into it; the scrollback brings a returning client's screen back."""

import sys

from frames import FrameChecker
from iedclient import Client, RpcFailure
from routed import RoutedClient
from server import IedServer, ServerTestCase

PY = sys.executable
ECHO = "print('ready', flush=True); import sys; line = sys.stdin.readline(); print('got ' + line.strip(), flush=True)"


class TerminalTests(ServerTestCase):
	def setUp(self):
		self.server = IedServer()
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.cookie = self.server.sign_in()
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		self.ws = (await self.client.request("ws.list"))["workspaces"][0]["wsId"]
		await self.client.sub(["terms:" + self.ws])
		await self.client.next_frame("terms:" + self.ws)

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def output_until(self, client, topic, text, timeout=20):
		seen = []
		frames = await client.wait_for(topic, lambda f: (seen.append(f.get("data", {}).get("data", "") if f.get("kind") == "data" else f.get("data", {}).get("scrollback", "")) or True) and text in "".join(seen), timeout=timeout)
		return "".join(seen), frames

	async def test_a_program_runs_under_a_pseudo_console(self):
		term = await self.client.request("term.create", {"wsId": self.ws, "argv": [PY, "-I", "-c", ECHO], "title": "echo", "cols": 100, "rows": 20})
		self.assertEqual(("echo", "shell", True), (term["title"], term["kind"], term["alive"]))
		topic = "term:" + term["termId"]
		await self.client.sub([topic])
		await self.output_until(self.client, topic, "ready")
		await self.client.request("term.input", {"termId": term["termId"], "data": "hello\r"})
		text, _ = await self.output_until(self.client, topic, "got hello")
		self.assertIn("got hello", text)
		exit = (await self.client.wait_for(topic, lambda f: f.get("kind") == "exit", timeout=20))[-1]["data"]
		self.assertEqual(0, exit["code"])
		changed = (await self.client.wait_for("terms:" + self.ws, lambda f: f.get("kind") == "term.changed" and not f["data"]["alive"], timeout=10))[-1]["data"]
		self.assertEqual((term["termId"], 0), (changed["termId"], changed["exitCode"]))
		listed = (await self.client.request("term.list", {"wsId": self.ws}))["terminals"]
		self.assertEqual([(term["termId"], False)], [(t["termId"], t["alive"]) for t in listed], "an exited terminal stays until closed")
		await self.client.request("term.close", {"termId": term["termId"]})
		self.assertEqual([], (await self.client.request("term.list", {"wsId": self.ws}))["terminals"])

	async def test_only_the_lease_holder_types(self):
		term = await self.client.request("term.create", {"wsId": self.ws, "argv": [PY, "-I", "-c", ECHO]})
		other = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		try:
			topic = "term:" + term["termId"]
			await other.sub([topic])
			snapshot = (await other.next_frame(topic))["data"]
			self.assertEqual(term["termId"], snapshot["terminal"]["termId"])
			with self.assertRaises(RpcFailure) as refused:
				await other.request("term.input", {"termId": term["termId"], "data": "x\r"})
			self.assertEqual(("forbidden", "lease"), (refused.exception.code, refused.exception.data["reason"]))
			taken = await other.request("term.takeLease", {"termId": term["termId"]})
			self.assertEqual(taken["holder"], (await other.wait_for(topic, lambda f: f.get("kind") == "lease"))[-1]["data"]["holder"])
			await other.request("term.resize", {"termId": term["termId"], "cols": 90, "rows": 25})
			resized = (await other.wait_for(topic, lambda f: f.get("kind") == "resize"))[-1]["data"]
			self.assertEqual((90, 25), (resized["cols"], resized["rows"]))
			await other.request("term.input", {"termId": term["termId"], "data": "from other\r"})
			text, _ = await self.output_until(other, topic, "got from other")
			self.assertIn("got from other", text)
			self.assertEqual([], other.violations)
		finally:
			other.abort()

	async def test_the_default_shell_and_a_returning_client(self):
		term = await self.client.request("term.create", {"wsId": self.ws})
		topic = "term:" + term["termId"]
		await self.client.sub([topic])
		await self.client.request("term.input", {"termId": term["termId"], "data": "echo ied-marker-%s\r" % term["termId"]})
		await self.output_until(self.client, topic, "ied-marker-%s" % term["termId"], timeout=30)
		renamed = await self.client.request("term.rename", {"termId": term["termId"], "title": "build shell"})
		self.assertEqual("build shell", renamed["title"])
		other = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		try:
			await other.sub([topic])
			snapshot = (await other.next_frame(topic))["data"]
			self.assertIn("ied-marker-%s" % term["termId"], snapshot["scrollback"], "the screen comes back from the scrollback")
			self.assertEqual("build shell", snapshot["terminal"]["title"])
		finally:
			other.abort()
		await self.client.request("term.close", {"termId": term["termId"]})
		removed = (await self.client.wait_for("terms:" + self.ws, lambda f: f.get("kind") == "term.removed", timeout=10))[-1]["data"]
		self.assertEqual(term["termId"], removed["termId"])
		with self.assertRaises(RpcFailure) as gone:
			await self.client.request("term.input", {"termId": term["termId"], "data": "x"})
		self.assertEqual("not_found", gone.exception.code)

	async def test_a_folder_outside_the_workspace_is_refused(self):
		with self.assertRaises(RpcFailure) as escape:
			await self.client.request("term.create", {"wsId": self.ws, "cwd": "../.."})
		self.assertEqual("bad_request", escape.exception.code)
