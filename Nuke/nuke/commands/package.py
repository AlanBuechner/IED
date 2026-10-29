"""package: the workspace's packages (IED docs/31, Nuke/README.md section 12) - what is locked and developed, choosing
newer versions, developing a source in place and going back to locked, pulling a checkout, releasing, and adding or
removing a package in the manifest."""

import argparse
import json
import os

from .. import fsutil, ied_files, tomledit
from .. import packages
from .. import workspace as workspaces
from ..errors import NukeError
from ..manifest import MANIFEST_NAME, split_source
from ..packages import bridge, git, index, install, lockfile, nuget, semver, writable
from ..packages.repository import Repository
from ..packages.store import Cache, repository_name, same_repository, url_key, web_url
from . import base

USAGE = """nuke package [status] [--fetch] [--json]
  every versioned package the workspace uses: version, commit, locked or developed, the projects it brings; --fetch asks
  the remotes for newer releases and how far each develop checkout is behind
nuke package update [NAME...]
  choose the newest versions the requirements allow (all, or the named sources, packages or source:package), never an
  older commit than the lock's
nuke package add SOURCE [REQUIREMENT] [--project NAME] [--name NAME] [--branch NAME | --rev COMMIT] [--writable]
  add SOURCE to a project's [dependencies]: a package from git, "<repository URL>/<its folder>" (without a requirement,
  ^ its newest release; the default branch's tip when it has none), or a local folder of the same package; the project
  is --project, else the one the current folder is in, else the workspace's default
nuke package remove NAME [--project NAME]
  take a dependency out of the projects that list it (by its name, source or folder)
nuke package develop SOURCE [--at FOLDER]
  build the source's packages from a git checkout: packages/<source>/, a clone at the tip of its default branch (or an
  existing clone, --at); nuke.lock follows the checkout's commits, and the workspace's Go and Rust projects - and IED's
  gopls and rust-analyzer - use the clone's modules and crates they require (a go.work, Cargo's [patch], under
  build/develop/)
nuke package lock SOURCE [--keep]
  stop developing: the lock's commits are installed read-only again, Go and Rust go back to their releases and the clone
  goes (refused while it has changes that are not committed and pushed; --keep moves the clone to packages/<source>.kept)
nuke package pull SOURCE
  bring a develop checkout up to date with its remote (git pull --rebase)
nuke package release PACKAGE... [--major | --minor | --patch | --version X.Y.Z] [--source NAME] [--no-push]
  tag the packages' next versions at HEAD of their checkout (a develop checkout, or this workspace's own repository) and
  push the branch and the tags; the first release of a package is 0.1.0, then a patch release unless told
nuke package browse REPOSITORY [--json] [--offline]
  every package of a repository (its URL, or the name of one the workspace uses) at the tip of its default branch:
  description, version and releases, its projects and dependencies, and what the workspace requires and has locked"""


class Package(base.Command):
	name = "package"
	description = "The workspace's packages: status, update, add, remove, develop, lock, pull, release (nuke package help)"
	usage = USAGE
	takes_args = True
	needs_workspace = False
	policy = base.NONE

	def main(self, cli, args):
		verb = args[0] if args and not args[0].startswith("-") else "status"
		rest = args[1:] if args and not args[0].startswith("-") else list(args)
		actions = {"status": _status, "update": _update, "add": _add, "remove": _remove, "develop": _develop,
			"lock": _lock, "pull": _pull, "release": _release, "browse": _browse}
		if verb in ("help", "-h", "--help") or "-h" in rest or "--help" in rest:
			cli.log.line(USAGE)
			return 0
		if verb not in actions:
			raise NukeError(f"nuke package {verb}: not an action (nuke package help)")
		return actions[verb](cli, rest) or 0


def _parse(action, args, build):
	parser = argparse.ArgumentParser(prog=f"nuke package {action}", add_help=False)
	build(parser)
	try:
		return parser.parse_args(args)
	except SystemExit:
		raise NukeError(f"usage:\n{USAGE}") from None


