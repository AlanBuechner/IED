"""Handshake, liveness, frame hygiene (docs/10 'Frame types', 'Liveness', 'Versioning')."""

import asyncio
import json

import ws
from harness import ProtocolTest
from iedclient import Client


class SessionTests(ProtocolTest):
	async def test_welcome_echoes_identity_and_names_an_epoch(self):
		client = await self.client(device_class="phone")
		welcome = client.welcome
		self.assertEqual(1, welcome["proto"])
		self.assertEqual(client.client_id, welcome["clientId"])
		self.assertTrue(welcome["sessionEpoch"])
		self.assertTrue(welcome["serverVersion"])
		self.assertNotIn("resume", welcome, "no resume requested, so none reported")

	async def test_unsupported_proto_gets_bye_protocol_mismatch(self):
		client = await Client.connect(self.url, self.checker, proto=99)
		self.clients.append(client)
		self.assertIsNone(client.welcome)
		self.assertEqual({"t": "bye", "reason": "protocol-mismatch"}, client.bye)
		await client.wait_closed()

	async def test_first_frame_must_be_hello(self):
		sock = await ws.connect(self.url)
		await sock.send_text(json.dumps({"t": "ping", "ts": 1}))
		with self.assertRaises(ws.ConnectionClosed):
			while True:
				frame = json.loads(await asyncio.wait_for(sock.recv(), 5))
				self.assertNotEqual("welcome", frame["t"])

	async def test_ping_gets_pong_with_same_ts(self):
		client = await self.client()
		await client.send({"t": "ping", "ts": 1729350000000})
		pong = await asyncio.wait_for(client.pongs.get(), 5)
		self.assertEqual({"t": "pong", "ts": 1729350000000}, pong)

	async def test_invalid_req_frame_gets_bad_request(self):
		client = await self.client()
		future = asyncio.get_running_loop().create_future()
		client.pending[41] = future
		await client.send({"t": "req", "id": 41, "method": 7})
		res = await asyncio.wait_for(future, 5)
		self.assertFalse(res["ok"])
		self.assertEqual("bad_request", res["error"]["code"])

	async def test_unknown_method_is_not_found(self):
		client = await self.client()
		res = await client.request_frame("nosuch.method", {})
		self.assertEqual("not_found", res["error"]["code"])

	async def test_silent_socket_is_closed_and_talking_one_is_not(self):
		silent = await self.client()
		talking = await self.client()
		for client in (silent, talking):
			await client.request("testhooks.configure", {"idleTimeoutMs": 400})
		for _ in range(12):
			await talking.send({"t": "ping"})
			await asyncio.sleep(0.1)
		await silent.wait_closed(timeout=5)
		self.assertFalse(talking.closed.is_set())
		await self.barrier(talking)

	async def test_presence_announces_new_clients(self):
		watcher = await self.client()
		await watcher.sub(["server"])
		await watcher.next_frame("server")
		newcomer = await self.client(device_class="tablet")
		frames = await watcher.wait_for("server", lambda f: f.get("kind") == "device.connected" and f["data"].get("clientId") == newcomer.client_id)
		self.assertEqual("tablet", frames[-1]["data"]["deviceClass"])
