"""What a request without a session gets, and the Host and Origin checks (docs/11 'Access defaults')."""

import json

import ws
from server import IedServer, ServerTestCase


class AccessTests(ServerTestCase):
	def setUp(self):
		self.server = IedServer()
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()

	def test_a_foreign_host_is_refused_everywhere(self):
		for path in ("/healthz", "/", "/ws", "/api/auth/state"):
			reply = self.server.request("GET", path, headers={"Host": "attacker.example:%d" % self.server.port})
			self.assertEqual(403, reply.status, path)

	def test_loopback_names_are_hosts(self):
		for host in ("localhost", "127.0.0.1", "[::1]"):
			reply = self.server.request("GET", "/healthz", headers={"Host": "%s:%d" % (host, self.server.port)})
			self.assertEqual(200, reply.status, host)

	def test_healthz_is_open_and_says_little(self):
		reply = self.server.request("GET", "/healthz")
		self.assertEqual(200, reply.status)
		self.assertEqual({"ok", "serverVersion", "sessionEpoch"}, set(json.loads(reply.body)))

	async def test_ws_without_a_session_is_refused_before_the_upgrade(self):
		with self.assertRaises(ws.HandshakeError) as refused:
			await ws.connect(self.server.ws_url)
		self.assertEqual(401, refused.exception.status)

	def test_api_without_a_session_is_401_json(self):
		for method, path in (("PUT", "/api/upload"), ("GET", "/api/blob/b1")):
			reply = self.server.request(method, path, body=b"x" if method == "PUT" else None)
			self.assertEqual(401, reply.status, path)
			self.assertEqual({"error": "unauthenticated"}, json.loads(reply.body))

	def test_a_page_without_a_session_gets_the_login_page(self):
		reply = self.server.request("GET", "/")
		self.assertEqual(401, reply.status)
		self.assertIn("IED - sign in", reply.text())
		self.assertIn("default-src 'self'", reply.header("Content-Security-Policy"))
		self.assertEqual("no-referrer", reply.header("Referrer-Policy"))
		self.assertEqual("nosniff", reply.header("X-Content-Type-Options"))

	def test_the_login_pages_files_are_public_and_nothing_else(self):
		for path in ("/login.html", "/login.js", "/css/theme.css", "/css/login.css"):
			self.assertEqual(200, self.server.request("GET", path).status, path)
		self.assertEqual(401, self.server.request("GET", "/mocks/index.html").status)

	async def test_ws_from_a_foreign_origin_is_refused_even_with_a_session(self):
		cookie = self.server.sign_in()
		with self.assertRaises(ws.HandshakeError) as refused:
			await ws.connect(self.server.ws_url, headers={"Cookie": cookie, "Origin": "http://attacker.example"})
		self.assertEqual(403, refused.exception.status)
		sock = await ws.connect(self.server.ws_url, headers={"Cookie": cookie, "Origin": "http://127.0.0.1:%d" % self.server.port})
		sock.abort()

	def test_auth_posts_from_a_foreign_origin_are_refused(self):
		reply = self.server.request("POST", "/api/auth/login", body=b'{"password":"x"}', headers={"Origin": "http://attacker.example"})
		self.assertEqual(403, reply.status)

	def test_a_wrong_cookie_is_no_session(self):
		cookie = self.server.sign_in()
		name, value = cookie.split("=", 1)
		forged = "%s=%s" % (name, value[:-1] + ("0" if value[-1] != "0" else "1"))
		other_port = "ied-1=%s" % value
		for bad in (forged, other_port, name + "=", "%s=%s" % (name, value.upper())):
			self.assertEqual(401, self.server.request("GET", "/api/blob/b1", headers={"Cookie": bad}).status, bad)
		self.assertEqual(404, self.server.request("GET", "/api/blob/b1", headers={"Cookie": cookie}).status)
