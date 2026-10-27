"""The machine's package cache: one bare mirror per repository, shared by every workspace (<nuke home>/.cache/packages,
or NUKE_PACKAGE_CACHE), and the list of develop checkouts on the machine, so a workspace can be told that another one
is changing the same source."""

import hashlib
import json
import os
import re
import urllib.parse

from .. import fsutil, toolchains
from . import git

CACHE_ENV = "NUKE_PACKAGE_CACHE"
CHECKOUTS = "checkouts.json"


def default_root():
	return fsutil.norm(os.environ.get(CACHE_ENV) or os.path.join(toolchains.nuke_home(), ".cache", "packages"))


def url_key(url):
	"""One spelling per repository: no trailing slash or .git, a local path absolute and in one case."""
	text = url.strip().rstrip("/\\")
	if text.lower().endswith(".git"):
		text = text[:-4]
	if os.path.isabs(text) or re.match(r"^[A-Za-z]:[\\/]", text):
		return os.path.normcase(os.path.abspath(text)).replace("\\", "/")
	if text.startswith("file://"):
		return "file://" + os.path.normcase(text[7:]).replace("\\", "/")
	return text.lower() if "github.com" in text.lower() else text


def same_repository(a, b):
	"""Whether two spellings name one repository, as another tool may spell a source (Cargo's git URLs): a local path and
	its file:// URL are one, and so are https://host/a/b, ssh://git@host/a/b.git and git@host:a/b."""
	return _identity(a) == _identity(b)


def _identity(url):
	text = url.strip()
	if text.lower().startswith("file://"):
		text = urllib.parse.unquote(text[7:])
		text = text[1:] if re.match(r"^/[A-Za-z]:", text) else text
	if os.path.isabs(text) or re.match(r"^[A-Za-z]:[\\/]", text):
		return url_key(text)
	remote = re.match(r"^[A-Za-z][A-Za-z0-9+.-]*://(?:[^@/]*@)?([^/:]+)(?::\d+)?/(.*)$", text) or re.match(r"^(?:[^@/]*@)?([^/:]+):(.*)$", text)
	if remote is None:
		return url_key(text)
	return url_key(f"{remote.group(1).lower()}/{remote.group(2).strip('/')}")


def web_url(url):
	"""The repository's page on its host, for links (https://github.com/owner/repo for its https or SSH URL); None for a
	local path."""
	text = url.strip().rstrip("/")
	if text.lower().endswith(".git"):
		text = text[:-4]
	match = re.match(r"^(?:ssh://)?[^@/]+@([^:/]+)[:/](.+)$", text)
	if match:
		return f"https://{match.group(1)}/{match.group(2).lstrip('/')}"
	if re.match(r"^https?://", text):
		return re.sub(r"^(https?://)[^@/]+@", r"\1", text)
	return None


def web_links(url, ref, path=""):
	"""{repository, tree, commit} pages for a package at `ref` (a tag or a commit), or {} for a local repository."""
	web = web_url(url)
	if not web:
		return {}
	links = {"repository": web, "tree": f"{web}/tree/{ref}" + (f"/{path}" if path else "")}
	if re.fullmatch(r"[0-9a-f]{40}", ref):
		links["commit"] = f"{web}/commit/{ref}"
	return links


def repository_name(url):
	"""The folder name a repository reached by its URL alone gets under packages/ (its last path part)."""
	name = re.split(r"[/:\\]", url_key(url))[-1]
	return re.sub(r"[^A-Za-z0-9_.-]", "-", name) or "source"


class Cache:
	def __init__(self, root=None):
		self.root = fsutil.norm(root or default_root())

	def mirror_path(self, url):
		key = url_key(url)
		digest = hashlib.sha1(key.encode("utf-8")).hexdigest()[:12]
		return fsutil.join(self.root, "git", f"{repository_name(url)}-{digest}.git")

	def mirror(self, url):
		"""The mirror of `url`, cloned on first use."""
		path = self.mirror_path(url)
		git.ensure_mirror(path, url)
		return path

	def has_mirror(self, url):
		return os.path.isfile(os.path.join(self.mirror_path(url), "HEAD"))

	# --- develop checkouts on this machine ---------------------------------------------------------

	def _checkouts_file(self):
		return fsutil.join(self.root, CHECKOUTS)

	def checkouts(self):
		"""{url key: [checkout folders]} of every develop checkout Nuke made or was pointed at on this machine."""
		try:
			with open(self._checkouts_file(), encoding="utf-8") as f:
				data = json.load(f)
		except (OSError, ValueError):
			return {}
		return {k: [p for p in v if isinstance(p, str)] for k, v in data.items() if isinstance(v, list)}

	def record_checkout(self, url, folder, present=True):
		data = self.checkouts()
		key = url_key(url)
		folders = [p for p in data.get(key, []) if not fsutil.same_path(p, folder) and os.path.isdir(p)]
		if present:
			folders.append(fsutil.norm(folder))
		if folders:
			data[key] = folders
		else:
			data.pop(key, None)
		fsutil.write_if_changed(self._checkouts_file(), json.dumps(data, indent=2, sort_keys=True) + "\n")
