"""What IED reads about a workspace's packages (docs/31 section 6), in its machine-local folder:

- `.ied/local/packages.json`: every versioned package with its folder, version, commit, projects and whether it is
  locked or developed; each develop checkout with its branch, how far it is ahead of and behind its remote, and whether
  it may be edited.
- `.ied/local/readonly/nuke.json`: the folders no one may write, each with the reason and the way out - every locked
  package, and every develop checkout that is behind its remote. IED opens their files read-only (IED docs/10 "Read-only
  folders"); any tool can name folders this way in a file of its own there."""

import json
import os

from .. import fsutil
from . import git, install
from .store import url_key

PACKAGES = ".ied/local/packages.json"
READONLY = ".ied/local/readonly/nuke.json"


def checkout_status(folder):
	"""{branch, upstream, ahead, behind} of a develop checkout, from what it last fetched (no network)."""
	branch = git.branch(folder)
	upstream = git.upstream(folder) if branch else None
	ahead, behind = git.ahead_behind(folder, upstream) if upstream else (0, 0)
	return {"branch": branch, "upstream": upstream, "ahead": ahead, "behind": behind}


def behind_reason(source, status):
	return (f"{source} is {status['behind']} commit(s) behind {status['upstream']}: changes to it were pushed from another "
		f"checkout since. Pull them (`nuke package pull {source}`) before editing, so every change starts from the newest code")


def locked_reason(source, line, version, commit):
	name = f"{source}:{line}" if line else source
	return (f"{name} {version} ({commit[:8]}) is a locked package: an installed version is never edited. "
		f"Develop it (`nuke package develop {source}`) to change it")


def _relative(path, workspace_dir):
	return os.path.relpath(path, workspace_dir).replace("\\", "/") if fsutil.is_within(path, workspace_dir) else fsutil.norm(path)


def write(spec, state, lock, checkouts, joined):
	body, readonly = describe(spec, state, lock, checkouts, joined)
	fsutil.write_if_changed(fsutil.join(spec.dir, PACKAGES), json.dumps(body, indent=2) + "\n")
	fsutil.write_if_changed(fsutil.join(spec.dir, READONLY), json.dumps({"version": 1, "writer": "nuke", "paths": readonly}, indent=2) + "\n")


def describe(spec, state, lock, checkouts, joined):
	"""(packages.json's body, the read-only folders)."""
	developed = {url_key(url): folder for url, folder in checkouts.items()}
	packages, readonly, described = [], [], {}
	for entry in lock.packages:
		checkout = developed.get(url_key(entry.url))
		folder = checkout or fsutil.join(state.root, install.export_name(entry.source, entry.line, entry.version))
		projects = sorted(p.name for p in joined if p.package and url_key(p.package.url) == url_key(entry.url) and p.package.line == entry.line)
		item = {"source": entry.source, "url": entry.url, "path": entry.line, "version": entry.version, "commit": entry.commit,
			"uses": list(entry.uses), "projects": projects, "folder": _relative(folder, spec.dir),
			"mode": "develop" if checkout else "locked"}
		if checkout:
			if checkout not in described:
				described[checkout] = checkout_status(checkout)
			item["writable"] = described[checkout]["behind"] == 0
		else:
			item["writable"] = False
			readonly.append({"path": item["folder"], "reason": locked_reason(entry.source, entry.line, entry.version, entry.commit)})
		packages.append(item)
	checkout_items = []
	for url, folder in sorted(checkouts.items()):
		status = described.get(folder) or checkout_status(folder)
		source = next((e["source"] for e in state.develop.values() if url_key(e["url"]) == url_key(url)), os.path.basename(folder))
		writable = status["behind"] == 0
		checkout_items.append(dict(status, source=source, url=url, folder=_relative(folder, spec.dir), writable=writable))
		if not writable and fsutil.is_within(folder, spec.dir):
			readonly.append({"path": _relative(folder, spec.dir), "reason": behind_reason(source, status)})
	body = {"version": 1, "packagesFolder": spec.packages_dir.replace("\\", "/"), "packages": packages, "checkouts": checkout_items}
	return body, readonly