class _Context:
	"""The workspace a package action works on, read from its manifest (its packages may be what is broken)."""

	def __init__(self, cli):
		root = workspaces.find_root(cli.cwd)
		if root is None:
			raise NukeError(f"no workspace or project in {cli.cwd} or above it (a nuke.toml with [project] is enough)")
		self.cli = cli
		self.log = cli.log
		self.root = root
		self.spec, self.projects = workspaces.read_projects(root)
		self.cache = Cache()
		self.state = install.State(install.folder(root, self.spec.packages_dir))

	def requirements(self):
		"""(requirement, the project naming it) of every package from git the workspace's projects name."""
		return [(r, project) for project in sorted(self.projects.values(), key=lambda p: p.name.lower())
			for r in project.dependencies if r.url is not None]

	def project(self, name):
		"""The project a dependency is added to: `name`, else the one whose folder holds the current folder, else the
		workspace's default project, else its only one."""
		if name:
			found = next((p for p in self.projects.values() if p.name.lower() == name.lower()), None)
			if found is None:
				raise NukeError(f"no project named '{name}' (the workspace's: {', '.join(sorted(self.projects))})")
			return found
		here = [p for p in self.projects.values() if fsutil.is_within(self.cli.cwd, p.dir)]
		if here:
			return max(here, key=lambda p: len(p.dir))
		if self.spec.default_project and self.spec.default_project in self.projects:
			return self.projects[self.spec.default_project]
		if len(self.projects) == 1:
			return next(iter(self.projects.values()))
		raise NukeError(f"which project uses it? --project NAME (the workspace's: {', '.join(sorted(self.projects))})")

	def load(self, **options):
		"""The workspace with its packages synced, and the IED solution kept in step with where they are now (N-26)."""
		workspace = workspaces.load_workspace(self.root, packages.Options(self.log, self.cache, **options))
		self.spec, self.projects = workspaces.read_projects(self.root)
		self.state = install.State(install.folder(self.root, self.spec.packages_dir))
		ied_files.refresh(workspace, self.cli.store, self.log)
		return workspace

	def lock(self):
		return lockfile.read(self.spec) or lockfile.Lock()

	def source(self, name):
		"""(url, folder name) of a repository: a URL (with or without a package's folder after it), or the name of one the
		workspace uses (claymore for .../Claymore.git)."""
		url, _ = split_source(name)
		if url is not None:
			return url, repository_name(url)
		for entry in self.lock().packages:
			if entry.source.lower() == name.lower():
				return entry.url, entry.source
		for requirement, _ in self.requirements():
			if repository_name(requirement.url).lower() == name.lower():
				return requirement.url, repository_name(requirement.url)
		known = sorted({repository_name(r.url) for r, _ in self.requirements()})
		raise NukeError(f"no repository named '{name}' (the workspace's: {', '.join(known) or 'none'}; or give its URL)")

	def checkout(self, url):
		entry = self.state.develop.get(url_key(url))
		return self.state.checkout_dir(entry) if entry else None

	def manifest_text(self):
		path = os.path.join(self.root, MANIFEST_NAME)
		with open(path, encoding="utf-8") as f:
			return path, f.read()

	def write_manifest(self, path, text):
		with open(path, "w", encoding="utf-8", newline="\n") as f:
			f.write(text)


# --- status ----------------------------------------------------------------------------------------

def _status(cli, args):
	options = _parse("status", args, lambda p: (p.add_argument("--fetch", action="store_true"), p.add_argument("--json", action="store_true")))
	context = _Context(cli)
	unfetched = _fetch_checkouts(context) if options.fetch else []
	workspace = context.load()
	joined = [p for p in workspace.projects.values() if p.package]
	roots = packages.workspace_roots(context.spec, workspace.projects)
	body, _ = bridge.describe(context.spec, roots, context.state, context.lock(), context.state.checkouts(), joined)
	_enrich(context, body, options.fetch)
	body["nuget"] = _nuget_json(context, workspace)
	if options.json:
		if unfetched:
			body["unfetched"] = unfetched
		cli.log.line(json.dumps(body, indent=2))
		return 1 if unfetched else 0
	_print_status(context, body)
	if unfetched:
		raise NukeError("\n".join(unfetched))
	return 0


