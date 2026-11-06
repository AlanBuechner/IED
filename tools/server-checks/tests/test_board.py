"""The planning board (docs/32): boards made and opened through ied.board/*, changes pushed on the board's topic with
undo by author, the file written one element per line and read again when it changes on disk, images kept beside the
board, the board tools answering through board.query (a picture on disk for board_view), and the tools in a chat turn
against the fake model - the picture reaching a model that sees images, an edit marked as the AI's."""

import asyncio
import json
import os
import struct
import tempfile
import time
import urllib.request
import zlib

from frames import FrameChecker
from routed import RoutedClient
from server import IedServer, ServerTestCase, remove_tree
import test_chat as chat_checks

WAIT = 20


def png(width, height):
	"""A small opaque PNG."""
	raw = b"".join(b"\x00" + bytes([120, 80, 200]) * width for _ in range(height))
	def chunk(kind, data):
		return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
	return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


class BoardTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-board-")
		self.addCleanup(remove_tree, self.root)
		self.ws = os.path.join(self.root, "ws")
		os.makedirs(os.path.join(self.ws, "design"))
		self.server = IedServer(data=os.path.join(self.root, "data"), extra=("--workspace", self.ws))
		self.addCleanup(self.server.stop)
		self.assertTrue(self.server.started, self.server.error)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.cookie = self.server.sign_in()
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def query(self, tool, args, by="ai"):
		return (await self.client.request("plugins.call", {"name": "board.query", "wsId": "w1", "args": {"wsId": "w1", "tool": tool, "args": args, "by": by}}, timeout=WAIT))["result"]

	async def open_board(self, path):
		opened = await self.client.request("ied.board/create", {"wsId": "w1", "path": path, "title": "Shop"}, timeout=WAIT)
		await self.client.sub([opened["topic"]])
		snap = (await self.client.wait_for(opened["topic"], lambda f: f["t"] == "snap", timeout=WAIT))[-1]["data"]
		return opened, snap

	async def test_changes_reach_the_topic_and_undo_by_author(self):
		opened, snap = await self.open_board("design/shop")
		self.assertEqual("design/shop.iedboard", opened["path"])
		self.assertEqual(["Layer 1"], [l["name"] for l in snap["layers"]])
		topic = opened["topic"]
		answer = await self.client.request("ied.board/edit", {"boardId": opened["boardId"], "opId": "o1", "ops": [
			{"op": "add", "ref": "t1", "type": "rect", "x": 0, "y": 0, "text": "API"},
			{"op": "add", "ref": "t2", "type": "cylinder", "x": 300, "y": 0, "text": "DB"},
			{"op": "add", "type": "connector", "from": "@t1", "to": "@t2"}]})
		self.assertEqual({"t1": "e1", "t2": "e2"}, answer["refs"])
		event = (await self.client.wait_for(topic, lambda f: f.get("kind") == "changed", timeout=WAIT))[-1]["data"]
		self.assertEqual("o1", event["opId"])
		self.assertEqual(["e1", "e2", "e3"], [p["el"]["id"] for p in event["put"]])
		self.assertEqual("owner", event["change"]["by"])
		await self.query("edit", {"board": "design/shop.iedboard", "ops": [{"op": "update", "id": "e1", "text": "Gateway"}]})
		ai = (await self.client.wait_for(topic, lambda f: f.get("kind") == "changed" and f["data"]["change"]["by"] == "ai", timeout=WAIT))[-1]["data"]
		self.assertEqual("Gateway", ai["put"][0]["el"]["text"])
		# The owner's undo takes back the owner's change and leaves the AI's.
		await self.client.request("ied.board/undo", {"boardId": opened["boardId"]})
		undone = (await self.client.wait_for(topic, lambda f: f.get("kind") == "changed" and f["data"]["change"].get("undone"), timeout=WAIT))[-1]["data"]
		self.assertEqual({"e1", "e2", "e3"}, set(undone["removed"]))
		await self.client.request("ied.board/redo", {"boardId": opened["boardId"]})
		redone = (await self.client.wait_for(topic, lambda f: f.get("kind") == "changed" and not f["data"]["change"].get("undone") and f["data"]["change"]["id"] == "c1", timeout=WAIT))[-1]["data"]
		self.assertEqual("Gateway", [p["el"] for p in redone["put"] if p["el"]["id"] == "e1"][0]["text"], "the AI's text comes back with it")
		# A refused change says why and changes nothing.
		with self.assertRaises(Exception) as refused:
			await self.client.request("ied.board/edit", {"boardId": opened["boardId"], "ops": [{"op": "update", "id": "e9", "text": "x"}]})
		self.assertIn("no element 'e9'", str(refused.exception))

	async def test_the_file_is_written_and_read_back(self):
		opened, _ = await self.open_board("design/file")
		await self.client.request("ied.board/edit", {"boardId": opened["boardId"], "ops": [{"op": "add", "type": "note", "x": 10, "y": 10, "text": "hello"}]})
		path = os.path.join(self.ws, "design", "file.iedboard")
		deadline = time.monotonic() + WAIT
		while time.monotonic() < deadline:
			with open(path, encoding="utf-8") as f:
				text = f.read()
			if "hello" in text:
				break
			await asyncio.sleep(0.2)
		self.assertIn('{"id":"e1","layer":"L1","type":"note"', text, "one element per line")
		data = json.loads(text)
		data["elements"][0]["text"] = "changed outside"
		with open(path, "w", encoding="utf-8") as f:
			json.dump(data, f)
		reloaded = (await self.client.wait_for(opened["topic"], lambda f: f.get("kind") == "reloaded", timeout=WAIT))[-1]["data"]
		self.assertEqual("changed outside", reloaded["elements"][0]["text"])

	async def test_an_uploaded_image_is_kept_beside_the_board(self):
		opened, _ = await self.open_board("design/pics")
		request = urllib.request.Request("http://%s:%d/w/w1/api/upload" % (self.server.host, self.server.port), data=png(40, 20), method="PUT", headers={"Cookie": self.cookie, "Content-Type": "image/png", "Host": "%s:%d" % (self.server.host, self.server.port), "Origin": "http://%s:%d" % (self.server.host, self.server.port)})
		blob = json.loads(urllib.request.urlopen(request).read())["blobId"]
		stored = await self.client.request("ied.board/image", {"boardId": opened["boardId"], "blobId": blob})
		self.assertTrue(stored["src"].startswith("design/pics.assets/") and stored["src"].endswith(".png"), stored)
		self.assertEqual((40, 20), (stored["iw"], stored["ih"]))
		self.assertTrue(os.path.exists(os.path.join(self.ws, *stored["src"].split("/"))))
		answer = await self.client.request("ied.board/edit", {"boardId": opened["boardId"], "ops": [{"op": "add", "type": "image", "src": stored["src"], "iw": 40, "ih": 20, "x": 0, "y": 0, "w": 80, "h": 40, "crop": [10, 0, 20, 20]}]})
		self.assertEqual("c1", answer["change"]["id"])
		view = await self.query("view", {"board": "design/pics.iedboard"})
		self.assertNotIn("Not drawn", view["text"], "the picture draws the image")

	async def test_the_tools_answer_through_the_binding(self):
		await self.open_board("design/tools")
		made = await self.query("edit", {"board": "design/tools.iedboard", "ops": [{"op": "add", "ref": "a", "type": "rect", "text": "Web"}, {"op": "add", "ref": "b", "type": "rect", "text": "API"}, {"op": "add", "type": "connector", "from": "@a", "to": "@b"}, {"op": "layout", "direction": "LR"}]})
		self.assertIn("Change c1 done", made["text"])
		read = await self.query("read", {"board": "design/tools.iedboard"})
		self.assertIn('e1 rect "Web"', read["text"])
		view = await self.query("view", {"board": "design/tools.iedboard"})
		self.assertTrue(view["image"].startswith(".ied/local/boards/views/"), view)
		mermaid = await self.query("export", {"board": "design/tools.iedboard"})
		self.assertIn("web --> api", mermaid["text"])
		imported = await self.query("import", {"board": "design/tools.iedboard", "text": "flowchart LR\n web[Web] --> api[API]\n api --> db[(DB)]\n"})
		self.assertIn("1 added", imported["text"])
		listed = await self.query("list", {})
		self.assertIn("design/tools.iedboard", listed["text"])


