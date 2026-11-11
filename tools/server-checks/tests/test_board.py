"""The planning board (docs/32): boards made and opened through ied.board/*, changes pushed on the board's topic with
undo by author, the file written one element per line and read again when it changes on disk, images kept beside the
board, the history kept across a restart (and dropped when the file changed while the server was down), an empty
.iedboard opening as a new board, the board tools answering through board.query (a picture on disk for board_view),
and the tools in a chat turn against the fake model - the picture reaching a model that sees images, an edit marked as
the AI's."""

import asyncio
import json
import os
import re
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

	def history(self):
		"""The board's kept history (a folder workspace keeps it in the app data), or None before the first save."""
		for folder, _, names in os.walk(self.root):
			for name in names:
				if name.endswith(".history.json"):
					with open(os.path.join(folder, name), encoding="utf-8") as f:
						return json.load(f)
		return None

	async def until(self, check, what):
		deadline = time.monotonic() + WAIT
		while time.monotonic() < deadline:
			if check():
				return
			await asyncio.sleep(0.2)
		self.fail(what)

	async def restart(self, while_down=None):
		self.client.abort()
		self.server.stop(remove_data=False)
		if while_down:
			while_down()
		self.server = IedServer(data=os.path.join(self.root, "data"), extra=("--workspace", self.ws))
		self.addCleanup(self.server.stop)
		self.assertTrue(self.server.started, self.server.error)
		self.cookie = self.server.sign_in()
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})

	async def reopen(self, path):
		opened = await self.client.request("ied.board/open", {"wsId": "w1", "path": path}, timeout=WAIT)
		await self.client.sub([opened["topic"]])
		snap = (await self.client.wait_for(opened["topic"], lambda f: f["t"] == "snap", timeout=WAIT))[-1]["data"]
		return opened, snap

	async def test_the_history_is_kept_across_a_restart(self):
		opened, _ = await self.open_board("design/kept")
		await self.client.request("ied.board/edit", {"boardId": opened["boardId"], "ops": [{"op": "add", "type": "rect", "x": 0, "y": 0, "w": 100, "h": 50, "text": "API"}]})
		await self.client.request("ied.board/edit", {"boardId": opened["boardId"], "ops": [{"op": "move", "ids": ["e1"], "dx": 40, "dy": 0}]})
		await self.query("edit", {"board": "design/kept.iedboard", "ops": [{"op": "update", "id": "e1", "text": "Gateway"}]})
		await self.client.request("ied.board/undo", {"boardId": opened["boardId"]})
		await self.until(lambda: [c["undone"] for c in (self.history() or {}).get("changes", [])] == [False, True, False], "the history is kept with the board's file")
		await self.restart()
		opened, snap = await self.reopen("design/kept.iedboard")
		self.assertEqual(["c1", "c2", "c3"], [c["id"] for c in snap["history"]])
		self.assertEqual([False, True, False], [bool(c.get("undone")) for c in snap["history"]], "with what was undone")
		redone = await self.client.request("ied.board/redo", {"boardId": opened["boardId"]})
		self.assertEqual("c2", redone["change"]["id"], "the owner's redo after the restart")
		read = await self.query("read", {"board": "design/kept.iedboard", "ids": ["e1"]})
		line = [l for l in read["text"].splitlines() if l.lstrip().startswith('e1 rect "Gateway"')]
		at = re.search(r" at (-?[0-9.]+),", line[0]) if line else None
		self.assertTrue(at, read["text"])
		self.assertAlmostEqual(40, float(at.group(1)), delta=3, msg="the move back, the AI's later text kept")
		later = await self.client.request("ied.board/edit", {"boardId": opened["boardId"], "ops": [{"op": "add", "type": "note", "text": "later"}]})
		self.assertEqual("c4", later["change"]["id"], "new changes are numbered after the kept ones")
		await self.until(lambda: any(c["id"] == "c4" for c in (self.history() or {}).get("changes", [])), "the new change is kept")
		path = os.path.join(self.ws, "design", "kept.iedboard")
		def edit_outside():
			with open(path, encoding="utf-8") as f:
				text = f.read()
			with open(path, "w", encoding="utf-8") as f:
				f.write(text.replace("Gateway", "Edge"))
		await self.restart(edit_outside)
		_, snap = await self.reopen("design/kept.iedboard")
		self.assertEqual([], snap["history"], "a file changed while the server was down starts its history empty")
		self.assertIn("Edge", [e.get("text") for e in snap["elements"]])

	async def test_boards_are_grouped_in_the_workspaces_boards_file(self):
		for path in ("design/a", "design/b", "notes/c"):
			await self.client.request("ied.board/create", {"wsId": "w1", "path": path}, timeout=WAIT)
		topic = "ied.board/ws:w1"
		await self.client.sub([topic])
		snap = (await self.client.wait_for(topic, lambda f: f["t"] == "snap", timeout=WAIT))[-1]["data"]
		self.assertEqual(([], ["design/a.iedboard", "design/b.iedboard", "notes/c.iedboard"]), (snap["groups"], [b["path"] for b in snap["boards"]]))
		listed = await self.client.request("ied.board/groups", {"wsId": "w1", "groups": [
			{"name": " Sprint 3 ", "boards": ["notes/c.iedboard", "design/a"]},
			{"name": "Later", "boards": ["design/a.iedboard", "gone/x.iedboard"]},
		]}, timeout=WAIT)
		self.assertEqual([{"name": "Sprint 3", "boards": ["notes/c.iedboard", "design/a.iedboard"]}, {"name": "Later", "boards": []}], listed["groups"],
			"names trimmed, paths made board paths, a board in one group (the first), boards that are gone left out of the list")
		with open(os.path.join(self.ws, ".ied", "boards.json"), encoding="utf-8") as f:
			kept = json.load(f)
		self.assertEqual(["notes/c.iedboard", "design/a.iedboard"], kept["groups"][0]["boards"])
		self.assertEqual(["gone/x.iedboard"], kept["groups"][1]["boards"], "a board that is gone keeps its place in the file")
		frames = await self.client.wait_for(topic, lambda f: f.get("kind") == "boards.changed", timeout=WAIT)
		self.assertEqual(listed["groups"], frames[-1]["data"]["groups"], "every window hears it")
		# Written by hand (a pull, another editor), the file changes the list too.
		with open(os.path.join(self.ws, ".ied", "boards.json"), "w", encoding="utf-8") as f:
			json.dump({"groups": [{"name": "Hand", "boards": ["design/b.iedboard"]}]}, f)
		frames = await self.client.wait_for(topic, lambda f: f.get("kind") == "boards.changed" and f["data"]["groups"][:1] == [{"name": "Hand", "boards": ["design/b.iedboard"]}], timeout=WAIT)
		for bad, why in (([{"name": "", "boards": []}], "a group needs a name"), ([{"name": "A", "boards": []}, {"name": "A", "boards": []}], "two groups have the same name"), ({"name": "A"}, "an array")):
			with self.assertRaises(Exception, msg=why) as refused:
				await self.client.request("ied.board/groups", {"wsId": "w1", "groups": bad}, timeout=WAIT)
			self.assertIn(why, str(refused.exception))

	async def test_an_empty_board_file_opens_as_a_new_board(self):
		path = os.path.join(self.ws, "design", "empty.iedboard")
		with open(path, "w", encoding="utf-8") as f:
			f.write("")
		_, snap = await self.reopen("design/empty.iedboard")
		self.assertEqual(("empty", ["Layer 1"], []), (snap["title"], [l["name"] for l in snap["layers"]], snap["elements"]))
		with open(path, encoding="utf-8") as f:
			self.assertEqual("ied.board/1", json.load(f)["format"], "and the file is a board now")

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
