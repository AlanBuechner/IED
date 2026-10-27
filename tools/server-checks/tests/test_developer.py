"""Developer mode (docs/28), off until the owner turns developer.enabled on: a server that runs from IED's source
checkout then gives its chats the source as ied:/ (the developer plugin's ai.roots binding, opened as a workspace when
first used), a system-message section and ied_known_bugs; writes there follow the chat's permission mode - auto mode
runs them without asking, ask mode asks until the owner allows them for the chat. An untrusted workspace gets none of it.
An installed IED finds its checkout through install.json; one without gives none of it. Rebuild and relaunch:
ied.developer/rebuild runs `nuke.bat install-ied` in the checkout (a fake one here), its lines and its end on the topic
ied.developer/state; ied_relaunch runs it too, then the server relaunches itself (no desktop app takes it) and the chat
goes on by itself on the new server."""

import asyncio
import json
import os
import shutil
import subprocess
import time

from iedclient import Client, RpcFailure
from frames import FrameChecker
from server import ControlPipe
from test_chat import REPO, ChatCase, call_step, text_step, write

BUILD_SECONDS = 30
RELAUNCH_SECONDS = 60

KNOWN_BUGS = """# Known bugs

Next ID: **BUG-008**

### Entry template

```text
### BUG-NNN Short title
- **Severity:** High | Medium | Low
```

## HIR and codegen

### BUG-007 A demo miscompile
- **Area:** Codegen
- **Severity:** High
- **Status:** Open
- **Symptom:** a loop runs once too often
- **Where:** somewhere
"""


def add_developer(home):
	shutil.copytree(os.path.join(REPO, "plugins", "developer"), os.path.join(home, "plugins", "developer"), ignore=shutil.ignore_patterns("code"))


def make_source_home(home):
	"""make_home's install made to look like IED's source checkout: .git, C4/C4Lib/src, src/nuke.toml, AGENTS.md and the
	C4 known bugs, with the developer plugin."""
	add_developer(home)
	os.makedirs(os.path.join(home, ".git"))
	os.makedirs(os.path.join(home, "C4", "C4Lib", "src"))
	write(os.path.join(home, "src", "nuke.toml"), "[project]\nname = \"ied-server\"\n")
	write(os.path.join(home, "AGENTS.md"), "Fix bugs at the source.\n")
	write(os.path.join(home, "C4", "docs", "status", "known-bugs.md"), KNOWN_BUGS)
	return home


def fake_nuke(home, body):
	"""A nuke.bat in the checkout that says what it was asked and runs `body` (batch lines)."""
	write(os.path.join(home, "nuke.bat"), "@echo off\r\necho nuke %*\r\n" + body.replace("\n", "\r\n"))


def commit_all(folder, message):
	"""A real git history in the fake checkout, so HEAD and the count since the install are known."""
	run = lambda *args: subprocess.run(["git", "-C", folder] + list(args), check=True, capture_output=True)
	run("add", "-A")
	run("-c", "user.name=IED", "-c", "user.email=ied@example.invalid", "commit", "-q", "--allow-empty", "-m", message)
	return subprocess.run(["git", "-C", folder, "rev-parse", "HEAD"], check=True, capture_output=True, text=True).stdout.strip()


async def enable(client):
	await client.request("settings.set", {"tier": "app", "key": "developer.enabled", "value": True})


def trust_folder(data, folder):
	"""The owner trusted `folder` before (the app tier's trust.json, docs/11), as the owner's real checkout is."""
	write(os.path.join(data, "app", "trust.json"), json.dumps({"version": 1, "trusted": [folder.replace("\\", "/").lower()]}))


class SourceCase(ChatCase):
	"""A server run from a (fake) source checkout the owner trusts, with developer mode turned on."""

	def start_server(self):
		make_source_home(self.home)
		trust_folder(self.data, self.home)
		return super().start_server()

	async def asyncSetUp(self):
		await super().asyncSetUp()
		await enable(self.client)

	def tool_results(self, request=-1):
		mine = self.model.requests()[request]["body"]["messages"]
		return {m["tool_call_id"]: m["content"] for m in mine if m["role"] == "tool"}


