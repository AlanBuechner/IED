"""The AI's git tools (docs/13 "git"): each git_* tool's answer through the git plugin's git.query binding against real
repositories - status, a diff with numbered hunks, staging and unstaging hunks, commits of named paths, an amend refused
once pushed, log, show, blame, branches and stashes - with paths the workspace's for a repository in a folder of it and
one above it, none in an untrusted workspace; and a chat whose model calls them, its commit approved by the owner."""

import asyncio
import json
import os
import shutil
import subprocess
import tempfile

from frames import FrameChecker
from iedclient import Client, RpcFailure
from server import REPO, IedServer, ServerTestCase, remove_tree
from test_chat import TURN_SECONDS, ChatCase, call_step, text_step
from test_git import LINES, git, make_repo, write


def git_manifest():
	with open(os.path.join(REPO, "plugins", "git", "ied-plugin.json"), encoding="utf-8") as f:
		return json.loads("".join(line for line in f if not line.lstrip().startswith("//")))


def add_git(home):
	"""The git plugin's server side in `home`: its entry, permissions, settings, output source, bindings, tools and
	system-message section, without its UI."""
	manifest = git_manifest()
	plugin = os.path.join(home, "plugins", "git")
	os.makedirs(plugin, exist_ok=True)
	shutil.copy(os.path.join(REPO, "plugins", "git", "settings.schema.json"), plugin)
	kept = {key: manifest["contributes"][key] for key in ("settings", "bindings", "outputSources", "toolCalls", "systemMessageSections")}
	write(os.path.join(plugin, "ied-plugin.json"), json.dumps({"manifestVersion": 1, "id": manifest["id"], "name": manifest["name"], "version": manifest["version"],
		"ied": {"api": "^1"}, "runtime": {"kind": "builtin", "entry": manifest["runtime"]["entry"]}, "permissions": manifest["permissions"], "contributes": kept}))


def git_out(folder, *args):
	return git(folder, *args).strip()


class GitToolCase(ServerTestCase):
	"""A server on a workspace folder with the git plugin, its repositories found."""

	workspace = None

	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-gittools-")
		self.addCleanup(remove_tree, self.root)
		self.repo = os.path.join(self.root, "Game")
		make_repo(self.repo)
		self.prepare()
		self.home = os.path.join(self.root, "home")
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		add_git(self.home)
		self.server = IedServer(extra=("--home", self.home, "--workspace", self.workspace or self.repo))
		self.addCleanup(self.server.stop)
		self.assertTrue(self.server.started, self.server.error)

	def prepare(self):
		pass

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		for _ in range(100):
			repos = await self.client.request("git.repos", {"wsId": "w1"})
			if repos["repos"] and not repos["discovering"]:
				break
			await asyncio.sleep(0.05)

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def ask(self, tool, **args):
		reply = await self.client.request("plugins.call", {"name": "git.query", "wsId": "w1", "args": {"wsId": "w1", "tool": tool, "args": args}})
		return reply["result"]["text"]

	async def refused(self, tool, **args):
		with self.assertRaises(RpcFailure) as failed:
			await self.ask(tool, **args)
		return str(failed.exception)


