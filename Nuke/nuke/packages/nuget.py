"""NuGet packages: native SDKs a project names in [dependencies] as { nuget = "<package id>", version = "<exact version>" }
- the D3D12 Agility SDK, DXC. Each version is downloaded once per machine from the feed (nuget.org's flat container, or
NUKE_NUGET_FEED: another URL, or a folder laid out the same way) into the package cache, checked against the hash
nuke.lock keeps, and extracted read-only into packages/nuget/<id>@<version>/; a manifest reaches its files as
${nuget:<entry name>} (session.py). Every requirement on one package names the same version: a program has one copy of
an SDK. While the lock names a version and its install is in place, nothing is fetched."""

import hashlib
import json
import os
import re
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from dataclasses import dataclass, replace

from .. import fsutil
from ..errors import NukeError
from . import install, lockfile

FEED = "https://api.nuget.org/v3-flatcontainer"
FEED_ENV = "NUKE_NUGET_FEED"
FOLDER = "nuget"                    # under the packages folder
MARKER = ".nuke-nuget.json"         # in an install: the package, its version and the hash of the .nupkg it came from
TIMEOUT_S = 120
PACKAGING = ("_rels/", "package/", "[Content_Types].xml", ".signature.p7s")   # NuGet's own files, not the package's


@dataclass(frozen=True)
class Need:
	requirement: object             # the PackageRequirement naming it
	project: str                    # the project whose manifest names it


def needs(projects):
	"""{package id, lower case: [Need]} of every NuGet package the projects name."""
	found = {}
	for project in sorted(projects, key=lambda p: p.name.lower()):
		for requirement in project.dependencies:
			if requirement.nuget is not None:
				found.setdefault(requirement.nuget.lower(), []).append(Need(requirement, project.name))
	return found


def sync(spec, projects, cache_root, log, update=None):
	"""{package id, lower case: its install folder} of every NuGet package the projects name: its version chosen (the
	lock's while the manifests accept it, unless `update(package id)` says to choose again), downloaded when the machine
	lacks it, installed once and locked. Installs no package names any more are removed."""
	wanted = needs(projects)
	root = fsutil.join(spec.dir, spec.packages_dir, FOLDER)
	lock = lockfile.read(spec)
	if not wanted and (lock is None or not lock.nuget):
		return {}
	lock = lock or lockfile.Lock()
	feed = os.environ.get(FEED_ENV) or FEED
	chosen, folders = [], {}
	for key in sorted(wanted):
		locked = None if update is not None and update(wanted[key][0].requirement.nuget) else lock.find_nuget(key)
		entry = choose(wanted[key], locked, feed)
		folder, entry = _install(root, entry, feed, cache_root, log)
		folders[key] = folder
		chosen.append(entry)
	_prune(root, {os.path.basename(f) for f in folders.values()}, log)
	if sorted(chosen, key=lambda n: n.id.lower()) != sorted(lock.nuget, key=lambda n: n.id.lower()):
		_report(log, lock.nuget, chosen)
		lock.nuget = chosen
		lockfile.write(spec, lock)
	if wanted:
		install.ensure_ignored(spec.dir, spec.packages_dir)
	return folders


def choose(found, locked, feed):
	"""The LockedNuget a package's requirements settle on: the one exact version they write (the lock's entry when it has
	that version), else - every one says `*` - the lock's, else the feed's newest stable version."""
	exact = sorted({n.requirement.version for n in found if n.requirement.version != "*"})
	package_id = found[0].requirement.nuget
	if len(exact) > 1:
		said = "; ".join(f"{n.project} wants {n.requirement.version} ({n.requirement.origin})" for n in found if n.requirement.version != "*")
		raise NukeError(f"NuGet package {package_id}: the projects want different versions - {said}. A program has one copy "
			f"of it: make them name the same version")
	if exact:
		if locked is not None and locked.version.lower() == exact[0].lower():
			return locked
		return lockfile.LockedNuget(package_id, exact[0], "")
	if locked is not None:
		return locked
	stable = [v for v in versions(feed, package_id) if "-" not in v]
	if not stable:
		raise NukeError(f"NuGet package {package_id} has no stable version on {feed}: name the version you want")
	return lockfile.LockedNuget(package_id, max(stable, key=version_key), "")


def version_key(version):
	"""Orders NuGet versions: by their numbers (missing ones are 0), a prerelease before its release."""
	release, _, label = version.partition("-")
	numbers = [int(part) for part in release.split(".") if part.isdigit()]
	return tuple(numbers + [0] * (4 - len(numbers))), label == "", label


def versions(feed, package_id):
	"""Every version of a package the feed has, as it lists them."""
	data = _read(_location(feed, package_id.lower(), "index.json"), f"the versions of NuGet package {package_id}")
	try:
		return [str(v) for v in json.loads(data.decode("utf-8"))["versions"]]
	except (ValueError, KeyError, TypeError):
		raise NukeError(f"{_location(feed, package_id.lower(), 'index.json')}: not a NuGet version list") from None


def install_folder_name(entry):
	return f"{entry.id.lower()}@{entry.version.lower()}"


# --- installing ------------------------------------------------------------------------------------