def _fetch_checkouts(context):
	"""Fetch every develop checkout, one that cannot be fetched not keeping the others' state from being written (IED's
	nuke plugin runs `status --fetch` on a timer, IED docs/31 section 6); what could not be fetched, and why."""
	unfetched = []
	own = {fsutil.norm(context.state.checkout_dir(e)) for e in context.state.develop.values() if e.get("own")}
	for url, folder in context.state.checkouts().items():
		if os.path.isdir(folder) and fsutil.norm(folder) not in own:     # the checkout the workspace is in is the person's
			try:
				git.run(["fetch", "--quiet", "--prune"], cwd=folder, what=f"git fetch in {folder}")
			except git.GitError as error:
				unfetched.append(str(git.unreachable(url, error)))
	return unfetched


def _enrich(context, body, fetch):
	"""Each versioned package's releases (newest first; from its remote with --fetch, else what the machine's mirror
	knows), `newest` where one is newer than the lock's and `compatible`, the newest of those with the locked version's
	major (Cargo's ^, what `update` reaches unless a requirement holds it back), and the packages of it the workspace
	uses with their descriptions and declared dependencies."""
	repositories = {}
	checkouts = {url_key(u): f for u, f in context.state.checkouts().items()}
	for item in body["packages"]:
		key = url_key(item["url"])
		if key not in repositories:
			repositories[key] = Repository(item["url"], item["source"], context.cache, checkout=checkouts.get(key),
				fresh=fetch and key not in checkouts)
		repo = repositories[key]
		releases = repo.releases(item["path"])
		item["versions"] = [str(version) for version, _ in releases]
		current = semver.parse_version(item["version"])
		if releases and current is not None and current.release < releases[0][0]:
			item["newest"] = str(releases[0][0])
			allowed = semver.compatible(current)
			reachable = next((v for v, _ in releases if current.release < v and allowed.matches(v)), None)
			if reachable is not None:
				item["compatible"] = str(reachable)
		found = repo.index(item["commit"])
		item["packages"] = [_package_json(found, path) for path in item["uses"] if path in found.packages]


def _browse(cli, args):
	options = _parse("browse", args, lambda p: (p.add_argument("source"), p.add_argument("--json", action="store_true"),
		p.add_argument("--offline", action="store_true")))
	context = _Context(cli)
	url, name = context.source(options.source)
	repo = Repository(url, name, context.cache, fresh=not options.offline)
	mirror = context.cache.mirror(url)
	if not options.offline:
		git.fetch(mirror, url)
	tip = repo.default_commit()
	found = repo.index(tip)
	wanted = {r.path: r.describe() for r, _ in context.requirements() if url_key(r.url) == url_key(url)}
	locked = {e.line: e.version for e in context.lock().packages if url_key(e.url) == url_key(url)}
	items = []
	for path in sorted(found.packages):
		line = repo.line_of(path)
		items.append(dict(_package_json(found, path), line=line, versions=[str(v) for v, _ in repo.releases(line)],
			requirement=wanted.get(path), locked=locked.get(line)))
	body = {"source": name, "url": url, "web": web_url(url), "commit": tip, "packages": items}
	if options.json:
		cli.log.line(json.dumps(body, indent=2))
		return 0
	cli.log.line(f"{name}: {url} at {tip[:8]}")
	width = max((len(i["path"]) for i in items), default=0)
	for item in items:
		version = item["versions"][0] if item["versions"] else "unreleased"
		mark = f"  [uses {item['requirement']}]" if item["requirement"] else ""
		cli.log.line(f"  {item['path'] or '(repository)':<{width}}  {version:<10} {item['description']}{mark}")
	return 0


def _package_json(found, path):
	info = found.packages[path]
	return {"path": path, "description": info.description, "projects": [p.name for p in info.projects],
		"dependencies": [_dependency_json(d) for d in info.dependencies]}


