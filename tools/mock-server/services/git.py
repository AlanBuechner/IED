"""Git (docs/10 "Git"): one canned repository per workspace, shaped like mockup 05 - branches with upstreams, a merge, a
worktree's branch, a tag, a stash, local changes - listed on 'gitrepos:<wsId>' and kept on 'git:<repoId>'. The methods
change it in memory as git would (staging moves files between the lists, a commit becomes HEAD's child) and fetch, pull
and push stream progress lines as operations."""

import asyncio
import hashlib
import time

from rpc import RpcError

AUTHOR = ("owner", "owner@example.com")
# subject, parents (indexes further down), branches/tags pointing at it, hours ago, files {path: (status, added, removed)}
HISTORY = [
	("Log: Log.ch declarations and ConsoleSink", [2], ["port/logging"], 1, {"src/engine/render.h": ("M", 12, 2), "src/engine/render.c": ("M", 30, 4)}),
	("Time: fix engine.time delta test after QPC fallback", [3], ["agents/fix-time-test"], 1.2, {"tests/test_math.c": ("M", 6, 3)}),
	("Log: port LogLevel and LogRecord from RefEngine", [3], [], 1.4, {"src/engine/render.h": ("M", 20, 0)}),
	("Merge branch 'port/events'", [5, 4], ["master", "origin/master", "origin/port/logging"], 20, {}),
	("Events: window resize and key events in the blob format", [6], ["port/events", "origin/port/events"], 21, {"src/game/player.c": ("M", 18, 5)}),
	("Core: Ring<T> fixed-capacity ring buffer + tests", [7], [], 22, {"src/engine/math.h": ("A", 64, 0), "tests/test_math.c": ("A", 48, 0), "src/main.c": ("M", 1, 0)}),
	("Events: EventBlock blob format matches RefEngine byte-for-byte", [7], [], 23, {"src/game/player.h": ("M", 9, 1)}),
	("Restore moduleLinks = [\"Engine\"] in the root manifest", [8], [], 72, {"assets/config.json": ("M", 1, 1)}),
	("Math: column-major Matrix4 to match RefEngine layout", [9], [], 74, {"src/engine/math.h": ("M", 40, 22)}),
	("Core: Time via QPC with GetTickCount64 fallback", [10], [], 98, {"src/main.c": ("M", 14, 2)}),
	("Application: staged driver swap checked every frame", [11], [], 100, {"src/main.c": ("M", 8, 3)}),
	("Windowing: headless WindowsManager path for tests", [12], [], 124, {"src/engine/render.c": ("M", 25, 0)}),
	("Baseline: MyGame engine port (RefEngine -> C4)", [13], ["tag:m0-baseline"], 150, {"docs/design.md": ("A", 40, 0)}),
	("Nuke: nuke.toml with the Engine module and Game root", [14], [], 200, {"README.md": ("M", 6, 0)}),
	("Initial commit", [], [], 220, {"README.md": ("A", 12, 0), ".gitignore": ("A", 3, 0), "src/main.c": ("A", 20, 0)}),
]
LOCAL = [("unstaged", "src/game/player.c", "M"), ("unstaged", "src/engine/render.h", "M"), ("untracked", "src/engine/log.ch", "?"), ("staged", "docs/design.md", "M")]
HUNK = {"oldStart": 3, "oldLines": 4, "newStart": 3, "newLines": 5, "header": "@@ -3,4 +3,5 @@", "lines": [" #include \"player.h\"", " ", "-int speed = 4;", "+int speed = 6;", "+int jump = 2;", " "]}


def oid_of(text):
	return hashlib.sha1(text.encode()).hexdigest()