class DeveloperTests(SourceCase):
	async def test_the_source_is_reachable(self):
		chat_id, _ = await self.chat(mode="agent")
		self.model.queue(call_step("ied_known_bugs", {}, "k1"), call_step("fs_read", {"path": "ied:/AGENTS.md"}, "r1"), text_step("Read."))
		await self.turn(chat_id)
		system = self.model.requests()[0]["body"]["messages"][0]["content"]
		self.assertIn("This IED is built from its source checkout at", system)
		self.assertIn("Every failing tool call is a chance to fix IED's tools", system, "a failing call: fix the tool or the text that misled")
		self.assertIn("Then make the call again.", system, "the section reaches the model whole")
		tools = {t["function"]["name"]: t["function"] for t in self.model.requests()[0]["body"]["tools"]}
		self.assertTrue(tools["ied_known_bugs"]["description"].endswith("[deferred]"), "its tools are offered, deferred")
		results = self.tool_results()
		self.assertIn("BUG-007 A demo miscompile", results["k1"])
		self.assertIn("**Severity:** High", results["k1"])
		self.assertIn("Next ID: **BUG-008**", results["k1"])
		self.assertNotIn("BUG-NNN", results["k1"], "the template is no bug")
		self.assertIn("Fix bugs at the source.", results["r1"])

	async def test_auto_mode_writes_there_without_asking(self):
		"""What developer mode is for (owner, 2026-10-23): an agent fixing IED or the C4 toolchain in auto mode edits and
		builds without the owner approving each step."""
		chat_id, topic = await self.chat(mode="agent", permission="auto")
		self.model.queue(call_step("fs_write", {"path": "ied:/notes.txt", "content": "fixed\n"}, "w1"),
			call_step("shell_run", {"command": "echo built", "cwd": "ied:/"}, "s1"), text_step("Written."))
		await self.turn(chat_id, "write it")
		self.assertFalse([f for f in self.frames if f.get("kind") == "approval.request"], "nothing asked")
		with open(os.path.join(self.home, "notes.txt"), encoding="utf-8") as f:
			self.assertEqual("fixed\n", f.read())
		self.assertIn("built", self.tool_results()["s1"])

	async def test_allow_for_this_chat_covers_the_writes_its_rule_names(self):
		"""In ask mode a write into the source asks; the owner's "Allow for this chat" lets the chat's later writes its
		rule names run without asking, and a write the rule does not name still asks."""
		chat_id, topic = await self.chat(mode="agent", permission="ask")
		self.model.queue(call_step("fs_write", {"path": "ied:/notes/one.txt", "content": "one\n"}, "w1"),
			call_step("fs_write", {"path": "ied:/notes/two.txt", "content": "two\n"}, "w2"),
			call_step("fs_write", {"path": "ied:/other.txt", "content": "three\n"}, "w3"), text_step("Written."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "write them"})
		first = (await self.client.wait_for(topic, lambda f: f.get("kind") == "approval.request", timeout=20))[-1]["data"]
		self.assertEqual("fs.write(ied:/notes/**)", first["subject"])
		await self.client.request("chat.approve", {"chatId": chat_id, "approvalId": first["approvalId"], "decision": "chat"})
		third = (await self.client.wait_for(topic, lambda f: f.get("kind") == "approval.request", timeout=20))[-1]["data"]
		self.assertEqual("fs.write(ied:/**)", third["subject"], "two.txt ran without asking; other.txt is outside the rule")
		self.assertTrue(os.path.exists(os.path.join(self.home, "notes", "two.txt")))
		await self.client.request("chat.approve", {"chatId": chat_id, "approvalId": third["approvalId"], "decision": "allow"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=20)
		self.assertTrue(os.path.exists(os.path.join(self.home, "other.txt")))

	async def test_an_untrusted_workspace_gets_none(self):
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": False})
		chat_id, _ = await self.chat(mode="agent")
		self.model.queue(call_step("fs_read", {"path": "ied:/AGENTS.md"}, "r1"), text_step("No."))
		await self.turn(chat_id)
		system = self.model.requests()[0]["body"]["messages"][0]["content"]
		self.assertNotIn("This IED is built from its source checkout", system, "its files could steer the AI into IED's source")
		self.assertNotIn("ied_known_bugs", system)
		self.assertIn("nothing is reachable as ied:/", self.tool_results()["r1"])

	async def test_off_takes_it_away(self):
		await self.client.request("settings.set", {"tier": "app", "key": "developer.enabled", "value": False})
		chat_id, _ = await self.chat(mode="agent")
		self.model.queue(call_step("fs_read", {"path": "ied:/AGENTS.md"}, "r1"), text_step("No."))
		await self.turn(chat_id)
		system = self.model.requests()[0]["body"]["messages"][0]["content"]
		self.assertNotIn("This IED is built from its source checkout", system)
		self.assertIn("nothing is reachable as ied:/", self.tool_results()["r1"])


class OffByDefaultTests(ChatCase):
	"""Developer mode is off until the owner turns it on, even where IED is built from source (owner, 2026-10-23)."""

	def start_server(self):
		make_source_home(self.home)
		return super().start_server()

	async def test_off_by_default(self):
		chat_id, _ = await self.chat(mode="agent")
		self.model.queue(call_step("fs_read", {"path": "ied:/AGENTS.md"}, "r1"), text_step("No."))
		await self.turn(chat_id)
		messages = self.model.requests()[-1]["body"]["messages"]
		self.assertNotIn("This IED is built from its source checkout", messages[0]["content"])
		self.assertNotIn("ied_relaunch", messages[0]["content"])
		self.assertIn("nothing is reachable as ied:/", [m["content"] for m in messages if m["role"] == "tool"][0])


class RelaunchTests(SourceCase):
	"""ied_relaunch: `nuke install-ied` (a fake one), then the server relaunches itself - no desktop app watches, so none
	claims it - on its port with its workspaces, and the chat goes on by itself there."""

	def start_server(self):
		server = super().start_server()
		fake_nuke(self.home, "echo installed\nexit /b 0\n")
		return server

	def tearDown(self):
		try:
			ControlPipe(self.server.control).call("stop")
		except OSError:
			pass
		super().tearDown()

	async def reconnect(self):
		"""Signs in to the server that took over: a launch URL from its control pipe, once it answers."""
		deadline = time.time() + RELAUNCH_SECONDS
		while True:
			try:
				reply = ControlPipe(self.server.control).call("url")
				break
			except OSError:
				self.assertLess(time.time(), deadline, "no server came back")
				await asyncio.sleep(0.25)
		self.server.launch_url = reply["result"]["url"]
		self.client.abort()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})

	async def test_the_chat_goes_on_after_the_relaunch(self):
		chat_id, topic = await self.chat(mode="agent", permission="auto")
		self.model.queue(call_step("ied_relaunch", {}, "x1"), call_step("finish", {"summary": "relaunching", "verification": "installed"}, "f1"),
			call_step("finish", {"summary": "back on the new build", "verification": "checked"}, "f2"))
		await self.turn(chat_id, "fix it and relaunch")
		self.assertIn("Installed. IED relaunches", self.tool_results()["x1"])
		self.server.process.wait(RELAUNCH_SECONDS)
		self.assertEqual(0, self.server.process.returncode, "the old server stopped on its own")
		await self.reconnect()
		status = ControlPipe(self.server.control).call("status")["result"]
		self.assertNotEqual(self.server.process.pid, status["pid"], "a new server")
		self.assertEqual(["Game"], [os.path.basename(w["root"].replace("/", os.sep)) for w in status["workspaces"]], "with the owner's workspace")
		deadline = time.time() + RELAUNCH_SECONDS
		while True:
			last = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"][-1]
			if last["user"].get("resumed") and last.get("stop"):
				break
			self.assertLess(time.time(), deadline, "the chat did not go on: %r" % last["user"])
			await asyncio.sleep(0.5)
		self.assertEqual("relaunch", last["user"]["resumed"])
		self.assertIn("as your call to ied_relaunch asked", last["user"]["text"])
		told = [m["content"] for r in self.model.requests() for m in r["body"]["messages"] if m["role"] == "user"]
		self.assertTrue(any("as your call to ied_relaunch asked" in text for text in told), "the model is told")
		self.assertEqual("finished", last["stop"])