def _dependency_json(d):
	if d.nuget is not None:
		return {"name": d.name, "nuget": d.nuget, "requirement": d.describe()}
	return {"name": d.name, "source": d.source, "url": d.url, "path": d.path if d.url else d.local, "requirement": d.describe()}


def _nuget_json(context, workspace):
	"""Each NuGet package the lock holds: its id, version, hash, install folder and who names it (name, project, file)."""
	users = nuget.needs(workspace.projects.values())
	return [{"id": entry.id, "version": entry.version, "sha256": entry.sha256,
		"folder": os.path.relpath(workspace.nuget.get(entry.id.lower(), ""), context.root).replace("\\", "/") if entry.id.lower() in workspace.nuget else None,
		"requiredBy": [{"name": n.requirement.name, "project": n.project, "requirement": n.requirement.version, "file": n.requirement.origin}
			for n in users.get(entry.id.lower(), [])]}
		for entry in sorted(context.lock().nuget, key=lambda n: n.id.lower())]


def _print_status(context, body):
	log = context.log
	if not body["packages"] and not body["checkouts"] and not body["nuget"]:
		log.line("no packages (add one with `nuke package add SOURCE PACKAGE`)")
		return
	for item in body["nuget"]:
		users = ", ".join(sorted({u["project"] for u in item["requiredBy"]}))
		log.line(f"nuget:{item['id']}  {item['version']}  {item['folder'] or 'not installed'}  {users}")
	for checkout in body["checkouts"]:
		state = "may be edited" if checkout["writable"] else "read-only until pulled"
		where = "builds from this checkout," if checkout.get("own") else "developing in"
		log.line(f"{checkout['source']}: {where} {checkout['folder']} (branch {checkout['branch'] or 'detached'}, "
			f"{checkout['ahead']} ahead, {checkout['behind']} behind {checkout['upstream'] or 'no upstream'}; {state})")
		changed = git.changes(os.path.join(context.root, checkout["folder"]) if not os.path.isabs(checkout["folder"]) else checkout["folder"])
		if changed:
			log.line(f"  {len(changed)} uncommitted change(s)")
		for other in context.cache.checkouts().get(url_key(checkout["url"]), []):
			if not fsutil.same_path(other, os.path.join(context.root, checkout["folder"])) and os.path.isdir(other) and git.changes(other):
				log.line(f"  also being changed in {other} (uncommitted changes there)")
	width = max((len(f"{p['source']}:{p['path'] or '(repository)'}") for p in body["packages"]), default=0)
	for item in body["packages"]:
		name = f"{item['source']}:{item['path'] or '(repository)'}"
		extra = f"  ({item['newest']} available: nuke package update {name})" if item.get("newest") else ""
		log.line(f"{name:<{width}}  {item['version']:<16} {item['commit'][:8]}  {item['mode']:<7}  {', '.join(item['projects'])}{extra}")


# --- update, add, remove ---------------------------------------------------------------------------

def _update(cli, args):
	context = _Context(cli)
	names = [a for a in args if not a.startswith("-")]
	if names:
		known = {repository_name(r.url).lower() for r, _ in context.requirements()} | {r.path.lower() for r, _ in context.requirements()}
		for entry in context.lock().packages:
			known |= {entry.source.lower(), entry.line.lower(), f"{entry.source}:{entry.line}".lower()}
		known |= {r.nuget.lower() for p in context.projects.values() for r in p.dependencies if r.nuget is not None}
		unknown = [n for n in names if n.lower() not in known]
		if unknown:
			raise NukeError(f"nuke package update: '{unknown[0]}' names no source or package of the workspace")
	wanted = {n.lower() for n in names}

	def chosen(repo, line):
		return not wanted or bool({repo.name.lower(), line.lower(), f"{repo.name}:{line}".lower()} & wanted)
	context.load(update=chosen, update_nuget=lambda package_id: not wanted or package_id.lower() in wanted)
	context.log.note("packages are at the newest versions their requirements allow")


