"""Packages: versioned projects from git repositories (IED docs/31, Nuke/README.md section 12).

A workspace names repositories in [sources] and what it uses of them in [packages]; `sync` makes the packages' projects
part of the workspace. While nuke.lock matches the manifests and every locked package is installed, sync only reads
files. Otherwise it resolves (resolve.py: one `git ls-remote` per source at most, manifests read from the machine's
mirrors), installs what changed (install.py) and writes the lock. A source being developed (`nuke package develop`)
builds from its checkout, and the lock follows the checkout's HEAD."""

import functools
import hashlib
import json
import os
from collections import defaultdict
from dataclasses import dataclass, replace

from .. import fsutil
from ..errors import NukeError
from ..manifest import MANIFEST_NAME, is_url, load
from . import bridge, git, index, install, lockfile, writable
from .resolve import Repositories, Resolver, Root
from .store import Cache, same_repository, url_key


@dataclass
class Options:
	log: object
	cache: Cache = None
	update: object = None       # None; or a function (repository, line) -> whether to choose that package's version again
	resolve: bool = False       # resolve even when the lock is current (develop, lock, status --fetch)


@dataclass(frozen=True)
class Origin:
	"""Where a project a package brings comes from."""
	source: str                 # the source's folder name under packages/
	url: str
	package: str                # the package that owns the project ("Windowing/Win32")
	line: str                   # the versioned package it is released with ("Windowing"; "" for the repository)
	version: str
	commit: str
	develop: bool               # built from a develop checkout (writable) rather than a locked install (read-only)
	folder: str                 # the versioned package's folder (a develop checkout's root)


def active(spec, projects=None):
	"""Whether the workspace uses packages at all: anything else costs no file access."""
	return any(p.dependencies for p in (projects or {}).values()) or lockfile.exists(spec)


def sync(spec, projects, options):
	"""The projects the workspace's packages bring (ProjectSpecs with `package` set), installed and locked."""
	cache = options.cache or Cache()
	state = install.State(install.folder(spec.dir, spec.packages_dir))
	local = {p.name.lower() for p in projects.values()}
	roots = workspace_roots(spec, projects)
	wanted = sorted({name for p in projects.values() for name in p.depends if name.lower() not in local})
	lock = lockfile.read(spec)
	if not roots and lock is None:
		return []
	stopping = writable.follow_manifest(spec, roots, state, cache, options.log)
	checkouts = {url: folder for url, folder in state.checkouts().items()}
	digest = _digest(roots, checkouts)
	try:
		joined, lock = _joined(spec, state, roots, wanted, local, checkouts, lock, digest, cache, options)
	except (NukeError, OSError):
		for stop in reversed(stopping):
			stop.undo()
		raise
	for stop in stopping:
		stop.finish(cache)
	_check_names(joined, projects)
	if spec.ide:
		bridge.write(spec, roots, state, lock, checkouts, joined)
	return joined


def _joined(spec, state, roots, wanted, local, checkouts, lock, digest, cache, options):
	"""(the packages' projects, the lock): from the lock as it is while it matches, else resolved again."""
	joined = None
	if options.update is None and not options.resolve and lock is not None and lock.digest == digest:
		joined = _from_lock(state, lock, checkouts, local, wanted)
	if joined is not None and lock.packages and not state.has_needs:
		return _record_needs(spec, state, roots, wanted, local, checkouts, lock, digest, cache, options, joined)
	if joined is None:
		return _resolve(spec, state, roots, wanted, local, checkouts, lock, digest, cache, options)
	return joined, lock


def _record_needs(spec, state, roots, wanted, local, checkouts, lock, digest, cache, options, joined):
	"""A state from before the dependency graph was recorded (status shows it, and IED's package manager): one
	resolution, which keeps every locked version, records it. Without the network it waits for the next time."""
	try:
		return _resolve(spec, state, roots, wanted, local, checkouts, lock, digest, cache, options)
	except NukeError:
		return joined, lock


