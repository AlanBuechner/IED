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


def add_git(home, plans=None):
	"""The git plugin's server side in `home`: its entry, permissions, settings, output source, bindings, tools and
	system-message section, without its UI; `plans` replaces tools' plan functions by name."""
	manifest = git_manifest()
	for tool in manifest["contributes"]["toolCalls"]:
		if tool["name"] in (plans or {}):
			tool["plan"] = plans[tool["name"]]
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

	async def plan(self, tool, **args):
		"""What a write tool's call would do, as its approval card shows it: {summary, diff, plus, minus} or {error}."""
		reply = await self.client.request("plugins.call", {"name": "git.query", "wsId": "w1", "args": {"wsId": "w1", "tool": tool, "args": args, "plan": True}})
		return reply["result"]


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


class GitToolPlanTests(GitToolCase):
	"""What each write tool's approval card shows (docs/13 "What the approval card shows"), read from what the call will
	act on, changing nothing."""

	def change_two_places(self):
		write(os.path.join(self.repo, "a.txt"), LINES.replace("line 2\n", "line two\n").replace("line 5\n", "line five\n").replace("line 25\n", "line twenty-five\n"))

	async def test_hunks_staged_and_unstaged_show_those_hunks(self):
		self.change_two_places()
		plan = await self.plan("stage", paths=["a.txt"], hunks=[2])
		self.assertEqual("Stage hunk 2 of a.txt", plan["summary"])
		self.assertTrue(plan["diff"].startswith("diff --git a/a.txt b/a.txt\n--- a/a.txt\n+++ b/a.txt\n@@ -22,7 +22,7 @@"), plan["diff"])
		self.assertIn("(hunk 2)\n", plan["diff"])
		self.assertIn("-line 25\n+line twenty-five\n", plan["diff"])
		self.assertNotIn("line five", plan["diff"], "only the hunk asked for")
		self.assertEqual((1, 1), (plan["plus"], plan["minus"]))
		self.assertEqual("", git(self.repo, "diff", "--cached"), "a plan changes nothing")
		self.assertIn("a.txt has 2 hunks in git_diff; there is no hunk 3", (await self.plan("stage", paths=["a.txt"], hunks=[3]))["error"])
		self.assertIn("hunks are of one file", (await self.plan("stage", paths=["a.txt", "src/main.c"], hunks=[1]))["error"])
		await self.ask("stage", paths=["a.txt"], hunks=[2, 1])
		unstage = await self.plan("unstage", paths=["a.txt"], hunks=[1])
		self.assertEqual("Unstage hunk 1 of a.txt", unstage["summary"])
		self.assertIn("-line 5\n+line five\n", unstage["diff"])
		self.assertNotIn("twenty-five", unstage["diff"])
		self.assertIn("src/main.c has nothing staged", (await self.plan("unstage", paths=["src/main.c"], hunks=[1]))["error"])

	async def test_whole_paths_show_their_diff_and_new_files(self):
		self.change_two_places()
		write(os.path.join(self.repo, "notes.txt"), "remember\n")
		plan = await self.plan("stage", paths=["."])
		self.assertEqual("Stage .: 2 files, +3 -2", plan["summary"])
		self.assertIn("diff --git a/a.txt b/a.txt\n", plan["diff"])
		self.assertIn("diff --git a/notes.txt b/notes.txt\nnew file mode 100644\n--- /dev/null\n+++ b/notes.txt\n@@ -0,0 +1 @@\n+remember\n", plan["diff"])
		self.assertIn("?? notes.txt", git(self.repo, "status", "--porcelain"), "still untracked")
		self.assertEqual("Unstage a.txt: nothing staged", (await self.plan("unstage", paths=["a.txt"]))["summary"])
		git(self.repo, "add", "a.txt")
		unstage = await self.plan("unstage", paths=["a.txt"])
		self.assertEqual(("Unstage a.txt: 1 file, +2 -2", 2, 2), (unstage["summary"], unstage["plus"], unstage["minus"]))

	async def test_a_commit_shows_its_files_and_counts(self):
		self.assertIn("nothing is staged", (await self.plan("commit", message="Nothing"))["error"])
		self.assertIn("a commit needs a message", (await self.plan("commit", message=" "))["error"])
		self.change_two_places()
		write(os.path.join(self.repo, "notes.txt"), "remember\n")
		git(self.repo, "add", "a.txt")
		staged = await self.plan("commit", message="Five and twenty-five\n\nThe body.")
		self.assertEqual("Commit to main: Five and twenty-five (1 file, +2 -2)", staged["summary"])
		self.assertIn("diff --git a/a.txt b/a.txt\n", staged["diff"])
		self.assertNotIn("notes.txt", staged["diff"], "what is staged, nothing else")
		named = await self.plan("commit", message="Notes", paths=["notes.txt", "src"])
		self.assertEqual("Commit to main: Notes (1 file, +1 -0)", named["summary"])
		self.assertIn("+++ b/notes.txt\n@@ -0,0 +1 @@\n+remember\n", named["diff"])
		self.assertNotIn("a.txt", named["diff"], "exactly its paths")
		git(self.repo, "reset", "-q")
		write(os.path.join(self.repo, "src", "main.c"), "int main(void) { return 2; }\n")
		git(self.repo, "add", "src/main.c")
		write(os.path.join(self.repo, "src", "main.c"), "int main(void) { return 3; }\n")
		worktree = await self.plan("commit", message="Three", paths=["src/main.c"])
		self.assertIn("-int main(void) { return 0; }\n+int main(void) { return 3; }\n", worktree["diff"], "the work tree against HEAD, staged or not")
		self.assertIn("return 2", git(self.repo, "diff", "--cached"), "the index as it was")
		head = git_out(self.repo, "rev-parse", "--short=7", "HEAD")
		self.assertEqual("Amend %s on main: Second again (1 file, +1 -1)" % head, (await self.plan("commit", message="Second again", amend=True))["summary"])
		remote = os.path.join(self.root, "remote.git")
		git(self.root, "init", "-q", "--bare", "-b", "main", remote)
		git(self.repo, "remote", "add", "origin", remote)
		git(self.repo, "push", "-q", "-u", "origin", "main")
		self.assertIn("amending it would rewrite pushed history", (await self.plan("commit", message="Fix", amend=True))["error"])

	async def test_branches_and_stashes_say_what_they_do(self):
		commit = git_out(self.repo, "log", "-1", "--format=%h %s")
		created = await self.plan("branch", action="create", name="feat", switch=True)
		self.assertEqual(("Create branch feat at HEAD (%s) and switch to it" % commit, "", 0, 0), (created["summary"], created["diff"], created["plus"], created["minus"]))
		self.assertIn("there is no commit nope", (await self.plan("branch", action="create", name="feat", **{"from": "nope"}))["error"])
		self.assertIn("there is no branch feat", (await self.plan("branch", action="switch", name="feat"))["error"])
		self.assertIn("is not a name git takes", (await self.plan("branch", action="create", name="--force"))["error"])
		git(self.repo, "branch", "feat")
		self.assertIn("a branch feat already exists", (await self.plan("branch", action="create", name="feat"))["error"])
		self.assertEqual("Switch from main to feat (%s)" % commit, (await self.plan("branch", action="switch", name="feat"))["summary"])
		self.assertEqual("Delete branch feat (%s); git keeps it unless it is merged" % commit, (await self.plan("branch", action="delete", name="feat"))["summary"])
		self.assertIn("there are no local changes to stash", (await self.plan("stash", action="push"))["error"])
		self.assertIn("there is no stash@{0}", (await self.plan("stash", action="pop"))["error"])
		write(os.path.join(self.repo, "a.txt"), "wip\n")
		write(os.path.join(self.repo, "notes.txt"), "remember\n")
		stash = await self.plan("stash", action="push", message="wip on a", untracked=True)
		self.assertEqual(("Stash the local changes: 1 file, +1 -30, 1 untracked file, as \"wip on a\"", 1, 30), (stash["summary"], stash["plus"], stash["minus"]))
		git(self.repo, "stash", "push", "-q", "-m", "wip on a")
		self.assertEqual("Pop stash@{0} (On main: wip on a): 1 file, +1 -30, taken off the list", (await self.plan("stash", action="pop"))["summary"])
		self.assertEqual("Apply stash@{0} (On main: wip on a): 1 file, +1 -30, kept on the list", (await self.plan("stash", action="apply", index=0))["summary"])
		self.assertEqual(1, len(git(self.repo, "stash", "list").splitlines()), "a plan changes nothing")


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
		write(os.path.join(self.lib, "y.c"), "int y;\n")
		plan = await self.plan("stage", paths=["lib"])
		self.assertEqual("Stage lib: 2 files, +2 -1", plan["summary"])
		self.assertIn("diff --git a/lib/x.c b/lib/x.c\n", plan["diff"])
		self.assertIn("+++ b/lib/y.c\n@@ -0,0 +1 @@\n+int y;\n", plan["diff"], "a new file under its workspace path")
		hunk = await self.plan("stage", paths=["lib/x.c"], hunks=[1])
		self.assertTrue(hunk["diff"].startswith("diff --git a/lib/x.c b/lib/x.c\n--- a/lib/x.c\n+++ b/lib/x.c\n@@ -1 +1 @@ (hunk 1)\n-int x;\n+int x = 1;\n"), hunk["diff"])
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
		git(self.repo, "add", "a.txt", "src/main.c")
		plan = await self.plan("commit", message="Both")
		self.assertEqual("Commit to main: Both (2 files, +1 -1, 1 outside the workspace's folder)", plan["summary"])
		self.assertTrue(plan["diff"].startswith("diff --git a/main.c b/main.c\n"), plan["diff"])
		self.assertNotIn("a.txt", plan["diff"])
		git(self.repo, "reset", "-q")
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
		self.assertEqual(("Commit to main: Return 1 (1 file, +1 -1)", 1, 1), (request["summary"], request["plus"], request["minus"]), "the card says what goes in")
		self.assertTrue(request["diff"].startswith("diff --git a/src/main.c b/src/main.c\n"), request["diff"])
		self.assertIn("-\treturn 0;\n+\treturn 1;\n", request["diff"])
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
	async def test_a_call_its_plan_says_cannot_work_fails_without_asking(self):
		await self.repos_found()
		chat_id, topic = await self.chat()
		self.model.queue(call_step("git_stage", {"paths": ["src/main.c"], "hunks": [9]}, "c1"), text_step("No such hunk."))
		await self.turn(chat_id, "stage hunk 9")
		self.assertEqual([], [f for f in self.frames if f.get("kind") == "approval.request"], "nothing to approve")
		results = {m["tool_call_id"]: m["content"] for m in self.model.requests()[-1]["body"]["messages"] if m["role"] == "tool"}
		self.assertIn("git_stage: src/main.c has 1 hunk in git_diff; there is no hunk 9", results["c1"])
		self.assertEqual("", subprocess.run(["git", "diff", "--cached"], cwd=self.repo, capture_output=True, text=True).stdout)

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


