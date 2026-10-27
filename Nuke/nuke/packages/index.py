"""What a repository holds at one commit, or in a checkout's working tree: its packages (folders whose nuke.toml has a
[package] table), the projects each one owns (its own folder's [project] and every project folder below it that is not
inside another package). Read without checking anything out: one `git ls-tree`
and one `git cat-file --batch` per commit."""

import hashlib
import os
import tomllib
from dataclasses import dataclass, field

from .. import manifest as manifests
from ..errors import NukeError
from ..manifest import MANIFEST_NAME
from . import git, install

# Never part of a package: build output, hidden folders (and, on disk, what Nuke installed: in_folder).
SKIP = {"build", "bin", "bin-int", "node_modules", "__pycache__"}


@dataclass(frozen=True)
class ProjectInfo:
	name: str
	folder: str          # relative to the repository ("" for its root)
	depends: tuple


@dataclass(frozen=True)
class PackageInfo:
	path: str            # relative to the repository ("" for its root)
	description: str
	dependencies: tuple  # manifest.PackageRequirement
	projects: tuple      # ProjectInfo
	version: str = None  # what its [package] says


@dataclass
class RepoIndex:
	packages: dict = field(default_factory=dict)   # path -> PackageInfo
	digest: str = ""     # of the manifests its packages were read from: what they hold and need changes only with it

	def owner(self, project_name):
		"""The package path owning the project named `project_name` (any case), or None."""
		wanted = project_name.lower()
		for package in self.packages.values():
			if any(p.name.lower() == wanted for p in package.projects):
				return package.path
		return None

	def package_of_folder(self, folder):
		"""The nearest package at or above `folder` (a repository-relative path), or None."""
		while True:
			if folder in self.packages:
				return folder
			if not folder:
				return None
			folder = folder.rpartition("/")[0]


def _parent(path):
	return path.rpartition("/")[0]


def _skipped(relative):
	parts = relative.split("/") if relative else []
	return any(p in SKIP or p.startswith(".") for p in parts)


def build(manifest_texts, label):
	"""The index from {repository-relative manifest path: bytes}; `label` names the repository and commit in errors."""
	raw = {}
	for path, data in manifest_texts.items():
		try:
			raw[path] = tomllib.loads(data.decode("utf-8"))
		except (UnicodeDecodeError, tomllib.TOMLDecodeError) as error:
			raise NukeError(f"{label}: {path}: {error}") from None
	index = RepoIndex()
	# A workspace inside the repository (a sample game) is its own: nothing in it belongs to the repository's packages.
	nested = [_parent(p) for p, d in raw.items() if p != MANIFEST_NAME and isinstance(d.get("workspace"), dict)]
	folders = {}
	for path, data in raw.items():
		folder = _parent(path) if path != MANIFEST_NAME else ""
		if not _skipped(folder) and not any(folder == n or folder.startswith(n + "/") for n in nested):
			folders[folder] = (path, data)
	index.digest = _digest({path: manifest_texts[path] for path, _ in folders.values()})
	parsed = {}
	for folder, (path, data) in folders.items():
		if "package" in data or "project" in data or folder == "":
			parsed[folder] = _parse(data, folder, path, label)
	for folder, spec in parsed.items():
		if spec.package is not None:
			index.packages[folder] = PackageInfo(folder, spec.package.description, spec.package.dependencies, (), spec.package.version)
	owned = {path: [] for path in index.packages}
	for folder, spec in sorted(parsed.items()):
		if spec.project is None:
			continue
		owner = index.package_of_folder(folder)
		if owner is not None:
			owned[owner].append(ProjectInfo(spec.project.name, folder, spec.project.depends))
	for path, projects in owned.items():
		package = index.packages[path]
		index.packages[path] = PackageInfo(package.path, package.description, package.dependencies, tuple(projects), package.version)
	return index


def _generated(folder, root_manifest):
	"""What Nuke made in the packages folder of the workspace at `folder` (its root manifest's bytes given), as
	normcased paths from `folder`."""
	try:
		workspace = tomllib.loads(root_manifest.decode("utf-8")).get("workspace")
	except (UnicodeDecodeError, tomllib.TOMLDecodeError):
		return set()        # build() names the error
	packages_dir = workspace.get("packages-dir") if isinstance(workspace, dict) else None
	packages_dir = packages_dir if isinstance(packages_dir, str) and packages_dir.strip() else "packages"
	base = packages_dir.strip("/\\").replace("\\", "/")
	return {os.path.normcase(f"{base}/{name}") for name in install.owned(os.path.join(folder, packages_dir))}


def _digest(texts):
	hasher = hashlib.sha256()
	for path in sorted(texts):
		hasher.update(path.encode("utf-8") + b"\0" + texts[path] + b"\0")
	return hasher.hexdigest()[:32]


def _parse(data, folder, path, label):
	virtual = os.path.abspath(os.path.join("nuke-package-index", path)).replace("\\", "/")
	try:
		return manifests.parse(data, virtual)
	except NukeError as error:
		raise NukeError(str(error).replace(virtual, f"{label}: {path}")) from None


def at_commit(repo, commit, label):
	"""The index of a commit of the repository at `repo` (a mirror)."""
	paths = git.tree_files(repo, commit, MANIFEST_NAME)
	return build(git.read_files(repo, commit, paths), label)


def in_folder(folder, label, prefix=""):
	"""The index of a folder on disk: a develop checkout or a workspace (the repository), or an installed versioned
	package whose folder is `prefix` in its repository. What Nuke made in the folder's packages folder - installed
	packages, clones - is not entered; anything else there is the repository's like any other folder."""
	texts = {}
	generated = set()
	for base, dirs, files in os.walk(folder):
		relative = os.path.relpath(base, folder).replace("\\", "/")
		relative = "" if relative == "." else relative
		if MANIFEST_NAME in files:
			path = "/".join(part for part in (prefix, relative, MANIFEST_NAME) if part)
			with open(os.path.join(base, MANIFEST_NAME), "rb") as f:
				texts[path] = f.read()
			if not relative:
				generated = _generated(folder, texts[path])
		dirs[:] = sorted(d for d in dirs if d not in SKIP and not d.startswith(".")
			and os.path.normcase(f"{relative}/{d}" if relative else d) not in generated)
		if relative and MANIFEST_NAME in files and b"[workspace]" in texts[path]:
			dirs[:] = []        # a workspace of its own (build() leaves it out): its installed packages too
	return build(texts, label)