def workspace_roots(spec, projects=None):
	"""The workspace's own needs: the packages from git its projects' [dependencies] name, and what its own packages
	need - less the packages its own folders hold, which a dependency on its own repository takes from here."""
	local = local_packages(spec)
	roots, declared = [], set()
	for project in sorted((p for p in (projects or {}).values() if p.package is None), key=lambda p: p.name.lower()):
		declared.add(os.path.relpath(project.dir, spec.dir).replace("\\", "/"))
		for requirement in project.dependencies:
			if requirement.url is not None and local.path_of(requirement.url, requirement.path) is None:
				roots.append(Root(requirement.url, requirement, f"project {project.name}", project.file, project.name))
	for folder, package in sorted(local.by_path.items()):
		if (folder or ".") in declared:
			continue
		for requirement in package.dependencies:
			if requirement.url is not None and local.path_of(requirement.url, requirement.path) is None:
				roots.append(Root(requirement.url, requirement, f"the workspace's package {folder or '(root)'}"))
	return roots


def check_local(spec, projects):
	"""Each local dependency of the workspace's projects names a project of the same package (or, for a project in no
	package, of the workspace): a package brings everything it uses, so whoever uses it gets that too."""
	local = local_packages(spec)
	for project in projects.values():
		if project.package is not None:
			continue
		for requirement in project.dependencies:
			if requirement.local is None:
				continue
			target = local_target(projects, project, requirement)
			if target is None:
				raise NukeError(f"{project.file}: dependencies.{requirement.name}: no project in '{requirement.local}' (a folder "
					f"of the project or of its package)")
			mine, theirs = local.package_holding(spec, project.dir), local.package_holding(spec, target.dir)
			if mine is not None and mine != theirs:     # a project in no package is no one's dependency: any of the workspace's
				raise NukeError(f"{project.file}: dependencies.{requirement.name}: {target.name} ('{requirement.local}') is not in "
					f"package {mine}: a local dependency is a project of the same package; another package is named by its "
					f"repository: {{ source = \"<repository URL>/{theirs or '<its folder>'}\", version = \"...\" }}")


def local_target(projects, project, requirement):
	"""The project a local dependency names: the folder relative to the project's, else to one of the folders above it
	(its package's or workspace's root)."""
	by_dir = {os.path.normcase(os.path.normpath(p.dir)): p for p in projects.values()}
	base = os.path.normpath(project.dir)
	while True:
		found = by_dir.get(os.path.normcase(os.path.normpath(os.path.join(base, requirement.local))))
		if found is not None:
			return found
		parent = os.path.dirname(base)
		if parent == base:
			return None
		base = parent


class LocalPackages:
	"""The packages in the workspace's own folders, as a dependency names them: by one of the checkout's remotes (or by
	no repository, from a package of this one) and the package's path from the repository's top."""

	def __init__(self, spec):
		self.by_path = index.in_folder(spec.dir, spec.name, skip=(spec.packages_dir,)).packages   # workspace-relative
		top, self.remotes = git.checkout_remotes(spec.dir)
		prefix = os.path.relpath(spec.dir, top).replace("\\", "/") if top else "."
		self.prefix = "" if prefix == "." else prefix

	def path_of(self, url, path):
		"""The workspace-relative path of the package of this workspace that a dependency on `path` of `url` names, or
		None when it comes from elsewhere."""
		if url is not None and not any(same_repository(url, remote) for remote in self.remotes):
			return None
		if self.prefix:
			if not path.startswith(self.prefix + "/"):
				return None
			path = path[len(self.prefix) + 1:]
		return path if path in self.by_path else None

	def package_holding(self, spec, folder):
		"""The workspace-relative path of the package `folder` is part of (the nearest one at or above it), or None."""
		relative = os.path.relpath(folder, spec.dir).replace("\\", "/")
		relative = "" if relative == "." else relative
		while True:
			if relative in self.by_path:
				return relative
			if not relative or relative.startswith(".."):
				return None
			relative = relative.rpartition("/")[0]


