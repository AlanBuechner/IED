"""Developer mode (docs/28): a server that runs from IED's source checkout gives its chats the source as ied:/ (the
developer plugin's ai.roots binding, opened as a workspace when first used), a system-message section and
ied_known_bugs; every write there asks, whatever the chat's permission mode; developer.enabled off, or an untrusted
workspace, takes it all away. An installed IED finds its checkout through install.json; one without gives none of it.
Rebuild and relaunch: ied.developer/rebuild runs `nuke.bat install-ied` in the checkout (a fake one here), its lines and
its end on the topic ied.developer/state."""

import json
import os
import shutil
import subprocess

from iedclient import RpcFailure
from test_chat import REPO, ChatCase, call_step, text_step, write

BUILD_SECONDS = 30

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


class DeveloperTests(ChatCase):
	def start_server(self):
		make_source_home(self.home)
		return super().start_server()

	def tool_results(self):
		mine = self.model.requests()[-1]["body"]["messages"]
		return {m["tool_call_id"]: m["content"] for m in mine if m["role"] == "tool"}

	async def test_the_source_is_reachable(self):
		chat_id, _ = await self.chat(mode="agent")
		self.model.queue(call_step("ied_known_bugs", {}, "k1"), call_step("fs_read", {"path": "ied:/AGENTS.md"}, "r1"), text_step("Read."))
		await self.turn(chat_id)
		system = self.model.requests()[0]["body"]["messages"][0]["content"]
		self.assertIn("This IED is built from its source checkout at", system)
		self.assertIn("- ied_known_bugs: ", system, "its tools are listed to load")
		results = self.tool_results()
		self.assertIn("BUG-007 A demo miscompile", results["k1"])
		self.assertIn("**Severity:** High", results["k1"])
		self.assertIn("Next ID: **BUG-008**", results["k1"])
		self.assertNotIn("BUG-NNN", results["k1"], "the template is no bug")
		self.assertIn("Fix bugs at the source.", results["r1"])

	async def test_writes_there_always_ask(self):
		chat_id, topic = await self.chat(mode="agent", permission="auto")
		self.model.queue(call_step("fs_write", {"path": "ied:/notes.txt", "content": "fixed\n"}, "w1"), text_step("Written."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "write it"})
		request = (await self.client.wait_for(topic, lambda f: f.get("kind") == "approval.request", timeout=20))[-1]["data"]
		self.assertEqual("fs.write(ied:/**)", request["subject"], "the rule Allow for this chat would add")
		self.assertIn("ied:/", request["why"])
		self.assertFalse(os.path.exists(os.path.join(self.home, "notes.txt")), "nothing is written before the decision")
		await self.client.request("chat.approve", {"chatId": chat_id, "approvalId": request["approvalId"], "decision": "allow"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=20)
		with open(os.path.join(self.home, "notes.txt"), encoding="utf-8") as f:
			self.assertEqual("fixed\n", f.read())

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
