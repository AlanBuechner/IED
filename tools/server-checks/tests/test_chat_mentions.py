"""Mentions and attachments (docs/04 "Attachments and @-mentions"): chat.send's mentions read when the turn starts - a
file, a folder, the Error List, an output source, a terminal (the terminal plugin's terminal.text binding), the working
tree's diff (the git plugin's git.workingDiff) and another chat - into the user message the model gets; attachments
copied out of their uploads into the chat's media folder, an image shown to a model that sees images and every other
file named for fs_read; a queued message keeps them, and so does one sent to a running turn, read when it gets it."""

import base64
import json
import os
import shutil
import subprocess
import sys

from test_chat import TURN_SECONDS, ChatCase, call_step, text_step, write
from iedclient import RpcFailure
from server import REPO, local_state

PNG = b"\x89PNG\r\n\x1a\n" + bytes(range(16))
FAILING = "import sys; sys.stdout.write('\\x1b[31mbuild failed\\x1b[0m: 2 errors\\r\\n'); sys.stdout.flush()"


def read_manifest(name):
	with open(os.path.join(REPO, "plugins", name, "ied-plugin.json"), encoding="utf-8") as f:
		return json.loads("".join(line for line in f if not line.lstrip().startswith("//")))


def add_builtin(home, name):
	"""A built-in plugin's server side in `home`: its entry, permissions, settings, output sources and bindings."""
	manifest = read_manifest(name)
	plugin = os.path.join(home, "plugins", name)
	os.makedirs(plugin, exist_ok=True)
	shutil.copy(os.path.join(REPO, "plugins", name, "settings.schema.json"), plugin)
	runtime = {"kind": "builtin", "entry": manifest["runtime"]["entry"]}
	kept = {key: manifest["contributes"][key] for key in ("settings", "bindings", "outputSources") if key in manifest["contributes"]}
	write(os.path.join(plugin, "ied-plugin.json"), json.dumps({"manifestVersion": 1, "id": manifest["id"], "name": manifest["name"], "version": manifest["version"],
		"ied": {"api": "^1"}, "runtime": runtime, "permissions": manifest["permissions"], "contributes": kept}))


def git(folder, *arguments):
	subprocess.run(["git", "-c", "user.name=IED", "-c", "user.email=ied@example.invalid", *arguments], cwd=folder, check=True, capture_output=True)


