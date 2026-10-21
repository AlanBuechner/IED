"""The mock's fixture behavior: scripted chat, approvals, task runs, output, diagnostics, terminal, layout, plugins, fs."""

import asyncio
import json

from mockcase import MockCase, RpcFailure


class ChatTests(MockCase):
	async def test_default_script_streams_segments_tools_and_ends_finished(self):
		client = await self.client()
		snap = await self.subscribed(client, "chat:c1")
		self.assertEqual(2, len(snap["turns"]))
		result = await client.request("chat.send", {"chatId": "c1", "text": "how does the player move?"}, op_id="op-1")
		frames = await client.wait_for("chat:c1", lambda f: f.get("kind") == "turn.end")
		kinds = self.kinds(frames)
		self.assertEqual("turn.start", kinds[0])
		for kind in ("segment.start", "segment.delta", "segment.end", "tool.state", "tasks.changed"):
			self.assertIn(kind, kinds)
		self.assertEqual({"turnId": result["turnId"], "stop": "finished"}, {k: frames[-1]["data"][k] for k in ("turnId", "stop")})
		text = "".join(f["data"].get("text", "") for f in frames if f.get("kind") == "segment.delta")
		self.assertIn("explicit Euler integration", text)
		again = await client.request("chat.send", {"chatId": "c1", "text": "how does the player move?"}, op_id="op-1")
		self.assertEqual(result, again, "a retried send with the same opId is not sent twice")

	async def test_snapshot_mid_stream_plus_later_deltas_rebuilds_the_text(self):
		client = await self.client()
		await client.sub(["chat:c2"])
		self.server.clock.scale = 1.0
		await client.request("chat.send", {"chatId": "c2", "text": "write a long essay", "mockScript": "long"})
		await client.wait_for("chat:c2", lambda f: f.get("kind") == "segment.delta" and f["data"]["segId"] != "s1" and len(f["data"]["text"]) > 0 and f["seq"] > 30, timeout=10)
		late = await self.client(device_class="phone")
		await late.sub(["chat:c2"])
		snap = (await late.next_frame("chat:c2"))["data"]
		self.server.clock.scale = 0.0
		frames = [{"t": "snap", "data": snap}] + await late.wait_for("chat:c2", lambda f: f.get("kind") == "turn.end", timeout=20)
		turn = snap["turns"][-1]
		self.assertIsNone(turn["stop"], "the snapshot was taken mid-turn")
		texts = {s["segId"]: s.get("text", "") for s in turn["segments"]}
		for frame in frames[1:]:
			if frame.get("kind") == "segment.start":
				texts[frame["data"]["segId"]] = ""
			if frame.get("kind") == "segment.delta":
				texts[frame["data"]["segId"]] += frame["data"]["text"]
		self.assertEqual(40, list(texts.values())[-1].count("The render loop owns the frame"))

	async def test_busy_while_running_and_stop_cancels(self):
		client = await self.client()
		await client.sub(["chat:c2"])
		await client.request("chat.send", {"chatId": "c2", "text": "x", "mockScript": "long"})
		with self.assertRaises(RpcFailure) as caught:
			await client.request("chat.send", {"chatId": "c2", "text": "y"})
		self.assertEqual("busy", caught.exception.code)
		await client.request("chat.stop", {"chatId": "c2"})
		frames = await client.wait_for("chat:c2", lambda f: f.get("kind") == "turn.end")
		self.assertEqual("user_cancelled", frames[-1]["data"]["stop"])

	async def test_cancel_frame_on_the_send_request_stops_the_turn(self):
		client = await self.client()
		await client.sub(["chat:c2"])
		req_id, future, _ = client.start_request("chat.send", {"chatId": "c2", "text": "x", "mockScript": "long"})
		await asyncio.wait_for(future, 5)
		await client.send({"t": "cancel", "id": req_id})
		frames = await client.wait_for("chat:c2", lambda f: f.get("kind") == "turn.end")
		self.assertEqual("user_cancelled", frames[-1]["data"]["stop"])

	async def test_approval_is_answered_once_across_devices(self):
		desktop, phone = await self.client(), await self.client(device_class="phone")
		for client in (desktop, phone):
			await self.subscribed(client, "chat:c1")
		await desktop.request("chat.send", {"chatId": "c1", "text": "please run the tests"})
		request = (await phone.wait_for("chat:c1", lambda f: f.get("kind") == "approval.request"))[-1]["data"]
		await phone.request("chat.approve", {"chatId": "c1", "approvalId": request["approvalId"], "decision": "allow"})
		with self.assertRaises(RpcFailure) as caught:
			await desktop.request("chat.approve", {"chatId": "c1", "approvalId": request["approvalId"], "decision": "deny"})
		self.assertEqual(("conflict", "allow", phone.client_id), (caught.exception.code, caught.exception.data["decision"], caught.exception.data["by"]))
		frames = await desktop.wait_for("chat:c1", lambda f: f.get("kind") == "turn.end")
		states = [f["data"]["state"] for f in frames if f.get("kind") == "tool.state"]
		self.assertEqual(["awaiting_approval", "running", "ok"], states)

	async def test_error_script_ends_with_error_and_reason(self):
		client = await self.client()
		await client.sub(["chat:c2"])
		await client.request("chat.send", {"chatId": "c2", "text": "this will fail"})
		end = (await client.wait_for("chat:c2", lambda f: f.get("kind") == "turn.end"))[-1]["data"]
		self.assertEqual(("error", "http_error"), (end["stop"], end["error"]["reason"]))

	async def test_history_pages_older_turns(self):
		client = await self.client()
		page = await client.request("chat.history", {"chatId": "c1", "limit": 1})
		self.assertEqual((1, True), (len(page["turns"]), page["hasMore"]))
		older = await client.request("chat.history", {"chatId": "c1", "before": page["turns"][0]["turnId"], "limit": 5})
		self.assertEqual((1, False), (len(older["turns"]), older["hasMore"]))

	async def test_restart_marks_running_turn_interrupted(self):
		client = await self.client()
		await client.request("chat.send", {"chatId": "c2", "text": "x", "mockScript": "long"})
		await self.server.restart()
		again = await self.client()
		snap = await self.subscribed(again, "chat:c2")
		self.assertEqual("interrupted", snap["turns"][-1]["stop"])


