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
from ..manifest import MANIFEST_NAME, is_url, load
from ..packages import bridge, git, index, install, lockfile, semver
from ..packages.repository import Repository
from ..packages.store import Cache, repository_name, url_key
from . import base

USAGE = """nuke package [status] [--fetch] [--json]
  every versioned package the workspace uses: version, commit, locked or developed, the projects it brings; --fetch asks
  the remotes for newer releases and how far each develop checkout is behind
nuke package update [NAME...]
  choose the newest versions the requirements allow (all, or the named sources, packages or source:package), never an
  older commit than the lock's
nuke package add SOURCE PACKAGE [REQUIREMENT] [--branch NAME | --rev COMMIT] [--name NAME]
  use PACKAGE (its path in the repository) of SOURCE (a [sources] name, or a git URL that [sources] then names); without a
  requirement, ^ the newest release (the default branch's tip when it has none)
nuke package remove PACKAGE
  stop using it
nuke package develop SOURCE [--at FOLDER]
  build the source's packages from a git checkout: packages/<source>/, a clone at the tip of its default branch (or an
  existing clone, --at); nuke.lock follows the checkout's commits, and the workspace's Go and Rust projects build the
  clone's modules and crates they require (a go.work, Cargo's [patch], under build/develop/)
nuke package lock SOURCE [--keep]
  stop developing: the lock's commits are installed read-only again, Go and Rust go back to their releases and the clone
  goes (refused while it has changes that are not committed and pushed; --keep moves the clone to packages/<source>.kept)
nuke package pull SOURCE
  bring a develop checkout up to date with its remote (git pull --rebase)
nuke package release PACKAGE... [--major | --minor | --patch | --version X.Y.Z] [--source NAME] [--no-push]
  tag the packages' next versions at HEAD of their checkout (a develop checkout, or this workspace's own repository) and
  push the branch and the tags; the first release of a package is 0.1.0, then a patch release unless told"""


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
			"lock": _lock, "pull": _pull, "release": _release}
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
			raise NukeError(f"no workspace in {cli.cwd} or above it")
		manifest = load(os.path.join(root, MANIFEST_NAME))
		if manifest.workspace is None:
			raise NukeError(f"{manifest.path}: packages belong to a workspace ([workspace] table)")
		self.cli = cli
		self.log = cli.log
		self.root = root
		self.spec = manifest.workspace
		self.cache = Cache()
		self.state = install.State(install.folder(root, self.spec.packages_dir))

	def load(self, **options):
		"""The workspace with its packages synced, and the IED solution kept in step with where they are now (N-26)."""
		workspace = workspaces.load_workspace(self.root, packages.Options(self.log, self.cache, **options))
		self.spec = workspace.spec
		self.state = install.State(install.folder(self.root, self.spec.packages_dir))
		ied_files.refresh(workspace, self.cli.store, self.log)
		return workspace

	def lock(self):
		return lockfile.read(self.root) or lockfile.Lock()

	def source(self, name):
		"""(url, folder name) of a source: a [sources] name, a source the lock names, or a URL."""
		if name in self.spec.sources:
			return self.spec.sources[name], name
		for entry in self.lock().packages:
			if entry.source.lower() == name.lower():
				return entry.url, entry.source
		if is_url(name):
			named = next((n for n, u in self.spec.sources.items() if url_key(u) == url_key(name)), None)
			return name, named or repository_name(name)
		raise NukeError(f"no source named '{name}' (the workspace's [sources]: {', '.join(self.spec.sources) or 'none'})")

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
	body, _ = bridge.describe(context.spec, context.state, context.lock(), context.state.checkouts(), joined)
	if options.fetch:
		_newest(context, body)
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
	for url, folder in context.state.checkouts().items():
		if os.path.isdir(folder):
			try:
				git.run(["fetch", "--quiet", "--prune"], cwd=folder, what=f"git fetch in {folder}")
			except git.GitError as error:
				unfetched.append(str(git.unreachable(url, error)))
	return unfetched


