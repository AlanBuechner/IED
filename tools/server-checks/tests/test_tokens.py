"""Launch tokens and the session cookie (docs/11 'Launch token', 'Exchange', 'Session cookie')."""

import asyncio
import json
import os

import ws
from frames import FrameChecker
from iedclient import Client
from routed import RoutedClient
from server import IedServer, ServerTestCase, read_lines


class TokenTests(ServerTestCase):
	def setUp(self):
		self.server = IedServer()
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()

	async def mint(self, cookie, ttl_ms):
		client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": cookie})
		try:
			return (await client.request("testhooks.mintToken", {"ttlMs": ttl_ms}))["token"]
		finally:
			client.abort()

	def test_the_open_line_carries_a_64_digit_token(self):
		self.assertRegex(self.server.open_line, r"^OPEN http://127\.0\.0\.1:%d/\?t=[0-9a-f]{64}$" % self.server.port)

	def test_exchange_redirects_without_the_token_and_sets_a_strict_cookie(self):
		token = self.server.launch_url.rsplit("=", 1)[1]
		reply = self.server.exchange(token, path="/some/page")
		self.assertEqual(303, reply.status)
		self.assertEqual("/some/page", reply.header("Location"))
		self.assertEqual("no-referrer", reply.header("Referrer-Policy"))
		cookie = reply.cookies()[0]
		self.assertRegex(cookie, r"^ied-%d=[0-9a-f]{64}; " % self.server.port)
		for attribute in ("HttpOnly", "SameSite=Strict", "Path=/", "Max-Age=2592000"):
			self.assertIn(attribute, cookie.split("; "))

	def test_a_token_works_once(self):
		token = self.server.launch_url.rsplit("=", 1)[1]
		self.assertEqual(303, self.server.exchange(token).status)
		again = self.server.exchange(token)
		self.assertEqual(401, again.status, "a spent token is no session: the login page")
		self.assertEqual([], again.cookies())

	async def test_a_token_dies_after_its_time(self):
		cookie = self.server.sign_in()
		short = await self.mint(cookie, 50)
		await asyncio.sleep(0.2)
		self.assertEqual(401, self.server.exchange(short).status)
		fresh = await self.mint(cookie, 60000)
		self.assertEqual(303, self.server.exchange(fresh).status)

	def test_unknown_and_malformed_tokens_are_refused(self):
		for token in ("0" * 64, "zz", "", "%00"):
			self.assertEqual(401, self.server.exchange(token).status, token)

	async def test_the_cookie_opens_a_protocol_session(self):
		cookie = self.server.sign_in()
		client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": cookie})
		self.assertIsNotNone(client.welcome)
		devices = (await client.request("auth.devices"))["devices"]
		self.assertEqual(1, len(devices))
		self.assertEqual("token", devices[0]["kind"])
		self.assertTrue(devices[0]["current"])
		client.abort()

	def test_the_state_endpoint_tells_who_is_signed_in(self):
		self.assertEqual({"authenticated": False, "passwordSet": False, "device": None}, json.loads(self.server.request("GET", "/api/auth/state").body))
		cookie = self.server.sign_in()
		state = json.loads(self.server.request("GET", "/api/auth/state", headers={"Cookie": cookie}).body)
		self.assertTrue(state["authenticated"])
		self.assertEqual({"id", "name", "kind", "created", "lastSeen"}, set(state["device"]))

	def test_devices_file_and_log_never_hold_the_cookie(self):
		cookie = self.server.sign_in()
		value = cookie.split("=", 1)[1]
		with open(os.path.join(self.server.data, "auth", "devices.json"), encoding="utf-8") as f:
			kept = f.read()
		self.assertNotIn(value, kept)
		self.assertIn("secretSha256", kept)
		log = "\n".join(read_lines(os.path.join(self.server.data, "logs", "security.log")))
		self.assertNotIn(value, log)
		events = [json.loads(line)["event"] for line in read_lines(os.path.join(self.server.data, "logs", "security.log"))]
		self.assertEqual(["start", "token-exchanged"], events)