class TaskTests(MockCase):
	async def run_task(self, client, task, params=None):
		await self.subscribed(client, "out:w1:build")
		result = await client.request("task.run", {"wsId": "w1", "task": task, "params": params or {}})
		self.assertEqual({result["topic"]: "ok"}, await client.sub([result["topic"]], {result["topic"]: 0}))
		return await client.wait_for(result["topic"], lambda f: f.get("kind") == "run.end")

	async def test_build_streams_ansi_output_mirrors_to_output_and_sets_diagnostics(self):
		client = await self.client()
		await self.subscribed(client, "diag:w1")
		frames = await self.run_task(client, "build")
		self.assertEqual("step.start", frames[0]["kind"])
		output = "".join(f["data"]["text"] for f in frames if f.get("kind") == "output")
		self.assertIn("\x1b[32mBuild succeeded\x1b[0m", output)
		self.assertEqual(("succeeded", 0), (frames[-1]["data"]["status"], frames[-1]["data"]["exitCode"]))
		mirrored = await client.wait_for("out:w1:build", lambda f: "Build succeeded" in f.get("data", {}).get("text", ""))
		self.assertEqual("clear", self.kinds(mirrored)[0])
		diag = (await client.wait_for("diag:w1", lambda f: f.get("kind") == "diag.set" and f["data"]["items"]))[-1]["data"]
		self.assertEqual(("src/engine/render.c", 2, 8), (diag["path"], diag["items"][0]["severity"], diag["items"][0]["range"]["start"]["line"]))

	async def test_broken_build_fails_with_an_error_diagnostic(self):
		client = await self.client()
		await self.subscribed(client, "diag:w1")
		frames = await self.run_task(client, "build-broken")
		self.assertEqual(("failed", 1), (frames[-1]["data"]["status"], frames[-1]["data"]["exitCode"]))
		errors = await client.wait_for("diag:w1", lambda f: f.get("kind") == "diag.set" and any(i["severity"] == 1 for i in f["data"]["items"]))
		self.assertEqual("src/game/player.c", errors[-1]["data"]["path"])

	async def test_flood_task_delivers_every_line(self):
		client = await self.client()
		frames = await self.run_task(client, "flood", {"lines": 3000})
		text = "".join(f["data"]["text"] for f in frames if f.get("kind") == "output")
		self.assertEqual(3000, text.count("flood line"))

	async def test_cancel_task(self):
		client = await self.client()
		self.server.clock.scale = 1.0
		result = await client.request("task.run", {"wsId": "w1", "task": "run"})
		await client.sub([result["topic"]])
		await client.request("task.cancel", {"runId": result["runId"]})
		end = (await client.wait_for(result["topic"], lambda f: f.get("kind") == "run.end"))[-1]["data"]
		self.assertEqual("cancelled", end["status"])

	async def test_unknown_task_and_bad_params(self):
		client = await self.client()
		for params, code in (({"wsId": "w1", "task": "nope"}, "not_found"), ({"wsId": "w1", "task": "flood", "params": {"lines": "many"}}, "bad_request"), ({"wsId": "zz", "task": "build"}, "not_found")):
			with self.assertRaises(RpcFailure) as caught:
				await client.request("task.run", params)
			self.assertEqual(code, caught.exception.code)


