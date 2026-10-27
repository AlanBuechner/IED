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
from .store import repository_name, url_key, web_links, web_url


def edge_key(url, line):
	"""How the dependency edges name a versioned package: its repository (url_key) and its path there."""
	return f"{url_key(url)}#{line}"

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


def _ref(entry):
	"""The tag of a locked release, else its commit: what its links point at."""
	if "+" in entry.version:
		return entry.commit
	return f"{entry.line}/v{entry.version}" if entry.line else f"v{entry.version}"


def _dependencies(entry, state, lock):
	"""What the versioned package needs of others (edges from it), each with the package it reached and its version."""
	by_key = {edge_key(e.url, e.line): e for e in lock.packages}
	mine = edge_key(entry.url, entry.line)
	found = []
	for key, needs in sorted(state.needs.items()):
		target = by_key.get(key)
		for need in needs:
			if need.get("from") == mine and target is not None:
				item = {"source": target.source, "url": target.url, "path": target.line, "version": target.version,
					"requirement": need.get("requirement", "*"), "because": need.get("by", "")}
				if item not in found:
					found.append(item)
	return found


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
			"mode": "develop" if checkout else "locked", "links": web_links(entry.url, _ref(entry), entry.line),
			"requiredBy": state.needs.get(edge_key(entry.url, entry.line), []), "dependsOn": _dependencies(entry, state, lock)}
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
	sources = [{"name": name, "url": url, "web": web_url(url)} for name, url in spec.sources.items()]
	named = {url_key(e.url): e.source for e in lock.packages}
	for requirement in spec.packages:
		url = spec.sources.get(requirement.source) or requirement.source
		if all(url_key(s["url"]) != url_key(url) for s in sources):
			sources.append({"name": named.get(url_key(url)) or repository_name(url), "url": url, "web": web_url(url), "git": True})
	for entry in lock.packages:
		if all(url_key(s["url"]) != url_key(entry.url) for s in sources):
			sources.append({"name": entry.source, "url": entry.url, "web": web_url(entry.url), "transitive": True})
	requirements = [{"path": r.path, "source": r.source, "url": spec.sources.get(r.source) or r.source, "requirement": r.describe(),
		"kind": "branch" if r.branch else "rev" if r.rev else "version"} for r in spec.packages]
	body = {"version": 1, "packagesFolder": spec.packages_dir.replace("\\", "/"), "sources": sources, "requirements": requirements,
		"packages": packages, "checkouts": checkout_items}
	return body, readonly
