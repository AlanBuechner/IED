"""One source during one resolution: its versions (from its tags), its mirror, and what its commits hold. A source being
developed reads everything from its checkout instead: its tags, its HEAD, and the packages in its working tree."""

import re

from ..errors import NukeError
from . import git, index, semver
from .store import url_key

TAG = re.compile(r"^refs/tags/(?:(.+)/)?(v\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?)$")


class Repository:
	def __init__(self, url, name, cache, checkout=None, fresh=False):
		self.url = url
		self.name = name
		self.key = url_key(url)
		self.cache = cache
		self.checkout = checkout      # a develop checkout's folder: everything comes from its working tree
		self.fresh = fresh            # ask the remote for its tags (an update), not the mirror
		self._refs = None
		self._remote = False
		self._lines = None
		self._indexes = {}
		self._versions = {}

	@property
	def develop(self):
		return self.checkout is not None

	def label(self, commit=None):
		return f"{self.name}@{commit[:8]}" if commit else self.name

	# --- refs and versions -----------------------------------------------------------------------

	def refs(self):
		if self._refs is None:
			if self.checkout:
				self._refs = git.local_refs(self.checkout)
			elif not self.fresh and self.cache.has_mirror(self.url):
				self._refs = git.local_refs(self.cache.mirror_path(self.url))
			else:
				self._refs = git.remote_refs(self.url)
				self._remote = True
		return self._refs

	def refresh(self):
		"""Ask the remote for its refs when they came from the mirror; True when that is new information."""
		if self._remote or self.checkout:
			return False
		self._refs = git.remote_refs(self.url)
		self._remote = True
		self._lines = None
		return True

	def lines(self):
		"""{versioned package path: [(Version, commit)] newest first}, from the tags."""
		if self._lines is None:
			lines = {}
			for ref, commit in self.refs().items():
				match = TAG.match(ref)
				version = semver.parse_version(match.group(2)) if match else None
				if version is not None:
					lines.setdefault(match.group(1) or "", []).append((version, commit))
			self._lines = {line: sorted(found, key=lambda v: v[0].key(), reverse=True) for line, found in lines.items()}
		return self._lines

	def line_of(self, package_path):
		"""The versioned package `package_path` is released with: itself or the nearest folder above it with tags of its
		own, else the repository ("")."""
		lines = self.lines()
		path = package_path
		while path:
			if path in lines:
				return path
			path = path.rpartition("/")[0]
		return ""

	def releases(self, line):
		return self.lines().get(line, [])

	def tag_pattern(self, line):
		return f"{line}/v[0-9]*" if line else "v[0-9]*"

	def tag_name(self, line, version):
		return f"{line}/v{version}" if line else f"v{version}"

	def default_commit(self):
		"""The tip of the repository's default branch."""
		refs = self.refs()
		head = refs.get("HEAD")
		if head is None:
			text = git.run(["symbolic-ref", "HEAD"], cwd=self.checkout or self.cache.mirror(self.url), check=False)
			head = text.strip() if text else "refs/heads/main"
		commit = refs.get(head) or refs.get(head.replace("refs/heads/", "refs/remotes/origin/"))
		if commit is None:
			raise NukeError(f"{self.url} has no default branch ({head})")
		return commit

	def branch_commit(self, branch):
		for ref in (f"refs/heads/{branch}", f"refs/remotes/origin/{branch}"):
			if ref in self.refs():
				return self.refs()[ref]
		if self.refresh():
			return self.branch_commit(branch)
		raise NukeError(f"{self.url} has no branch '{branch}'")

	def resolve_rev(self, rev):
		repo = self.repo_dir()
		if re.fullmatch(r"[0-9a-f]{40}", rev) and not self.checkout:
			git.fetch_commit(repo, self.url, rev)
			return rev
		text = git.run(["rev-parse", "--verify", "--quiet", f"{rev}^{{commit}}"], cwd=repo, check=False)
		if not text and not self.checkout:
			git.fetch(repo, self.url)
			text = git.run(["rev-parse", "--verify", "--quiet", f"{rev}^{{commit}}"], cwd=repo, check=False)
		if not text:
			raise NukeError(f"{self.url} has no commit '{rev}'")
		return text.strip()

	# --- commits ---------------------------------------------------------------------------------

	def repo_dir(self):
		return self.checkout or self.cache.mirror(self.url)

	def ensure(self, commit):
		if not self.checkout:
			git.fetch_commit(self.cache.mirror(self.url), self.url, commit)

	def head(self):
		return git.head(self.checkout)

	def index(self, commit):
		"""What the commit holds (a develop checkout: its working tree, whatever the commit)."""
		if self.checkout:
			if "tree" not in self._indexes:
				self._indexes["tree"] = index.in_folder(self.checkout, f"{self.name} (checkout)")
			return self._indexes["tree"]
		if commit not in self._indexes:
			self.ensure(commit)
			self._indexes[commit] = index.at_commit(self.repo_dir(), commit, self.label(commit))
		return self._indexes[commit]

	def release_before(self, line, commit):
		"""The latest release of `line` in the commit's history (a Version), or None."""
		tag, _ = git.describe(self.repo_dir(), commit, self.tag_pattern(line))
		return semver.parse_version(tag.rpartition("/")[2]) if tag else None

	def version_of(self, line, commit):
		"""The release a commit is (the highest of its tags), else its pseudo-version after the last release."""
		key = (line, commit)
		if key not in self._versions:
			exact = [v for v, c in self.releases(line) if c == commit]
			if exact:
				self._versions[key] = exact[0]
			else:
				self.ensure(commit)
				tag, distance = git.describe(self.repo_dir(), commit, self.tag_pattern(line))
				release = semver.parse_version(tag.rpartition("/")[2]) if tag else None
				self._versions[key] = semver.pseudo_version(release, distance, commit)
		return self._versions[key]

	def is_ancestor(self, older, newer):
		return git.is_ancestor(self.repo_dir(), older, newer)