class GitToolTests(GitToolCase):
	def change_two_places(self):
		write(os.path.join(self.repo, "a.txt"), LINES.replace("line 2\n", "line two\n").replace("line 5\n", "line five\n").replace("line 25\n", "line twenty-five\n"))

	async def test_status_and_a_diff_with_numbered_hunks(self):
		self.change_two_places()
		write(os.path.join(self.repo, "notes.txt"), "remember\n")
		status = await self.ask("status")
		self.assertIn("repository . (Game): on branch main, no upstream\n", status)
		self.assertIn("not staged (1):\n  modified: a.txt\n", status)
		self.assertIn("untracked (1):\n  notes.txt\n", status)
		diff = await self.ask("diff")
		self.assertIn("1 file changed, +2 -2\n", diff)
		self.assertIn("(hunk 1)\n", diff)
		self.assertIn("(hunk 2)\n", diff)
		self.assertIn("-line 25\n+line twenty-five\n", diff)
		self.assertIn("untracked, not in the diff (git_stage adds them, fs_read reads them): notes.txt\n", diff)
		self.assertEqual("nothing is staged\n", await self.ask("diff", staged=True))
		self.assertNotIn("(hunk", await self.ask("diff", context=10), "hunks are numbered only as git_stage counts them")
		self.assertIn("-line 2\n+line two\n", await self.ask("diff", rev_range="HEAD~1"), "against a revision")

	async def test_stage_and_unstage_hunks_then_commit_named_paths(self):
		self.change_two_places()
		write(os.path.join(self.repo, "notes.txt"), "remember\n")
		staged = await self.ask("stage", paths=["a.txt"], hunks=[2])
		self.assertTrue(staged.startswith("staged hunk 2 of a.txt\nrepository . (Game)"), staged)
		self.assertIn("staged (1):\n  modified: a.txt\n", staged)
		self.assertIn("not staged (1):\n  modified: a.txt\n", staged)
		cached = git(self.repo, "diff", "--cached")
		self.assertIn("+line twenty-five", cached)
		self.assertNotIn("line five", cached, "only the hunk asked for")
		self.assertIn("(hunk 1)", await self.ask("diff", staged=True))
		self.assertIn("there is no hunk 3", await self.refused("stage", paths=["a.txt"], hunks=[3]))
		await self.ask("unstage", paths=["a.txt"], hunks=[1])
		self.assertEqual("", git(self.repo, "diff", "--cached"))
		committed = await self.ask("commit", message="Add the notes", paths=["notes.txt"])
		short = git_out(self.repo, "rev-parse", "--short=7", "HEAD")
		self.assertTrue(committed.startswith("committed %s: Add the notes\n notes.txt | 1 +\n 1 file changed, 1 insertion(+)\nrepository . (Game)" % short), committed)
		self.assertEqual("Add the notes", git_out(self.repo, "log", "-1", "--format=%s"))
		self.assertEqual(["notes.txt"], git_out(self.repo, "show", "--name-only", "--format=", "HEAD").split())
		self.assertIn(" M a.txt", git(self.repo, "status", "--porcelain"), "the rest left as it was")

	async def test_commits_need_something_and_amends_stay_unpushed(self):
		self.assertIn("nothing is staged", await self.refused("commit", message="Nothing"))
		self.assertIn("a commit needs a message", await self.refused("commit", message="   ", paths=["a.txt"]))
		remote = os.path.join(self.root, "remote.git")
		git(self.root, "init", "-q", "--bare", "-b", "main", remote)
		git(self.repo, "remote", "add", "origin", remote)
		git(self.repo, "push", "-q", "-u", "origin", "main")
		write(os.path.join(self.repo, "a.txt"), "changed\n")
		self.assertIn("amending it would rewrite pushed history", await self.refused("commit", message="Fix", paths=["a.txt"], amend=True))
		self.assertIn(" M a.txt", git(self.repo, "status", "--porcelain"), "nothing changed by the refusal")
		await self.ask("stage", paths=["a.txt"])
		await self.ask("commit", message="Change a")
		self.assertIn("tracking origin/main (1 ahead)", await self.ask("status"))
		write(os.path.join(self.repo, "b.txt"), "b\n")
		await self.ask("commit", message="Change a and add b", paths=["b.txt"], amend=True)
		self.assertEqual("Change a and add b", git_out(self.repo, "log", "-1", "--format=%s"))
		self.assertEqual(["a.txt", "b.txt"], sorted(git_out(self.repo, "show", "--name-only", "--format=", "HEAD").split()), "an unpushed commit amended")

	async def test_log_show_and_blame(self):
		log = await self.ask("log")
		lines = log.splitlines()
		self.assertEqual(2, len(lines), log)
		self.assertRegex(lines[0], r"^[0-9a-f]{7} \d{4}-\d\d-\d\d \d\d:\d\d Owner: Second: line two  \(HEAD -> main\)$")
		self.assertTrue(lines[1].endswith("Owner: First: the files"), lines[1])
		self.assertIn("[more: skip 1 for the next ones]", await self.ask("log", n=1))
		self.assertEqual(1, len((await self.ask("log", query="FIRST")).splitlines()), "a message search in any case")
		self.assertIn("First", await self.ask("log", paths=["src/main.c"]))
		self.assertNotIn("Second", await self.ask("log", paths=["src/main.c"]), "the commits that changed a path")
		show = await self.ask("show")
		self.assertIn("commit " + git_out(self.repo, "rev-parse", "HEAD"), show)
		self.assertIn("author Owner <owner@example.com>", show)
		self.assertIn("Second: line two\n\nSpelled out.", show)
		self.assertIn("1 file changed, +1 -1\n", show)
		self.assertIn("+line two", show)
		first = await self.ask("show", rev="HEAD~1", file="a.txt")
		self.assertTrue(first.startswith("1\tline 1\n2\tline 2\n"), first[:40])
		self.assertEqual("29\tline 29\n30\tline 30\n", await self.ask("show", rev="HEAD~1", file="a.txt", offset=29), "the file as it was, from a line")
		self.assertIn("path must be workspace-relative", await self.refused("show", file="../outside.txt"))
		blame = await self.ask("blame", path="a.txt", start_line=1, end_line=3)
		second = git_out(self.repo, "rev-parse", "--short=7", "HEAD")
		self.assertIn("a.txt, lines 1-3, from 2 commits:\n", blame)
		self.assertIn("2 %s\tline two\n" % second, blame)
		self.assertIn("[lines 1-3 of 30; git_blame with start_line 4 reads on]", blame)
		write(os.path.join(self.repo, "a.txt"), "new first line\n" + LINES)
		self.assertIn("  0000000 not committed yet\n", await self.ask("blame", path="a.txt", end_line=2))

	async def test_branches_switches_and_stashes(self):
		created = await self.ask("branch", action="create", name="feat", switch=True)
		self.assertIn("created branch feat\nrepository . (Game): on branch feat, no upstream", created)
		branches = await self.ask("branches")
		self.assertIn("* feat, no upstream: ", branches)
		self.assertIn("  main, no upstream: ", branches)
		self.assertIn("Second: line two", branches)
		self.assertIn("on branch main", await self.ask("branch", action="switch", name="main"))
		self.assertIn("deleted branch feat", await self.ask("branch", action="delete", name="feat"))
		self.assertIn("is not a name git takes", await self.refused("branch", action="create", name="--force"))
		write(os.path.join(self.repo, "a.txt"), "wip\n")
		stashed = await self.ask("stash", action="push", message="wip on a")
		self.assertIn("nothing to commit, the work tree is clean", stashed)
		self.assertIn("stashes (1):\n  0: On main: wip on a\n", await self.ask("branches"))
		self.assertIn("there are no local changes to stash", await self.refused("stash", action="push"))
		popped = await self.ask("stash", action="pop")
		self.assertIn("popped stash@{0}", popped)
		self.assertIn("modified: a.txt", popped)
		write(os.path.join(self.repo, "a.txt"), "other\n")
		git(self.repo, "stash", "push", "-q")
		write(os.path.join(self.repo, "a.txt"), "conflicting\n")
		self.assertIn("commit or git_stash the local changes first", await self.refused("stash", action="apply"))

	async def test_the_section_and_an_untrusted_workspace(self):
		self.assertIn("The workspace is in a git repository.", await self.ask("section"))
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": False})
		self.assertIn("untrusted", await self.refused("status"))
		self.assertEqual("", await self.ask("section"), "no repositories, no section")