class TerminalLayoutFsTests(MockCase):
	async def test_terminal_echo_commands_and_lease(self):
		owner, phone = await self.client(), await self.client(device_class="phone")
		term = await owner.request("term.create", {"wsId": "w1", "cols": 80, "rows": 24})
		topic = "term:" + term["termId"]
		snap = await self.subscribed(owner, topic)
		self.assertIn("mock", snap["scrollback"])
		await owner.request("term.input", {"termId": term["termId"], "data": "echo hi\r"})
		frames = await owner.wait_for(topic, lambda f: "hi\r\n" in f["data"].get("data", "") and f["data"]["data"] != "i")
		self.assertTrue(frames)
		with self.assertRaises(RpcFailure) as caught:
			await phone.request("term.input", {"termId": term["termId"], "data": "x"})
		self.assertEqual("forbidden", caught.exception.code)
		await phone.request("term.takeLease", {"termId": term["termId"]})
		await phone.request("term.input", {"termId": term["termId"], "data": "exit\r"})
		end = (await owner.wait_for(topic, lambda f: f.get("kind") == "exit"))[-1]["data"]
		self.assertEqual(0, end["code"])

	async def test_layouts_start_empty_for_every_device_class(self):
		client = await self.client()
		for device_class in ("desktop", "tablet", "phone"):
			self.assertEqual({"revision": 0, "layout": None}, await self.subscribed(client, "layout:w1:%s" % device_class))

	async def test_plugins_are_the_repository_built_ins_with_their_ui_modules(self):
		client = await self.client()
		listed = (await self.subscribed(client, "plugins"))["plugins"]
		contributions = (await client.request("plugins.contributions", {"wsId": "w1"}))["plugins"]
		loaded = [p["id"] for p in listed if p["state"] == "loaded"]
		self.assertEqual(loaded, [p["id"] for p in contributions])
		for plugin in contributions:
			if "ui" in plugin:
				self.assertTrue(plugin["ui"].startswith("/plugins/%s/" % plugin["id"]), plugin["ui"])

	async def test_fs_tree_operations_publish_tree_changes(self):
		client = await self.client()
		await self.subscribed(client, "ws:w1")
		root = await client.request("fs.list", {"wsId": "w1"})
		names = [e["name"] for e in root["entries"]]
		self.assertEqual(["assets", "docs", "scratch", "src", "tests"], names[:5], "directories first")
		await client.request("fs.write", {"wsId": "w1", "path": "notes/todo.txt", "text": "a\n", "expectedRevision": 0})
		await client.request("fs.rename", {"wsId": "w1", "from": "notes/todo.txt", "to": "notes/done.txt"})
		await client.request("fs.delete", {"wsId": "w1", "path": "notes/done.txt"})
		frames = await client.wait_for("ws:w1", lambda f: f.get("kind") == "tree.changed" and f["data"]["changes"][0]["change"] == "deleted" and f["data"]["changes"][0]["path"] == "notes/done.txt")
		self.assertEqual(3, len([f for f in frames if f.get("kind") == "tree.changed"]))
		hits = await client.request("fs.search", {"wsId": "w1", "query": "vec2_add"})
		self.assertIn("src/engine/math.h", {m["path"] for m in hits["matches"]})
		for path in ("../outside.txt", "C:/Windows/win.ini"):
			with self.assertRaises(RpcFailure) as caught:
				await client.request("fs.read", {"wsId": "w1", "path": path})
			self.assertEqual("bad_request", caught.exception.code)

	async def test_disk_write_reloads_clean_doc_and_flags_dirty_doc(self):
		client = await self.client()
		doc = await client.request("doc.open", {"wsId": "w1", "path": "README.md"})
		topic = "doc:" + doc["docId"]
		await self.subscribed(client, topic)
		await client.request("fs.write", {"wsId": "w1", "path": "README.md", "text": "# reloaded\n"})
		edit = (await client.wait_for(topic, lambda f: f.get("kind") == "doc.edit"))[-1]["data"]
		self.assertEqual("disk", edit["origin"])
		change = {"range": {"start": {"line": 0, "col": 0}, "end": {"line": 0, "col": 0}}, "text": "x"}
		await client.request("doc.edit", {"docId": doc["docId"], "baseVersion": edit["version"], "changes": [change]})
		await client.request("fs.write", {"wsId": "w1", "path": "README.md", "text": "# again\n"})
		flagged = (await client.wait_for(topic, lambda f: f.get("kind") == "doc.diskChanged"))[-1]["data"]
		self.assertEqual(doc["docId"], flagged["docId"])


