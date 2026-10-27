"""The sources a workspace develops, as language drivers see them (IED docs/31 section 7): a driver points its own tool
at a clone - a go.work for Go, Cargo's [patch] for Rust - with files in the project's folder under build/develop/,
which `nuke package lock` removes. A workspace that develops nothing costs one missing-file check."""

import os
from dataclasses import dataclass

from .. import fsutil
from . import git, install
from .store import same_repository


@dataclass(frozen=True)
class Checkout:
	"""A source the workspace develops: its repository and the clone its packages build from."""
	source: str
	url: str
	folder: str

	def serves(self, url):
		"""Whether `url`, as another tool spells a repository (a Cargo git dependency), is this source's."""
		return same_repository(url, self.url)

	def files(self, name):
		"""The clone's files called `name` (go.mod, Cargo.toml) in its working tree: committed or new, not ignored."""
		listed = git.run(["ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", f":(glob)**/{name}"],
			cwd=self.folder, check=False) or ""
		paths = sorted({path for path in listed.split("\0") if path})
		return [fsutil.join(self.folder, path) for path in paths if os.path.isfile(os.path.join(self.folder, path))]


def of_workspace(spec):
	"""The sources the workspace develops whose clones are on disk, from nuke-packages.json alone."""
	state = install.State(install.folder(spec.dir, spec.packages_dir))
	found = []
	for entry in state.develop.values():
		folder = state.checkout_dir(entry)
		if os.path.isdir(folder):
			found.append(Checkout(entry.get("source") or os.path.basename(folder), entry["url"], folder))
	return tuple(sorted(found, key=lambda checkout: checkout.folder))