def _newest(context, body):
	"""Each locked package's newest release on its remote, where it is newer than the lock's."""
	repositories = {}
	for item in body["packages"]:
		if item["mode"] != "locked":
			continue
		key = url_key(item["url"])
		if key not in repositories:
			repositories[key] = Repository(item["url"], item["source"], context.cache, fresh=True)
		releases = repositories[key].releases(item["path"])
		current = semver.parse_version(item["version"])
		if releases and current is not None and current.release < releases[0][0]:
			item["newest"] = str(releases[0][0])


def _print_status(context, body):
	log = context.log
	if not body["packages"] and not body["checkouts"]:
		log.line("no packages (add one with `nuke package add SOURCE PACKAGE`)")
		return
	for checkout in body["checkouts"]:
		state = "may be edited" if checkout["writable"] else "read-only until pulled"
		log.line(f"{checkout['source']}: developing in {checkout['folder']} (branch {checkout['branch'] or 'detached'}, "
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
		known = {s.lower() for s in context.spec.sources} | {r.path.lower() for r in context.spec.packages}
		for entry in context.lock().packages:
			known |= {entry.source.lower(), entry.line.lower(), f"{entry.source}:{entry.line}".lower()}
		unknown = [n for n in names if n.lower() not in known]
		if unknown:
			raise NukeError(f"nuke package update: '{unknown[0]}' names no source or package of the workspace")
	wanted = {n.lower() for n in names}

	def chosen(repo, line):
		return not wanted or bool({repo.name.lower(), line.lower(), f"{repo.name}:{line}".lower()} & wanted)
	context.load(update=chosen)
	context.log.note("packages are at the newest versions their requirements allow")


def _add(cli, args):
	options = _parse("add", args, lambda p: (p.add_argument("source"), p.add_argument("package"), p.add_argument("requirement", nargs="?"),
		p.add_argument("--branch"), p.add_argument("--rev"), p.add_argument("--name")))
	context = _Context(cli)
	path, text = context.manifest_text()
	original = text
	source = options.source
	if is_url(source):
		named = next((n for n, u in context.spec.sources.items() if url_key(u) == url_key(source)), None)
		if named is None:
			named = options.name or repository_name(source)
			text = tomledit.set_key(text, "sources", named, source)
		source = named
	elif source not in context.spec.sources:
		raise NukeError(f"no source named '{source}' in [sources]: give its git URL instead")
	package = options.package.strip("/")
	if options.branch or options.rev:
		value = {"source": source, ("branch" if options.branch else "rev"): options.branch or options.rev}
	else:
		value = f"{source}@{options.requirement or '*'}"
	context.write_manifest(path, tomledit.set_key(text, "packages", package, value))
	try:
		context.load()
	except NukeError:
		context.write_manifest(path, original)
		raise
	if not options.requirement and not options.branch and not options.rev:
		_pin_newest(context, path, source, package)
	context.log.note(f"the workspace uses {source}:{package}")


def _pin_newest(context, path, source, package):
	"""`*` becomes ^ the version the lock chose, when that is a release."""
	url = context.spec.sources[source]
	for entry in context.lock().packages:
		version = semver.parse_version(entry.version)
		if url_key(entry.url) == url_key(url) and package in entry.uses and version is not None and not version.build:
			_, text = context.manifest_text()
			context.write_manifest(path, tomledit.set_key(text, "packages", package, f"{source}@^{version}"))
			context.load()
			return


def _remove(cli, args):
	options = _parse("remove", args, lambda p: p.add_argument("package"))
	context = _Context(cli)
	path, text = context.manifest_text()
	if not tomledit.has_table(text, "packages") or all(r.path != options.package for r in context.spec.packages):
		raise NukeError(f"the workspace does not use a package '{options.package}' ([packages])")
	context.write_manifest(path, tomledit.remove_key(text, "packages", options.package))
	context.load()
	context.log.note(f"the workspace no longer uses {options.package}")


# --- develop, lock, pull ---------------------------------------------------------------------------

def _develop(cli, args):
	options = _parse("develop", args, lambda p: (p.add_argument("source"), p.add_argument("--at")))
	context = _Context(cli)
	url, name = context.source(options.source)
	key = url_key(url)
	if options.at:
		folder = fsutil.norm(os.path.abspath(os.path.join(cli.cwd, options.at)))
		install.check_checkout(folder, url)
		origin = git.remote_url(folder)
		if origin and url_key(origin) != key:
			raise NukeError(f"{folder} is a clone of {origin}, not of {url}")
	else:
		folder = fsutil.join(context.state.root, name)
		if not os.path.exists(os.path.join(folder, ".git")):
			_make_room(context, key, folder)
			mirror = context.cache.mirror(url)
			git.fetch(mirror, url)
			context.log.note(f"cloning {url} into {os.path.relpath(folder, context.root)}")
			git.clone(url, folder, reference=mirror)
		elif url_key(git.remote_url(folder) or "") != key:
			raise NukeError(f"{folder} is a clone of {git.remote_url(folder)}, not of {url}")
	relative = os.path.relpath(folder, context.state.root).replace("\\", "/") if fsutil.is_within(folder, context.state.root) else folder
	context.state.develop[key] = {"url": url, "source": name, "folder": relative}
	context.state.save()
	context.cache.record_checkout(url, folder)
	context.load(resolve=True)
	context.log.note(f"developing {name} in {folder} (branch {git.branch(folder) or 'detached'}): its packages build from "
		f"there and nuke.lock follows its commits; `nuke package lock {name}` goes back")
	for other in context.cache.checkouts().get(key, []):
		if not fsutil.same_path(other, folder) and os.path.isdir(other) and git.changes(other):
			context.log.warn(f"{name} is also being changed in {other} (uncommitted changes there)")


def _make_room(context, key, folder):
	"""The source's locked installs go: its folder becomes the clone."""
	for relative, entry in list(context.state.installed.items()):
		if url_key(entry.get("url", "")) == key:
			install.remove(fsutil.join(context.state.root, relative))
			del context.state.installed[relative]
	context.state.save()
	if os.path.isdir(folder) and os.listdir(folder):
		raise NukeError(f"{folder} is in the way (not a clone of the source); move it, then develop again")
	if os.path.isdir(folder):
		os.rmdir(folder)


def _lock(cli, args):
	options = _parse("lock", args, lambda p: (p.add_argument("source"), p.add_argument("--keep", action="store_true")))
	context = _Context(cli)
	url, name = context.source(options.source)
	folder = context.checkout(url)
	if folder is None:
		raise NukeError(f"{name} is not being developed in this workspace")
	inside = fsutil.is_within(folder, context.state.root)
	if os.path.isdir(folder):
		changed = git.changes(folder)
		if changed:
			raise NukeError(f"{folder} has {len(changed)} uncommitted change(s): commit and push them (or discard them) first\n  "
				+ "\n  ".join(changed[:10]))
		unpushed = git.unpushed(folder)
		if unpushed:
			raise NukeError(f"{folder} has {len(unpushed)} commit(s) no remote has: push them first (the lock names "
				f"{unpushed[0][:8]}, which other machines could not fetch)")
	entry = context.state.develop.pop(url_key(url))
	context.state.save()
	# The clone steps aside (its folder is where the installs go) and comes back if the lock cannot be installed.
	aside = fsutil.join(context.state.root, f".{name}.develop") if inside else None
	if aside:
		install.remove(aside)
		os.replace(folder, aside)
	try:
		workspace = context.load(resolve=True)
	except (NukeError, OSError):
		if aside:
			install.remove(folder)
			os.replace(aside, folder)
		context.state = install.State(context.state.root)
		context.state.develop[url_key(url)] = entry
		context.state.save()
		raise
	# Go and Rust projects go back to the releases; their next build points them again at what is still developed.
	install.remove(workspace.develop_root())
	context.cache.record_checkout(url, folder, present=False)
	if aside and not options.keep:
		install.remove(aside)
	elif aside:
		os.replace(aside, fsutil.join(context.state.root, f"{name}.kept"))
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
	_check_released_dependencies(repo, found, paths, head, folder)
	versions = {path: _next_version(repo, path, options) for path in paths}
	for path, version, why in _dependents(repo, found, versions):
		versions[path] = version
		context.log.note(f"also releasing {path} {version}, so a release of it accepts the new versions ({'; '.join(why)})")
	tags = []
	for path, version in versions.items():
		tag = repo.tag_name(path, version)
		git.run(["tag", "-a", tag, "-m", f"{path} {version}", head], cwd=folder, what=f"git tag {tag}")
		tags.append((path, version, tag))
		context.log.note(f"tagged {tag} at {head[:8]}")
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
	if os.path.exists(os.path.join(context.root, ".git")) and index.in_folder(context.root, context.spec.name, skip=(context.spec.packages_dir,)).packages:
		return context.root, None, context.spec.name
	if len(checkouts) == 1:
		source, (folder, url) = next(iter(checkouts.items()))
		return folder, url, source
	if not checkouts:
		raise NukeError("nothing to release from: the workspace is no repository of packages and develops no source")
	raise NukeError(f"several sources are being developed ({', '.join(sorted(checkouts))}): say which with --source")


def _check_released_dependencies(repo, found, paths, head, folder):
	"""A package released at `head` must not use another package of the repository with unreleased changes there."""
	for path in paths:
		for project in found.packages[path].projects:
			for dependency in project.depends:
				owner = found.owner(dependency)
				if owner is None or any(_under(owner, p) for p in paths):
					continue
				line = repo.line_of(owner)
				if not line or line in paths or line == repo.line_of(path):
					continue
				release = repo.release_before(line, head)
				if release is None:
					continue
				since = git.run(["rev-list", f"{repo.tag_name(line, release)}..{head}", "--", line], cwd=folder).split()
				if since:
					raise NukeError(f"{path} uses {dependency} of {line}, which has {len(since)} unreleased commit(s) since "
						f"{release}: release it too (`nuke package release {' '.join(paths)} {line}`)")


def _under(path, folder):
	return not folder or path == folder or path.startswith(folder + "/")


def _dependents(repo, found, versions):
	"""The released packages of the repository whose newest release would not accept a version being released (it was
	made against an older, incompatible release): each is released again (a patch) at the same commit, which accepts
	it. Yields (path, version, the projects that use it)."""
	pending = dict(versions)

	def released_line(owner):
		inside = [p for p in pending if p and _under(owner, p)]
		return max(inside, key=len) if inside else repo.line_of(owner)

	lines = {}
	for path, info in found.packages.items():
		lines.setdefault(repo.line_of(path), []).extend(info.projects)   # a sub-package's projects are its package's
	added = True
	while added:
		added = False
		for line in sorted(lines):
			if line in pending or not repo.releases(line):
				continue
			newest, commit = repo.releases(line)[0]
			why = set()
			for project in lines[line]:
				for dependency in project.depends:
					owner = found.owner(dependency)
					target = released_line(owner) if owner is not None else None
					if target is None or target == line or target not in pending:
						continue
					before = repo.release_before(target, commit)
					if before is not None and not semver.compatible(before).matches(pending[target]):
						why.add(f"its {project.name} needs {target} ^{before}")
			if why:
				pending[line] = semver.bump(newest, "patch")
				added = True
				yield line, pending[line], sorted(why)


def _next_version(repo, path, options):
	existing = repo.releases(path)
	if options.version:
		version = semver.require_version(options.version, "--version")
		if any(v.key() >= version.key() for v, _ in existing):
			raise NukeError(f"{path} {version} is not newer than its newest release, {existing[0][0]}")
		return version
	if not existing and options.part is None:
		return semver.Version(0, 1, 0)
	newest = existing[0][0] if existing else repo.release_before(repo.line_of(path), repo.head())
	return semver.bump(newest, options.part or "patch")


def _follow_release(context, url, name, tags):
	"""The workspace that develops the source wants what it released: a requirement the new version does not meet becomes
	^ that version, so going back to locked keeps it."""
	path, text = context.manifest_text()
	original = text
	for requirement in context.spec.packages:
		source_url = context.spec.sources.get(requirement.source) or requirement.source
		if url_key(source_url) != url_key(url) or requirement.branch or requirement.rev:
			continue
		for released, version, _ in tags:
			if requirement.path != released and not requirement.path.startswith(released + "/"):
				continue
			if not semver.parse_requirement(requirement.version or "*", path).matches(version):
				text = tomledit.set_key(text, "packages", requirement.path, f"{requirement.source}@^{version}")
				context.log.note(f"the workspace now wants {requirement.source}:{requirement.path} ^{version}")
	if text != original:
		context.write_manifest(path, text)
	context.load(resolve=True)
