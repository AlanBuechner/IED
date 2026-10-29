"""The MSVC C/C++ runtime (its headers and x64 libraries) and the Windows SDK, without Visual Studio: the packages Visual
Studio's installer downloads, found in its channel manifest (pinned in deps.toml by URL and SHA256; the manifest pins
every package it names by SHA256 in turn), unpacked in Visual Studio's own layout - <dest>/VC/Tools/MSVC/<version>/ and
<dest>/Windows Kits/10/ - which clang's -Xmicrosoft-windows-sys-root and lld-link's /winsysroot read.

The runtime's packages are VSIX (zip) files; the SDK's are MSI installers with their CAB files beside them, which
Windows' msiexec unpacks into a folder (an administrative install: no elevation, nothing registered)."""

import glob
import json
import os
import re
import shutil
import subprocess
import time
import urllib.parse
import zipfile

VSIX_CONTENTS = "Contents/"
CAB_NAME = re.compile(rb"[0-9a-fA-F]{32}\.cab")
MSIEXEC_BUSY = 1618          # another installation is in progress: msiexec runs one at a time
MSIEXEC_RETRIES = 30
MSIEXEC_RETRY_SECONDS = 10


class KitError(Exception):
	pass


def install(package, staging, downloads, fetch, log):
	"""Unpack the package's runtime and SDK into `staging`. `fetch(url, sha256, path, label)` downloads one verified
	file (or keeps a verified copy) and returns its path."""
	manifest = load_manifest(package, downloads, fetch)
	log(f"msvc: by installing it you accept Microsoft's license terms for it: {package['license']}")
	for name in package["msvc-packages"]:
		for payload in payloads_of(manifest, name):
			vsix = fetch(payload["url"], payload["sha256"], os.path.join(downloads, payload["fileName"]), name)
			unpack_vsix(vsix, staging)
	sdk = package_named(manifest, package["sdk-package"])
	by_name = {payload["fileName"].replace("\\", "/"): payload for payload in sdk["payloads"]}
	installers = os.path.join(downloads, "sdk")
	for installer in package["sdk-installers"]:
		msi = fetch_installer(by_name, installer, installers, fetch)
		admin_install(msi, staging, log)
	for leftover in glob.glob(os.path.join(staging, "*.msi")):
		os.remove(leftover)
	prune_architectures(staging, package["architectures"])


def prune_architectures(staging, kept):
	"""The SDK's libraries for processors nothing here targets (its installers carry x86 and arm64 beside x64)."""
	for folder in glob.glob(os.path.join(staging, "Windows Kits", "10", "Lib", "*", "*", "*")):
		if os.path.isdir(folder) and os.path.basename(folder).lower() not in kept:
			shutil.rmtree(folder)


def load_manifest(package, downloads, fetch):
	path = fetch(package["url"], package["sha256"], os.path.join(downloads, "VisualStudio.vsman"), "the Visual Studio manifest")
	with open(path, encoding="utf-8-sig") as f:
		return json.load(f)


def package_named(manifest, name):
	for candidate in manifest["packages"]:
		if candidate["id"].lower() == name.lower() and candidate.get("language") in (None, "neutral", "en-US"):
			return candidate
	raise KitError(f"the Visual Studio manifest has no package {name}")


def payloads_of(manifest, name):
	payloads = package_named(manifest, name).get("payloads", [])
	if not payloads:
		raise KitError(f"the Visual Studio manifest's package {name} has no files")
	return payloads


def unpack_vsix(vsix, staging):
	"""A VSIX's Contents/ folder into `staging` (its member names are percent-encoded)."""
	with zipfile.ZipFile(vsix) as archive:
		for info in archive.infolist():
			name = urllib.parse.unquote(info.filename)
			if not name.startswith(VSIX_CONTENTS) or info.is_dir():
				continue
			parts = [part for part in name[len(VSIX_CONTENTS):].split("/") if part]
			if ".." in parts:
				continue
			target = os.path.join(staging, *parts)
			os.makedirs(os.path.dirname(target), exist_ok=True)
			with archive.open(info) as source, open(target, "wb") as out:
				shutil.copyfileobj(source, out, 1 << 20)


def fetch_installer(by_name, installer, folder, fetch):
	"""An SDK installer and the CAB files it names, side by side in `folder` as msiexec wants them."""
	payload = by_name.get(f"Installers/{installer}")
	if payload is None:
		raise KitError(f"the Windows SDK package has no installer {installer}")
	msi = fetch(payload["url"], payload["sha256"], os.path.join(folder, installer), installer)
	with open(msi, "rb") as f:
		cabs = sorted({name.decode() for name in CAB_NAME.findall(f.read())})
	for cab in cabs:
		part = by_name.get(f"Installers/{cab}")
		if part is None:
			raise KitError(f"{installer} needs {cab}, which the Windows SDK package does not have")
		fetch(part["url"], part["sha256"], os.path.join(folder, cab), cab)
	return msi


def admin_install(msi, target, log):
	log(f"msvc: unpacking {os.path.basename(msi)}")
	command = ["msiexec.exe", "/a", os.path.abspath(msi), "/quiet", "/qn", f"TARGETDIR={os.path.abspath(target)}"]
	for _ in range(MSIEXEC_RETRIES):
		code = subprocess.run(command).returncode
		if code != MSIEXEC_BUSY:
			break
		log("msvc: another installation is running; waiting for it")
		time.sleep(MSIEXEC_RETRY_SECONDS)
	if code != 0:
		raise KitError(f"msiexec could not unpack {os.path.basename(msi)} (exit code {code})")