class RebuildTests(ChatCase):
	def start_server(self):
		make_source_home(self.home)
		shutil.rmtree(os.path.join(self.home, ".git"))
		subprocess.run(["git", "init", "-q", self.home], check=True, capture_output=True)
		fake_nuke(self.home, "echo compiling ied-server\n1>&2 echo a warning\nexit /b 0\n")
		self.first = commit_all(self.home, "first")
		self.head = commit_all(self.home, "second")
		return super().start_server()

	async def state(self):
		await self.client.sub(["ied.developer/state"])
		return (await self.client.next_frame("ied.developer/state"))["data"]

	async def build_ends(self):
		frames = await self.client.wait_for("ied.developer/state", lambda f: f.get("kind") == "changed" and f["data"]["build"]["state"] not in ("idle", "running"), timeout=BUILD_SECONDS)
		return frames

	async def test_a_rebuild_streams_its_lines_and_its_end(self):
		state = await self.state()
		self.assertEqual(self.home.replace("\\", "/").lower(), state["source"].replace("\\", "/").lower())
		self.assertIsNone(state["install"], "it runs from the checkout")
		self.assertEqual("idle", state["build"]["state"])
		await self.client.request("ied.developer/refresh", {})
		await self.client.wait_for("ied.developer/state", lambda f: f.get("kind") == "changed" and f["data"]["head"] == self.head, timeout=10)
		await self.client.request("ied.developer/rebuild", {})
		frames = await self.build_ends()
		lines = [line for f in frames if f.get("kind") == "build.lines" for line in f["data"]["lines"]]
		self.assertIn("nuke install-ied", lines, "it runs install-ied")
		self.assertIn("compiling ied-server", lines)
		self.assertIn("a warning", lines, "errors come in the same log")
		self.assertTrue(all("\r" not in line for line in lines), lines)
		end = frames[-1]["data"]["build"]
		self.assertEqual(("succeeded", 0), (end["state"], end["exitCode"]))
		self.assertEqual(lines, end["lines"][-len(lines):], "the snapshot keeps the log")
		self.assertGreaterEqual(end["endedMs"], end["startedMs"])
		state = (await self.client.wait_for("ied.developer/state", lambda f: f.get("kind") == "changed" and f["data"]["build"]["state"] == "succeeded" and f["data"]["head"], timeout=10))[-1]["data"]
		self.assertEqual(self.head, state["head"], "the end refreshes what the checkout is at")

	async def test_a_failed_rebuild_keeps_its_exit_code(self):
		fake_nuke(self.home, "echo error: it broke\nexit /b 3\n")
		await self.state()
		await self.client.request("ied.developer/rebuild", {})
		end = (await self.build_ends())[-1]["data"]["build"]
		self.assertEqual(("failed", 3), (end["state"], end["exitCode"]))
		self.assertIn("error: it broke", end["lines"])

	async def test_a_second_rebuild_waits_for_the_first(self):
		fake_nuke(self.home, "ping -n 30 127.0.0.1 > nul\nexit /b 0\n")
		await self.state()
		await self.client.request("ied.developer/rebuild", {})
		with self.assertRaises(RpcFailure) as twice:
			await self.client.request("ied.developer/rebuild", {})
		self.assertEqual("conflict", twice.exception.code)
		await self.client.request("ied.developer/cancel", {})
		end = (await self.build_ends())[-1]["data"]["build"]
		self.assertEqual("canceled", end["state"])
		self.assertLess(end["endedMs"] - end["startedMs"], 20000, "the cancel ended the whole tree, ping too")