class HttpTests(MockCase):
	config = {"allow_origins": ["http://127.0.0.1:9999"]}

	async def http(self, request, head_only=False):
		reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
		writer.write(request)
		data = await (reader.readuntil(b"\r\n\r\n") if head_only else reader.read())
		writer.close()
		head, _, body = data.partition(b"\r\n\r\n")
		return int(head.split(b" ")[1]), body

	async def test_healthz_blob_roundtrip_and_console(self):
		status, body = await self.http(b"GET /healthz HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
		self.assertEqual((200, self.server.epoch), (status, json.loads(body)["sessionEpoch"]))
		payload = b"\x00\x01binary\xff"
		status, body = await self.http(b"PUT /api/upload HTTP/1.1\r\nHost: localhost\r\nContent-Type: image/png\r\nContent-Length: %d\r\n\r\n" % len(payload) + payload)
		blob = json.loads(body)["blobId"]
		self.assertEqual(201, status)
		self.assertEqual((200, payload), await self.http(b"GET /api/blob/%s HTTP/1.1\r\nHost: localhost\r\n\r\n" % blob.encode()))
		status, body = await self.http(b"GET /_mock/ HTTP/1.1\r\nHost: localhost\r\n\r\n")
		self.assertEqual(200, status)
		self.assertIn(b"<title>", body)

	async def test_dns_rebinding_host_and_cross_origin_socket_are_refused(self):
		status, _ = await self.http(b"GET /healthz HTTP/1.1\r\nHost: evil.example:8765\r\n\r\n")
		self.assertEqual(403, status)
		upgrade = b"GET /ws HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nOrigin: %s\r\n\r\n"
		self.assertEqual(403, (await self.http(upgrade % b"http://evil.example"))[0])
		self.assertEqual(101, (await self.http(upgrade % b"http://127.0.0.1:9999", head_only=True))[0])

	async def test_static_root_cannot_be_escaped(self):
		status, _ = await self.http(b"GET /_mock/../state.py HTTP/1.1\r\nHost: localhost\r\n\r\n")
		self.assertIn(status, (403, 404))
