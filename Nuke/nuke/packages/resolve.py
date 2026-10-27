"""Choosing a version of every versioned package a workspace needs (docs/31 section 2).

Needs come from the workspace's [packages], from each used package's [package.dependencies], and - inside a repository -
from its projects' `depends`: a project of another package of the same repository released on its own needs at least the
release that package had at the dependent's commit. A package is used when the workspace lists it or a used project
depends on one of its projects. Each versioned package gets the highest version every need on it accepts, its locked one
while that still does (unless it is being updated), and never an older commit than its lock's on an update. When needs
exclude each other, the package that made the last of them tries its next lower version; when only the workspace's own
needs conflict, the error names every need and who made it."""

from collections import defaultdict, deque
from dataclasses import dataclass, field

from ..errors import NukeError
from ..manifest import is_url
from . import semver
from .repository import Repository
from .store import repository_name, url_key

ANY = semver.parse_requirement("*", "")
SETTLE_ROUNDS = 30
BACKTRACK_LIMIT = 200


@dataclass(frozen=True)
class Need:
	kind: str               # "version", "branch" or "rev"
	value: object           # a semver.Requirement, a branch name or a commit
	by: str                 # who needs it, for messages
	by_key: tuple = None    # the versioned package whose chosen commit made the need (None: the workspace's)
	by_commit: str = None

	def describe(self):
		if self.kind == "branch":
			return f"branch {self.value}"
		if self.kind == "rev":
			return f"commit {self.value[:12]}"
		return str(self.value)


@dataclass(frozen=True)
class Choice:
	commit: str
	version: object         # semver.Version
	develop: bool = False


@dataclass
class Selected:
	repo: Repository
	line: str
	choice: Choice
	uses: set
	needs: list


@dataclass
class Root:
	"""A need of the workspace itself: a project's [dependencies] entry, a [packages] entry, or a dependency of one of
	its own packages."""
	url: str
	requirement: object     # manifest.PackageRequirement
	by: str
	file: str = None        # the manifest that declares it


@dataclass
class Result:
	selected: list = field(default_factory=list)      # Selected
	warnings: list = field(default_factory=list)
	missing: list = field(default_factory=list)       # (project name, who depends on it) found in no package


class Conflict(Exception):
	def __init__(self, repo, line, needs, detail=""):
		super().__init__()
		self.repo = repo
		self.line = line
		self.needs = list(needs)
		self.detail = detail

	def explain(self):
		name = f"{self.repo.name}:{self.line or '(repository)'}"
		wants = "; ".join(f"{n.by} wants {n.describe()}" for n in self.needs)
		return f"no version of {name} satisfies every requirement: {wants}{self.detail}"


def requirement_need(requirement, by, by_key=None, by_commit=None):
	if requirement.branch:
		return Need("branch", requirement.branch, by, by_key, by_commit)
	if requirement.rev:
		return Need("rev", requirement.rev, by, by_key, by_commit)
	return Need("version", semver.parse_requirement(requirement.version or "*", requirement.origin), by, by_key, by_commit)


