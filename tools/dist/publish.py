"""Publishes prebuilt releases to a Gitea package registry and serves their one-command installers (docs/21 "Prebuilt
releases on the owner's Gitea"). Product-neutral: a product is a generic package whose versions each hold the
archives of a release and its `release.json`, and whose version `latest` holds the newest release's `release.json`
with the installers made for this server - so one command installs or updates it on a machine:

	Windows:  irm <server>/api/packages/<owner>/generic/<product>/latest/install.ps1 | iex
	Linux:    curl -fsSL <server>/api/packages/<owner>/generic/<product>/latest/install.sh | sh

	python -I tools/dist/publish.py publish <release folder> [--keep 3] [--no-latest]
	python -I tools/dist/publish.py list <product>
	python -I tools/dist/publish.py promote <product> <version>     (latest = that version: a rollback)
	python -I tools/dist/publish.py remove <product> <version>
	python -I tools/dist/publish.py commands <product>               (the install commands for this server)

A release folder holds `release.json` and the archives it names (tools/dist/ied_release.py makes IED's):

	{"product": "ied", "name": "IED", "version": "2026.11.21-c38038d0", "commit": "<full>", "built": "<ISO 8601>",
	 "platforms": {"windows-x64": {
	    "folder": "%LOCALAPPDATA%\\Programs\\IED",          the install folder (the installer's default)
	    "archives": [{"file": "...zip", "to": "", "sha256": "...", "size": 123}, ...],   each extracted into `to`
	    "keep": ["toolchains", ".cache"],                    what a new version leaves alone in the folder
	    "path": ["", "bin"],                                 folders on the user's PATH
	    "register": [["ied-desktop.exe", "--register"]],     run after an install (Windows)
	    "unregister": [["ied-desktop.exe", "--unregister"]], run before an uninstall (Windows)
	    "packages": {"apt": [...], "dnf": [...]},            system packages a Linux install needs
	    "check": ["ied-server", ...],                        programs a Linux install checks with ldd
	    "glibc": "2.36"}}}                                   the oldest glibc a Linux build runs on

Server, owner and sign-in: tools/dist/gitea.py. Exit code 0 when everything was done.
"""

import argparse
import hashlib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CHECKOUT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import gitea  # noqa: E402

LATEST = "latest"
RELEASE = "release.json"
SUMS = "SHA256SUMS"
INSTALLERS = ("install.ps1", "install.sh")
NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")


class PublishError(Exception):
	pass


def sha256_of(path):
	digest = hashlib.sha256()
	with open(path, "rb") as f:
		for block in iter(lambda: f.read(1 << 20), b""):
			digest.update(block)
	return digest.hexdigest()


def read_release(folder):
	"""release.json of a release folder, checked: names, platforms, and every archive there with its SHA-256."""
	path = os.path.join(folder, RELEASE)
	if not os.path.isfile(path):
		raise PublishError("%s has no %s" % (folder, RELEASE))
	with open(path, encoding="utf-8") as f:
		release = json.load(f)
	for key in ("product", "version"):
		if not NAME.match(str(release.get(key, ""))) or release[key] == LATEST:
			raise PublishError("%s: %r is no %s (letters, digits, '.', '_', '-'; not %r)" % (path, release.get(key), key, LATEST))
	platforms = release.get("platforms") or {}
	if not platforms:
		raise PublishError("%s names no platform" % path)
	for platform, target in platforms.items():
		if not target.get("folder") or not target.get("archives"):
			raise PublishError("%s: %s needs a folder and archives" % (path, platform))
		for archive in target["archives"]:
			name = archive.get("file", "")
			if not NAME.match(name) or name in (RELEASE, SUMS) + INSTALLERS:
				raise PublishError("%s: %r is no archive name" % (path, name))
			file = os.path.join(folder, name)
			if not os.path.isfile(file):
				raise PublishError("%s: %s is not in %s" % (path, name, folder))
			if os.path.getsize(file) != archive.get("size") or sha256_of(file) != archive.get("sha256"):
				raise PublishError("%s: %s is not the file release.json describes (size or SHA-256)" % (path, name))
	return release


def archives_of(release):
	"""Every archive of a release, once each (platforms may share one)."""
	seen = {}
	for target in release["platforms"].values():
		for archive in target["archives"]:
			seen[archive["file"]] = archive
	return list(seen.values())


def sums_text(release):
	return "".join("%s  %s\n" % (a["sha256"], a["file"]) for a in sorted(archives_of(release), key=lambda a: a["file"]))


def render_installer(name, server, owner, product):
	"""An installer for one product on one server: tools/dist/<name> with its @DIST_*@ marks filled in."""
	with open(os.path.join(HERE, name), encoding="utf-8") as f:
		text = f.read()
	text = text.replace("@DIST_SERVER@", server).replace("@DIST_OWNER@", owner).replace("@DIST_PRODUCT@", product)
	left = re.findall(r"@DIST_[A-Z]+@", text)
	if left:
		raise PublishError("%s: unknown marks %s" % (name, ", ".join(sorted(set(left)))))
	# Whatever the checkout's line endings: sh takes LF only (a CR ends up in every word), PowerShell gets CRLF.
	text = text.replace("\r\n", "\n")
	if name.endswith(".ps1"):
		text = text.replace("\n", "\r\n")
	return text.encode("utf-8")