@functools.lru_cache(maxsize=8)
def _local_packages(folder, name, packages_dir):
	from ..manifest import WorkspaceSpec
	return LocalPackages(WorkspaceSpec(name=name, dir=folder, file=os.path.join(folder, "nuke.toml"), packages_dir=packages_dir))


def local_packages(spec):
	"""The workspace's LocalPackages, read once per command."""
	return _local_packages(spec.dir, spec.name, spec.packages_dir)


def link(spec, projects):
	"""Each project - the workspace's and the packages' - depends also on the projects of the packages its
	[dependencies] name, so `depends` lists only what is not a package."""
	if not any(p.dependencies for p in projects.values()):
		return projects
	local = local_packages(spec)
	provided = defaultdict(list)        # (url key, package path) or ("", workspace-relative path) -> project names
	for project in projects.values():
		if project.package is not None:
			provided[(url_key(project.package.url), project.package.package)].append(project.name)
	for path, info in local.by_path.items():
		provided[("", path)] += [p.name for p in info.projects if p.name in projects]
	linked = {}
	for name, project in projects.items():
		names = []
		for requirement in project.dependencies:
			if requirement.local is not None:
				target = local_target(projects, project, requirement)
				names += [target.name] if target is not None else []
			else:
				names += _provided(spec, local, provided, project, requirement)
		known = {d.lower() for d in project.depends} | {project.name.lower()}
		extra = [n for n in dict.fromkeys(names) if n.lower() not in known]
		linked[name] = replace(project, depends=tuple(project.depends) + tuple(extra)) if extra else project
	return linked


def _provided(spec, local, provided, project, requirement):
	"""The projects of the package a dependency from git names: this workspace's own, or a package's."""
	path = local.path_of(requirement.url, requirement.path) if project.package is None else None
	return provided.get(("", path), []) if path is not None else provided.get((url_key(requirement.url), requirement.path), [])


def _digest(roots, checkouts):
	body = sorted((url_key(r.url), r.requirement.path, r.requirement.describe(), r.by) for r in roots)
	developed = sorted(url_key(url) for url in checkouts)
	return "sha256:" + hashlib.sha256(json.dumps([body, developed]).encode("utf-8")).hexdigest()[:32]


# --- the fast path: the lock as it is --------------------------------------------------------------

def _from_lock(state, lock, checkouts, local, wanted):
	"""The packages' projects from the lock and what is installed, or None when anything is missing or a project depends
	on a project nothing installed has (then the lock is resolved again)."""
	developed = {url_key(url): folder for url, folder in checkouts.items()}
	joined = []
	for entry in lock.packages:
		checkout = developed.get(url_key(entry.url))
		if checkout is not None:
			resolved_at = state.develop.get(url_key(entry.url), {}).get("head")
			if not os.path.isdir(checkout) or git.head(checkout) != resolved_at:
				return None
			base, found = checkout, index.in_folder(checkout, entry.source)
		else:
			relative = install.export_name(entry.source, entry.line, entry.version)
			base = fsutil.join(state.root, install.content_name(entry.source, entry.line, entry.version, entry.url))
			if state.installed.get(relative, {}).get("commit") != entry.commit or not os.path.isdir(base):
				return None
			found = index.in_folder(base, entry.source, prefix=entry.line)
		for path in entry.uses:
			info = found.packages.get(path)
			if info is None:
				return None
			joined += _load_projects(entry, info, base, checkout is not None)
	names = {p.name.lower() for p in joined} | local
	needed = set(n.lower() for n in wanted) | {n.lower() for p in joined for n in p.depends}
	return joined if needed <= names else None