def _add(cli, args):
	options = _parse("add", args, lambda p: (p.add_argument("source"), p.add_argument("requirement", nargs="?"),
		p.add_argument("--branch"), p.add_argument("--rev"), p.add_argument("--writable", action="store_true"), p.add_argument("--name"),
		p.add_argument("--project")))
	context = _Context(cli)
	project = context.project(options.project)
	url, folder = split_source(options.source)
	name = options.name or _entry_name(project, options.source, folder or repository_name(url or options.source))
	with open(project.file, encoding="utf-8") as f:
		text = f.read()
	context.write_manifest(project.file, tomledit.set_key(text, "dependencies", name, _entry(options, url, options.requirement or "*")))
	try:
		context.load()
	except NukeError:
		context.write_manifest(project.file, text)
		raise
	if url is not None and not options.requirement and not options.branch and not options.rev:
		_pin_newest(context, project.file, url, folder, name, options)
	context.log.note(f"{project.name} uses {options.source} as {name}" + (" (writable: a clone in the packages folder)" if options.writable else ""))


def _entry_name(project, source, folder):
	"""An entry's name: the entry that already names this source, else the package's folder name (Win32 for
	.../Windowing/Win32), else the whole folder when that name is taken."""
	for requirement in project.dependencies:
		if requirement.source == source:
			return requirement.name
	short = folder.rstrip("/").rpartition("/")[2] or folder
	return folder if any(r.name == short for r in project.dependencies) else short


def _entry(options, url, requirement):
	"""A [dependencies] entry: { source, version | branch | rev, writable }; a local folder's is its source alone."""
	value = {"source": options.source}
	if url is None:
		return value
	if options.branch or options.rev:
		value["branch" if options.branch else "rev"] = options.branch or options.rev
	else:
		value["version"] = requirement
	if options.writable:
		value["writable"] = True
	return value


def _pin_newest(context, file, url, folder, name, options):
	"""`*` becomes ^ the version the lock chose, when that is a release."""
	for entry in context.lock().packages:
		version = semver.parse_version(entry.version)
		if url_key(entry.url) == url_key(url) and folder in entry.uses and version is not None and not version.build:
			with open(file, encoding="utf-8") as f:
				text = f.read()
			context.write_manifest(file, tomledit.set_key(text, "dependencies", name, _entry(options, url, f"^{version}")))
			context.load()
			return


def _remove(cli, args):
	options = _parse("remove", args, lambda p: (p.add_argument("package"), p.add_argument("--project")))
	context = _Context(cli)
	wanted = options.package.strip("/")
	found = [(r, project) for project in context.projects.values() for r in project.dependencies
		if wanted in (r.name, r.source, r.path) and (not options.project or project.name.lower() == options.project.lower())]
	if not found:
		raise NukeError(f"no project of the workspace names a dependency '{options.package}' in its [dependencies]")
	for requirement, project in found:
		with open(project.file, encoding="utf-8") as f:
			text = f.read()
		context.write_manifest(project.file, tomledit.remove_key(text, "dependencies", requirement.name))
		context.log.note(f"{project.name} no longer uses {requirement.source}")
	context.load()


# --- develop, lock, pull ---------------------------------------------------------------------------

def _develop(cli, args):
	options = _parse("develop", args, lambda p: (p.add_argument("source"), p.add_argument("--at")))
	context = _Context(cli)
	url, name = context.source(options.source)
	key = url_key(url)
	if context.state.develop.get(key, {}).get("own"):
		raise NukeError(f"{name} is the repository this workspace is in: its packages build from this checkout already")
	if options.at:
		folder = fsutil.norm(os.path.abspath(os.path.join(cli.cwd, options.at)))
		install.check_checkout(folder, url)
		origin = git.remote_url(folder)
		if origin and url_key(origin) != key:
			raise NukeError(f"{folder} is a clone of {origin}, not of {url}")
	else:
		folder = writable.clone(context.state, context.cache, context.log, url, name)
	writable.start(context.state, context.cache, url, name, folder, by_manifest=False)
	context.load(resolve=True)
	context.log.note(f"developing {name} in {folder} (branch {git.branch(folder) or 'detached'}): its packages build from "
		f"there and nuke.lock follows its commits; `nuke package lock {name}` goes back (to keep it for every checkout of "
		f"the workspace, mark its package `writable = true` in nuke.toml)")
	for other in context.cache.checkouts().get(key, []):
		if not fsutil.same_path(other, folder) and os.path.isdir(other) and git.changes(other):
			context.log.warn(f"{name} is also being changed in {other} (uncommitted changes there)")