def install_commands(server, owner, product):
	base = "%s/api/packages/%s/generic/%s/%s" % (server.rstrip("/"), owner, product, LATEST)
	return {
		"windows": "irm %s/install.ps1 | iex" % base,
		"linux": "curl -fsSL %s/install.sh | sh" % base,
	}


def set_latest(client, release_bytes, product, say=print):
	"""Points `latest` at a release: the installers for this server, then its release.json (last: an installer finds
	a whole release or, for a moment, none)."""
	for name in INSTALLERS:
		client.upload_bytes(product, LATEST, name, render_installer(name, client.server, client.owner, product))
	client.upload_bytes(product, LATEST, RELEASE, release_bytes)
	say("%s: latest is %s" % (product, json.loads(release_bytes)["version"]))


def publish(client, folder, repo=None, keep=3, latest=True, say=print):
	release = read_release(folder)
	product, version = release["product"], release["version"]
	present = client.files(product, version)
	for archive in archives_of(release):
		name = archive["file"]
		if present.get(name) == archive["sha256"]:
			say("%s %s: %s is there already" % (product, version, name))
			continue
		say("%s %s: uploading %s (%.1f MB)" % (product, version, name, archive["size"] / 1e6))
		client.upload(product, version, name, os.path.join(folder, name))
	client.upload_bytes(product, version, SUMS, sums_text(release).encode("ascii"))
	with open(os.path.join(folder, RELEASE), "rb") as f:
		release_bytes = f.read()
	client.upload_bytes(product, version, RELEASE, release_bytes)
	if repo:
		client.link(product, repo)
	if latest:
		set_latest(client, release_bytes, product, say)
	if keep:
		prune(client, product, keep, protect={version}, say=say)
	return release


def published_versions(client, product):
	return [v for v, _ in client.versions(product) if v != LATEST]


def latest_version(client, product):
	try:
		_, payload = client.request("GET", client.package_path(product, LATEST, RELEASE), ok=(200, 404), signed=False)
	except gitea.GiteaError:
		return None
	try:
		return json.loads(payload).get("version")
	except ValueError:
		return None


def prune(client, product, keep, protect=(), say=print):
	"""Removes all but the `keep` newest versions, never the one `latest` points at nor those in `protect`."""
	current = latest_version(client, product)
	versions = published_versions(client, product)
	kept = set(versions[:keep]) | set(protect) | ({current} if current else set())
	for version in versions:
		if version not in kept:
			client.delete_version(product, version)
			say("%s: removed %s" % (product, version))


def promote(client, product, version, say=print):
	status, payload = client.request("GET", client.package_path(product, version, RELEASE), ok=(200, 404), signed=False)
	if status == 404:
		raise PublishError("%s has no published version %s" % (product, version))
	set_latest(client, payload, product, say)


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--server", help="the Gitea server (default: DIST_SERVER or .ied/local/dist.json)")
	parser.add_argument("--owner", help="the user or organization owning the packages")
	parser.add_argument("--repo", help="the owner's repository the package is linked to")
	commands = parser.add_subparsers(dest="command", required=True)
	one = commands.add_parser("publish", help="upload a release folder and make it latest")
	one.add_argument("folder")
	one.add_argument("--keep", type=int, default=3, help="versions kept (default 3; 0 keeps all)")
	one.add_argument("--no-latest", action="store_true", help="upload only; latest stays where it is")
	for name in ("list", "commands"):
		commands.add_parser(name).add_argument("product")
	for name in ("promote", "remove"):
		command = commands.add_parser(name)
		command.add_argument("product")
		command.add_argument("version")
	args = parser.parse_args(argv)
	try:
		config = gitea.load_config(CHECKOUT, args.server, args.owner, args.repo)
		client = gitea.Gitea(config["server"], config["owner"])
		if args.command == "publish":
			release = publish(client, os.path.abspath(args.folder), config["repo"], args.keep, not args.no_latest)
			for system, line in install_commands(client.server, client.owner, release["product"]).items():
				print("%s: %s" % (system, line))
		elif args.command == "list":
			current = latest_version(client, args.product)
			for version, created in client.versions(args.product):
				if version != LATEST:
					print("%s  %s%s" % (version, created, "  (latest)" if version == current else ""))
		elif args.command == "commands":
			for system, line in install_commands(client.server, client.owner, args.product).items():
				print("%s: %s" % (system, line))
		elif args.command == "promote":
			promote(client, args.product, args.version)
		elif args.command == "remove":
			if args.version == latest_version(client, args.product):
				raise PublishError("%s is latest; promote another version first" % args.version)
			client.delete_version(args.product, args.version)
			print("%s: removed %s" % (args.product, args.version))
	except (gitea.GiteaError, PublishError, OSError, ValueError) as error:
		print("publish: %s" % error, file=sys.stderr)
		return 1
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