class Repo:
	def __init__(self, repo_id, name):
		self.repo_id = repo_id
		self.name = name
		self.commits = []
		self.by_oid = {}
		now_ms = int(time.time() * 1000)
		oids = [oid_of("%s|%d" % (h[0], i)) for i, h in enumerate(HISTORY)]
		for i, (subject, parents, _, hours, files) in enumerate(HISTORY):
			self.add_commit(oids[i], [oids[p] for p in parents], subject, now_ms - int(hours * 3600000), files)
		self.branches = {}
		self.remotes = {"origin": {}}
		self.tags = {}
		for i, (_, _, refs, _, _) in enumerate(HISTORY):
			for ref in refs:
				if ref.startswith("tag:"):
					self.tags[ref[4:]] = oids[i]
				elif ref.startswith("origin/"):
					self.remotes["origin"][ref[7:]] = oids[i]
				else:
					self.branches[ref] = oids[i]
		self.upstreams = {"port/logging": "origin/port/logging", "master": "origin/master", "port/events": "origin/port/events"}
		self.current = "port/logging"
		self.stashes = [{"message": "On port/logging: fmt experiment", "oid": oid_of("stash"), "files": {"src/main.c": "M"}}]
		self.changes = {"staged": [], "unstaged": [], "untracked": [], "conflicted": []}
		for where, path, status in LOCAL:
			self.changes[where].append({"path": path, "status": status})
		self.op = None

	def add_commit(self, oid, parents, subject, ms, files, body=""):
		commit = {"oid": oid, "parents": parents, "author": AUTHOR[0], "email": AUTHOR[1], "authorMs": ms, "subject": subject, "body": body, "files": files}
		if self.commits and ms > self.commits[0]["authorMs"]:
			self.commits.insert(0, commit)
		else:
			self.commits.append(commit)
		self.by_oid[oid] = commit
		return commit

	def ahead_behind(self, branch):
		upstream = self.upstreams.get(branch)
		if not upstream:
			return 0, 0
		mine = self.reachable(self.branches[branch])
		theirs = self.reachable(self.remotes["origin"].get(upstream[7:]))
		return len(mine - theirs), len(theirs - mine)

	def reachable(self, oid):
		seen, todo = set(), [oid] if oid else []
		while todo:
			oid = todo.pop()
			if oid in seen or oid not in self.by_oid:
				continue
			seen.add(oid)
			todo.extend(self.by_oid[oid]["parents"])
		return seen

	def head(self):
		ahead, behind = self.ahead_behind(self.current)
		return {"branch": self.current, "oid": self.branches[self.current], "detached": False, "upstream": self.upstreams.get(self.current, ""), "ahead": ahead, "behind": behind, "state": ""}

	def refs(self):
		branches = []
		for name in sorted(self.branches):
			ahead, behind = self.ahead_behind(name)
			branches.append({"name": name, "oid": self.branches[name], "upstream": self.upstreams.get(name, ""), "ahead": ahead, "behind": behind, "current": name == self.current, "gone": False})
		remotes = [{"name": r, "url": "https://example.com/mygame.git", "branches": [{"name": b, "oid": o} for b, o in sorted(bs.items())]} for r, bs in self.remotes.items()]
		return {
			"branches": branches, "remotes": remotes,
			"tags": [{"name": n, "oid": o} for n, o in sorted(self.tags.items())],
			"stashes": [{"index": i, "message": s["message"], "oid": s["oid"]} for i, s in enumerate(self.stashes)],
			"worktrees": [{"path": "C:/Work/MyGame", "branch": self.current, "oid": self.branches[self.current], "current": True, "locked": False},
				{"path": "C:/Work/MyGame/.ied/worktrees/fix-time-test", "branch": "agents/fix-time-test", "oid": self.branches.get("agents/fix-time-test", ""), "current": False, "locked": False}],
			"submodules": [],
		}

	def snapshot(self):
		return {"repoId": self.repo_id, "root": "", "head": self.head(), "changes": self.changes, "refs": self.refs(), "op": self.op}


