"""Base test case: the server URL comes from IED_CONFORMANCE_URL; every server frame must be schema-valid."""

import asyncio
import os
import unittest

from frames import FrameChecker
from iedclient import Client

URL_ENV = "IED_CONFORMANCE_URL"


class ProtocolTest(unittest.IsolatedAsyncioTestCase):
	async def asyncSetUp(self):
		asyncio.get_running_loop().slow_callback_duration = 60   # debug mode need not report slow test steps
		self.url = os.environ.get(URL_ENV)
		if not self.url:
			self.fail("%s is not set; run tools/conformance/run_conformance.py" % URL_ENV)
		self.checker = FrameChecker()
		self.clients = []
		self.hooks = await self.client()

	async def asyncTearDown(self):
		for client in self.clients:
			client.abort()
		await asyncio.sleep(0)
		violations = [v for c in self.clients for v in c.violations]
		self.assertEqual([], violations, "server sent frames that break protocol v1")

	async def client(self, **kwargs):
		client = await Client.connect(self.url, self.checker, **kwargs)
		self.clients.append(client)
		self.assertIsNotNone(client.welcome, "no welcome (bye=%r)" % client.bye)
		return client

	async def reconnect(self, old, resume=True, **kwargs):
		"""Opens a new socket with the same client identity, optionally resuming the old socket's cursors."""
		resume_info = {"sessionEpoch": old.welcome["sessionEpoch"], "cursors": old.cursors()} if resume else None
		return await self.client(client_id=old.client_id, device_id=old.device_id, resume=resume_info, **kwargs)

	async def new_topic(self, **options):
		return (await self.hooks.request("testhooks.topic", options))["topic"]

	async def emit(self, topic, count, **options):
		return await self.hooks.request("testhooks.emit", dict(options, topic=topic, count=count), timeout=60)

	async def barrier(self, client):
		"""A ping round trip: frames queued for the client before the ping have arrived when this returns."""
		await client.send({"t": "ping", "ts": 1})
		await asyncio.wait_for(client.pongs.get(), 10)

	def seqs(self, frames):
		return [f["seq"] for f in frames if f["t"] == "evt"]