def _lock(cli, args):
	options = _parse("lock", args, lambda p: (p.add_argument("source"), p.add_argument("--keep", action="store_true")))
	context = _Context(cli)
	url, name = context.source(options.source)
	if context.checkout(url) is None:
		raise NukeError(f"{name} is not being developed in this workspace")
	if context.state.develop.get(url_key(url), {}).get("own"):
		raise NukeError(f"{name} is the repository this workspace is in: its packages build from this checkout as long as a "
			f"dependency names it")
	marked = [r.name for r, _ in context.requirements() if r.writable and url_key(r.url) == url_key(url)]
	if marked:
		raise NukeError(f"nuke.toml marks {', '.join(marked)} writable: take `writable = true` away there, and the next build "
			f"goes back to locked installs")
	# Go, Rust and IED's language servers go back to the releases first: a language server pointed at the clone holds
	# files in it until IED starts it again without them. Loading writes again what the sources still developed need.
	install.remove(workspaces.develop_root(context.root))
	stop = writable.Stopping(context.state, url_key(url))
	stop.begin()
	try:
		context.load(resolve=True)
	except (NukeError, OSError):
		stop.undo()
		raise
	stop.finish(context.cache, keep=options.keep)
	context.log.note(f"{name} is locked again: its packages are installed read-only at the lock's commits")


def _pull(cli, args):
	options = _parse("pull", args, lambda p: p.add_argument("source"))
	context = _Context(cli)
	url, name = context.source(options.source)
	folder = context.checkout(url)
	if folder is None:
		raise NukeError(f"{name} is not being developed in this workspace (`nuke package update {name}` takes newer releases)")
	git.run(["pull", "--rebase", "--autostash", "--quiet"], cwd=folder, what=f"git pull in {folder}")
	context.load(resolve=True)
	status = bridge.checkout_status(folder)
	context.log.note(f"{name} is up to date with {status['upstream']} ({status['ahead']} commit(s) of its own to push)")


# --- release ---------------------------------------------------------------------------------------

def _release(cli, args):
	def build(parser):
		parser.add_argument("packages", nargs="+")
		group = parser.add_mutually_exclusive_group()
		for part in ("major", "minor", "patch"):
			group.add_argument(f"--{part}", dest="part", action="store_const", const=part)
		group.add_argument("--version")
		parser.add_argument("--source")
		parser.add_argument("--no-push", dest="push", action="store_false")
	options = _parse("release", args, build)
	context = _Context(cli)
	folder, url, name = _release_repository(context, options.source)
	if options.push:
		git.run(["fetch", "--quiet", "--tags"], cwd=folder, what=f"git fetch in {folder}")
	changed = [line for line in git.run(["status", "--porcelain", "--untracked-files=no"], cwd=folder).splitlines() if line.strip()]
	if changed:
		raise NukeError(f"{folder} has uncommitted changes; a release tags a commit: commit them first")
	branch = git.branch(folder)
	upstream = git.upstream(folder) if branch else None
	if options.push and upstream and git.ahead_behind(folder, upstream)[1]:
		raise NukeError(f"{folder} is behind {upstream}: `nuke package pull {name}` (or git pull) first")
	head = git.head(folder)
	repo = Repository(url or folder, name, context.cache, checkout=folder)
	found = index.at_commit(folder, head, f"{name}@{head[:8]}")
	paths = [p.strip("/") for p in options.packages]
	for path in paths:
		if path not in found.packages:
			raise NukeError(f"{name} has no package '{path}' at HEAD (no [package] table in {path}/{MANIFEST_NAME})")
	ours = _own_repository(repo, folder)
	_check_released_dependencies(repo, found, paths, head, folder, ours)
	versions = {path: _next_version(repo, found, path, options) for path in paths}
	moved = {}
	for path, version, why, edits in _dependents(repo, found, versions, ours):
		versions[path] = version
		moved[path] = edits
		context.log.note(f"also releasing {path} {version}, so it takes the new versions ({'; '.join(why)})")
	_write_versions(folder, found, versions, moved)
	if git.run(["status", "--porcelain", "--untracked-files=no"], cwd=folder).strip():     # not when they said so already
		git.run(["commit", "--quiet", "-am", "Release " + ", ".join(f"{p} {v}" for p, v in versions.items())], cwd=folder,
			what=f"git commit in {folder}")
	head = git.head(folder)
	tags = []
	for path, version in versions.items():
		tag = repo.tag_name(path, version)
		git.run(["tag", "-a", tag, "-m", f"{path} {version}", head], cwd=folder, what=f"git tag {tag}")
		tags.append((path, version, tag))
		context.log.note(f"{path}/{MANIFEST_NAME}: version = \"{version}\"; tagged {tag} at {head[:8]}")
	if options.push:
		refs = ([f"HEAD:refs/heads/{branch}"] if branch and upstream else []) + [f"refs/tags/{t}" for _, _, t in tags]
		git.run(["push", "--quiet", "--atomic", "origin"] + refs, cwd=folder, what=f"git push from {folder}")
		context.log.note(f"pushed {', '.join(t for _, _, t in tags)}")
	if url:
		_follow_release(context, url, name, tags)
	return 0