class MentionTests(ChatCase):
	def start_server(self):
		add_builtin(self.home, "terminal")
		add_builtin(self.home, "git")
		git(self.repo, "init", "-q")
		git(self.repo, "add", ".")
		git(self.repo, "commit", "-q", "-m", "First")
		write(os.path.join(self.repo, "src", "main.c"), "int main(void)\n{\n\treturn 1;\n}\n")
		write(os.path.join(self.repo, "notes.txt"), "remember\n")
		return super().start_server()

	def upload(self, body, content_type):
		reply = self.server.request("PUT", "/api/upload", body=body, headers={"Cookie": self.cookie, "Content-Type": content_type})
		self.assertEqual(201, reply.status, reply.text())
		return json.loads(reply.body)["blobId"]

	def state_file(self, path):
		"""A ".ied/local/..." path's file: the folder workspace's local state, in the server's app data (docs/07)."""
		return os.path.join(local_state(self.data, self.repo), path[len(".ied/local/"):])

	def first_user(self):
		return [m for m in self.model.requests()[-1]["body"]["messages"] if m["role"] == "user"][0]

	def block(self, content, label):
		start = content.index('<mention label="@%s"' % label)
		return content[start:content.index("</mention>", start)]

	async def failing_terminal(self):
		term = await self.client.request("term.create", {"wsId": "w1", "argv": [sys.executable, "-I", "-c", FAILING], "title": "Build"})
		topic = "term:" + term["termId"]
		await self.client.sub([topic])
		await self.client.wait_for(topic, lambda f: f.get("kind") == "exit", timeout=20)
		return term["termId"]

	async def test_mentions_are_read_when_the_turn_starts(self):
		other, _ = await self.chat()
		self.model.queue(text_step("Fixed the loop."))
		await self.turn(other, "Fix the loop")
		term = await self.failing_terminal()
		chat_id, topic = await self.chat()
		mentions = [
			{"kind": "file", "ref": "src/main.c", "label": "src/main.c"},
			{"kind": "folder", "ref": "src", "label": "src/"},
			{"kind": "problems", "ref": "", "label": "problems"},
			{"kind": "output", "ref": "ied", "label": "output:IED"},
			{"kind": "output", "ref": "nope", "label": "output:nope"},
			{"kind": "terminal", "ref": term, "label": "terminal:Build"},
			{"kind": "diff", "ref": "", "label": "diff"},
			{"kind": "chat", "ref": other, "label": "chat:Fix the loop"},
		]
		self.model.queue(text_step("Seen."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "Look at @src/main.c and the rest", "mentions": mentions})
		start = (await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.start", timeout=20))[-1]["data"]
		self.assertEqual(mentions, start["user"]["mentions"], "turn.start carries them")
		await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=TURN_SECONDS)
		content = self.first_user()["content"]
		self.assertTrue(content.startswith("Look at @src/main.c and the rest\n\n<mention"), content[:200])
		self.assertIn("return 1;", self.block(content, "src/main.c"), "the file as fs_read reads it")
		self.assertIn("main.c", self.block(content, "src/"), "the folder listed")
		self.assertIn('kind="problems"', self.block(content, "problems"))
		self.assertNotIn("(not read", self.block(content, "output:IED"), "the server's own log")
		self.assertIn("(not read: the workspace has no output source nope)", self.block(content, "output:nope"))
		terminal = self.block(content, "terminal:Build")
		self.assertIn("Terminal Build (exited), its last lines:\nbuild failed: 2 errors", terminal, "plain text through terminal.text")
		self.assertNotIn("\x1b", terminal)
		diff = self.block(content, "diff")
		self.assertIn("# repository .", diff)
		self.assertIn("+\treturn 1;", diff, "git diff HEAD through git.workingDiff")
		self.assertIn("# untracked files\nnotes.txt", diff)
		self.assertIn("Fix the loop", self.block(content, "chat:Fix the loop"), "the other chat's turns")
		turn = await self.last_turn(chat_id)
		self.assertEqual(mentions, turn["user"]["mentions"], "the transcript keeps them")
		history = await self.client.request("chat.history", {"chatId": chat_id})
		self.assertEqual("Look at @src/main.c and the rest", history["turns"][-1]["user"]["text"], "the owner's words as written")

	async def test_a_mention_is_checked(self):
		chat_id, _ = await self.chat()
		for mentions in ([{"kind": "url", "ref": "x", "label": "x"}], [{"kind": "terminal", "ref": "", "label": "terminal:"}], {"kind": "file"}):
			with self.assertRaises(RpcFailure) as refused:
				await self.client.request("chat.send", {"chatId": chat_id, "text": "hi", "mentions": mentions})
			self.assertEqual("bad_request", refused.exception.code, mentions)
		self.assertEqual(0, (await self.summary(chat_id))["turnCount"], "no turn for a refused message")

	async def test_attachments_go_to_the_media_folder_and_the_model(self):
		await self.use_model({"vision": True})
		chat_id, _ = await self.chat()
		shot = self.upload(PNG, "image/png")
		notes = self.upload(b"line one\nline two\n", "text/plain")
		self.model.queue(text_step("A screen and notes."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "What is wrong?", "attachments": [{"blobId": notes, "name": "C:\\Users\\me\\my notes.txt"}, {"blobId": shot, "name": "shot.png"}]})
		self.frames = await self.client.wait_for("chat:" + chat_id, lambda f: f.get("kind") == "turn.end", timeout=TURN_SECONDS)
		kept = (await self.last_turn(chat_id))["user"]["attachments"]
		self.assertEqual([("C:\\Users\\me\\my notes.txt", "text/plain", 18), ("shot.png", "image/png", len(PNG))], [(a["name"], a["mime"], a["bytes"]) for a in kept])
		media = ".ied/local/chats/%s/media/" % chat_id
		self.assertTrue(kept[0]["path"].startswith(media) and kept[0]["path"].endswith("-my_notes.txt"), kept[0]["path"])
		with open(self.state_file(kept[1]["path"]), "rb") as f:
			self.assertEqual(PNG, f.read(), "the upload's bytes in the chat's media folder")
		self.assertFalse(os.path.exists(os.path.join(self.repo, ".ied", "local")), "in the app data, not in the folder")
		parts = self.first_user()["content"]
		self.assertEqual(["text", "text", "image_url"], [p["type"] for p in parts], "the image in the owner's own message")
		self.assertEqual("What is wrong?\n[attached: %s (C:\\Users\\me\\my notes.txt, 18 bytes, text/plain); fs_read reads it]" % kept[0]["path"], parts[0]["text"])
		self.assertEqual("The attached image %s:" % kept[1]["path"], parts[1]["text"])
		self.assertEqual("data:image/png;base64," + base64.b64encode(PNG).decode(), parts[2]["image_url"]["url"])

		await self.use_model({})
		blind, _ = await self.chat()
		self.model.queue(text_step("Told."))
		await self.client.request("chat.send", {"chatId": blind, "text": "And this?", "attachments": [{"blobId": shot, "name": "shot.png"}]})
		await self.client.wait_for("chat:" + blind, lambda f: f.get("kind") == "turn.end", timeout=TURN_SECONDS)
		content = self.first_user()["content"]
		self.assertIn("(image/png): this model is not sent images]", content, "a model without vision is told the path")

	async def test_attachments_are_refused_when_they_cannot_be_kept(self):
		chat_id, _ = await self.chat()
		with self.assertRaises(RpcFailure) as gone:
			await self.client.request("chat.send", {"chatId": chat_id, "text": "hi", "attachments": [{"blobId": "b999", "name": "a.txt"}]})
		self.assertEqual("not_found", gone.exception.code)
		self.assertIn("send it again", gone.exception.message)
		blob = self.upload(b"x", "text/plain")
		with self.assertRaises(RpcFailure) as many:
			await self.client.request("chat.send", {"chatId": chat_id, "text": "hi", "attachments": [{"blobId": blob, "name": "a.txt"}] * 17})
		self.assertEqual("bad_request", many.exception.code)

	async def test_a_queued_message_keeps_its_mentions(self):
		chat_id, topic = await self.chat()
		self.model.queue(call_step("fs_write", {"path": "src/new.c", "content": "int x;\n"}), text_step("Done."), text_step("Read it."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "write it"})
		request = (await self.client.wait_for(topic, lambda f: f.get("kind") == "approval.request", timeout=20))[-1]["data"]
		mention = [{"kind": "file", "ref": "notes.txt", "label": "notes.txt"}]
		queued = await self.client.request("chat.send", {"chatId": chat_id, "text": "then @notes.txt", "as": "queue", "mentions": mention})
		self.assertEqual(1, queued["queued"])
		with open(self.state_file(".ied/local/chats/%s/chat.json" % chat_id), encoding="utf-8") as f:
			self.assertEqual([{"text": "then @notes.txt", "mentions": mention}], json.load(f)["queued"], "the header keeps it whole")
		await self.client.request("chat.approve", {"chatId": chat_id, "approvalId": request["approvalId"], "decision": "deny"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.start" and f["data"]["user"]["text"] == "then @notes.txt", timeout=TURN_SECONDS)
		await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=TURN_SECONDS)
		turn = await self.last_turn(chat_id)
		self.assertEqual(mention, turn["user"]["mentions"])
		last = [m for m in self.model.requests()[-1]["body"]["messages"] if m["role"] == "user"][-1]["content"]
		self.assertIn("remember", self.block(last, "notes.txt"), "read when its turn started")


	async def test_a_message_to_the_running_turn_brings_what_it_names(self):
		chat_id, topic = await self.chat()
		self.model.queue(call_step("fs_write", {"path": "src/new.c", "content": "int x;\n"}), text_step("Done."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "write it"})
		request = (await self.client.wait_for(topic, lambda f: f.get("kind") == "approval.request", timeout=20))[-1]["data"]
		mention = [{"kind": "file", "ref": "notes.txt", "label": "notes.txt"}]
		blob = self.upload(PNG, "image/png")
		sent = await self.client.request("chat.send", {"chatId": chat_id, "text": "and @notes.txt", "mentions": mention, "attachments": [{"blobId": blob, "name": "shot.png"}]})
		self.assertEqual({"pending": 1}, sent)
		await self.client.request("chat.approve", {"chatId": chat_id, "approvalId": request["approvalId"], "decision": "allow"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=TURN_SECONDS)
		turn = await self.last_turn(chat_id)
		joined = [s for s in turn["segments"] if s["type"] == "message"]
		self.assertEqual(1, len(joined))
		self.assertEqual(("and @notes.txt", mention, "shot.png"), (joined[0]["text"], joined[0]["mentions"], joined[0]["attachments"][0]["name"]), "shown with its chips")
		last = [m for m in self.model.requests()[-1]["body"]["messages"] if m["role"] == "user"][-1]["content"]
		self.assertTrue(last.startswith("and @notes.txt"), last)
		self.assertIn("remember", self.block(last, "notes.txt"), "read when the turn got it")

	async def test_a_queued_message_comes_back_whole(self):
		chat_id, topic = await self.chat()
		self.model.queue(call_step("fs_write", {"path": "src/new.c", "content": "x"}), text_step("Read it."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "write it"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "approval.request", timeout=20)
		mention = [{"kind": "file", "ref": "notes.txt", "label": "notes.txt"}]
		blob = self.upload(b"twelve bytes", "text/plain")
		await self.client.request("chat.send", {"chatId": chat_id, "text": "then @notes.txt", "as": "queue", "mentions": mention, "attachments": [{"blobId": blob, "name": "log.txt"}]})
		taken = await self.client.request("chat.unqueue", {"chatId": chat_id, "index": 0})
		message = taken["message"]
		self.assertEqual(([], "then @notes.txt", mention), (taken["queued"], message["text"], message["mentions"]), "edited: the whole message back")
		kept = message["attachments"][0]
		self.assertEqual(("log.txt", "text/plain", 12), (kept["name"], kept["mime"], kept["bytes"]))
		for path in ("../chat.json", ".ied/local/chats/%s/chat.json" % chat_id, "src/main.c"):
			with self.assertRaises(RpcFailure) as outside:
				await self.client.request("chat.send", {"chatId": chat_id, "text": "x", "as": "queue", "attachments": [{"path": path}]})
			self.assertEqual("bad_request", outside.exception.code, path)
		again = {"chatId": chat_id, "text": message["text"], "as": "queue", "mentions": message["mentions"], "attachments": [{"path": kept["path"], "name": kept["name"], "mime": kept["mime"]}]}
		self.assertEqual(1, (await self.client.request("chat.send", again))["queued"], "sent again by its path")
		with self.assertRaises(RpcFailure) as running:
			await self.client.request("chat.unqueue", {"chatId": chat_id, "index": 0, "start": True})
		self.assertEqual("conflict", running.exception.code)
		await self.client.request("chat.stop", {"chatId": chat_id})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=TURN_SECONDS)
		started = await self.client.request("chat.unqueue", {"chatId": chat_id, "index": 0, "start": True})
		self.assertIn("turnId", started, "send it now: started on the server")
		await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end" and f["data"]["turnId"] == started["turnId"], timeout=TURN_SECONDS)
		turn = await self.last_turn(chat_id)
		self.assertEqual((mention, [kept]), (turn["user"]["mentions"], turn["user"]["attachments"]), "with what it named and carried")
		content = [m for m in self.model.requests()[-1]["body"]["messages"] if m["role"] == "user"][-1]["content"]
		self.assertIn("[attached: %s (log.txt, 12 bytes, text/plain); fs_read reads it]" % kept["path"], content)
