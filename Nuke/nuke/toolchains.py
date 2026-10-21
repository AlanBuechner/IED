"""The toolchain store: versioned C4 installs shared by every workspace, at <nuke home>/toolchains/c4/<version>/.

A version folder is immutable once installed; "dev" is the one mutable entry, refreshed by every
Development build of the compiler in this repository (C4/nuke.toml)."""

import datetime
import functools
import json
import os
import re
import subprocess
from dataclasses import dataclass, field

from . import fsutil
from .errors import NukeError

KIND = "c4"
DEV = "dev"
LATEST = "latest"
METADATA_FILE = "toolchain.json"
STORE_ENV = "NUKE_TOOLCHAINS"
VERSION_PATTERN = re.compile(r"^\d+\.\d+\.\d+([-+][0-9A-Za-z.-]+)?$")
OPTION = re.compile(r"(?<![\w-])(--?[A-Za-z][\w.=-]*)")
# What a C4 install must hold to compile and link a program (the compiler finds the rest relative to itself).
REQUIRED = ("C4.exe", "Bedrock/bedrock.ch", "C4Runtime", "toolchain/bin/clang.exe", "toolchain/bin/lld-link.exe")


def nuke_home():
	"""The folder that holds Nuke/ (the IED checkout): toolchains/ and vendor/ live beside it."""
	return fsutil.norm(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))


def default_store_root():
	return fsutil.norm(os.environ.get(STORE_ENV) or os.path.join(nuke_home(), "toolchains"))


def version_key(version):
	core = re.split(r"[-+]", version, maxsplit=1)[0]
	return tuple(int(part) for part in core.split("."))


@dataclass(frozen=True)
class Toolchain:
	version: str
	dir: str
	metadata: dict = field(default_factory=dict)
	command: tuple = None            # how to start the compiler; tests substitute a stub

	def compiler_command(self):
		return list(self.command) if self.command else [fsutil.join(self.dir, "C4.exe")]

	def compiler_path(self):
		return self.compiler_command()[-1] if self.command else fsutil.join(self.dir, "C4.exe")


	def accepts(self, option):
		"""Whether the compiler takes `option`: its -h lists every option it has, and an older install lacks newer ones."""
		return option in listed_options(tuple(self.compiler_command()))

	def describe(self):
		commit = self.metadata.get("commit")
		extra = []
		if commit:
			extra.append(f"commit {commit[:10]}{', modified' if self.metadata.get('dirty') else ''}")
		if self.metadata.get("built"):
			extra.append(f"built {self.metadata['built']}")
		underlying = self.metadata.get("version")
		name = f"c4 {self.version}" + (f" ({underlying})" if underlying and underlying != self.version else "")
		return name + (f" ({'; '.join(extra)})" if extra else "")


@functools.lru_cache(maxsize=None)
def listed_options(command):
	"""The options `<command> -h` lists (none when it cannot run)."""
	try:
		result = subprocess.run(list(command) + ["-h"], capture_output=True, text=True, timeout=60)
	except (OSError, subprocess.TimeoutExpired):
		return frozenset()
	return frozenset(OPTION.findall(result.stdout))


def missing_files(folder):
	return [f for f in REQUIRED if not os.path.exists(os.path.join(folder, *f.split("/")))]


def read_metadata(folder):
	text = fsutil.read_text(os.path.join(folder, METADATA_FILE))
	if text is None:
		return {}
	try:
		return json.loads(text)
	except json.JSONDecodeError as error:
		raise NukeError(f"{folder}/{METADATA_FILE}: {error}") from None


def probe_version(folder):
	"""The version `C4.exe --version` reports ("C4 0.1.0" -> "0.1.0")."""
	try:
		result = subprocess.run([os.path.join(folder, "C4.exe"), "--version"], capture_output=True, text=True, timeout=60)
	except (OSError, subprocess.TimeoutExpired) as error:
		raise NukeError(f"cannot run {folder}/C4.exe --version: {error}") from None
	match = re.search(r"(\d+\.\d+\.\d+\S*)", result.stdout)
	if result.returncode != 0 or not match:
		raise NukeError(f"{folder}/C4.exe --version printed no version (exit {result.returncode}): {result.stdout.strip()}")
	return match.group(1)


def open_folder(folder):
	"""A toolchain from any install folder (a store entry, a build image or a vendored copy)."""
	folder = fsutil.norm(os.path.abspath(folder))
	missing = missing_files(folder)
	if missing:
		raise NukeError(f"{folder} is not a complete C4 install (missing {', '.join(missing)})")
	metadata = read_metadata(folder)
	return Toolchain(version=metadata.get("version") or probe_version(folder), dir=folder, metadata=metadata)


