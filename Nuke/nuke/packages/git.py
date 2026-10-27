"""Git, run as a program: the one tool packages need. Git's own configuration does authentication (the credential
manager, SSH keys), so a private repository works wherever `git clone` works; Nuke never sees a credential. Git never
asks on the terminal (GIT_TERMINAL_PROMPT=0): a build in IED has no one to answer, so a missing sign-in is an error
saying how to sign in."""

import os
import shutil
import subprocess
import tarfile
import tempfile

from ..errors import NukeError

TOOL_ENV = "NUKE_TOOL_GIT"
QUIET_ENV = {"GIT_TERMINAL_PROMPT": "0", "GIT_ADVICE": "0", "LC_ALL": "C"}


class GitError(NukeError):
	pass


def program():
	found = os.environ.get(TOOL_ENV) or shutil.which("git")
	if not found:
		raise NukeError(f"packages need git, which is not on PATH (or set {TOOL_ENV} to its path)")
	return found


def _environment():
	env = dict(os.environ)
	env.update(QUIET_ENV)
	return env


def run(args, cwd=None, check=True, input_bytes=None, what=None):
	"""Run git; returns its stdout as text. A failure raises GitError with git's own message."""
	result = subprocess.run([program()] + list(args), cwd=cwd, env=_environment(), input=input_bytes,
		stdin=None if input_bytes is not None else subprocess.DEVNULL, capture_output=True)
	if check and result.returncode != 0:
		message = result.stderr.decode("utf-8", "replace").strip() or result.stdout.decode("utf-8", "replace").strip()
		raise GitError(f"{what or 'git ' + ' '.join(args[:2])}: {message}")
	return result.stdout.decode("utf-8", "replace") if result.returncode == 0 else None


def run_bytes(args, cwd=None, input_bytes=None, what=None):
	result = subprocess.run([program()] + list(args), cwd=cwd, env=_environment(), input=input_bytes, capture_output=True)
	if result.returncode != 0:
		raise GitError(f"{what or 'git ' + ' '.join(args[:2])}: {result.stderr.decode('utf-8', 'replace').strip()}")
	return result.stdout


def unreachable(url, error):
	return GitError(f"cannot read {url}: {error}\n"
		"  For a private repository, sign in once in a terminal (`git ls-remote <url>` asks the credential manager) or use an "
		"SSH URL (git@host:owner/repo.git) with your key loaded.")


def remote_refs(url):
	"""{ref name: commit} of a repository: its tags (peeled to commits) and branches, and HEAD; one network round trip."""
	try:
		text = run(["ls-remote", "--tags", "--heads", url], what=f"git ls-remote {url}")
		head = run(["ls-remote", "--symref", url, "HEAD"], check=False) or ""
	except GitError as error:
		raise unreachable(url, str(error).split(": ", 1)[-1]) from None
	refs = _refs(text)
	for line in head.splitlines():
		if line.startswith("ref: ") and "\tHEAD" in line:
			refs["HEAD"] = line[5:].split("\t", 1)[0]
	return refs


def _refs(text):
	refs = {}
	for line in text.splitlines():
		commit, _, name = line.partition("\t")
		if not name:
			continue
		if name.endswith("^{}"):
			refs[name[:-3]] = commit     # an annotated tag's commit
		else:
			refs.setdefault(name, commit)
	return refs


def local_refs(repo):
	"""{ref name: commit} of a local repository (a mirror or a checkout): tags peeled, branches, remote branches."""
	text = run(["for-each-ref", "--format=%(objectname) %(*objectname) %(refname)"], cwd=repo)
	refs = {}
	for line in text.splitlines():
		parts = line.split(" ")
		if len(parts) == 3:
			refs[parts[2]] = parts[1] or parts[0]
		elif len(parts) == 2:
			refs[parts[1]] = parts[0]
	return refs


# --- the mirror ------------------------------------------------------------------------------------

def ensure_mirror(path, url):
	"""A bare mirror of `url` at `path`, created on first use. Returns True when it had to be created (fetched)."""
	if os.path.isfile(os.path.join(path, "HEAD")):
		return False
	os.makedirs(os.path.dirname(path), exist_ok=True)
	temporary = path + ".partial"
	shutil.rmtree(temporary, ignore_errors=True)
	try:
		run(["clone", "--bare", "--quiet", url, temporary], what=f"git clone {url}")
	except GitError as error:
		shutil.rmtree(temporary, ignore_errors=True)
		raise unreachable(url, str(error).split(": ", 1)[-1]) from None
	run(["config", "remote.origin.fetch", "+refs/heads/*:refs/heads/*"], cwd=temporary)
	run(["config", "--add", "remote.origin.fetch", "+refs/tags/*:refs/tags/*"], cwd=temporary)
	os.replace(temporary, path)
	return True


def fetch(path, url):
	try:
		run(["fetch", "--quiet", "--prune", "--force", "origin"], cwd=path, what=f"git fetch {url}")
	except GitError as error:
		raise unreachable(url, str(error).split(": ", 1)[-1]) from None


