"""Web search's starts (docs/29 "SearXNG"): a start runs on a thread of its own, so a websearch.search binding call
answers within the host's 30 s - "web search is starting" while Tor bootstraps - instead of timing out, and a later
search hears how the start ended. Tor here is a plugin of the check's own that provides tor.proxy and says it is
starting until a file appears; this home has no SearXNG, so the start then fails saying so."""

import json
import os
import shutil
import tempfile
import time

from frames import FrameChecker
from iedclient import Client, RpcFailure
from routed import RoutedClient
from server import REPO, IedServer, ServerTestCase

FAKE_TOR = r'''include "api/Plugin.ch"

namespace FakeTor
{
	IED::Result Proxy(const Serialization::DomNode& args)
	{
		if (FS::FileExists((IED::HomeFolder() + "/tor-ready").CStr())) return IED::Answer(Core::StringView("{\"state\":\"ready\",\"host\":\"127.0.0.1\",\"port\":9}"));
		return IED::Answer(Core::StringView("{\"state\":\"starting\",\"progress\":52,\"summary\":\"Loading relay descriptors\"}"));
	}

	void Activate(IED::PluginContext& context)
	{
		context.Provide(Core::StringView("tor.proxy"), Proxy);
	}
}
'''


def write(path, text):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8", newline="") as f:
		f.write(text if isinstance(text, str) else json.dumps(text, indent="\t"))


class WebSearchStartTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-websearch-")
		self.home = os.path.join(self.root, "home")
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		shutil.copytree(os.path.join(REPO, "plugins", "websearch"), os.path.join(self.home, "plugins", "websearch"))
		tor = os.path.join(self.home, "plugins", "faketor")
		write(os.path.join(tor, "ied-plugin.json"), {
			"manifestVersion": 1, "id": "test.faketor", "name": "Fake Tor", "version": "1.0.0", "ied": {"api": "^1"},
			"runtime": {"kind": "c4", "entry": "FakeTor::Activate", "servers": ["master"]},
			"contributes": {"bindings": {"provides": [{"name": "tor.proxy", "version": "1", "kind": "function"}]}}})
		write(os.path.join(tor, "code", "faketor.cf"), FAKE_TOR)
		self.server = IedServer(extra=("--home", self.home))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def search(self, **args):
		begun = time.monotonic()
		with self.assertRaises(RpcFailure) as refused:
			await self.client.request("plugins.call", {"name": "websearch.search", "args": dict(query="ied", **args)}, timeout=60)
		return refused.exception, time.monotonic() - begun

	async def test_a_binding_call_hears_web_search_is_starting(self):
		refused, seconds = await self.search()
		self.assertIn("web search is starting (waiting for Tor: 52% Loading relay descriptors)", refused.message)
		self.assertTrue(15 < seconds < 29, "its own wait, within the binding's 30 s: %.1f s" % seconds)
		status = (await self.client.request("plugins.call", {"name": "websearch.status", "args": {}}))["result"]
		self.assertEqual("starting", status["state"], "the start goes on")
		refused, seconds = await self.search(wait=1)
		self.assertIn("web search is starting", refused.message)
		self.assertLess(seconds, 5, "a caller can ask to wait less")
		write(os.path.join(self.home, "tor-ready"), "")
		refused, _ = await self.search(wait=10)
		self.assertIn("SearXNG is not installed", refused.message, "how the start ended, once it has")