class ToolchainStore:
	def __init__(self, root):
		self.root = fsutil.norm(root)

	def kind_dir(self):
		return fsutil.join(self.root, KIND)

	def entry_dir(self, version):
		return fsutil.join(self.kind_dir(), version)

	def installed(self):
		"""Every complete install, numbered versions ascending, then dev."""
		if not os.path.isdir(self.kind_dir()):
			return []
		names = [n for n in os.listdir(self.kind_dir()) if not missing_files(self.entry_dir(n))]
		numbered = sorted((n for n in names if VERSION_PATTERN.match(n)), key=version_key)
		return [self._entry(n) for n in numbered + [n for n in names if n == DEV]]

	def _entry(self, name):
		toolchain = open_folder(self.entry_dir(name))
		return Toolchain(version=name, dir=toolchain.dir, metadata=toolchain.metadata)

	def get(self, version):
		folder = self.entry_dir(version)
		return self._entry(version) if os.path.isdir(folder) and not missing_files(folder) else None

	def latest(self):
		numbered = [t for t in self.installed() if t.version != DEV]
		return numbered[-1] if numbered else None

	def resolve(self, spec, base_dir):
		"""The toolchain a workspace's `toolchain = ...` denotes: a version, "latest", "dev" or a folder path."""
		spec = (spec or LATEST).strip()
		if "/" in spec or "\\" in spec or spec.startswith("."):
			path = spec if os.path.isabs(spec) else os.path.join(base_dir, spec)
			return open_folder(path)
		found = self.latest() if spec == LATEST else self.get(spec)
		if found is None:
			raise NukeError(self._missing_message(spec))
		return found

	def _missing_message(self, spec):
		names = ", ".join(t.version for t in self.installed()) or "none"
		hint = ("build the compiler in the IED checkout (`nuke build -p C4` there makes 'dev'; `nuke install-c4` freezes it as a version)"
			if spec in (DEV, LATEST) else f"install it with `nuke toolchain install <folder>` or pick one of the installed versions")
		return f"toolchain '{spec}' is not installed in {self.kind_dir()} (installed: {names}); {hint}"

	def install(self, source, as_version=None, force=False):
		"""Copy a C4 install (a folder, or an installed name such as "dev") into the store. Returns (toolchain, copied)."""
		is_name = not os.path.isabs(source) and "/" not in source and "\\" not in source and self.get(source) is not None
		toolchain = self.get(source) if is_name else open_folder(source)
		version = as_version or toolchain.metadata.get("version") or toolchain.version
		if version != DEV and not VERSION_PATTERN.match(version):
			raise NukeError(f"'{version}' is not a version number like 1.2.3 (or {DEV})")
		dest = self.entry_dir(version)
		if fsutil.same_path(dest, toolchain.dir):
			raise NukeError(f"{dest} is already the store entry for {version}")
		if os.path.isdir(dest) and version != DEV and not force:
			existing = read_metadata(dest)
			if _same_build(existing, toolchain.metadata):
				return self.get(version), 0
			raise NukeError(f"toolchain {version} is already installed (from commit {existing.get('commit', '?')[:10]}); "
				"installed versions never change - bump the compiler version (C4/C4Lib/src/Compiler/Version.h) or pass --force")
		copied = fsutil.mirror_tree(toolchain.dir, dest)
		metadata = dict(toolchain.metadata, version=version, installed=_now())
		fsutil.write_if_changed(os.path.join(dest, METADATA_FILE), json.dumps(metadata, indent=2) + "\n")
		return self.get(version), copied

	def remove(self, version):
		folder = self.entry_dir(version)
		if not fsutil.remove_tree(folder):
			raise NukeError(f"toolchain {version} is not installed in {self.kind_dir()}")


def _same_build(a, b):
	return bool(a.get("commit")) and a.get("commit") == b.get("commit") and not a.get("dirty") and not b.get("dirty")


def _now():
	return datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat()


# ------------------------------------------------------------------------------------------------ #

WORKSPACE_HEADER = re.compile(r"^\s*\[workspace\]\s*(#.*)?$")
TABLE_HEADER = re.compile(r"^\s*\[")
TOOLCHAIN_LINE = re.compile(r"^(\s*)toolchain\s*=.*$")


def set_workspace_toolchain(text, version):
	"""`text` of a nuke.toml with [workspace].toolchain set to `version` (inserted if missing)."""
	lines = text.splitlines()
	start = next((i for i, line in enumerate(lines) if WORKSPACE_HEADER.match(line)), None)
	if start is None:
		raise NukeError("the workspace manifest has no [workspace] table")
	end = next((i for i in range(start + 1, len(lines)) if TABLE_HEADER.match(lines[i])), len(lines))
	for i in range(start + 1, end):
		match = TOOLCHAIN_LINE.match(lines[i])
		if match:
			lines[i] = f'{match.group(1)}toolchain = "{version}"'
			break
	else:
		lines.insert(start + 1, f'toolchain = "{version}"')
	return "\n".join(lines) + "\n"
