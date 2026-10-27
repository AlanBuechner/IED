"""The packages folder of a workspace (`packages/`, [workspace] packages-dir): each locked versioned package exported from
its mirror into `<source>/<path>@<version>/` with every file read-only, each source being developed as a git clone in
`<source>/`, and nuke-packages.json saying which is which. Nothing in it is committed (ensure_ignored)."""

import json
import os
import shutil
import stat
import sys

from .. import fsutil
from ..errors import NukeError
from ..manifest import MANIFEST_NAME
from . import git
from .store import repository_name

IGNORED_NOTE = "# Nuke's packages folder: installed packages and nuke.lock, all generated"

STATE = "nuke-packages.json"
FORMAT = 1


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


def ensure_ignored(workspace_dir, packages_dir):
	"""The packages folder kept out of git by the checkout's own exclude file (.git/info/exclude), not by .gitignore: git
	ignores it all the same, while search in IED and the AI's tools, which skip what .gitignore files ignore, still find
	the packages' code - one codebase with the program's (docs/31 section 4)."""
	exclude, entry = _exclude_entry(workspace_dir, packages_dir)
	if exclude is None:
		return
	text = fsutil.read_text(exclude) or ""
	if entry in (line.strip() for line in text.splitlines()):
		return
	os.makedirs(os.path.dirname(exclude), exist_ok=True)
	addition = ("" if not text or text.endswith("\n") else "\n") + f"{IGNORED_NOTE}\n{entry}\n"
	with open(exclude, "a", encoding="utf-8", newline="\n") as f:
		f.write(addition)


def forget_ignored(workspace_dir, packages_dir):
	"""Takes ensure_ignored's entry (and the note above it) out of the exclude file again."""
	exclude, entry = _exclude_entry(workspace_dir, packages_dir)
	text = fsutil.read_text(exclude) if exclude else None
	if not text:
		return
	lines, kept = text.splitlines(), []
	for line in lines:
		if line.strip() == entry:
			while kept and kept[-1].startswith("# Nuke's"):
				kept.pop()
			continue
		kept.append(line)
	if len(kept) != len(lines):
		with open(exclude, "w", encoding="utf-8", newline="\n") as f:
			f.write("".join(line + "\n" for line in kept))


def _exclude_entry(workspace_dir, packages_dir):
	"""(the checkout's exclude file, the packages folder's line in it), or (None, None) outside a checkout."""
	top = git.run(["rev-parse", "--show-toplevel"], cwd=workspace_dir, check=False)
	exclude = git.run(["rev-parse", "--git-path", "info/exclude"], cwd=workspace_dir, check=False)
	if not top or not exclude:
		return None, None
	folder = os.path.relpath(os.path.join(workspace_dir, packages_dir), top.strip()).replace("\\", "/")
	return os.path.join(workspace_dir, exclude.strip()), f"/{folder}/"


def check_folder(spec):
	"""The packages folder is Nuke's alone: discovery and git skip it. A folder of the workspace's own in it - a project,
	or `Packages/` where the file system does not tell it from `packages/` - is refused instead of being skipped without
	a word, and the exclude entry that kept it out of git goes."""
	root = folder(spec.dir, spec.packages_dir)
	if not os.path.isdir(root):
		return
	state = State(root)
	nukes = {os.path.normcase(relative.split("/", 1)[0]) for relative in state.installed}
	nukes |= {os.path.normcase(entry["folder"].replace("\\", "/").split("/", 1)[0]) for entry in state.develop.values()
		if not os.path.isabs(entry.get("folder", ""))}
	for name in sorted(os.listdir(root)):
		path = os.path.join(root, name)
		if name.startswith(".") or os.path.normcase(name) in nukes or not os.path.isdir(path):
			continue
		manifest = next((os.path.join(base, MANIFEST_NAME) for base, _, files in os.walk(path) if MANIFEST_NAME in files), None)
		if manifest is not None:
			forget_ignored(spec.dir, spec.packages_dir)
			shown = os.path.relpath(os.path.realpath(manifest), os.path.realpath(spec.dir)).replace("\\", "/")
			on_disk = os.path.basename(os.path.realpath(root))
			same = "" if on_disk == os.path.basename(root) else f", which this file system does not tell from {on_disk}/"
			raise NukeError(f"{spec.file}: {shown} is the workspace's own, but it is in the folder Nuke installs packages "
				f"into ([workspace] packages-dir, \"{spec.packages_dir}\"{same}) - builds and git skip that folder: give Nuke "
				f"another one in [workspace], such as packages-dir = \"deps\"")


def check_checkout(folder, url):
	"""A develop checkout must be a clone of its source."""
	if not os.path.isdir(os.path.join(folder, ".git")) and not os.path.isfile(os.path.join(folder, ".git")):
		raise NukeError(f"{folder} is not a git checkout (develop needs a clone of {url})")