class Git:
	def __init__(self, server):
		self.server = server
		self.repos = {}
		self.by_ws = {}
		self.op_count = 0
		for n, ws_id in enumerate(server.workspace.items, start=1):
			repo = Repo("r%d" % n, server.workspace.items[ws_id].name)
			self.repos[repo.repo_id] = repo
			self.by_ws[ws_id] = repo
			server.hub.add("gitrepos:" + ws_id, lambda repo=repo: {"repos": [{"repoId": repo.repo_id, "root": "", "name": repo.name, "kind": "repo"}], "error": "", "discovering": False})
			server.hub.add("git:" + repo.repo_id, repo.snapshot)
		for name in ("repos", "log", "show", "diff", "fileAt", "stage", "unstage", "stagePatch", "discardPatch", "discard", "commit", "lastMessage",
				"checkout", "branch", "branchDelete", "branchRename", "stash", "stashApply", "stashDrop", "fetch", "pull", "push"):
			server.rpc.register("git." + name, getattr(self, "m_" + name))

	def repo(self, p):
		repo = self.repos.get(p.str("repoId"))
		if repo is None:
			raise RpcError("not_found", "no repository %s" % p.str("repoId"))
		return repo

	def status_changed(self, repo):
		self.server.hub.publish("git:" + repo.repo_id, "status.changed", {"head": repo.head(), "changes": repo.changes})

	def refs_changed(self, repo):
		self.server.hub.publish("git:" + repo.repo_id, "refs.changed", {"refs": repo.refs()})
		self.status_changed(repo)

	async def m_repos(self, call, p):
		repo = self.by_ws.get(p.str("wsId"))
		if repo is None:
			raise RpcError("not_found", "no workspace %s" % p.str("wsId"))
		return {"repos": [{"repoId": repo.repo_id, "root": "", "name": repo.name, "kind": "repo"}], "error": "", "discovering": False}

	async def m_log(self, call, p):
		repo = self.repo(p)
		skip, limit, query = p.int("skip", 0, required=False), p.int("limit", 200, required=False), p.str("query", "", required=False).strip().lower()
		if not 1 <= limit <= 2000:
			raise RpcError("bad_request", "limit is 1 to 2000")
		order = [c for c in repo.commits if c["oid"] in set().union(*(repo.reachable(o) for o in self.tips(repo)))]
		if query:
			order = [c for c in order if c["oid"].startswith(query) or query in (c["subject"] + c["body"] + c["author"] + c["email"]).lower()]
		page = order[skip:skip + limit]
		return {"commits": [{k: c[k] for k in ("oid", "parents", "author", "email", "authorMs", "subject")} for c in page], "more": len(order) > skip + limit}

	def tips(self, repo):
		return list(repo.branches.values()) + [o for bs in repo.remotes.values() for o in bs.values()] + list(repo.tags.values())

	async def m_show(self, call, p):
		repo = self.repo(p)
		oid = p.str("oid")
		commit = next((c for c in repo.commits if c["oid"].startswith(oid)), None) if len(oid) >= 4 else None
		if commit is None:
			raise RpcError("not_found", "no commit %s" % oid)
		files = [{"path": path, "status": s, "added": a, "removed": r, "binary": False} for path, (s, a, r) in sorted(commit["files"].items())]
		return dict({k: commit[k] for k in ("oid", "parents", "author", "email", "authorMs", "subject", "body")}, committer=commit["author"], committerEmail=commit["email"], commitMs=commit["authorMs"], files=files)

	async def m_diff(self, call, p):
		self.repo(p)
		path = p.str("path")
		return {"path": path, "binary": False, "hunks": [HUNK], "truncated": False}

	async def m_fileAt(self, call, p):
		self.repo(p)
		path = p.str("path")
		ws = next(iter(self.server.workspace.items.values()))
		file = ws.files.get(path)
		return {"text": file.text if file else "// " + path + "\n", "binary": False, "missing": False, "truncated": False}

	def move(self, repo, paths, frm, to):
		for path in paths:
			for where in frm:
				for change in list(repo.changes[where]):
					if change["path"] == path:
						repo.changes[where].remove(change)
						if to and not any(c["path"] == path for c in repo.changes[to]):
							repo.changes[to].append({"path": path, "status": "A" if change["status"] == "?" and to == "staged" else "?" if change["status"] == "A" and to == "untracked" else change["status"]})

	async def m_stage(self, call, p):
		repo = self.repo(p)
		self.move(repo, p.list("paths"), ("unstaged", "untracked"), "staged")
		self.status_changed(repo)
		return {}

	async def m_unstage(self, call, p):
		repo = self.repo(p)
		paths = p.list("paths")
		for change in [c for c in repo.changes["staged"] if c["path"] in paths]:
			self.move(repo, [change["path"]], ("staged",), "untracked" if change["status"] == "A" else "unstaged")
		self.status_changed(repo)
		return {}

	def hunk_check(self, p):
		if p.int("hunk") != 0 or p.str("header") != HUNK["header"]:
			raise RpcError("conflict", "the file changed since its diff was read; look again")

	async def m_stagePatch(self, call, p):
		repo = self.repo(p)
		self.hunk_check(p)
		staged = p.bool("staged", False, required=False)
		self.move(repo, [p.str("path")], ("staged",) if staged else ("unstaged",), "unstaged" if staged else "staged")
		self.status_changed(repo)
		return {}

	async def m_discardPatch(self, call, p):
		repo = self.repo(p)
		self.hunk_check(p)
		self.move(repo, [p.str("path")], ("unstaged",), None)
		self.status_changed(repo)
		return {}

	async def m_discard(self, call, p):
		repo = self.repo(p)
		self.move(repo, p.list("paths"), ("unstaged", "untracked"), None)
		self.status_changed(repo)
		return {}

	async def m_commit(self, call, p):
		repo = self.repo(p)
		message = p.str("message").strip()
		amend = p.bool("amend", False, required=False)
		if not message:
			raise RpcError("bad_request", "a commit needs a message")
		if not repo.changes["staged"] and not amend:
			raise RpcError("bad_request", "nothing is staged")
		subject, _, body = message.partition("\n")
		head = repo.by_oid[repo.branches[repo.current]]
		parents = head["parents"] if amend else [head["oid"]]
		files = dict(head["files"]) if amend else {}
		files.update({c["path"]: (c["status"], 3, 1) for c in repo.changes["staged"]})
		commit = repo.add_commit(oid_of(message + str(time.time())), parents, subject, int(time.time() * 1000), files, body.strip())
		if amend:
			repo.commits.remove(head)
		repo.branches[repo.current] = commit["oid"]
		repo.changes["staged"] = []
		self.refs_changed(repo)
		return {"oid": commit["oid"]}

	async def m_lastMessage(self, call, p):
		repo = self.repo(p)
		head = repo.by_oid[repo.branches[repo.current]]
		return {"message": (head["subject"] + ("\n\n" + head["body"] if head["body"] else ""))}

	def name(self, p, key):
		name = p.str(key).strip()
		if not name or name.startswith("-"):
			raise RpcError("bad_request", "%s is not a name git takes" % key)
		return name

	async def m_checkout(self, call, p):
		repo = self.repo(p)
		branch = self.name(p, "branch")
		if p.bool("remote", False, required=False):
			remote, _, short = branch.partition("/")
			if short not in repo.remotes.get(remote, {}):
				raise RpcError("conflict", "fatal: invalid reference: " + branch)
			repo.branches.setdefault(short, repo.remotes[remote][short])
			repo.upstreams[short] = branch
			branch = short
		if branch not in repo.branches:
			raise RpcError("conflict", "fatal: invalid reference: " + branch)
		repo.current = branch
		self.refs_changed(repo)
		return {}

	async def m_branch(self, call, p):
		repo = self.repo(p)
		name = self.name(p, "name")
		if name in repo.branches:
			raise RpcError("bad_request", "fatal: a branch named '%s' already exists" % name)
		start = p.str("from", "", required=False)
		repo.branches[name] = next((c["oid"] for c in repo.commits if start and c["oid"].startswith(start)), repo.branches.get(start, repo.branches[repo.current]))
		if p.bool("checkout", False, required=False):
			repo.current = name
		self.refs_changed(repo)
		return {}

	async def m_branchDelete(self, call, p):
		repo = self.repo(p)
		name = self.name(p, "name")
		if name == repo.current or name not in repo.branches:
			raise RpcError("bad_request", "error: cannot delete branch '%s'" % name)
		merged = repo.branches[name] in repo.reachable(repo.branches[repo.current])
		if not merged and not p.bool("force", False, required=False):
			raise RpcError("conflict", "error: the branch '%s' is not fully merged" % name)
		del repo.branches[name]
		self.refs_changed(repo)
		return {}

	async def m_branchRename(self, call, p):
		repo = self.repo(p)
		name, to = self.name(p, "name"), self.name(p, "to")
		repo.branches[to] = repo.branches.pop(name)
		if repo.current == name:
			repo.current = to
		self.refs_changed(repo)
		return {}

	async def m_stash(self, call, p):
		repo = self.repo(p)
		changed = repo.changes["staged"] + repo.changes["unstaged"] + (repo.changes["untracked"] if p.bool("untracked", False, required=False) else [])
		if not changed:
			raise RpcError("bad_request", "no local changes to stash")
		message = p.str("message", "", required=False).strip() or "WIP"
		repo.stashes.insert(0, {"message": "On %s: %s" % (repo.current, message), "oid": oid_of(message + str(time.time())), "files": {c["path"]: c["status"] for c in changed}})
		repo.changes = {"staged": [], "unstaged": [], "untracked": [] if p.bool("untracked", False, required=False) else repo.changes["untracked"], "conflicted": []}
		self.refs_changed(repo)
		return {}

	def stash_at(self, repo, p):
		index = p.int("index")
		if not 0 <= index < len(repo.stashes):
			raise RpcError("not_found", "no stash@{%d}" % index)
		return index

	async def m_stashApply(self, call, p):
		repo = self.repo(p)
		index = self.stash_at(repo, p)
		for path, status in repo.stashes[index]["files"].items():
			where = "untracked" if status == "?" else "unstaged"
			if not any(c["path"] == path for c in repo.changes[where]):
				repo.changes[where].append({"path": path, "status": status})
		if p.bool("pop", False, required=False):
			repo.stashes.pop(index)
		self.refs_changed(repo)
		return {}

	async def m_stashDrop(self, call, p):
		repo = self.repo(p)
		repo.stashes.pop(self.stash_at(repo, p))
		self.refs_changed(repo)
		return {}

	async def m_fetch(self, call, p):
		return self.start_op(self.repo(p), "fetch", ["git fetch --progress --all"])

	async def m_pull(self, call, p):
		return self.start_op(self.repo(p), "pull", ["git pull --progress --no-rebase"])

	async def m_push(self, call, p):
		repo = self.repo(p)
		remote = p.str("remote", "", required=False)
		if remote and remote not in repo.remotes:
			raise RpcError("bad_request", "remote is not one of the repository's")
		return self.start_op(repo, "push", ["git push --progress" + (" --set-upstream" if p.bool("setUpstream", False, required=False) else "")])

	def start_op(self, repo, kind, lines):
		if repo.op:
			raise RpcError("busy", "a %s is running in this repository" % repo.op["kind"])
		self.op_count += 1
		op = {"opId": "op%d" % self.op_count, "kind": kind, "title": lines[0], "lines": []}
		repo.op = op
		asyncio.get_running_loop().create_task(self.run_op(repo, op))
		return {"opId": op["opId"]}

	async def run_op(self, repo, op):
		topic = "git:" + repo.repo_id
		steps = [op["title"], "Enumerating objects: 12, done.", "Counting objects: 50% (6/12)", "Counting objects: 100% (12/12), done.", "Writing objects: 100% (7/7), 1.2 KiB | 1.2 MiB/s, done."]
		for line in steps:
			op["lines"] = (op["lines"] + [line])[-20:]
			data = {"opId": op["opId"], "kind": op["kind"], "line": line}
			if "%" in line:
				data["percent"] = int(line.split("%")[0].split()[-1])
			self.server.hub.publish(topic, "op.progress", data)
			await self.server.clock.sleep(0.3)
		if op["kind"] == "push":
			repo.remotes["origin"][repo.current] = repo.branches[repo.current]
			repo.upstreams.setdefault(repo.current, "origin/" + repo.current)
		elif op["kind"] == "pull":
			upstream = repo.upstreams.get(repo.current)
			theirs = repo.remotes["origin"].get(upstream[7:]) if upstream else None
			if theirs and repo.branches[repo.current] in repo.reachable(theirs):
				repo.branches[repo.current] = theirs
		repo.op = None
		self.server.hub.publish(topic, "op.end", {"opId": op["opId"], "kind": op["kind"], "ok": True})
		self.refs_changed(repo)