def _release_repository(context, source):
	"""(checkout folder, source URL or None, name) of the repository to release from."""
	checkouts = {entry["source"]: (context.state.checkout_dir(entry), entry["url"]) for entry in context.state.develop.values()}
	if source:
		if source not in checkouts:
			raise NukeError(f"{source} is not being developed in this workspace (`nuke package develop {source}` first)")
		folder, url = checkouts[source]
		return folder, url, source
	if os.path.exists(os.path.join(context.root, ".git")) and index.in_folder(context.root, context.spec.name).packages:
		return context.root, None, context.spec.name
	if len(checkouts) == 1:
		source, (folder, url) = next(iter(checkouts.items()))
		return folder, url, source
	if not checkouts:
		raise NukeError("nothing to release from: the workspace is no repository of packages and develops no source")
	raise NukeError(f"several sources are being developed ({', '.join(sorted(checkouts))}): say which with --source")


def _own_repository(repo, folder):
	"""Whether a dependency names the repository being released: one of its URLs."""
	_, remotes = git.checkout_remotes(folder)
	urls = [repo.url] + remotes

	def ours(requirement):
		return requirement.url is not None and any(same_repository(requirement.url, u) for u in urls)
	return ours


def _uses(found, info, ours):
	"""(package path, dependency) of each [dependencies] entry of a package that names its own repository."""
	return [(d.path, d) for d in info.dependencies if ours(d)]


def _check_released_dependencies(repo, found, paths, head, folder, ours):
	"""A package released at `head` must not use another package of the repository with unreleased changes there."""
	for path in paths:
		for owner, _ in _uses(found, found.packages[path], ours):
			if any(_under(owner, p) for p in paths):
				continue
			line = repo.line_of(owner)
			if not line or line in paths or line == repo.line_of(path):
				continue
			release = repo.release_before(line, head)
			if release is None:
				continue
			since = git.run(["rev-list", f"{repo.tag_name(line, release)}..{head}", "--"] + repo.pathspec(line), cwd=folder).split()
			if since:
				raise NukeError(f"{path} uses {owner} of {line}, which has {len(since)} unreleased commit(s) since "
					f"{release}: release it too (`nuke package release {' '.join(paths)} {line}`)")


def _under(path, folder):
	return not folder or path == folder or path.startswith(folder + "/")


