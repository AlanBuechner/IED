"""What a repository holds at one commit, or in a checkout's working tree: its packages (folders whose nuke.toml has a
[package] table), the projects each one owns (its own folder's [project] and every project folder below it that is not
inside another package), and the repository workspace's [sources]. Read without checking anything out: one `git ls-tree`
and one `git cat-file --batch` per commit."""

import os
import tomllib
from dataclasses import dataclass, field

from .. import manifest as manifests
from ..errors import NukeError
from ..manifest import MANIFEST_NAME
from . import git

# Never part of a package: build output, the repository's own installed packages (packages-dir), hidden folders.
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


@dataclass
class RepoIndex:
	packages: dict = field(default_factory=dict)   # path -> PackageInfo
	sources: dict = field(default_factory=dict)    # the repository workspace's [sources]
	packages_dir: str = "packages"

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


def _skipped(relative, packages_dir):
	parts = relative.split("/") if relative else []
	return any(p in SKIP or p.startswith(".") for p in parts) or (packages_dir and relative.startswith(packages_dir.rstrip("/") + "/"))


def build(manifest_texts, label):
	"""The index from {repository-relative manifest path: bytes}; `label` names the repository and commit in errors."""
	raw = {}
	for path, data in manifest_texts.items():
		try:
			raw[path] = tomllib.loads(data.decode("utf-8"))
		except (UnicodeDecodeError, tomllib.TOMLDecodeError) as error:
			raise NukeError(f"{label}: {path}: {error}") from None
	root = raw.get(MANIFEST_NAME, {})
	workspace = root.get("workspace") if isinstance(root.get("workspace"), dict) else {}
	packages_dir = workspace.get("packages-dir", "packages") if isinstance(workspace.get("packages-dir", "packages"), str) else "packages"
	index = RepoIndex(packages_dir=packages_dir)
	# A workspace inside the repository (a sample game) is its own: nothing in it belongs to the repository's packages.
	nested = [_parent(p) for p, d in raw.items() if p != MANIFEST_NAME and isinstance(d.get("workspace"), dict)]
	folders = {}
	for path, data in raw.items():
		folder = _parent(path) if path != MANIFEST_NAME else ""
		if not _skipped(folder, packages_dir) and not any(folder == n or folder.startswith(n + "/") for n in nested):
			folders[folder] = (path, data)
	parsed = {}
	for folder, (path, data) in folders.items():
		if "package" in data or "project" in data or folder == "":
			parsed[folder] = _parse(data, folder, path, label)
	for folder, spec in parsed.items():
		if spec.package is not None:
			index.packages[folder] = PackageInfo(folder, spec.package.description, spec.package.dependencies, ())
	if "" in parsed and parsed[""].workspace is not None:
		index.sources = dict(parsed[""].workspace.sources)
	owned = {path: [] for path in index.packages}
	for folder, spec in sorted(parsed.items()):
		if spec.project is None:
			continue
		owner = index.package_of_folder(folder)
		if owner is not None:
			owned[owner].append(ProjectInfo(spec.project.name, folder, spec.project.depends))
	for path, projects in owned.items():
		package = index.packages[path]
		index.packages[path] = PackageInfo(package.path, package.description, package.dependencies, tuple(projects))
	return index


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


def in_folder(folder, label, prefix="", skip=()):
	"""The index of a folder on disk: a develop checkout (the repository), or an installed versioned package whose folder
	is `prefix` in its repository. `skip`: folders (relative) not to enter, such as the workspace's own packages folder."""
	texts = {}
	skipped = {s.strip("/").replace("\\", "/") for s in skip}
	for base, dirs, files in os.walk(folder):
		relative = os.path.relpath(base, folder).replace("\\", "/")
		relative = "" if relative == "." else relative
		dirs[:] = sorted(d for d in dirs if d not in SKIP and not d.startswith(".")
			and (f"{relative}/{d}" if relative else d) not in skipped)
		if MANIFEST_NAME in files:
			path = "/".join(part for part in (prefix, relative, MANIFEST_NAME) if part)
			with open(os.path.join(base, MANIFEST_NAME), "rb") as f:
				texts[path] = f.read()
			if relative and b"[workspace]" in texts[path]:
				dirs[:] = []        # a workspace of its own (build() leaves it out): its installed packages too
	return build(texts, label)