def _load_projects(entry, info, base, develop):
	"""The ProjectSpecs of a used package, from its installed folder (`base` holds the versioned package `entry.line`)."""
	projects = []
	for project in info.projects:
		relative = project.folder[len(entry.line):].lstrip("/") if entry.line and not develop else project.folder
		manifest = load(fsutil.join(base, relative, MANIFEST_NAME))
		origin = Origin(entry.source, entry.url, info.path, entry.line, entry.version, entry.commit, develop, base)
		projects.append(replace(manifest.project, package=origin))
	return projects


# --- resolving -------------------------------------------------------------------------------------

def _resolve(spec, state, roots, wanted, local, checkouts, lock, digest, cache, options):
	log = options.log
	repositories = Repositories(cache, {}, checkouts, fresh=options.update is not None)
	log.note("resolving packages" + (" (choosing the newest versions)" if options.update is not None else ""))
	resolver = Resolver(roots, wanted, local, repositories, locked=lock, update=_update_keys(options.update))
	result = resolver.solve()
	for warning in result.warnings:
		log.warn(warning)
	new_lock = lockfile.Lock(digest=digest)
	joined = []
	wanted_installs = []
	for selected in result.selected:
		repo, choice = selected.repo, selected.choice
		entry = lockfile.Locked(repo.url, repo.name, selected.line, str(choice.version), choice.commit, tuple(sorted(selected.uses)))
		new_lock.packages.append(entry)
		if choice.develop:
			base = repo.checkout
		else:
			base = install.export(state, cache.mirror(repo.url), repo.url, repo.name, selected.line, entry.version, choice.commit, log)
			wanted_installs.append(install.export_name(repo.name, selected.line, entry.version))
		found = repo.index(choice.commit)
		for path in sorted(selected.uses):
			joined += _load_projects(entry, found.packages[path], base, choice.develop)
	install.prune(state, wanted_installs, log)
	state.needs = _edges(result)
	for repo in repositories.all():
		if repo.develop and url_key(repo.url) in state.develop:
			state.develop[url_key(repo.url)]["head"] = repo.head()    # the lock is current while HEAD stays here
	state.save()
	install.ensure_ignored(spec.dir, spec.packages_dir)
	previous = lockfile.read(spec)
	if lockfile.write(spec, new_lock):
		_report_changes(log, previous, new_lock)
	return joined, new_lock


def _edges(result):
	"""{edge_key: [{by, requirement, from}]}: who needs each versioned package and what they accept (`from` the edge_key of
	the package that needs it, "" for the workspace) - the dependency graph IED's package manager shows."""
	edges = {}
	for selected in result.selected:
		seen = []
		for need in selected.needs:
			item = {"by": need.by, "requirement": need.describe(), "from": f"{need.by_key[0]}#{need.by_key[1]}" if need.by_key else ""}
			if item not in seen:
				seen.append(item)
		edges[bridge.edge_key(selected.repo.url, selected.line)] = seen
	return edges


def _update_keys(update):
	if update is None:
		return None
	return lambda repo, line: update(repo, line)


def _report_changes(log, previous, lock):
	before = {p.key: p for p in (previous.packages if previous else [])}
	for entry in lock.packages:
		old = before.pop(entry.key, None)
		name = f"{entry.source}:{entry.line or '(repository)'}"
		if old is None:
			log.note(f"locked {name} {entry.version} ({entry.commit[:8]})")
		elif old.commit != entry.commit:
			log.note(f"locked {name} {entry.version} ({entry.commit[:8]}), was {old.version}")
	for old in before.values():
		log.note(f"unlocked {old.source}:{old.line or '(repository)'} (no longer used)")


def _check_names(joined, projects):
	seen = {p.name.lower(): p for p in projects.values()}
	for project in joined:
		clash = seen.get(project.name.lower())
		if clash is not None:
			raise NukeError(f"two projects are named '{project.name}': {clash.file} and {project.file} "
				f"(package {project.package.source}:{project.package.package}); rename one")
		seen[project.name.lower()] = project