class Resolver:
	"""`repositories` gives the Repository of a URL (one per resolution); `local` is the workspace's own project names
	(lower case); `locked` the current lock (or None); `update` None, or a function (repository, line) -> whether that
	versioned package's version is chosen again."""

	def __init__(self, roots, wanted, local, repositories, locked=None, update=None):
		self.roots = roots
		self.wanted = wanted            # project names the workspace's own projects depend on and do not have
		self.local = local
		self.repositories = repositories
		self.locked = locked
		self.update = update
		self.excluded = set()
		self.warnings = []

	def solve(self):
		"""The choices; a conflict tries older versions of the packages that made it, and when none helps, the first
		conflict is the error (the later ones are only the older versions running out)."""
		first = None
		for _ in range(BACKTRACK_LIMIT):
			try:
				return self._settle()
			except Conflict as conflict:
				first = first or conflict
				culprit = next((n for n in reversed(conflict.needs) if n.by_key is not None
					and (n.by_key, n.by_commit) not in self.excluded and not self._repository(n.by_key).develop), None)
				if culprit is None:
					tried = sorted({f"{self._repository(k).name}:{k[1] or '(repository)'}" for k, _ in self.excluded})
					extra = f" (no older version of {', '.join(tried)} helps)" if tried else ""
					raise NukeError(first.explain() + extra) from None
				self.excluded.add((culprit.by_key, culprit.by_commit))
		raise NukeError(f"no set of package versions satisfies every requirement (tried {BACKTRACK_LIMIT})")

	def _repository(self, key):
		return self.repositories.by_key(key[0])

	def _settle(self):
		choices = {}
		for _ in range(SETTLE_ROUNDS):
			self.warnings = []
			needs, used, current, missing = self._derive(choices)
			chosen = {key: self._choose(key, needs[key]) for key in needs}
			if chosen == choices:
				result = Result(warnings=sorted(set(self.warnings)), missing=sorted(missing))
				for key in sorted(chosen):
					repo = self._repository(key)
					result.selected.append(Selected(repo, key[1], chosen[key], used[key], needs[key]))
				return result
			choices = chosen
		raise NukeError("package versions did not settle (each choice changed what another needs); report this with the workspace")

	# --- the graph for one set of choices --------------------------------------------------------

	def _derive(self, choices):
		current = dict(choices)
		needs = defaultdict(list)
		used = defaultdict(set)
		queue = deque()
		missing = set()

		def need(key, item):
			needs[key].append(item)
			if key not in current:
				current[key] = self._choose(key, needs[key])

		def use(key, path):
			if path not in used[key]:
				used[key].add(path)
				queue.append((key, path))

		for root in self.roots:
			repo = self.repositories.get(root.url)
			key = (repo.key, repo.line_of(root.requirement.path))
			need(key, requirement_need(root.requirement, root.by))
			use(key, root.requirement.path)
		wanted = {(name, "the workspace") for name in self.wanted}
		while True:
			while queue:
				key, path = queue.popleft()
				wanted |= self._visit(key, path, current, need, use)
			for name, by in sorted(wanted):
				found = self._find(name, current, used)
				if found:
					wanted.discard((name, by))
					use(*found)
			if not queue:
				break
		missing = {(name, by) for name, by in wanted}
		return needs, used, current, missing

	def _visit(self, key, path, current, need, use):
		"""Marks what the used package `path` of `key` needs; returns the project names it depends on that its own
		repository does not have (found among the other packages afterwards)."""
		repo = self._repository(key)
		choice = current[key]
		found = repo.index(choice.commit)
		info = found.packages.get(path)
		if info is None:
			released = choice.version.release != semver.Version(0, 0, 0) or not choice.version.build
			where = "its checkout" if repo.develop else f"{key[1] or 'the repository'} {choice.version} ({choice.commit[:8]})" if released \
				else f"commit {choice.commit[:8]}"
			raise NukeError(f"{repo.name} has no package '{path}' at {where}: no {path}/nuke.toml with a [package] table "
				f"(its packages there: {', '.join(sorted(p for p in found.packages if p)) or 'none'})")
		label = f"{repo.name}:{path or '(repository)'} {choice.version}"
		elsewhere = set()
		for dependency in info.dependencies:
			if dependency.local is not None:
				continue        # a project of this package: it comes with it
			target = self.repositories.get(dependency.url)
			target_key = (target.key, target.line_of(dependency.path))
			if target_key != key:
				need(target_key, requirement_need(dependency, label, key, choice.commit))
			use(target_key, dependency.path)
		for project in info.projects:
			for name in project.depends:
				if name.lower() in self.local:
					continue
				owner = found.owner(name)
				if owner is None:
					elsewhere.add((name, label))
					continue
				if repo.line_of(owner) != key[1]:
					if not any(d.path == owner for d in info.dependencies):
						raise NukeError(f"{label}: its project {project.name} depends on {name}, which is the package {owner} - "
							f"another package: name it in {path or '.'}/nuke.toml [dependencies] with its repository's URL "
							f"({name} = {{ git = \"{repo.url}\", path = \"{owner}\", version = \"...\" }})")
					continue
				use(key, owner)
		return elsewhere

	def _find(self, name, current, used):
		"""(key, package path) of the package whose project is called `name`, among every chosen versioned package."""
		hits = []
		for key in sorted(current):
			repo = self._repository(key)
			owner = repo.index(current[key].commit).owner(name)
			if owner is not None and repo.line_of(owner) == key[1]:
				hits.append((key, owner))
		hits.sort(key=lambda hit: hit[1] not in used[hit[0]])
		return hits[0] if hits else None

	# --- choosing a version ----------------------------------------------------------------------

	def _choose(self, key, needs):
		repo = self._repository(key)
		line = key[1]
		if repo.develop:
			return self._develop_choice(repo, line, needs)
		locked = self.locked.find(key) if self.locked else None
		updating = self.update is not None and self.update(repo, line)
		revs = {n.value for n in needs if n.kind == "rev"}
		branches = {n.value for n in needs if n.kind == "branch"}
		versions = [n for n in needs if n.kind == "version"]
		if len(revs) > 1 or len(branches) > 1 or (revs and branches):
			raise Conflict(repo, line, needs, " (two different commits or branches)")
		if revs:
			rev = next(iter(revs))
			commit = locked.commit if locked and locked.commit.startswith(rev) else repo.resolve_rev(rev)
			return self._pinned(repo, line, commit, versions, needs)
		if branches:
			tip = repo.branch_commit(next(iter(branches)))
			# The locked commit stays while it is on the branch (moving to another branch moves the lock to its tip).
			if locked and not updating and not self._excluded(key, locked.commit) and repo.is_ancestor(locked.commit, tip):
				return self._pinned(repo, line, locked.commit, versions, needs)
			return self._pinned(repo, line, tip, versions, needs)
		if locked and not updating and not self._excluded(key, locked.commit):
			version = semver.parse_version(locked.version)
			if version is not None and all(n.value.matches(version) for n in versions):
				return Choice(locked.commit, version)
		choice = self._best(repo, line, versions)
		if choice is None and repo.refresh():
			choice = self._best(repo, line, versions)
		if choice is None:
			if not repo.releases(line) and all(n.value.text == "*" for n in versions):
				commit = repo.default_commit()
				return Choice(commit, repo.version_of(line, commit))
			detail = "" if repo.releases(line) else (f" ({repo.name} has no releases of {line or 'the repository'}: no tags "
				f"{repo.tag_pattern(line).replace('[0-9]*', 'X.Y.Z')}; release one with `nuke package release`, or use branch = \"main\")")
			raise Conflict(repo, line, needs, detail)
		if updating and locked and not self._excluded(key, locked.commit) and locked.commit != choice.commit:
			# Never back to an older commit than the lock's, unless the package's folder is the same there.
			version = semver.parse_version(locked.version)
			if version is not None and all(n.value.matches(version) for n in versions) and repo.is_ancestor(choice.commit, locked.commit) \
					and not repo.unchanged(line, choice.commit, locked.commit):
				return Choice(locked.commit, version)
		return choice

	def _excluded(self, key, commit):
		return (key, commit) in self.excluded

	def _best(self, repo, line, versions):
		key = (repo.key, line)
		for version, commit in repo.releases(line):
			if not self._excluded(key, commit) and all(n.value.matches(version) for n in versions):
				return Choice(commit, version)
		return None

	def _pinned(self, repo, line, commit, versions, needs):
		version = repo.version_of(line, commit)
		if not all(n.value.matches(version) for n in versions):
			raise Conflict(repo, line, needs, f" ({commit[:8]} is {version})")
		return Choice(commit, version)

	def _develop_choice(self, repo, line, needs):
		"""A source being developed: its HEAD - or, for a versioned package whose folder is the same at HEAD as at its
		last release, that release, so the lock moves only for the packages a commit changed."""
		commit = repo.head()
		version = repo.version_of(line, commit)
		if version.build:
			release = repo.release_before(line, commit)
			tagged = repo.release_commit(line, release) if release else None
			if tagged and repo.unchanged(line, tagged, commit):
				commit, version = tagged, release
		for n in needs:
			if n.kind == "version" and not n.value.matches(version):
				self.warnings.append(f"{repo.name}:{line or '(repository)'} is {version} in its checkout, but {n.by} wants {n.value}")
		return Choice(commit, version, develop=True)


class Repositories:
	"""Every repository of one resolution, by URL: named by the workspace's [sources] where it names them, else by the
	repository's own name (made unique)."""

	def __init__(self, cache, names, checkouts, fresh=False):
		self.cache = cache
		self.names = {url_key(url): name for name, url in names.items()}
		self.checkouts = {url_key(url): folder for url, folder in checkouts.items()}
		self.fresh = fresh
		self._by_key = {}

	def get(self, url):
		key = url_key(url)
		if key not in self._by_key:
			name = self.names.get(key) or self._unique(repository_name(url))
			self.names[key] = name
			self._by_key[key] = Repository(url, name, self.cache, self.checkouts.get(key), self.fresh)
		return self._by_key[key]

	def by_key(self, key):
		return self._by_key[key]

	def _unique(self, name):
		taken = {n.lower() for n in self.names.values()}
		candidate, number = name, 2
		while candidate.lower() in taken:
			candidate = f"{name}-{number}"
			number += 1
		return candidate

	def all(self):
		return list(self._by_key.values())
