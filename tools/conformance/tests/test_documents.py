"""Server-owned documents: lease, edits, rebase, conflicts, idempotent retry (docs/10 'Document editing protocol').

Positions are 0-based {line, col}; col counts UTF-16 code units.
"""

from harness import ProtocolTest
from iedclient import RpcFailure, new_id


def change(line, col, end_line, end_col, text):
	return {"range": {"start": {"line": line, "col": col}, "end": {"line": end_line, "col": end_col}}, "text": text}


class DocumentTests(ProtocolTest):
	async def open_scratch(self, client, lines=5):
		scratch = await client.request("testhooks.scratch", {"lines": lines})
		doc = await client.request("doc.open", {"wsId": scratch["wsId"], "path": scratch["path"]})
		return scratch, doc

	async def text_of(self, client, doc_id):
		topic = "doc:" + doc_id
		await client.sub([topic])
		frames = await client.wait_for(topic, lambda f: f["t"] == "snap")
		return frames[-1]["data"]

	async def test_first_opener_holds_the_lease_and_others_are_read_only(self):
		owner, viewer = await self.client(), await self.client(device_class="phone")
		scratch, doc = await self.open_scratch(owner)
		self.assertEqual(owner.client_id, doc["holder"])
		seen = await viewer.request("doc.open", {"wsId": scratch["wsId"], "path": scratch["path"]})
		self.assertEqual((doc["docId"], owner.client_id), (seen["docId"], seen["holder"]))
		with self.assertRaises(RpcFailure) as caught:
			await viewer.request("doc.edit", {"docId": doc["docId"], "baseVersion": doc["version"], "changes": [change(0, 0, 0, 0, "x")]})
		self.assertEqual("forbidden", caught.exception.code)
		self.assertEqual(owner.client_id, caught.exception.data["holder"])

	async def test_edit_is_broadcast_to_every_subscriber_including_the_sender(self):
		owner, viewer = await self.client(), await self.client()
		_, doc = await self.open_scratch(owner)
		topic = "doc:" + doc["docId"]
		for client in (owner, viewer):
			await client.sub([topic])
			await client.next_frame(topic)
		result = await owner.request("doc.edit", {"docId": doc["docId"], "baseVersion": doc["version"], "changes": [change(1, 0, 1, 4, "LINE")]})
		self.assertEqual(doc["version"] + 1, result["version"])
		for client in (owner, viewer):
			event = (await client.wait_for(topic, lambda f: f.get("kind") == "doc.edit"))[-1]["data"]
			self.assertEqual((result["version"], "user:" + owner.client_id), (event["version"], event["origin"]))
		state = await self.text_of(await self.client(), doc["docId"])
		self.assertEqual("line 0\nLINE 1\nline 2\nline 3\nline 4\n", state["text"])
		self.assertTrue(state["dirty"])
		saved = await owner.request("doc.save", {"docId": doc["docId"]})
		event = (await viewer.wait_for(topic, lambda f: f.get("kind") == "doc.saved"))[-1]["data"]
		self.assertEqual(saved["revision"], event["revision"])

	async def test_take_lease_moves_the_lease_and_notifies_everyone(self):
		owner, phone = await self.client(), await self.client(device_class="phone")
		_, doc = await self.open_scratch(owner)
		topic = "doc:" + doc["docId"]
		for client in (owner, phone):
			await client.sub([topic])
			await client.next_frame(topic)
		await phone.request("doc.takeLease", {"docId": doc["docId"]})
		for client in (owner, phone):
			event = (await client.wait_for(topic, lambda f: f.get("kind") == "doc.lease"))[-1]["data"]
			self.assertEqual(phone.client_id, event["holder"])
		with self.assertRaises(RpcFailure):
			await owner.request("doc.edit", {"docId": doc["docId"], "baseVersion": doc["version"], "changes": [change(0, 0, 0, 0, "x")]})
		await phone.request("doc.edit", {"docId": doc["docId"], "baseVersion": doc["version"], "changes": [change(0, 0, 0, 0, "x")]})

	async def test_stale_edit_is_rebased_over_a_server_edit(self):
		owner = await self.client()
		_, doc = await self.open_scratch(owner)
		await self.hooks.request("testhooks.serverEdit", {"docId": doc["docId"], "changes": [change(0, 0, 0, 0, "inserted\nlines\n")]})
		result = await owner.request("doc.edit", {"docId": doc["docId"], "baseVersion": doc["version"], "changes": [change(3, 0, 3, 6, "LINE THREE"), change(4, 6, 4, 6, "!")]})
		self.assertTrue(result["rebased"])
		self.assertEqual(doc["version"] + 2, result["version"])
		self.assertEqual(5, result["changes"][0]["range"]["start"]["line"])
		state = await self.text_of(owner, doc["docId"])
		self.assertEqual("inserted\nlines\nline 0\nline 1\nline 2\nLINE THREE\nline 4!\n", state["text"])

	async def test_overlapping_stale_edit_conflicts_then_resync_succeeds(self):
		owner = await self.client()
		_, doc = await self.open_scratch(owner)
		server_edit = await self.hooks.request("testhooks.serverEdit", {"docId": doc["docId"], "changes": [change(2, 0, 2, 6, "ai line")], "origin": "ai:c1"})
		with self.assertRaises(RpcFailure) as caught:
			await owner.request("doc.edit", {"docId": doc["docId"], "baseVersion": doc["version"], "changes": [change(2, 2, 2, 4, "NE")]})
		self.assertEqual("conflict", caught.exception.code)
		self.assertEqual(server_edit["version"], caught.exception.data["version"])
		state = await self.text_of(owner, doc["docId"])
		await owner.request("doc.edit", {"docId": doc["docId"], "baseVersion": state["version"], "changes": [change(2, 0, 2, 2, "an")]})
		self.assertEqual("line 0\nline 1\nan line\nline 3\nline 4\n", (await self.text_of(await self.client(), doc["docId"]))["text"])

	async def test_edit_retried_after_reconnect_applies_once(self):
		owner = await self.client()
		_, doc = await self.open_scratch(owner)
		op = new_id("op")
		params = {"docId": doc["docId"], "baseVersion": doc["version"], "changes": [change(0, 0, 0, 0, "once ")]}
		first = await owner.request("doc.edit", params, op_id=op)
		owner.abort()
		again = await self.reconnect(owner, resume=False)
		self.assertEqual(first, await again.request("doc.edit", params, op_id=op))
		self.assertEqual("once line 0\n", (await self.text_of(again, doc["docId"]))["text"][:12])

	async def test_columns_count_utf16_code_units(self):
		owner = await self.client()
		scratch = await owner.request("testhooks.scratch", {"lines": 0})
		await owner.request("fs.write", {"wsId": scratch["wsId"], "path": scratch["path"], "text": "a\U0001F3AEb\n"})
		doc = await owner.request("doc.open", {"wsId": scratch["wsId"], "path": scratch["path"]})
		await owner.request("doc.edit", {"docId": doc["docId"], "baseVersion": doc["version"], "changes": [change(0, 3, 0, 3, "X")]})
		self.assertEqual("a\U0001F3AEXb\n", (await self.text_of(owner, doc["docId"]))["text"])
		with self.assertRaises(RpcFailure) as caught:
			await owner.request("doc.edit", {"docId": doc["docId"], "baseVersion": doc["version"] + 1, "changes": [change(0, 2, 0, 2, "Y")]})
		self.assertEqual("bad_request", caught.exception.code, "a column inside a surrogate pair is rejected")

	async def test_file_write_is_revision_checked(self):
		client = await self.client()
		scratch = await client.request("testhooks.scratch", {"lines": 1})
		read = await client.request("fs.read", {"wsId": scratch["wsId"], "path": scratch["path"]})
		written = await client.request("fs.write", {"wsId": scratch["wsId"], "path": scratch["path"], "text": "new\n", "expectedRevision": read["revision"]})
		with self.assertRaises(RpcFailure) as caught:
			await client.request("fs.write", {"wsId": scratch["wsId"], "path": scratch["path"], "text": "lost\n", "expectedRevision": read["revision"]})
		self.assertEqual("conflict", caught.exception.code)
		self.assertEqual(written["revision"], caught.exception.data["revision"])