def _install(root, entry, feed, cache_root, log):
	"""(the install folder, the entry with its hash): extracted from the machine's copy of the .nupkg, downloaded first
	when there is none; kept as it is while its marker names the same hash."""
	folder = fsutil.join(root, install_folder_name(entry))
	installed = _marker(folder).get("sha256")
	if installed and (not entry.sha256 or installed == entry.sha256):
		return folder, replace(entry, sha256=installed)
	nupkg = _cached_package(entry, feed, cache_root, log)
	digest = _sha256(nupkg)
	if entry.sha256 and digest != entry.sha256:
		raise NukeError(f"NuGet package {entry.id} {entry.version}: {nupkg} has the hash {digest}, not the {entry.sha256} "
			f"nuke.lock recorded: the file is damaged or is not the package that was locked (a feed never changes a "
			f"published version). Delete it to download it again")
	log.note(f"installing NuGet package {entry.id} {entry.version}")
	install.remove(folder)
	_extract(nupkg, folder)
	with open(os.path.join(folder, MARKER), "w", encoding="utf-8", newline="\n") as f:
		json.dump({"id": entry.id, "version": entry.version, "sha256": digest}, f, indent=2)
	install.make_read_only(folder)
	return folder, replace(entry, sha256=digest)


def _marker(folder):
	try:
		with open(os.path.join(folder, MARKER), encoding="utf-8") as f:
			data = json.load(f)
		return data if isinstance(data, dict) else {}
	except (OSError, ValueError):
		return {}


def _cached_package(entry, feed, cache_root, log):
	"""The machine's copy of the package's .nupkg (<cache>/nuget/<id>.<version>.nupkg), downloaded on first use."""
	name = f"{entry.id.lower()}.{entry.version.lower()}.nupkg"
	path = fsutil.join(cache_root, FOLDER, name)
	if os.path.isfile(path):
		return path
	where = _location(feed, entry.id.lower(), entry.version.lower(), name)
	log.note(f"downloading NuGet package {entry.id} {entry.version}")
	try:
		data = _read(where, f"NuGet package {entry.id} {entry.version}")
	except NukeError as error:
		raise NukeError(f"{error}{_known_versions(feed, entry.id)}") from None
	os.makedirs(os.path.dirname(path), exist_ok=True)
	partial = path + ".partial"
	with open(partial, "wb") as f:
		f.write(data)
	os.replace(partial, path)
	return path


def _known_versions(feed, package_id):
	"""What to say after a version the feed lacks: the versions it has (the newest few)."""
	try:
		have = sorted(versions(feed, package_id), key=version_key, reverse=True)
	except NukeError:
		return ""
	return f" (its newest versions there: {', '.join(have[:8])})" if have else f" (the feed has no package {package_id})"


def _extract(nupkg, folder):
	"""The package's files into `folder`: NuGet's own packaging files are left out, and a name that would land outside
	the folder is refused (a .nupkg is a zip from the network)."""
	try:
		with zipfile.ZipFile(nupkg) as archive:
			for item in archive.infolist():
				name = urllib.parse.unquote(item.filename)
				if item.is_dir() or name.startswith(PACKAGING):
					continue
				target = os.path.normpath(os.path.join(folder, name))
				if os.path.isabs(name) or not fsutil.is_within(target, folder) or re.match(r"^[A-Za-z]:", name):
					raise NukeError(f"{nupkg}: '{name}' would be written outside the package's folder")
				os.makedirs(os.path.dirname(target), exist_ok=True)
				with archive.open(item) as source, open(target, "wb") as out:
					out.write(source.read())
	except zipfile.BadZipFile:
		install.remove(folder)
		raise NukeError(f"{nupkg} is not a NuGet package (not a zip); delete it to download it again") from None


def _prune(root, keep, log):
	if not os.path.isdir(root):
		return
	for name in sorted(os.listdir(root)):
		if name not in keep and os.path.isdir(os.path.join(root, name)):
			install.remove(os.path.join(root, name))
			log.detail(f"removed NuGet package {name} (no longer used)")
	if not os.listdir(root):
		os.rmdir(root)


def _report(log, before, after):
	old = {n.id.lower(): n for n in before}
	for entry in after:
		previous = old.pop(entry.id.lower(), None)
		if previous is None:
			log.note(f"locked NuGet package {entry.id} {entry.version}")
		elif previous.version != entry.version:
			log.note(f"locked NuGet package {entry.id} {entry.version}, was {previous.version}")
	for gone in old.values():
		log.note(f"unlocked NuGet package {gone.id} (no longer used)")


def _sha256(path):
	digest = hashlib.sha256()
	with open(path, "rb") as f:
		for block in iter(lambda: f.read(1 << 20), b""):
			digest.update(block)
	return digest.hexdigest()


# --- the feed --------------------------------------------------------------------------------------

def _location(feed, *parts):
	if re.match(r"^[a-z][a-z0-9+.-]*://", feed, re.IGNORECASE):
		return feed.rstrip("/") + "/" + "/".join(urllib.parse.quote(p) for p in parts)
	return os.path.join(feed, *parts)


def _read(where, what):
	"""The bytes at a URL of the feed, or a file of a folder feed."""
	if not re.match(r"^[a-z][a-z0-9+.-]*://", where, re.IGNORECASE):
		try:
			with open(where, "rb") as f:
				return f.read()
		except OSError:
			raise NukeError(f"{what}: {where} does not exist") from None
	request = urllib.request.Request(where, headers={"User-Agent": "nuke"})
	try:
		with urllib.request.urlopen(request, timeout=TIMEOUT_S) as response:
			return response.read()
	except urllib.error.HTTPError as error:
		raise NukeError(f"{what}: {where} answered {error.code} {error.reason}") from None
	except (urllib.error.URLError, OSError) as error:
		raise NukeError(f"{what}: cannot reach {where} ({getattr(error, 'reason', error)})") from None
