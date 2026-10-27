"""The owner password, logins and the throttle (docs/11 'Auth endpoints', 'Rate limit', 'Methods')."""

import json
import os

from frames import FrameChecker
from iedclient import Client, RpcFailure
from routed import RoutedClient
from server import IedServer, ServerTestCase, read_lines

PASSWORD = "correct horse battery"


class PasswordTests(ServerTestCase):
	def setUp(self):
		self.server = IedServer()
		self.assertTrue(self.server.started, self.server.error)
		self.cookie = self.server.sign_in()

	def tearDown(self):
		self.server.stop()

	async def client(self, cookie=None):
		return await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": cookie or self.cookie})

	async def set_password(self, password, current=None):
		client = await self.client()
		try:
			params = {"password": password}
			if current is not None:
				params["current"] = current
			return await client.request("auth.setPassword", params, timeout=30)
		finally:
			client.abort()

	def login(self, password, device=None, extra_headers=None):
		body = {"password": password}
		if device:
			body["device"] = device
		return self.server.request("POST", "/api/auth/login", body=json.dumps(body).encode(), headers=extra_headers)

	def test_without_a_password_login_is_409(self):
		reply = self.login("anything at all")
		self.assertEqual(409, reply.status)
		self.assertEqual({"error": "no-password"}, json.loads(reply.body))

	def test_a_malformed_login_is_400(self):
		for body in (b"", b"{", b'{"password": 7}', b"[]"):
			self.assertEqual(400, self.server.request("POST", "/api/auth/login", body=body).status, body)

	async def test_a_password_has_12_characters(self):
		with self.assertRaises(RpcFailure) as refused:
			await self.set_password("short")
		self.assertEqual("bad_request", refused.exception.code)

	async def test_set_then_login(self):
		await self.set_password(PASSWORD)
		state = json.loads(self.server.request("GET", "/api/auth/state").body)
		self.assertTrue(state["passwordSet"])
		reply = self.login(PASSWORD, device="phone")
		self.assertEqual(200, reply.status)
		device = json.loads(reply.body)["device"]
		self.assertEqual(("phone", "password"), (device["name"], device["kind"]))
		cookie = reply.cookies()[0].split(";", 1)[0]
		client = await self.client(cookie)
		names = sorted(d["name"] for d in (await client.request("auth.devices"))["devices"])
		self.assertEqual(2, len(names))
		self.assertIn("phone", names)
		client.abort()
		wrong = self.login(PASSWORD + "!")
		self.assertEqual(401, wrong.status)
		self.assertEqual({"error": "wrong-password"}, json.loads(wrong.body))
		self.assertEqual([], wrong.cookies())

	async def test_changing_the_password_needs_the_current_one(self):
		await self.set_password(PASSWORD)
		for current in (None, "not the password"):
			with self.assertRaises(RpcFailure) as refused:
				await self.set_password("another long password", current)
			self.assertEqual("forbidden", refused.exception.code)
		await self.set_password("another long password", PASSWORD)
		self.assertEqual(401, self.login(PASSWORD).status)
		self.assertEqual(200, self.login("another long password").status)

	async def test_five_failures_lock_the_address_out(self):
		await self.set_password(PASSWORD)
		for attempt in range(5):
			self.assertEqual(401, self.login("wrong %d" % attempt).status)
		locked = self.login(PASSWORD)
		self.assertEqual(429, locked.status, "locked out: not even the right password is checked")
		body = json.loads(locked.body)
		self.assertEqual("locked", body["error"])
		self.assertTrue(55 <= body["retryAfterS"] <= 60, body)
		self.assertEqual(str(body["retryAfterS"]), locked.header("Retry-After"))

	async def test_the_log_records_without_secrets(self):
		await self.set_password(PASSWORD)
		self.login(PASSWORD)
		for attempt in range(5):
			self.login("wrong %d" % attempt)
		lines = read_lines(os.path.join(self.server.data, "logs", "security.log"))
		events = [json.loads(line)["event"] for line in lines]
		self.assertEqual(["start", "token-exchanged", "password-set", "login", "login-failed", "login-failed", "login-failed", "login-failed", "locked-out"], events)
		text = "\n".join(lines)
		self.assertNotIn(PASSWORD, text)
		self.assertNotIn("wrong 0", text)
		with open(os.path.join(self.server.data, "auth", "owner.json"), encoding="utf-8") as f:
			owner = json.load(f)
		self.assertTrue(owner["password"].startswith("$argon2id$v=19$m=65536,t=3,p=4$"))