class BoardChatTests(ServerTestCase):
	"""The board tools in a chat turn (docs/32 "AI tools"), against the fake model."""

	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-board-chat-")
		self.addCleanup(remove_tree, self.root)
		self.ws = os.path.join(self.root, "ws")
		os.makedirs(os.path.join(self.ws, "design"))
		self.model = chat_checks.FakeModel()
		self.addCleanup(self.model.stop)
		self.server = IedServer(data=os.path.join(self.root, "data"), extra=("--workspace", self.ws))
		self.addCleanup(self.server.stop)
		self.assertTrue(self.server.started, self.server.error)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		model = {"endpoint": self.model.url + "/v1", "vision": True, "watchdog": {"prefillBaseS": 2, "idleS": 1, "prefillRate": 1000000}}
		await self.client.request("settings.set", {"tier": "app", "key": "ai.models", "value": {"fake": model}})

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def test_the_ai_sees_and_changes_a_board(self):
		await self.client.request("ied.board/create", {"wsId": "w1", "path": "design/plan"})
		created = await self.client.request("chat.create", {"wsId": "w1", "mode": "ask", "permission": "auto"})
		chat_id = created["chatId"]
		await self.client.sub(["chat:" + chat_id])
		await self.client.next_frame("chat:" + chat_id)
		self.model.queue(
			chat_checks.call_step("board_edit", {"board": "design/plan.iedboard", "ops": [{"op": "add", "type": "rect", "text": "Server"}]}, "c1"),
			chat_checks.call_step("board_view", {"board": "design/plan.iedboard"}, "c2"),
			chat_checks.text_step("Drawn."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "Draw the server"})
		frames = await self.client.wait_for("chat:" + chat_id, lambda f: f.get("kind") == "turn.end", timeout=WAIT * 3)
		self.assertEqual("finished", frames[-1]["data"]["stop"], frames[-1]["data"])
		turn = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"][-1]
		edit, view = [s for s in turn["segments"] if s["type"] == "tool"]
		self.assertIn("added e1 rect", edit["result"]["preview"], edit)
		self.assertEqual(1, len(view["result"]["images"]), view)
		self.assertTrue(view["result"]["images"][0]["path"].startswith(".ied/local/boards/views/"), view)
		parts = self.model.requests()[-1]["body"]["messages"][-1]["content"]
		self.assertIn("image_url", [p["type"] for p in parts], "the picture reaches the model")
		read = (await self.client.request("plugins.call", {"name": "board.query", "wsId": "w1", "args": {"wsId": "w1", "tool": "read", "args": {"board": "design/plan.iedboard"}}}))["result"]["text"]
		self.assertIn('e1 rect "Server"', read)
		self.assertIn("(by ai)", read, "the AI's element says so")
