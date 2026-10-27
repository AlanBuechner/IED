"""The packages folder of a workspace (`packages/`, [workspace] packages-dir): each locked versioned package exported from
its mirror into `<source>/<path>@<version>/` with every file read-only, each source being developed as a git clone in
`<source>/`, and nuke-packages.json saying which is which. Nothing Nuke puts there is committed (ensure_ignored); the
folder may hold the workspace's own folders too, which are its own (owned)."""

import json
import os
import shutil
import stat
import sys

from .. import fsutil
from ..errors import NukeError
from . import git
from .store import repository_name

STATE = "nuke-packages.json"
FORMAT = 1
GENERATED = ("nuke.lock", STATE, "nuget")      # the lock, this state, nuget.py's installs
IGNORED_NOTE = "# What Nuke made in its packages folder: installed packages, clones, nuke.lock (all generated)"


def folder(workspace_dir, packages_dir):
	return fsutil.join(workspace_dir, packages_dir)


def export_name(source, line, version):
	"""The folder of a locked versioned package, relative to the packages folder."""
	return f"{source}/{line or source}@{version}"


def content_name(source, line, version, url):
	"""Where the versioned package's own folder sits in its install: inside it, under the name it has in its repository
	(the repository's own for the whole repository), so a project named by its folder keeps its name."""
	return f"{export_name(source, line, version)}/{line.rpartition('/')[2] if line else repository_name(url)}"


class State:
	"""nuke-packages.json: {"develop": {url key: {"url", "folder"}}, "installed": {relative folder: {"url", "line",
	"commit"}}}. Machine-local, like the folder."""

	def __init__(self, root):
		self.root = root
		self.path = fsutil.join(root, STATE)
		try:
			with open(self.path, encoding="utf-8") as f:
				data = json.load(f)
		except (OSError, ValueError):
			data = {}
		self.develop = data.get("develop", {}) if isinstance(data.get("develop"), dict) else {}
		self.installed = data.get("installed", {}) if isinstance(data.get("installed"), dict) else {}
		self.needs = data.get("needs", {}) if isinstance(data.get("needs"), dict) else {}   # the last resolution's edges
		self.has_needs = "needs" in data   # false for a state written before the edges were recorded

	def save(self):
		body = {"format": FORMAT, "develop": self.develop, "installed": self.installed, "needs": self.needs}
		fsutil.write_if_changed(self.path, json.dumps(body, indent=2, sort_keys=True) + "\n")

	def checkouts(self):
		"""{url: absolute checkout folder} of the sources being developed."""
		return {entry["url"]: self.checkout_dir(entry) for entry in self.develop.values()}

	def checkout_dir(self, entry):
		where = entry["folder"]
		return fsutil.norm(where if os.path.isabs(where) else os.path.join(self.root, where))


def make_read_only(top):
	for base, dirs, files in os.walk(top):
		for name in files:
			path = os.path.join(base, name)
			os.chmod(path, stat.S_IREAD)


def _writable_again(function, path, *_):
	os.chmod(path, stat.S_IWRITE | stat.S_IREAD)
	function(path)


def remove(path):
	"""Deletes an installed package's folder, read-only files included."""
	if os.path.isdir(path):
		if sys.version_info >= (3, 12):
			shutil.rmtree(path, onexc=_writable_again)
		else:
			shutil.rmtree(path, onerror=_writable_again)


def export(state, mirror, url, source, line, version, commit, log):
	"""The locked versioned package in place; returns the folder holding its content (content_name). Exported again
	when another commit sits there."""
	relative = export_name(source, line, version)
	target = fsutil.join(state.root, relative)
	content = fsutil.join(state.root, content_name(source, line, version, url))
	entry = state.installed.get(relative)
	if entry and entry.get("commit") == commit and os.path.isdir(content):
		return content
	remove(target)
	log.note(f"installing {source}:{line or '(repository)'} {version} ({commit[:8]})")
	os.makedirs(target, exist_ok=True)
	git.export(mirror, commit, line, content)
	make_read_only(target)
	state.installed[relative] = {"url": url, "line": line, "commit": commit}
	return content


