"""Layouts per workspace and device class (docs/10 'layout:<wsId>:<deviceClass>', docs/15): kept verbatim, revision-checked.

Tests share one server, so each reads the revision it starts from instead of assuming one.
"""

from harness import ProtocolTest
from iedclient import RpcFailure


def topic_of(ws_id, device_class):
	return "layout:%s:%s" % (ws_id, device_class)


class LayoutTests(ProtocolTest):
	async def workspace(self, client):
		return (await client.request("ws.list", {}))["workspaces"][0]["wsId"]

	async def stored(self, client, ws_id, device_class):
		topic = topic_of(ws_id, device_class)
		await client.sub([topic])
		return (await client.wait_for(topic, lambda f: f["t"] == "snap"))[-1]["data"]

	async def store(self, client, ws_id, device_class, layout):
		current = await self.stored(client, ws_id, device_class)
		params = {"wsId": ws_id, "deviceClass": device_class, "layout": layout, "expectedRevision": current["revision"]}
		return (await client.request("layout.set", params))["revision"]

	async def refused(self, client, params):
		with self.assertRaises(RpcFailure) as caught:
			await client.request("layout.set", params)
		return caught.exception

	async def test_set_keeps_the_document_verbatim_and_tells_every_subscriber(self):
		writer, watcher = await self.client(), await self.client()
		ws_id = await self.workspace(writer)
		before = await self.stored(watcher, ws_id, "tablet")
		document = {"v": 1, "activity": "x", "activities": {"x": {"kind": "tabs", "tabs": ["a", "doc:b.txt"], "active": "a", "well": True}}, "note": "é\U0001F3AE", "sizes": [0.25, 0.75]}
		revision = await self.store(writer, ws_id, "tablet", document)
		self.assertEqual(before["revision"] + 1, revision)
		changed = (await watcher.wait_for(topic_of(ws_id, "tablet"), lambda f: f.get("kind") == "layout.changed"))[-1]["data"]
		self.assertEqual((revision, document, writer.client_id), (changed["revision"], changed["layout"], changed["by"]))
		self.assertEqual({"revision": revision, "layout": document}, await self.stored(await self.client(), ws_id, "tablet"))

	async def test_a_stale_revision_is_a_conflict_naming_the_current_one(self):
		desktop, other = await self.client(), await self.client()
		ws_id = await self.workspace(desktop)
		before = await self.stored(desktop, ws_id, "desktop")
		await self.store(other, ws_id, "desktop", {"v": 1, "by": "other"})
		failure = await self.refused(desktop, {"wsId": ws_id, "deviceClass": "desktop", "layout": {"v": 1}, "expectedRevision": before["revision"]})
		self.assertEqual(("conflict", before["revision"] + 1), (failure.code, failure.data["revision"]))
		retried = await desktop.request("layout.set", {"wsId": ws_id, "deviceClass": "desktop", "layout": {"v": 1, "by": "desktop"}, "expectedRevision": failure.data["revision"]})
		self.assertEqual(before["revision"] + 2, retried["revision"], "the retry on the new revision wins")

	async def test_device_classes_are_independent(self):
		client = await self.client()
		ws_id = await self.workspace(client)
		phone = await self.stored(client, ws_id, "phone")
		await self.store(client, ws_id, "desktop", {"v": 1, "only": "desktop"})
		self.assertEqual(phone, await self.stored(await self.client(), ws_id, "phone"))

	async def test_null_forgets_the_layout(self):
		client = await self.client()
		ws_id = await self.workspace(client)
		await self.store(client, ws_id, "phone", {"v": 1})
		revision = await self.store(client, ws_id, "phone", None)
		self.assertEqual({"revision": revision, "layout": None}, await self.stored(await self.client(), ws_id, "phone"))

	async def test_malformed_requests_are_refused(self):
		client = await self.client()
		ws_id = await self.workspace(client)
		revision = (await self.stored(client, ws_id, "desktop"))["revision"]
		base = {"wsId": ws_id, "deviceClass": "desktop", "layout": {"v": 1}, "expectedRevision": revision}
		cases = [
			("an unknown device class", dict(base, deviceClass="watch"), "bad_request"),
			("a layout that is not an object", dict(base, layout="tabs"), "bad_request"),
			("a layout that is a list", dict(base, layout=[1]), "bad_request"),
			("no layout", {k: v for k, v in base.items() if k != "layout"}, "bad_request"),
			("no expectedRevision", {k: v for k, v in base.items() if k != "expectedRevision"}, "bad_request"),
			("more than 256 KB", dict(base, layout={"v": 1, "pad": "x" * 262144}), "bad_request"),
			("an unknown workspace", dict(base, wsId="w-none"), "not_found"),
		]
		for label, params, code in cases:
			with self.subTest(label):
				self.assertEqual(code, (await self.refused(client, params)).code)
		self.assertEqual(revision, (await self.stored(client, ws_id, "desktop"))["revision"], "nothing was stored")
