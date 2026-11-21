"""Web search's starts (docs/29 "SearXNG"): a start runs on a thread of its own, so a websearch.search binding call
answers within the host's 30 s - "web search is starting" while Tor bootstraps - instead of timing out, and a later
search hears how the start ended. Tor here is a plugin of the check's own that provides tor.proxy and says it is
starting until a file appears; this home has no SearXNG, so the start then fails saying so.

With IED_SERVER_CHECKS_REAL_WEB=1, RealWebTests also searches and fetches for real with the services of the server's
own home (an install's vendor/: the private Python, SearXNG with its packages, the Tor Expert Bundle), which needs the
network: how a Linux install is checked (docs/29 "On Linux")."""

import json
import os
import shutil
import tempfile
import time
import unittest

from frames import FrameChecker
from iedclient import Client, RpcFailure
from routed import RoutedClient
from server import REPO, IedServer, ServerTestCase
import test_chat as chat_checks

REAL_WEB_ENV = "IED_SERVER_CHECKS_REAL_WEB"

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


@unittest.skipUnless(os.environ.get(REAL_WEB_ENV), "searches and fetches for real: set %s=1 (the server's home has the services; needs the network)" % REAL_WEB_ENV)
class RealWebTests(ServerTestCase):
	"""The services of the server's own home, for real: a search from cold - the plugins starting Tor (its bootstrap) and
	SearXNG under the private Python - that leaves through Tor, then a chat turn's web_search and web_fetch against the
	fake model. web_fetch's curl is a program that does not exist, so only the impersonator (curl_cffi under the private
	Python) can have fetched the page."""

	QUERY = "Tor Project anonymity network"
	PAGE = "https://example.com/"

	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-realweb-")
		self.addCleanup(shutil.rmtree, self.root, True)
		folder = os.path.join(self.root, "folder")
		os.makedirs(folder)
		self.model = chat_checks.FakeModel()
		self.addCleanup(self.model.stop)
		self.server = IedServer(data=os.path.join(self.root, "data"), extra=("--workspace", folder))
		self.addCleanup(self.server.stop)
		self.assertTrue(self.server.started, self.server.error)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.claude.command", "value": chat_checks.SIGNED_OUT_CLAUDE})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": {"fake": {"endpoint": self.model.url + "/v1"}}})
		await self.client.request("settings.set", {"tier": "app", "key": "webfetch.curl", "value": os.path.join(self.root, "no-curl")})

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def search_through_tor(self):
		"""The Search page's search (it waits for a start); new circuits and once more when every engine refused this
		exit."""
		for attempt in range(3):
			begun = time.monotonic()
			found = await self.client.request("websearch.search", {"query": self.QUERY, "count": 5}, timeout=240)
			print("\nsearch %d: %d results in %.1f s, viaTor %s" % (attempt + 1, len(found["results"]), time.monotonic() - begun, found["viaTor"]))
			if found["results"]:
				return found
			await self.client.request("websearch.restart", {"newCircuit": True}, timeout=120)
		return found

	def tool_result(self, call_id):
		"""What the model got back for its tool call `call_id`."""
		for request in self.model.requests():
			for message in request["body"]["messages"]:
				if message.get("role") == "tool" and message.get("tool_call_id") == call_id:
					content = message["content"]
					return content if isinstance(content, str) else "".join(p.get("text", "") for p in content)
		self.fail("the model never got the result of %s" % call_id)

	async def test_a_search_through_tor_and_a_page_fetched_like_a_browser(self):
		tor = (await self.client.request("plugins.call", {"name": "tor.status", "args": {}}))["result"]
		self.assertEqual("stopped", tor["state"], "Tor starts with the first search, not before")
		found = await self.search_through_tor()
		self.assertTrue(found["viaTor"], found["text"])
		self.assertTrue(found["results"], "no engine answered through Tor: " + found["text"])
		self.assertIn("through Tor; ", found["text"])
		tor = (await self.client.request("plugins.call", {"name": "tor.status", "args": {}}))["result"]
		self.assertEqual(("ready", 100), (tor["state"], tor["progress"]), tor)
		status = (await self.client.request("websearch.status", {}))
		self.assertEqual("ready", status["state"], status)
		print("Tor: %s (%s); SearXNG: %s" % (tor["socks"], tor["executable"], status["url"]))
		print(found["text"][:1500])

		created = await self.client.request("chat.create", {"wsId": "w1", "mode": "ask", "permission": "auto"})
		chat_id = created["chatId"]
		await self.client.sub(["chat:" + chat_id])
		await self.client.next_frame("chat:" + chat_id)
		self.model.queue(chat_checks.call_step("web_search", {"query": self.QUERY, "count": 5}, "c1"),
			chat_checks.call_step("web_fetch", {"url": self.PAGE}, "c2"), chat_checks.text_step("Read it."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "Search, then read the page"})
		frames = await self.client.wait_for("chat:" + chat_id, lambda f: f.get("kind") == "turn.end", timeout=300)
		self.assertEqual("finished", frames[-1]["data"]["stop"], frames[-1]["data"])
		turn = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"][-1]
		tools = [s for s in turn["segments"] if s["type"] == "tool"]
		self.assertEqual([("web_search", False), ("web_fetch", False)], [(t["tool"], t["result"]["isError"]) for t in tools], tools)
		searched = self.tool_result("c1")
		print("web_search gave the model:\n" + searched[:1200])
		self.assertIn("through Tor; ", searched)
		fetched = self.tool_result("c2")
		print("web_fetch gave the model:\n" + fetched[:800])
		self.assertIn("Status: 200", fetched)
		self.assertIn("Title: Example Domain", fetched)
		self.assertNotIn("no-curl", fetched, "the impersonator fetched it, not curl")