def has_commit(repo, commit):
	return run(["cat-file", "-e", f"{commit}^{{commit}}"], cwd=repo, check=False) is not None


def fetch_commit(path, url, commit):
	"""Make sure the mirror has `commit`: a fetch of everything, then of the commit itself (a server that lets one fetch
	any reachable commit has it even when no branch or tag names it any more)."""
	if has_commit(path, commit):
		return
	fetch(path, url)
	if not has_commit(path, commit):
		run(["fetch", "--quiet", "origin", commit], cwd=path, check=False)
	if not has_commit(path, commit):
		raise GitError(f"commit {commit[:12]} is not in {url}: it was never pushed, or it was removed (a force-push). "
			"Push it from the checkout that made it, or run `nuke package update`")


# --- reading a commit ------------------------------------------------------------------------------

def tree_files(repo, commit, name):
	"""Paths (relative to the repository) of every file called `name` in the commit."""
	text = run(["ls-tree", "-r", "--name-only", "-z", commit], cwd=repo)
	return sorted(p for p in text.split("\0") if p and (p == name or p.endswith("/" + name)))


def read_files(repo, commit, paths):
	"""{path: bytes} of files of a commit, in one `git cat-file --batch`."""
	if not paths:
		return {}
	request = "".join(f"{commit}:{p}\n" for p in paths).encode("utf-8")
	data = run_bytes(["cat-file", "--batch"], cwd=repo, input_bytes=request)
	files = {}
	at = 0
	for path in paths:
		end = data.index(b"\n", at)
		header = data[at:end].decode("utf-8", "replace").split(" ")
		at = end + 1
		if len(header) < 3 or header[1] == "missing":
			continue
		size = int(header[2])
		files[path] = data[at:at + size]
		at += size + 1
	return files


def describe(repo, commit, pattern):
	"""The nearest tag matching `pattern` in the commit's history, and how many commits the commit is after it: (tag,
	distance), or (None, commits in the history) when there is none."""
	text = run(["describe", "--tags", "--long", "--abbrev=40", f"--match={pattern}", commit], cwd=repo, check=False)
	if text:
		tag, distance, _ = text.strip().rsplit("-", 2)
		return tag, int(distance)
	count = run(["rev-list", "--count", commit], cwd=repo)
	return None, int(count.strip())


def is_ancestor(repo, older, newer):
	result = subprocess.run([program(), "merge-base", "--is-ancestor", older, newer], cwd=repo, env=_environment(),
		capture_output=True)
	return result.returncode == 0


def export(repo, commit, path, dest):
	"""The folder `path` of the commit (the whole tree for "") written into `dest`, which must not exist."""
	spec = f"{commit}:{path}" if path else commit
	data = run_bytes(["archive", "--format=tar", spec], cwd=repo, what=f"git archive {commit[:12]} {path or '.'}")
	temporary = dest + ".partial"
	shutil.rmtree(temporary, ignore_errors=True)
	os.makedirs(temporary)
	with tempfile.TemporaryFile() as buffer:
		buffer.write(data)
		buffer.seek(0)
		with tarfile.open(fileobj=buffer) as archive:
			archive.extractall(temporary, filter="data")
	os.replace(temporary, dest)


# --- checkouts -------------------------------------------------------------------------------------

def clone(url, dest, reference=None, branch=None):
	"""A working clone of `url` at `dest`; objects come from `reference` (a mirror) when given, then are copied so the
	clone does not depend on it."""
	args = ["clone", "--quiet"]
	if reference:
		args += ["--reference", reference, "--dissociate"]
	if branch:
		args += ["--branch", branch]
	try:
		run(args + [url, dest], what=f"git clone {url}")
	except GitError as error:
		raise unreachable(url, str(error).split(": ", 1)[-1]) from None


def head(repo):
	return run(["rev-parse", "HEAD"], cwd=repo).strip()


def branch(repo):
	"""The checked-out branch, or None on a detached HEAD."""
	text = run(["symbolic-ref", "--quiet", "--short", "HEAD"], cwd=repo, check=False)
	return text.strip() if text else None


def upstream(repo):
	text = run(["rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}"], cwd=repo, check=False)
	return text.strip() if text else None


def changes(repo):
	"""Uncommitted changes as `git status --porcelain` lines (untracked files included)."""
	text = run(["status", "--porcelain"], cwd=repo)
	return [line for line in text.splitlines() if line.strip()]


def ahead_behind(repo, upstream_ref):
	"""(commits HEAD has that the upstream lacks, commits the upstream has that HEAD lacks)."""
	text = run(["rev-list", "--left-right", "--count", f"HEAD...{upstream_ref}"], cwd=repo, check=False)
	if not text:
		return 0, 0
	ahead, behind = text.split()
	return int(ahead), int(behind)


def remote_url(repo):
	text = run(["remote", "get-url", "origin"], cwd=repo, check=False)
	return text.strip() if text else None


def unpushed(repo):
	"""Commits of HEAD that no remote branch has."""
	text = run(["rev-list", "HEAD", "--not", "--remotes"], cwd=repo, check=False) or ""
	return [line for line in text.splitlines() if line.strip()]
