"""Starts an in-process mock server per test and connects strict protocol clients to it."""

import pathlib
import sys
import unittest

HERE = pathlib.Path(__file__).resolve().parent
TOOLS = HERE.parents[1]
for path in (TOOLS / "pylib", TOOLS / "mock-server", TOOLS / "conformance"):
	if str(path) not in sys.path:
		sys.path.insert(0, str(path))

import mock_server  # noqa: E402
from frames import FrameChecker  # noqa: E402
from iedclient import Client, RpcFailure  # noqa: E402,F401
from state import Config  # noqa: E402


class MockCase(unittest.IsolatedAsyncioTestCase):
	config = {}

	async def asyncSetUp(self):
		options = dict(port=0, time_scale=0.0, quiet=True)
		options.update(self.config)
		self.server, self.listener, self.port = await mock_server.start(Config(**options))
		self.url = "ws://127.0.0.1:%d/ws" % self.port
		self.checker = FrameChecker()
		self.clients = []

	async def asyncTearDown(self):
		for client in self.clients:
			client.abort()
		self.listener.close()
		self.assertEqual([], [v for c in self.clients for v in c.violations])

	async def client(self, **kwargs):
		client = await Client.connect(self.url, self.checker, **kwargs)
		self.clients.append(client)
		return client

	async def subscribed(self, client, topic):
		await client.sub([topic])
		return (await client.next_frame(topic))["data"]

	def kinds(self, frames):
		return [f["kind"] for f in frames if f["t"] == "evt"]