def _dependents(repo, found, versions, ours):
	"""The packages of the repository whose [dependencies] entry on a package being released does not accept the new
	version: the entry becomes ^ that version, and the package is released again (a patch) with it. Yields (path,
	version, why, {dependency name: (dependency, new requirement)})."""
	pending = dict(versions)

	def target_of(path):
		inside = [p for p in pending if p and _under(path, p)]
		return max(inside, key=len) if inside else repo.line_of(path)

	added = True
	while added:
		added = False
		for path, info in sorted(found.packages.items()):
			if path in pending:
				continue
			why, edits = [], {}
			for used, dependency in _uses(found, info, ours):
				target = target_of(used)
				if target not in pending or target == repo.line_of(path):
					continue
				if dependency.version and not semver.parse_requirement(dependency.version, info.path or ".").matches(pending[target]):
					edits[dependency.name or dependency.path] = (dependency, f"^{pending[target]}")
					why.append(f"its {dependency.name or dependency.path} wanted {dependency.version}")
			if why:
				pending[path] = _bumped(repo, info, path)
				added = True
				yield path, pending[path], why, edits


def _bumped(repo, info, path):
	"""A patch after the package's newest release, or after the version its manifest says."""
	existing = repo.releases(path)
	current = existing[0][0] if existing else semver.parse_version(info.version or "0.0.0")
	stated = semver.parse_version(info.version) if info.version else None
	if stated is not None and current is not None and current.key() < stated.key():
		current = stated
	return semver.bump(current, "patch")


def _next_version(repo, found, path, options):
	"""--version; else the manifest's version when it is newer than every release (written by hand, Cargo's way); else
	the next major, minor or patch (0.1.0 for a first release)."""
	existing = repo.releases(path)
	stated = semver.parse_version(found.packages[path].version) if found.packages[path].version else None
	if options.version:
		version = semver.require_version(options.version, "--version")
	elif options.part is None and stated is not None and (not existing or existing[0][0].key() < stated.key()):
		version = stated
	elif not existing and options.part is None and stated is None:
		return semver.Version(0, 1, 0)
	else:
		newest = existing[0][0] if existing else (stated or repo.release_before(repo.line_of(path), repo.head()))
		return semver.bump(newest, options.part or "patch")
	if any(v.key() >= version.key() for v, _ in existing):
		raise NukeError(f"{path} {version} is not newer than its newest release, {existing[0][0]}")
	return version


def _write_versions(folder, found, versions, moved):
	"""Each released package's nuke.toml says its new version, and a dependent's moved requirements."""
	for path, version in versions.items():
		file = os.path.join(folder, path, MANIFEST_NAME)
		with open(file, encoding="utf-8") as f:
			text = f.read()
		text = tomledit.set_key(text, "package", "version", str(version))
		for name, (dependency, requirement) in moved.get(path, {}).items():
			text = tomledit.set_key(text, "dependencies", name, _dependency_value(dependency, requirement))
		with open(file, "w", encoding="utf-8", newline="\n") as f:
			f.write(text)


def _dependency_value(requirement, version):
	"""A [dependencies] entry for `requirement` wanting `version`."""
	value = {"source": requirement.source, "version": version}
	if requirement.writable:
		value["writable"] = True
	return value


def _follow_release(context, url, name, tags):
	"""The workspace that develops the source wants what it released: a project's [dependencies] entry the new version
	does not meet becomes ^ that version, so going back to locked keeps it."""
	workspace = context.load()
	for root in packages.workspace_roots(context.spec, workspace.projects):
		requirement = root.requirement
		if url_key(root.url) != url_key(url) or requirement.branch or requirement.rev or not root.file:
			continue
		for released, version, _ in tags:
			if not _under(requirement.path, released) or semver.parse_requirement(requirement.version or "*", root.file).matches(version):
				continue
			with open(root.file, encoding="utf-8") as f:
				text = f.read()
			text = tomledit.set_key(text, "dependencies", requirement.name, _dependency_value(requirement, f"^{version}"))
			context.write_manifest(root.file, text)
			context.log.note(f"{os.path.relpath(root.file, context.root)}: {requirement.name} now wants ^{version}")
	context.load(resolve=True)


