"""Developer mode (docs/28): a server that runs from IED's source checkout gives its chats the source as ied:/ (the
developer plugin's ai.roots binding, opened as a workspace when first used), a system-message section and
ied_known_bugs; every write there asks, whatever the chat's permission mode; developer.enabled off, or an untrusted
workspace, takes it all away. An installed IED (no source) gives none of it."""

import os
import shutil

from test_chat import REPO, ChatCase, call_step, text_step, write

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
		self.assertIn("This IED runs from its source checkout at", system)
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
		self.assertNotIn("This IED runs from its source checkout", system, "its files could steer the AI into IED's source")
		self.assertNotIn("ied_known_bugs", system)
		self.assertIn("nothing is reachable as ied:/", self.tool_results()["r1"])

	async def test_off_takes_it_away(self):
		await self.client.request("settings.set", {"tier": "app", "key": "developer.enabled", "value": False})
		chat_id, _ = await self.chat(mode="agent")
		self.model.queue(call_step("fs_read", {"path": "ied:/AGENTS.md"}, "r1"), text_step("No."))
		await self.turn(chat_id)
		system = self.model.requests()[0]["body"]["messages"][0]["content"]
		self.assertNotIn("This IED runs from its source checkout", system)
		self.assertIn("nothing is reachable as ied:/", self.tool_results()["r1"])


class InstalledTests(ChatCase):
	def start_server(self):
		add_developer(self.home)
		return super().start_server()

	async def test_an_install_has_no_source(self):
		chat_id, _ = await self.chat(mode="agent")
		self.model.queue(call_step("fs_read", {"path": "ied:/AGENTS.md"}, "r1"), text_step("No."))
		await self.turn(chat_id)
		messages = self.model.requests()[-1]["body"]["messages"]
		self.assertNotIn("This IED runs from its source checkout", messages[0]["content"])
		self.assertNotIn("ied_known_bugs", messages[0]["content"])
		self.assertIn("nothing is reachable as ied:/", [m["content"] for m in messages if m["role"] == "tool"][0])