class InstalledFromSourceTests(ChatCase):
	"""An installed IED (no source in its home) with an install.json naming the checkout it came from."""

	def start_server(self):
		add_developer(self.home)
		self.source = make_source_home(os.path.join(self.root, "checkout"))
		shutil.rmtree(os.path.join(self.source, ".git"))
		subprocess.run(["git", "init", "-q", self.source], check=True, capture_output=True)
		self.installed = commit_all(self.source, "installed")
		commit_all(self.source, "one")
		self.head = commit_all(self.source, "two")
		record = {"source": self.source.replace("\\", "/"), "commit": self.installed, "dirty": True, "installedMs": 1792630000123, "bin": "x"}
		write(os.path.join(self.home, "install.json"), json.dumps(record))
		return super().start_server()

	async def test_it_finds_its_checkout_and_how_far_it_has_moved_on(self):
		await self.client.sub(["ied.developer/state"])
		await self.client.next_frame("ied.developer/state")
		await self.client.request("ied.developer/refresh", {})
		state = (await self.client.wait_for("ied.developer/state", lambda f: f.get("kind") == "changed" and f["data"]["head"], timeout=10))[-1]["data"]
		self.assertEqual(self.source.replace("\\", "/").lower(), state["source"].replace("\\", "/").lower())
		self.assertEqual({"commit": self.installed, "dirty": True, "installedMs": 1792630000123}, state["install"])
		self.assertEqual(self.head, state["head"])
		self.assertEqual(2, state["behind"])
		await enable(self.client)
		chat_id, _ = await self.chat(mode="agent")
		self.model.queue(text_step("Hi."))
		await self.turn(chat_id)
		self.assertIn("This IED is built from its source checkout at", self.model.requests()[0]["body"]["messages"][0]["content"])


class InstalledTests(ChatCase):
	def start_server(self):
		add_developer(self.home)
		return super().start_server()

	async def test_an_install_has_no_source(self):
		with self.assertRaises(RpcFailure) as refused:
			await self.client.request("ied.developer/rebuild", {})
		self.assertEqual("unavailable", refused.exception.code)
		chat_id, _ = await self.chat(mode="agent")
		self.model.queue(call_step("fs_read", {"path": "ied:/AGENTS.md"}, "r1"), text_step("No."))
		await self.turn(chat_id)
		messages = self.model.requests()[-1]["body"]["messages"]
		self.assertNotIn("This IED is built from its source checkout", messages[0]["content"])
		self.assertNotIn("ied_known_bugs", messages[0]["content"])
		self.assertIn("nothing is reachable as ied:/", [m["content"] for m in messages if m["role"] == "tool"][0])
