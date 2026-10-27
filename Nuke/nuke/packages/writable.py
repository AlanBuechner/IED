"""Writable packages (IED docs/31 section 5): a source the workspace develops builds from a git clone of its repository
in the packages folder instead of read-only installs, and the lock follows the clone's HEAD.

`writable = true` on a dependency declares it in nuke.toml, so the build makes the clone wherever it is missing
(every package of that repository then builds from it: one clone per source); `nuke package develop` does the same for
one machine only. Taking `writable` away goes back to locked installs at the next build, which is refused while the
clone holds work no remote has - nothing written there is ever thrown away."""

import os
import time

from .. import fsutil
from ..errors import NukeError
from . import git, install
from .store import repository_name, url_key

HELD_WAIT_SECONDS = 20   # how long a clone may be held before it can step aside (IED restarts a language server in a few)


def source_name(spec, url):
	"""The folder name of a repository under packages/: its name (claymore for .../Claymore.git)."""
	return repository_name(url)


def declared(spec, roots):
	"""{url key: (url, name, branch)} of the sources a dependency of the workspace (a root) marks writable."""
	found = {}
	for root in roots:
		if root.requirement.writable:
			found.setdefault(url_key(root.url), (root.url, source_name(spec, root.url), root.requirement.branch))
	return found


def clone(state, cache, log, url, name, branch=None):
	"""packages/<name>/ as a clone of `url` (its default branch, or `branch`): the source's installs make room for it."""
	folder = fsutil.join(state.root, name)
	key = url_key(url)
	if os.path.exists(os.path.join(folder, ".git")):
		if url_key(git.remote_url(folder) or "") != key:
			raise NukeError(f"{folder} is a clone of {git.remote_url(folder)}, not of {url}")
		return folder
	make_room(state, key, folder)
	mirror = cache.mirror(url)
	git.fetch(mirror, url)
	shown = os.path.relpath(folder, os.path.dirname(state.root)).replace("\\", "/")
	log.note(f"cloning {url} into {shown} (writable)")
	git.clone(url, folder, reference=mirror)
	if branch and git.branch(folder) != branch:
		git.run(["checkout", "--quiet", branch], cwd=folder, what=f"git checkout {branch} in {folder}")
	return folder


def make_room(state, key, folder):
	"""The source's locked installs go: its folder becomes the clone."""
	for relative, entry in list(state.installed.items()):
		if url_key(entry.get("url", "")) == key:
			install.remove(fsutil.join(state.root, relative))
			del state.installed[relative]
	state.save()
	if os.path.isdir(folder) and os.listdir(folder):
		raise NukeError(f"{folder} is in the way (not a clone of the source); move it, then develop again")
	if os.path.isdir(folder):
		os.rmdir(folder)


def start(state, cache, url, name, folder, by_manifest):
	"""The workspace develops `url` from `folder` from now on."""
	relative = os.path.relpath(folder, state.root).replace("\\", "/") if fsutil.is_within(folder, state.root) else folder
	state.develop[url_key(url)] = {"url": url, "source": name, "folder": relative, "declared": by_manifest}
	state.save()
	cache.record_checkout(url, folder)


def check_saved(folder):
	"""Refuses a clone whose work would be lost going back to locked installs: uncommitted changes, unpushed commits."""
	changed = git.changes(folder)
	if changed:
		raise NukeError(f"{folder} has {len(changed)} uncommitted change(s): commit and push them (or discard them) first\n  "
			+ "\n  ".join(changed[:10]))
	unpushed = git.unpushed(folder)
	if unpushed:
		raise NukeError(f"{folder} has {len(unpushed)} commit(s) no remote has: push them first (the lock names "
			f"{unpushed[0][:8]}, which other machines could not fetch)")


class Stopping:
	"""A source going back to locked installs: its clone steps aside (its folder is where the installs go) until the
	installs are made, and comes back if they cannot be."""

	def __init__(self, state, key):
		self.state = state
		self.key = key
		self.entry = state.develop[key]
		self.folder = state.checkout_dir(self.entry)
		inside = fsutil.is_within(self.folder, state.root)
		self.aside = fsutil.join(state.root, f".{self.entry.get('source') or os.path.basename(self.folder)}.develop") if inside else None

	def begin(self):
		"""Checks nothing would be lost, moves the clone aside, then forgets it: a clone a program holds stays developed."""
		if os.path.isdir(self.folder):
			check_saved(self.folder)
		if self.aside and os.path.isdir(self.folder):
			install.remove(self.aside)
			_move_aside(self.folder, self.aside, self.entry.get("source") or os.path.basename(self.folder))
		del self.state.develop[self.key]
		self.state.save()

	def undo(self):
		if self.aside and os.path.isdir(self.aside):
			install.remove(self.folder)
			os.replace(self.aside, self.folder)
		self.state.develop[self.key] = self.entry
		self.state.save()

	def finish(self, cache, keep=False):
		cache.record_checkout(self.entry["url"], self.folder, present=False)
		if self.aside and os.path.isdir(self.aside):
			if keep:
				os.replace(self.aside, fsutil.join(self.state.root, f"{os.path.basename(self.folder)}.kept"))
			else:
				install.remove(self.aside)


def _move_aside(folder, aside, name):
	"""Renames the clone; while another program holds a file in it (a language server IED is starting again without it)
	Nuke waits for it a while, then says what to close. Nothing has changed then but what the next command writes again."""
	deadline = time.monotonic() + HELD_WAIT_SECONDS
	while True:
		try:
			os.replace(folder, aside)
			return
		except PermissionError as error:
			if time.monotonic() >= deadline:
				raise NukeError(f"{folder} is in use by another program ({error.strerror}): close what has a file of it open - an "
					f"editor, a terminal in it, a language server - and go back to locked again") from None
			time.sleep(0.25)


def follow_manifest(spec, roots, state, cache, log):
	"""Makes the develop clones match the manifests' `writable` dependencies: a clone for each source newly marked, and
	the Stoppings of those no longer marked (sources developed by `nuke package develop` stay)."""
	wanted = declared(spec, roots)
	for key, (url, name, branch) in wanted.items():
		entry = state.develop.get(key)
		if entry is None or not os.path.isdir(state.checkout_dir(entry)):
			folder = clone(state, cache, log, url, name, branch)
			start(state, cache, url, name, folder, by_manifest=True)
		elif not entry.get("declared"):
			entry["declared"] = True
			state.save()
	stopping = []
	try:
		for key in [k for k, entry in state.develop.items() if entry.get("declared") and k not in wanted]:
			stop = Stopping(state, key)
			stop.begin()
			stopping.append(stop)
			log.note(f"{stop.entry.get('source')} is no longer writable in nuke.toml: back to locked installs")
	except NukeError:
		for stop in reversed(stopping):
			stop.undo()
		raise
	return stopping