class GitToolPlanFailureChatTests(ChatCase):
	"""A plan that cannot be had leaves the approval card as it was, saying why."""

	def start_server(self):
		add_git(self.home, plans={"git_branch": "Git::NoSuchPlan"})
		subprocess.run(["git", "init", "-q", "-b", "main"], cwd=self.repo, check=True)
		for args in (["config", "user.name", "Owner"], ["config", "user.email", "owner@example.com"], ["add", "."], ["commit", "-q", "-m", "First"]):
			subprocess.run(["git", *args], cwd=self.repo, check=True, capture_output=True)
		return super().start_server()

	async def test_the_plain_card_says_why(self):
		for _ in range(100):
			if (await self.client.request("git.repos", {"wsId": "w1"}))["repos"]:
				break
			await asyncio.sleep(0.05)
		chat_id, topic = await self.chat()
		self.model.queue(call_step("git_branch", {"action": "create", "name": "feat"}, "c1"), text_step("Made."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "make a branch"})
		request = (await self.client.wait_for(topic, lambda f: f.get("kind") == "approval.request", timeout=TURN_SECONDS))[-1]["data"]
		self.assertTrue(request["summary"].startswith("Run git_branch (plugin ied.git); no plan: its code has no function 'Git::NoSuchPlan'"), request["summary"])
		self.assertFalse(request.get("diff"))
		await self.client.request("chat.approve", {"chatId": chat_id, "approvalId": request["approvalId"], "decision": "allow"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=TURN_SECONDS)
		self.assertIn("feat", subprocess.run(["git", "branch", "--list", "feat"], cwd=self.repo, capture_output=True, text=True).stdout, "the call runs as before")
