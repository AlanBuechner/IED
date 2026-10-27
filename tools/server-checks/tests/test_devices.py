"""Device sessions: revoke, logout, persistence, and the LAN bind rule (docs/11 'Methods', 'LAN bind')."""

import json
import os
import socket
import unittest

import ws
from frames import FrameChecker
from iedclient import Client, RpcFailure
from routed import RoutedClient
from server import IedServer, ServerTestCase

PASSWORD = "correct horse battery"


def free_port():
	with socket.socket() as s:
		s.bind(("127.0.0.1", 0))
		return s.getsockname()[1]


class DeviceTests(ServerTestCase):
	def setUp(self):
		self.server = IedServer()
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()

	async def client(self, cookie):
		return await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": cookie})

	async def test_revoke_closes_the_devices_sockets_and_kills_its_cookie(self):
		mine = self.server.sign_in()
		admin = await self.client(mine)
		other_cookie = self.server.exchange((await admin.request("testhooks.mintToken", {"ttlMs": 60000}))["token"]).cookies()[0].split(";", 1)[0]
		other = await self.client(other_cookie)
		devices = (await admin.request("auth.devices"))["devices"]
		victim = [d for d in devices if not d["current"]][0]
		await admin.request("auth.revoke", {"deviceId": victim["id"]})
		await other.wait_closed(10)
		self.assertEqual({"t": "bye", "reason": "auth-revoked"}, other.bye)
		self.assertEqual(1008, other.close_code)
		self.assertEqual([], other.violations + admin.violations)
		self.assertEqual(401, self.server.request("GET", "/api/blob/b1", headers={"Cookie": other_cookie}).status)
		self.assertEqual(1, len((await admin.request("auth.devices"))["devices"]))
		with self.assertRaises(RpcFailure) as missing:
			await admin.request("auth.revoke", {"deviceId": victim["id"]})
		self.assertEqual("not_found", missing.exception.code)
		admin.abort()

	async def test_logout_clears_the_cookie_and_the_session(self):
		cookie = self.server.sign_in()
		reply = self.server.request("POST", "/api/auth/logout", headers={"Cookie": cookie})
		self.assertEqual(200, reply.status)
		self.assertIn("Max-Age=0", reply.cookies()[0])
		self.assertEqual(401, self.server.request("GET", "/api/blob/b1", headers={"Cookie": cookie}).status)
		with self.assertRaises(ws.HandshakeError):
			await ws.connect(self.server.ws_url, headers={"Cookie": cookie})


class PersistenceTests(unittest.TestCase):
	def test_device_sessions_and_the_password_survive_a_restart(self):
		port = free_port()
		first = IedServer(port=port)
		self.assertTrue(first.started, first.error)
		cookie = first.sign_in()
		first.stop(remove_data=False)
		second = IedServer(data=first.data, port=port)
		try:
			self.assertTrue(second.started, second.error)
			self.assertEqual(404, second.request("GET", "/api/blob/b1", headers={"Cookie": cookie}).status, "the old cookie still works")
		finally:
			second.stop(remove_data=True)

	def test_a_corrupt_devices_file_stops_the_server(self):
		first = IedServer()
		first.stop(remove_data=False)
		with open(os.path.join(first.data, "auth", "devices.json"), "w", encoding="utf-8") as f:
			f.write("{not json")
		second = IedServer(data=first.data)
		try:
			self.assertFalse(second.started)
			self.assertEqual(2, second.process.returncode)
			self.assertIn("devices", second.error)
		finally:
			second.stop(remove_data=True)


class LanBindTests(ServerTestCase):
	async def test_listening_beyond_loopback_needs_a_password(self):
		refused = IedServer(host="0.0.0.0")
		try:
			self.assertFalse(refused.started)
			self.assertEqual(2, refused.process.returncode)
			self.assertIn("owner password", refused.error)
		finally:
			refused.stop(remove_data=False)
		local = IedServer(data=refused.data)
		try:
			self.assertTrue(local.started, local.error)
			client = await RoutedClient.connect(local.ws_url, FrameChecker(), headers={"Cookie": local.sign_in()})
			await client.request("auth.setPassword", {"password": PASSWORD}, timeout=30)
			client.abort()
		finally:
			local.stop(remove_data=False)
		lan = IedServer(data=refused.data, host="0.0.0.0")
		try:
			self.assertTrue(lan.started, lan.error)
			self.assertEqual(200, lan.request("GET", "/healthz").status)
		finally:
			lan.stop(remove_data=True)
