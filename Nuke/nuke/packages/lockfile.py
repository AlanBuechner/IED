"""nuke.lock: the exact commit of every versioned package a workspace uses, and a digest of the requirements it was
chosen for. Written by Nuke, committed with the workspace, read on every command: while it matches the manifests and the
packages are in place, nothing is resolved and no git process starts."""

import os
import tomllib
from dataclasses import dataclass, field

from .. import fsutil
from ..errors import NukeError

FILE = "nuke.lock"
FORMAT = 1
HEADER = ("# nuke.lock - the exact versions of the packages this workspace builds with (Nuke/README.md section 12).\n"
	"# Written by Nuke: `nuke package update` chooses them again. Commit it.\n")


@dataclass(frozen=True)
class Locked:
	url: str            # the source repository
	source: str         # its folder under packages/
	line: str           # the versioned package's path ("" for the repository)
	version: str
	commit: str
	uses: tuple = ()    # the package paths of it the workspace uses

	@property
	def key(self):
		from .store import url_key
		return (url_key(self.url), self.line)


@dataclass
class Lock:
	digest: str = ""
	packages: list = field(default_factory=list)

	def find(self, key):
		return next((p for p in self.packages if p.key == key), None)


def path_of(workspace_dir):
	return fsutil.join(workspace_dir, FILE)


def read(workspace_dir):
	path = path_of(workspace_dir)
	if not os.path.isfile(path):
		return None
	try:
		with open(path, "rb") as f:
			data = tomllib.load(f)
	except (OSError, tomllib.TOMLDecodeError) as error:
		raise NukeError(f"{path}: {error} (delete it and run `nuke package update` to write it again)") from None
	if data.get("format") != FORMAT:
		raise NukeError(f"{path}: written by another Nuke (format {data.get('format')}); `nuke package update` writes it again")
	lock = Lock(digest=str(data.get("requirements", "")))
	for entry in data.get("package", []):
		try:
			lock.packages.append(Locked(entry["source"], entry["name"], entry.get("path", ""), entry["version"],
				entry["commit"], tuple(entry.get("uses", ()))))
		except (KeyError, TypeError):
			raise NukeError(f"{path}: a [[package]] entry lacks source, name, version or commit; `nuke package update` writes it again") from None
	return lock


def _string(text):
	return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render(lock):
	lines = [HEADER, f"format = {FORMAT}", f"requirements = {_string(lock.digest)}", ""]
	for entry in sorted(lock.packages, key=lambda p: (p.source.lower(), p.line.lower())):
		lines += [
			"[[package]]",
			f"name = {_string(entry.source)}",
			f"source = {_string(entry.url)}",
			f"path = {_string(entry.line)}",
			f"version = {_string(entry.version)}",
			f"commit = {_string(entry.commit)}",
			"uses = [" + ", ".join(_string(u) for u in sorted(entry.uses)) + "]",
			"",
		]
	return "\n".join(lines)


def write(workspace_dir, lock):
	"""Writes the lock when it changed; True if it did."""
	return fsutil.write_if_changed(path_of(workspace_dir), render(lock))