def prune(state, wanted, log):
	"""Removes the installed packages no longer wanted (`wanted`: relative folders), and nothing Nuke did not install."""
	for relative in sorted(set(state.installed) - set(wanted)):
		remove(fsutil.join(state.root, relative))
		parent = os.path.dirname(fsutil.join(state.root, relative))
		while fsutil.is_within(parent, state.root) and not fsutil.same_path(parent, state.root) and os.path.isdir(parent) and not os.listdir(parent):
			os.rmdir(parent)
			parent = os.path.dirname(parent)
		del state.installed[relative]
		log.detail(f"removed {relative} (no longer used)")


def owned(root):
	"""The entries of a packages folder Nuke made, by name: its files, each source's folder of installs or its clone, a
	clone moved aside. Nothing else there is Nuke's: whatever else the folder holds - `Packages/` on Windows is the same
	folder as `packages/` - is the workspace's own, found, built and committed like any other folder."""
	if not os.path.isdir(root):
		return []
	state = State(root)
	sources = {os.path.normcase(relative.split("/", 1)[0]) for relative in state.installed}
	sources |= {os.path.normcase(entry["folder"].replace("\\", "/").split("/", 1)[0]) for entry in state.develop.values()
		if not os.path.isabs(entry.get("folder", ""))}
	return sorted(name for name in os.listdir(root) if name in GENERATED or name.startswith(".") or os.path.normcase(name) in sources)


def owned_paths(spec):
	"""owned() of the workspace's packages folder, as paths from the workspace's folder."""
	base = spec.packages_dir.strip("/\\").replace("\\", "/")
	return [f"{base}/{name}" for name in owned(folder(spec.dir, spec.packages_dir))]


def ensure_ignored(workspace_dir, packages_dir):
	"""What Nuke made in the packages folder kept out of git by the checkout's own exclude file (.git/info/exclude), not by
	.gitignore: git ignores it all the same, while search in IED and the AI's tools, which skip what .gitignore files
	ignore, still find the packages' code - one codebase with the program's (docs/31 section 4). Entry by entry, so
	nothing else in the folder is ever hidden; read and written as files, no git process."""
	top, exclude = git.exclude_file(workspace_dir)
	if exclude is None:
		return
	root = folder(workspace_dir, packages_dir)
	relative = os.path.relpath(root, top).replace("\\", "/")
	used = os.path.isfile(os.path.join(root, STATE))       # Nuke has used the folder: its lock may come just after
	names = set(owned(root)) | ({"nuke.lock", STATE} if used else set())
	entries = [f"/{relative}/{name}" + ("/" if os.path.isdir(os.path.join(root, name)) else "") for name in sorted(names)]
	text = fsutil.read_text(exclude) or ""
	lines = _without_entries(text.splitlines(), f"/{relative}/")
	if entries:
		lines += [IGNORED_NOTE] + entries
	wanted = "".join(line + "\n" for line in lines)
	if wanted != text:
		os.makedirs(os.path.dirname(exclude), exist_ok=True)
		with open(exclude, "w", encoding="utf-8", newline="\n") as f:
			f.write(wanted)


def _without_entries(lines, prefix):
	"""An exclude file's lines less Nuke's for one packages folder: each line under `prefix` (the whole folder, as older
	Nukes wrote it, or an entry of it), and a note of Nuke's with no line of its own left after it."""
	kept = [line for line in lines if not line.strip().startswith(prefix)]
	return [line for i, line in enumerate(kept)
		if not (line.startswith("# Nuke's") or line.startswith("# What Nuke made")) or (i + 1 < len(kept) and kept[i + 1].strip().startswith("/"))]


def check_checkout(folder, url):
	"""A develop checkout must be a clone of its source."""
	if not os.path.isdir(os.path.join(folder, ".git")) and not os.path.isfile(os.path.join(folder, ".git")):
		raise NukeError(f"{folder} is not a git checkout (develop needs a clone of {url})")