class NestedRepositoryToolTests(GitToolCase):
	def prepare(self):
		self.lib = os.path.join(self.repo, "lib")
		os.makedirs(self.lib)
		git(self.lib, "init", "-q", "-b", "main")
		git(self.lib, "config", "user.name", "Owner")
		git(self.lib, "config", "user.email", "owner@example.com")
		write(os.path.join(self.lib, "x.c"), "int x;\n")
		git(self.lib, "add", ".")
		git(self.lib, "commit", "-q", "-m", "Lib")
		write(os.path.join(self.repo, ".gitignore"), "lib/\n")
		git(self.repo, "add", ".gitignore")
		git(self.repo, "commit", "-q", "-m", "Ignore lib")

	async def test_paths_are_the_workspaces(self):
		write(os.path.join(self.lib, "x.c"), "int x = 1;\n")
		write(os.path.join(self.repo, "a.txt"), "changed\n")
		status = await self.ask("status")
		self.assertIn("repository . (Game)", status)
		self.assertIn("repository lib (lib)", status)
		self.assertIn("  modified: lib/x.c\n", status)
		self.assertIn("  modified: a.txt\n", status)
		self.assertNotIn("modified: a.txt", await self.ask("status", repo="lib"))
		diff = await self.ask("diff", paths=["lib/x.c"])
		self.assertIn("diff --git a/lib/x.c b/lib/x.c\n", diff)
		self.assertIn("+++ b/lib/x.c\n", diff)
		self.assertIn("different repositories", await self.refused("stage", paths=["lib/x.c", "a.txt"]))
		await self.ask("stage", paths=["lib/x.c"])
		self.assertIn("x.c", git(self.lib, "diff", "--cached", "--name-only"))
		self.assertIn("no repository at 'nope'; the workspace's are: ., lib", await self.refused("branches", repo="nope"))
		self.assertIn("has git repositories at ., lib", await self.ask("section"))


