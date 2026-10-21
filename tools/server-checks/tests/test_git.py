"""Git (docs/10 "Git"): a trusted workspace's repositories found and listed on gitrepos:<wsId>, each git:<repoId> topic
following the work tree and the refs as they change, the methods that read (log, show, diff, fileAt) and those that
change (stage, a hunk or some lines of one, discard, commit, branches, stashes), fetch and push as operations that
stream, and no git at all in an untrusted workspace. Against real repositories made with the git CLI."""

import asyncio
import json
import os
import shutil
import subprocess
import tempfile

from frames import FrameChecker
from iedclient import Client, RpcFailure
from server import REPO, IedServer, ServerTestCase

LINES = "".join("line %d\n" % n for n in range(1, 31))


def write(path, text):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8", newline="") as f:
		f.write(text)


def git(folder, *args):
	return subprocess.run(["git", "-c", "core.autocrlf=false", *args], cwd=folder, check=True, capture_output=True, text=True).stdout


def make_repo(folder):
	os.makedirs(folder, exist_ok=True)
	git(folder, "init", "-q", "-b", "main")
	git(folder, "config", "user.name", "Owner")
	git(folder, "config", "user.email", "owner@example.com")
	git(folder, "config", "core.autocrlf", "false")
	write(os.path.join(folder, "a.txt"), LINES)
	write(os.path.join(folder, "src", "main.c"), "int main(void) { return 0; }\n")
	git(folder, "add", ".")
	git(folder, "commit", "-q", "-m", "First: the files")
	write(os.path.join(folder, "a.txt"), LINES.replace("line 2\n", "line two\n"))
	git(folder, "commit", "-q", "-am", "Second: line two\n\nSpelled out.")


def make_home(home):
	"""An install with the host's schemas and the git plugin's settings, without its UI."""
	shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(home, "shared", "host-api", "schemas"))
	plugin = os.path.join(home, "plugins", "git")
	os.makedirs(plugin)
	shutil.copy(os.path.join(REPO, "plugins", "git", "settings.schema.json"), plugin)
	write(os.path.join(plugin, "ied-plugin.json"), json.dumps({"manifestVersion": 1, "id": "ied.git", "name": "Git", "version": "0.1.0", "ied": {"api": "^1"},
		"runtime": {"kind": "declarative"}, "contributes": {"settings": {"schema": "settings.schema.json", "page": "Source Control"}}}))
	return home


class GitTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-git-")
		self.repo = os.path.join(self.root, "Game")
		make_repo(self.repo)
		self.home = make_home(os.path.join(self.root, "home"))
		self.server = IedServer(extra=("--home", self.home, "--workspace", self.repo))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		self.repo_id = await self.first_repo()
		self.topic = "git:" + self.repo_id
		await self.client.sub([self.topic])
		self.state = {}
		self.apply(await self.client.next_frame(self.topic))
		self.snap = await self.until(lambda s: s["head"] and s["refs"])

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def first_repo(self):
		for _ in range(100):
			repos = await self.client.request("git.repos", {"wsId": "w1"})
			if repos["repos"]:
				return repos["repos"][0]["repoId"]
			await asyncio.sleep(0.05)
		self.fail("no repository found: %s" % repos)

	def apply(self, frame):
		"""The topic's state as the frames say: its snapshot, then each change."""
		kind, data = frame.get("kind"), frame.get("data") or {}
		if frame.get("t") == "snap":
			self.state = dict(data)
		elif kind == "status.changed":
			self.state.update(head=data["head"], changes=data["changes"])
		elif kind == "refs.changed":
			self.state["refs"] = data["refs"]

	async def until(self, predicate, timeout=10):
		"""The state once `predicate` holds for it (it may already)."""
		deadline = asyncio.get_running_loop().time() + timeout
		while not predicate(self.state):
			self.apply(await self.client.next_frame(self.topic, max(0.01, deadline - asyncio.get_running_loop().time())))
		return self.state

	async def status(self, predicate):
		return await self.until(lambda s: s.get("changes") is not None and predicate(s["changes"]))

	async def refs(self, predicate):
		return (await self.until(lambda s: s.get("refs") is not None and predicate(s["refs"])))["refs"]

	# --- Reading --------------------------------------------------------------------------------------------------------

	async def test_the_repository_and_its_snapshot(self):
		repos = await self.client.request("git.repos", {"wsId": "w1"})
		self.assertEqual([("", "Game", "repo")], [(r["root"], r["name"], r["kind"]) for r in repos["repos"]])
		head = self.snap["head"]
		self.assertEqual(("main", False, ""), (head["branch"], head["detached"], head["state"]))
		self.assertEqual(git(self.repo, "rev-parse", "HEAD").strip(), head["oid"])
		self.assertEqual({"staged": [], "unstaged": [], "untracked": [], "conflicted": []}, self.snap["changes"])
		self.assertEqual(["main"], [b["name"] for b in self.snap["refs"]["branches"]])
		self.assertTrue(self.snap["refs"]["branches"][0]["current"])
		self.assertIsNone(self.snap["op"])

	async def test_log_show_diff_and_file_at(self):
		log = await self.client.request("git.log", {"repoId": self.repo_id})
		self.assertEqual(["Second: line two", "First: the files"], [c["subject"] for c in log["commits"]])
		self.assertFalse(log["more"])
		second, first = log["commits"]
		self.assertEqual([first["oid"]], second["parents"])
		page = await self.client.request("git.log", {"repoId": self.repo_id, "limit": 1})
		self.assertEqual(([second["oid"]], True), ([c["oid"] for c in page["commits"]], page["more"]))
		found = await self.client.request("git.log", {"repoId": self.repo_id, "query": "FIRST"})
		self.assertEqual([first["oid"]], [c["oid"] for c in found["commits"]], "the message in any case")
		self.assertEqual([second["oid"]], [c["oid"] for c in (await self.client.request("git.log", {"repoId": self.repo_id, "query": second["oid"][:7]}))["commits"]], "a hash it starts")
		self.assertEqual(2, len((await self.client.request("git.log", {"repoId": self.repo_id, "query": "owner@example"}))["commits"]), "the author's email")
		shown = await self.client.request("git.show", {"repoId": self.repo_id, "oid": second["oid"]})
		self.assertEqual(("Second: line two", "Spelled out.", "Owner"), (shown["subject"], shown["body"], shown["author"]))
		self.assertEqual([{"path": "a.txt", "status": "M", "added": 1, "removed": 1, "binary": False}], shown["files"])
		root = await self.client.request("git.show", {"repoId": self.repo_id, "oid": first["oid"]})
		self.assertEqual(["a.txt", "src/main.c"], sorted(f["path"] for f in root["files"]), "a root commit against nothing")
		diff = await self.client.request("git.diff", {"repoId": self.repo_id, "path": "a.txt", "oid": second["oid"]})
		self.assertEqual(1, len(diff["hunks"]))
		self.assertIn("-line 2", diff["hunks"][0]["lines"])
		self.assertIn("+line two", diff["hunks"][0]["lines"])
		before = await self.client.request("git.fileAt", {"repoId": self.repo_id, "path": "a.txt", "rev": first["oid"]})
		self.assertEqual((LINES, False, False), (before["text"], before["binary"], before["missing"]))
		gone = await self.client.request("git.fileAt", {"repoId": self.repo_id, "path": "nope.txt", "rev": first["oid"]})
		self.assertTrue(gone["missing"])
		with self.assertRaises(RpcFailure) as unknown:
			await self.client.request("git.show", {"repoId": self.repo_id, "oid": "0000000"})
		self.assertEqual("not_found", unknown.exception.code)

	async def test_git_folders_are_not_listed(self):
		make_repo(os.path.join(self.repo, "libs", "inner"))
		root = await self.client.request("fs.list", {"wsId": "w1", "path": ""})
		self.assertNotIn(".git", [e["name"] for e in root["entries"]])
		inner = await self.client.request("fs.list", {"wsId": "w1", "path": "libs/inner"})
		self.assertEqual(["src", "a.txt"], [e["name"] for e in inner["entries"]], "a nested repository's .git is hidden too")

	# --- The work tree --------------------------------------------------------------------------------------------------

	async def test_the_status_follows_the_work_tree(self):
		write(os.path.join(self.repo, "notes.txt"), "new\n")
		data = await self.status(lambda c: c["untracked"])
		self.assertEqual([{"path": "notes.txt", "status": "?"}], data["changes"]["untracked"])
		write(os.path.join(self.repo, "src", "main.c"), "int main(void) { return 1; }\n")
		data = await self.status(lambda c: c["unstaged"])
		self.assertEqual([{"path": "src/main.c", "status": "M"}], data["changes"]["unstaged"])

	async def test_stage_unstage_and_commit(self):
		write(os.path.join(self.repo, "src", "main.c"), "int main(void) { return 1; }\n")
		write(os.path.join(self.repo, "notes.txt"), "new\n")
		await self.status(lambda c: c["unstaged"] and c["untracked"])
		with self.assertRaises(RpcFailure) as nothing:
			await self.client.request("git.commit", {"repoId": self.repo_id, "message": "x"})
		self.assertEqual("bad_request", nothing.exception.code)
		await self.client.request("git.stage", {"repoId": self.repo_id, "paths": ["src/main.c", "notes.txt"]})
		data = await self.status(lambda c: len(c["staged"]) == 2)
		self.assertEqual([], data["changes"]["unstaged"] + data["changes"]["untracked"])
		await self.client.request("git.unstage", {"repoId": self.repo_id, "paths": ["notes.txt"]})
		await self.status(lambda c: c["untracked"] == [{"path": "notes.txt", "status": "?"}])
		with self.assertRaises(RpcFailure) as empty:
			await self.client.request("git.commit", {"repoId": self.repo_id, "message": "  "})
		self.assertEqual("bad_request", empty.exception.code)
		made = await self.client.request("git.commit", {"repoId": self.repo_id, "message": "Third: return 1\n\nThe body."})
		self.assertEqual(git(self.repo, "rev-parse", "HEAD").strip(), made["oid"])
		self.assertEqual("Third: return 1\n\nThe body.", git(self.repo, "log", "-1", "--format=%B").strip())
		self.assertEqual({"message": "Third: return 1\n\nThe body."}, await self.client.request("git.lastMessage", {"repoId": self.repo_id}))
		await self.client.request("git.commit", {"repoId": self.repo_id, "message": "Third, amended", "amend": True})
		self.assertEqual(["Third, amended", "Second: line two"], git(self.repo, "log", "-2", "--format=%s").split("\n")[:2])
		sources = await self.client.request("out.sources", {"wsId": "w1"})
		self.assertIn("git", [s["sourceId"] for s in sources["sources"]])
		await self.client.sub(["out:w1:git"])
		tail = (await self.client.next_frame("out:w1:git"))["data"]["tail"]
		self.assertIn("> git commit -F ", tail, "a change's command goes to the Git output")

	async def test_stage_a_hunk_then_some_lines(self):
		text = LINES.replace("line 2\n", "line two\n").replace("line 25\n", "line twenty-five\nline 25.5\n")
		write(os.path.join(self.repo, "a.txt"), text)
		await self.status(lambda c: c["unstaged"])
		diff = await self.client.request("git.diff", {"repoId": self.repo_id, "path": "a.txt"})
		self.assertEqual(1, len(diff["hunks"]), "line 2 was committed as 'line two' already")
		hunk = diff["hunks"][0]
		self.assertEqual(["-line 25", "+line twenty-five", "+line 25.5"], [l for l in hunk["lines"] if l[0] in "+-"])
		added = hunk["lines"].index("+line 25.5")
		await self.client.request("git.stagePatch", {"repoId": self.repo_id, "path": "a.txt", "staged": False, "hunk": 0, "header": hunk["header"], "lines": [added]})
		staged = await self.client.request("git.diff", {"repoId": self.repo_id, "path": "a.txt", "staged": True})
		self.assertEqual(["+line 25.5"], [l for l in staged["hunks"][0]["lines"] if l[0] in "+-"], "one added line staged, the removed one left")
		with self.assertRaises(RpcFailure) as stale:
			await self.client.request("git.stagePatch", {"repoId": self.repo_id, "path": "a.txt", "staged": False, "hunk": 0, "header": "@@ -1 +1 @@"})
		self.assertEqual("conflict", stale.exception.code)
		staged_hunk = staged["hunks"][0]
		await self.client.request("git.stagePatch", {"repoId": self.repo_id, "path": "a.txt", "staged": True, "hunk": 0, "header": staged_hunk["header"]})
		self.assertEqual("", git(self.repo, "diff", "--cached"), "the hunk unstaged again")
		rest = await self.client.request("git.diff", {"repoId": self.repo_id, "path": "a.txt"})
		await self.client.request("git.stagePatch", {"repoId": self.repo_id, "path": "a.txt", "staged": False, "hunk": 0, "header": rest["hunks"][0]["header"]})
		self.assertEqual("", git(self.repo, "diff"), "the whole hunk staged")

	async def test_discard_a_hunk(self):
		committed = LINES.replace("line 2\n", "line two\n")
		write(os.path.join(self.repo, "a.txt"), committed.replace("line 5\n", "line five\n").replace("line 28\n", "line 28b\n"))
		await self.status(lambda c: c["unstaged"])
		diff = await self.client.request("git.diff", {"repoId": self.repo_id, "path": "a.txt"})
		self.assertEqual(2, len(diff["hunks"]))
		await self.client.request("git.discardPatch", {"repoId": self.repo_id, "path": "a.txt", "hunk": 1, "header": diff["hunks"][1]["header"]})
		with open(os.path.join(self.repo, "a.txt"), encoding="utf-8", newline="") as f:
			self.assertEqual(committed.replace("line 5\n", "line five\n"), f.read(), "the second hunk taken out of the work tree, the first kept")
		self.assertEqual("", git(self.repo, "diff", "--cached"), "the index untouched")

	async def test_discard(self):
		write(os.path.join(self.repo, "src", "main.c"), "changed\n")
		write(os.path.join(self.repo, "scratch.txt"), "throw away\n")
		await self.status(lambda c: c["unstaged"] and c["untracked"])
		await self.client.request("git.discard", {"repoId": self.repo_id, "paths": ["src/main.c", "scratch.txt"]})
		await self.status(lambda c: not c["unstaged"] and not c["untracked"])
		with open(os.path.join(self.repo, "src", "main.c"), encoding="utf-8") as f:
			self.assertEqual("int main(void) { return 0; }\n", f.read())
		self.assertFalse(os.path.exists(os.path.join(self.repo, "scratch.txt")))
		trash = await self.client.request("trash.list", {})
		self.assertIn("scratch.txt", [e["path"] for e in trash["entries"]], "an untracked file goes to the trash")

	# --- Branches and stashes -------------------------------------------------------------------------------------------

	async def test_branches(self):
		await self.client.request("git.branch", {"repoId": self.repo_id, "name": "feature/x", "checkout": True})
		self.assertEqual("feature/x", git(self.repo, "branch", "--show-current").strip())
		await self.refs(lambda r: [b["name"] for b in r["branches"] if b["current"]] == ["feature/x"])
		await self.client.request("git.branchRename", {"repoId": self.repo_id, "name": "feature/x", "to": "feature/y"})
		await self.client.request("git.checkout", {"repoId": self.repo_id, "branch": "main"})
		write(os.path.join(self.repo, "a.txt"), "on y\n")
		git(self.repo, "switch", "-q", "feature/y")
		git(self.repo, "commit", "-q", "-am", "On y")
		git(self.repo, "switch", "-q", "main")
		write(os.path.join(self.repo, "a.txt"), "local change\n")
		with self.assertRaises(RpcFailure) as dirty:
			await self.client.request("git.checkout", {"repoId": self.repo_id, "branch": "feature/y"})
		self.assertEqual(("conflict", ["a.txt"]), (dirty.exception.code, dirty.exception.data["files"]))
		git(self.repo, "checkout", "--", "a.txt")
		with self.assertRaises(RpcFailure) as unmerged:
			await self.client.request("git.branchDelete", {"repoId": self.repo_id, "name": "feature/y"})
		self.assertEqual("conflict", unmerged.exception.code)
		await self.client.request("git.branchDelete", {"repoId": self.repo_id, "name": "feature/y", "force": True})
		await self.refs(lambda r: [b["name"] for b in r["branches"]] == ["main"])
		with self.assertRaises(RpcFailure) as option:
			await self.client.request("git.branch", {"repoId": self.repo_id, "name": "--force"})
		self.assertEqual("bad_request", option.exception.code)

	async def test_stashes(self):
		write(os.path.join(self.repo, "a.txt"), "stashed\n")
		write(os.path.join(self.repo, "new.txt"), "untracked\n")
		await self.status(lambda c: c["unstaged"] and c["untracked"])
		await self.client.request("git.stash", {"repoId": self.repo_id, "message": "fmt experiment", "untracked": True})
		refs = await self.refs(lambda r: r["stashes"])
		self.assertEqual([(0, "On main: fmt experiment")], [(s["index"], s["message"]) for s in refs["stashes"]])
		await self.status(lambda c: not c["unstaged"] and not c["untracked"])
		with self.assertRaises(RpcFailure) as clean:
			await self.client.request("git.stash", {"repoId": self.repo_id})
		self.assertEqual("bad_request", clean.exception.code)
		await self.client.request("git.stashApply", {"repoId": self.repo_id, "index": 0})
		await self.status(lambda c: c["unstaged"] and c["untracked"])
		await self.client.request("git.stashDrop", {"repoId": self.repo_id, "index": 0})
		await self.refs(lambda r: r["stashes"] == [])
		log = await self.client.request("git.log", {"repoId": self.repo_id})
		self.assertEqual(2, len(log["commits"]), "a stash is not a commit of the graph")

	# --- Operations -----------------------------------------------------------------------------------------------------

	async def test_push_and_fetch_stream_as_operations(self):
		remote = os.path.join(self.root, "remote.git")
		git(self.root, "init", "-q", "--bare", remote)
		git(self.repo, "remote", "add", "origin", remote)
		started = await self.client.request("git.push", {"repoId": self.repo_id, "remote": "origin", "branch": "main", "setUpstream": True})
		frames = await self.client.wait_for(self.topic, lambda f: f.get("kind") == "op.end", timeout=30)
		for frame in frames:
			self.apply(frame)
		progress = [f["data"] for f in frames if f.get("kind") == "op.progress"]
		self.assertTrue(progress and progress[0]["line"].startswith("git push --progress"), progress)
		end = frames[-1]["data"]
		self.assertEqual((started["opId"], "push", True), (end["opId"], end["kind"], end["ok"]))
		refs = await self.refs(lambda r: r["remotes"] and r["remotes"][0]["branches"])
		self.assertEqual([("origin", ["main"])], [(r["name"], [b["name"] for b in r["branches"]]) for r in refs["remotes"]])
		self.assertEqual("origin/main", [b for b in refs["branches"] if b["name"] == "main"][0]["upstream"])
		await self.client.request("git.fetch", {"repoId": self.repo_id, "prune": True})
		end = (await self.client.wait_for(self.topic, lambda f: f.get("kind") == "op.end", timeout=30))[-1]["data"]
		self.assertTrue(end["ok"], end)
		await self.client.request("git.push", {"repoId": self.repo_id, "remote": "nowhere"})
		failed = (await self.client.wait_for(self.topic, lambda f: f.get("kind") == "op.end", timeout=30))[-1]["data"]
		self.assertFalse(failed["ok"])
		self.assertTrue(failed["error"], "git's words")

	# --- Trust ----------------------------------------------------------------------------------------------------------

	async def test_an_untrusted_workspace_runs_no_git(self):
		await self.client.sub(["gitrepos:w1"])
		await self.client.next_frame("gitrepos:w1")
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": False})
		changed = (await self.client.wait_for("gitrepos:w1", lambda f: f.get("kind") == "repos.changed"))[-1]["data"]
		self.assertEqual(([], "untrusted"), (changed["repos"], changed["error"]))
		with self.assertRaises(RpcFailure) as gone:
			await self.client.request("git.log", {"repoId": self.repo_id})
		self.assertEqual("not_found", gone.exception.code)
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		back = (await self.client.wait_for("gitrepos:w1", lambda f: f.get("kind") == "repos.changed" and f["data"]["repos"], timeout=10))[-1]["data"]
		self.assertEqual("", back["error"])


class NestedRepoTests(ServerTestCase):
	"""Repositories in the workspace's folders, and the one it is in when it is a folder of a repository."""

	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-git-nested-")
		self.outer = os.path.join(self.root, "Outer")
		make_repo(self.outer)
		make_repo(os.path.join(self.outer, "libs", "inner"))
		make_repo(os.path.join(self.outer, "build", "skipped"))
		self.home = make_home(os.path.join(self.root, "home"))

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def repos(self, folder):
		self.server = IedServer(extra=("--home", self.home, "--workspace", folder))
		self.assertTrue(self.server.started, self.server.error)
		client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		await client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		for _ in range(100):
			listed = await client.request("git.repos", {"wsId": "w1"})
			if listed["repos"]:
				break
			await asyncio.sleep(0.05)
		client.abort()
		return [(r["root"], r["name"]) for r in listed["repos"]]

	async def test_nested_repositories(self):
		self.assertEqual([("", "Outer"), ("libs/inner", "inner")], await self.repos(self.outer), "build folders are not looked in")

	async def test_a_folder_inside_a_repository(self):
		self.assertEqual([("..", "Outer")], await self.repos(os.path.join(self.outer, "src")))
