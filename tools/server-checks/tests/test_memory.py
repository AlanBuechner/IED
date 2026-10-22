"""testhooks.gc (tools/conformance/README.md): full collections and the collector's counts, which the idle soak
(tools/soak) judges leaks by, since private memory also holds garbage not collected yet; and requests over and over
leave no more live than before them. It collects twice: one collection keeps what waits for its finalizer alive, so
after one the last few thousand requests' garbage still counted as live (500 KB per 2000 requests)."""

from frames import FrameChecker
from iedclient import Client
from server import IedServer, ServerTestCase

MB = 1024 * 1024


class MemoryTests(ServerTestCase):
	def setUp(self):
		self.server = IedServer()
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def test_a_collection_and_its_counts(self):
		first = await self.client.request("testhooks.gc")
		second = await self.client.request("testhooks.gc")
		self.assertEqual(first["collections"] + 2, second["collections"], "two collections each")
		self.assertGreater(second["liveBytes"], 0)
		self.assertLessEqual(second["liveBytes"], second["heapBytes"])

	async def test_requests_leave_nothing_live(self):
		for _ in range(200):
			await self.client.request("server.info")
		before = (await self.client.request("testhooks.gc"))["liveBytes"]
		for _ in range(3000):
			await self.client.request("server.info")
			await self.client.request("ws.list")
		after = (await self.client.request("testhooks.gc"))["liveBytes"]
		self.assertLess(after - before, MB // 8, "6000 requests kept %.0f KB live" % ((after - before) / 1024))