class FolderInARepositoryToolTests(GitToolCase):
	def prepare(self):
		self.workspace = os.path.join(self.repo, "src")

	async def test_only_the_folder_is_seen(self):
		write(os.path.join(self.repo, "src", "main.c"), "int main(void) { return 1; }\n")
		write(os.path.join(self.repo, "a.txt"), "changed\n")
		status = await self.ask("status")
		self.assertIn("repository . (Game)", status)
		self.assertIn("not staged (1):\n  modified: main.c\n", status)
		self.assertIn("1 changes outside the workspace's folder are not shown", status)
		diff = await self.ask("diff")
		self.assertIn("diff --git a/main.c b/main.c\n", diff)
		self.assertNotIn("a.txt", diff)
		await self.ask("commit", message="Return 1", paths=["main.c"])
		self.assertEqual(["src/main.c"], git_out(self.repo, "show", "--name-only", "--format=", "HEAD").split())
		self.assertIn("main.c", await self.ask("show"))


class GitToolChatTests(ChatCase):
	def start_server(self):
		add_git(self.home)
		subprocess.run(["git", "init", "-q", "-b", "main"], cwd=self.repo, check=True)
		for args in (["config", "user.name", "Owner"], ["config", "user.email", "owner@example.com"], ["add", "."], ["commit", "-q", "-m", "First"]):
			subprocess.run(["git", *args], cwd=self.repo, check=True, capture_output=True)
		write(os.path.join(self.repo, "src", "main.c"), "int main(void)\n{\n\treturn 1;\n}\n")
		return super().start_server()

	async def repos_found(self):
		for _ in range(100):
			if (await self.client.request("git.repos", {"wsId": "w1"}))["repos"]:
				return
			await asyncio.sleep(0.05)

	async def test_the_model_reads_and_commits_with_the_owners_approval(self):
		await self.repos_found()
		chat_id, topic = await self.chat()
		self.model.queue(call_step("git_status", {}, "c1"), call_step("git_commit", {"message": "Return 1", "paths": ["src/main.c"]}, "c2"), text_step("Committed."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "commit it"})
		request = (await self.client.wait_for(topic, lambda f: f.get("kind") == "approval.request", timeout=TURN_SECONDS))[-1]["data"]
		self.assertEqual(("git_commit", "plugin(ied.git/git_commit)"), (request["tool"], request["subject"]))
		self.assertEqual("First", subprocess.run(["git", "log", "-1", "--format=%s"], cwd=self.repo, capture_output=True, text=True).stdout.strip(), "nothing committed before the decision")
		await self.client.request("chat.approve", {"chatId": chat_id, "approvalId": request["approvalId"], "decision": "allow"})
		frames = await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=TURN_SECONDS)
		self.assertEqual("finished", frames[-1]["data"]["stop"])
		self.assertEqual([], [f["data"] for f in frames if f.get("kind") == "files.changed"], "a write tool without a path names no modified file")
		self.assertEqual("Return 1", subprocess.run(["git", "log", "-1", "--format=%s"], cwd=self.repo, capture_output=True, text=True).stdout.strip())
		sent = self.model.requests()[-1]["body"]
		self.assertIn("## Git\n\nThe workspace is in a git repository.", sent["messages"][0]["content"])
		results = {m["tool_call_id"]: m["content"] for m in sent["messages"] if m["role"] == "tool"}
		self.assertIn("not staged (1):\n  modified: src/main.c\n", results["c1"])
		self.assertIn("committed ", results["c2"])
		tools = self.segments(await self.last_turn(chat_id), "tool")
		self.assertEqual([("git_status", "ok", "plugin ied.git"), ("git_commit", "ok", "plugin ied.git")], [(t["tool"], t["state"], t.get("source")) for t in tools])
	async def test_plan_mode_offers_only_the_reading_tools(self):
		await self.repos_found()
		chat_id, _ = await self.chat(mode="plan")
		self.model.queue(text_step("A plan."))
		await self.turn(chat_id)
		system = self.model.requests()[-1]["body"]["messages"][0]["content"]
		for name in ("git_status", "git_diff", "git_log", "git_show", "git_blame", "git_branches"):
			self.assertIn("- %s: " % name, system)
		for name in ("git_stage", "git_unstage", "git_commit", "git_branch", "git_stash"):
			self.assertNotIn("- %s: " % name, system)
