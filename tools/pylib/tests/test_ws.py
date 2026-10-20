import asyncio
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import ws  # noqa: E402


class FramingTests(unittest.TestCase):
	"""Byte-exact examples from RFC 6455 section 5.7 and 1.3."""

	def test_accept_key(self):
		self.assertEqual("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", ws.accept_key("dGhlIHNhbXBsZSBub25jZQ=="))

	def test_unmasked_text(self):
		self.assertEqual(bytes([0x81, 0x05]) + b"Hello", ws.encode_frame(ws.OP_TEXT, b"Hello"))

	def test_mask(self):
		masked = bytes([0x7F, 0x9F, 0x4D, 0x51, 0x58])
		self.assertEqual(masked, ws.apply_mask(b"Hello", bytes([0x37, 0xFA, 0x21, 0x3D])))
		self.assertEqual(b"Hello", ws.apply_mask(masked, bytes([0x37, 0xFA, 0x21, 0x3D])))

	def test_length_encodings(self):
		self.assertEqual(bytes([0x82, 0x7E, 0x01, 0x00]), ws.encode_frame(ws.OP_BINARY, bytes(256))[:4])
		self.assertEqual(bytes([0x82, 0x7F, 0, 0, 0, 0, 0, 1, 0, 0]), ws.encode_frame(ws.OP_BINARY, bytes(65536))[:10])

	def test_read_rfc_masked_and_fragmented(self):
		async def go():
			reader = asyncio.StreamReader()
			reader.feed_data(bytes([0x81, 0x85, 0x37, 0xFA, 0x21, 0x3D, 0x7F, 0x9F, 0x4D, 0x51, 0x58]))
			reader.feed_data(bytes([0x01, 0x03]) + b"Hel" + bytes([0x80, 0x02]) + b"lo")
			first = await ws.read_frame(reader, True, 1 << 20)
			second = await ws.read_frame(reader, False, 1 << 20)
			third = await ws.read_frame(reader, False, 1 << 20)
			return first, second, third
		first, second, third = asyncio.run(go())
		self.assertEqual((True, ws.OP_TEXT, b"Hello"), first)
		self.assertEqual((False, ws.OP_TEXT, b"Hel"), second)
		self.assertEqual((True, ws.OP_CONT, b"lo"), third)

	def test_server_requires_masked_frames(self):
		async def go():
			reader = asyncio.StreamReader()
			reader.feed_data(bytes([0x81, 0x05]) + b"Hello")
			await ws.read_frame(reader, True, 1 << 20)
		with self.assertRaises(ws.ProtocolError):
			asyncio.run(go())

	def test_handshake_validation(self):
		good = {"upgrade": "websocket", "connection": "keep-alive, Upgrade", "sec-websocket-version": "13", "sec-websocket-key": "dGhlIHNhbXBsZSBub25jZQ=="}
		self.assertIn(b"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", ws.server_handshake("GET", good))
		for broken in ({**good, "sec-websocket-key": "short"}, {**good, "upgrade": "h2c"}, {**good, "sec-websocket-version": "8"}):
			with self.assertRaises(ws.HandshakeError):
				ws.server_handshake("GET", broken)


class LoopbackTests(unittest.IsolatedAsyncioTestCase):
	async def asyncSetUp(self):
		self.server_sockets = []
		self.server = await asyncio.start_server(self._serve, "127.0.0.1", 0)
		self.url = "ws://127.0.0.1:%d/ws" % self.server.sockets[0].getsockname()[1]

	async def asyncTearDown(self):
		self.server.close()
		await self.server.wait_closed()

	async def _serve(self, reader, writer):
		head = await reader.readuntil(b"\r\n\r\n")
		(method, _, _), headers = ws.parse_head(head.decode("latin-1"))
		writer.write(ws.server_handshake(method, headers))
		sock = ws.WebSocket(reader, writer, is_client=False, max_message=1 << 20)
		self.server_sockets.append(sock)
		try:
			while True:
				message = await sock.recv()
				if message == "close-please":
					await sock.close(4001, "asked")
					return
				if isinstance(message, bytes):
					await sock.send_bytes(message)
				else:
					await sock.send_text(message)
		except ws.ConnectionClosed:
			pass

	async def test_echo_text_unicode_and_large_binary(self):
		client = await ws.connect(self.url)
		await client.send_text("h\u00e9llo \U0001F600")
		self.assertEqual("h\u00e9llo \U0001F600", await client.recv())
		blob = bytes(range(256)) * 3000
		await client.send_bytes(blob)
		self.assertEqual(blob, await client.recv())
		await client.close()

	async def test_fragmented_message_with_interleaved_ping(self):
		client = await ws.connect(self.url)
		client.writer.write(ws.encode_frame(ws.OP_TEXT, b"frag", mask=True, fin=False))
		client.writer.write(ws.encode_frame(ws.OP_PING, b"p", mask=True))
		client.writer.write(ws.encode_frame(ws.OP_CONT, b"mented", mask=True))
		self.assertEqual("fragmented", await client.recv())
		await client.close()

	async def test_server_close_code_reaches_client(self):
		client = await ws.connect(self.url)
		await client.send_text("close-please")
		with self.assertRaises(ws.ConnectionClosed) as caught:
			await client.recv()
		self.assertEqual(4001, caught.exception.code)

	async def test_invalid_utf8_closes_1007(self):
		client = await ws.connect(self.url)
		await client.send(ws.OP_TEXT, b"\xff\xfe")
		with self.assertRaises(ws.ConnectionClosed) as caught:
			await client.recv()
		self.assertEqual(1007, caught.exception.code)

	async def test_oversized_message_closes_1009(self):
		client = await ws.connect(self.url)
		await client.send_bytes(bytes(2 << 20))
		with self.assertRaises(ws.ConnectionClosed) as caught:
			await client.recv()
		self.assertEqual(1009, caught.exception.code)

	async def test_abort_is_seen_as_1006(self):
		client = await ws.connect(self.url)
		await client.send_text("x")
		await client.recv()
		client.abort()
		await asyncio.sleep(0.05)
		self.assertTrue(self.server_sockets[0].closed)
		self.assertEqual(1006, self.server_sockets[0].close_code)


if __name__ == "__main__":
	unittest.main()
