"""IED setup (run through setup.bat, which first installs the private Python): git submodules, the pinned
downloads in deps.toml, optionally the C4 "dev" toolchain, and optionally bin/ on the user PATH."""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tomllib
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEPS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "deps.toml")
DOWNLOADS = os.path.join(ROOT, "vendor", "_downloads")
MARKER = ".setup.json"
DEFAULT_GROUPS = ("core", "ied")
CHUNK = 1 << 20
# Characters Windows cannot have in a file name; archive members containing them are skipped.
WINDOWS_ILLEGAL = set(':*?"<>|')


def log(text):
	print(f"[setup] {text}", flush=True)


class SetupError(Exception):
	pass


# --- packages ---------------------------------------------------------------------------------------

def load_packages():
	with open(DEPS, "rb") as f:
		packages = tomllib.load(f)["packages"]
	for name, package in packages.items():
		package["name"] = name
		package.setdefault("strip", 0)
		if "format" not in package:
			package["format"] = _format_of(package["url"])
	return packages


def _format_of(url):
	for suffix, kind in ((".tar.xz", "txz"), (".tgz", "tgz"), (".tar.gz", "tgz"), (".zip", "zip"), (".nupkg", "zip")):
		if url.endswith(suffix):
			return kind
	raise SetupError(f"cannot tell the archive format of {url}; give the package a 'format'")


def dest_of(package):
	return os.path.join(ROOT, *package["dest"].split("/"))


def installed_state(package):
	"""'installed', 'outdated', 'unverified' (present but not installed by setup) or 'missing'."""
	dest = dest_of(package)
	marker = os.path.join(dest, MARKER)
	if os.path.isfile(marker):
		with open(marker, encoding="utf-8") as f:
			recorded = json.load(f)
		return "installed" if recorded.get("sha256") == package["sha256"] else "outdated"
	if os.path.exists(os.path.join(dest, *package["probe"].split("/"))):
		return "unverified"
	return "missing"


def download(package):
	os.makedirs(DOWNLOADS, exist_ok=True)
	path = os.path.join(DOWNLOADS, f"{package['name']}-{package['sha256'][:12]}.{package['format']}")
	if os.path.isfile(path) and sha256_of(path) == package["sha256"]:
		return path
	log(f"downloading {package['name']} {package['version']}")
	partial = path + ".part"
	with urllib.request.urlopen(package["url"]) as response, open(partial, "wb") as out:
		total = int(response.headers.get("Content-Length") or 0)
		done, shown = 0, 0
		while chunk := response.read(CHUNK):
			out.write(chunk)
			done += len(chunk)
			if total and done * 10 // total > shown:
				shown = done * 10 // total
				log(f"  {package['name']}: {done * 100 // total}%")
	actual = sha256_of(partial)
	if actual != package["sha256"]:
		os.remove(partial)
		raise SetupError(f"{package['name']}: the download's SHA256 is {actual}, expected {package['sha256']}; not installing it")
	os.replace(partial, path)
	return path


def sha256_of(path):
	digest = hashlib.sha256()
	with open(path, "rb") as f:
		while chunk := f.read(CHUNK):
			digest.update(chunk)
	return digest.hexdigest()


def extract(package, archive):
	dest = dest_of(package)
	staging = dest + ".setup-tmp"
	shutil.rmtree(staging, ignore_errors=True)
	log(f"extracting {package['name']} into {os.path.relpath(dest, ROOT)}")
	skipped = _extract_zip(archive, staging, package["strip"]) if package["format"] == "zip" else \
		_extract_tar(archive, staging, package["strip"], "r:xz" if package["format"] == "txz" else "r:gz")
	if skipped:
		log(f"  skipped {skipped} file(s) whose names Windows cannot create")
	remove_installed(dest)
	os.makedirs(os.path.dirname(dest), exist_ok=True)
	os.replace(staging, dest)
	with open(os.path.join(dest, MARKER), "w", encoding="utf-8") as f:
		json.dump({"name": package["name"], "version": package["version"], "sha256": package["sha256"]}, f, indent=2)


def _member_path(name, strip):
	parts = [p for p in name.replace("\\", "/").split("/") if p not in ("", ".")]
	if ".." in parts or len(parts) <= strip:
		return None
	return parts[strip:]


def _extract_tar(archive, dest, strip, mode):
	skipped = 0
	with tarfile.open(archive, mode) as tar:
		for member in tar:
			parts = _member_path(member.name, strip)
			if parts is None or not (member.isfile() or member.isdir()):
				continue
			if any(WINDOWS_ILLEGAL & set(p) for p in parts):
				skipped += 1
				continue
			target = os.path.join(dest, *parts)
			if member.isdir():
				os.makedirs(target, exist_ok=True)
				continue
			os.makedirs(os.path.dirname(target), exist_ok=True)
			with tar.extractfile(member) as source, open(target, "wb") as out:
				shutil.copyfileobj(source, out, CHUNK)
	return skipped


def _extract_zip(archive, dest, strip):
	skipped = 0
	with zipfile.ZipFile(archive) as z:
		for info in z.infolist():
			parts = _member_path(info.filename, strip)
			if parts is None:
				continue
			if any(WINDOWS_ILLEGAL & set(p) for p in parts):
				skipped += 1
				continue
			target = os.path.join(dest, *parts)
			if info.is_dir():
				os.makedirs(target, exist_ok=True)
				continue
			os.makedirs(os.path.dirname(target), exist_ok=True)
			with z.open(info) as source, open(target, "wb") as out:
				shutil.copyfileobj(source, out, CHUNK)
	return skipped


def main_checkout():
	"""The main working tree when this checkout is a linked git worktree, else None."""
	def git_path(flag):
		result = subprocess.run(["git", "-C", ROOT, "rev-parse", "--path-format=absolute", flag], capture_output=True, text=True)
		return os.path.normcase(os.path.normpath(result.stdout.strip())) if result.returncode == 0 else None
	common, own = git_path("--git-common-dir"), git_path("--git-dir")
	if not common or not own or common == own:
		return None
	return os.path.dirname(common)


def link_from_main_checkout(package):
	"""A worktree shares the main checkout's copy of a large package (a directory junction) instead of downloading it."""
	main = main_checkout()
	if not package.get("share-with-worktrees") or main is None:
		return False
	source = os.path.join(main, *package["dest"].split("/"))
	if not os.path.exists(os.path.join(source, *package["probe"].split("/"))):
		return False
	dest = dest_of(package)
	os.makedirs(os.path.dirname(dest), exist_ok=True)
	if subprocess.run(["cmd", "/c", "mklink", "/J", dest, source], capture_output=True).returncode != 0:
		return False
	log(f"{package['name']}: linked to the main checkout's copy ({source})")
	return True


def remove_installed(dest):
	"""Delete an installed package; a junction to the main checkout's copy is unlinked, never followed."""
	if os.path.isjunction(dest) or os.path.islink(dest):
		os.rmdir(dest) if os.path.isdir(dest) else os.remove(dest)
	else:
		shutil.rmtree(dest, ignore_errors=True)


def install(package, force, keep_downloads):
	state = installed_state(package)
	if state == "missing" and not force and link_from_main_checkout(package):
		return
	if state == "installed" and not force:
		log(f"{package['name']} {package['version']}: up to date")
		return
	if state == "unverified" and not force:
		log(f"{package['name']}: found an existing copy in {package['dest']} that setup did not install; keeping it (--force replaces it)")
		return
	archive = download(package)
	extract(package, archive)
	if not keep_downloads:
		os.remove(archive)
	log(f"{package['name']} {package['version']}: installed")


# --- submodules, toolchain, PATH --------------------------------------------------------------------

def update_submodules():
	if not os.path.exists(os.path.join(ROOT, ".git")):
		log("not a git checkout; skipping submodules")
		return
	log("updating git submodules")
	if subprocess.run(["git", "-C", ROOT, "submodule", "update", "--init", "--recursive", "--jobs", "4"]).returncode != 0:
		raise SetupError("git submodule update failed")


def build_toolchain():
	log("building the C4 compiler and the dev toolchain (nuke build -p C4); the first build takes a while")
	nuke = os.path.join(ROOT, "Nuke", "nuke")
	if subprocess.run([sys.executable, nuke, "build", "-p", "C4", "dev"], cwd=ROOT).returncode != 0:
		raise SetupError("building the C4 compiler failed (see above)")


def bin_dir():
	return os.path.join(ROOT, "bin")


def on_user_path():
	import winreg
	with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as key:
		try:
			value, _ = winreg.QueryValueEx(key, "Path")
		except FileNotFoundError:
			return False, ""
	entries = [os.path.normcase(os.path.normpath(os.path.expandvars(e))) for e in value.split(";") if e]
	return os.path.normcase(os.path.normpath(bin_dir())) in entries, value


def add_to_user_path():
	import ctypes
	import winreg
	present, value = on_user_path()
	if present:
		log(f"{bin_dir()} is already on your user PATH")
		return
	updated = (value.rstrip(";") + ";" if value else "") + bin_dir()
	with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment", 0, winreg.KEY_SET_VALUE) as key:
		winreg.SetValueEx(key, "Path", 0, winreg.REG_EXPAND_SZ, updated)
	HWND_BROADCAST, WM_SETTINGCHANGE, SMTO_ABORTIFHUNG = 0xFFFF, 0x1A, 0x2
	ctypes.windll.user32.SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, "Environment", SMTO_ABORTIFHUNG, 5000, None)
	log(f"added {bin_dir()} to your user PATH; open a new terminal to use `nuke` anywhere")


def offer_path(options):
	if options.no_path or sys.platform != "win32":
		return
	if options.add_to_path:
		add_to_user_path()
		return
	if on_user_path()[0] or not sys.stdin.isatty():
		return
	answer = input(f"[setup] add {bin_dir()} to your user PATH so `nuke` works in any folder? [Y/n] ").strip().lower()
	if answer in ("", "y", "yes"):
		add_to_user_path()


# --- command line -----------------------------------------------------------------------------------

def selected(packages, options):
	if options.only:
		names = [n.strip() for n in options.only.split(",") if n.strip()]
		unknown = [n for n in names if n not in packages]
		if unknown:
			raise SetupError(f"unknown package(s): {', '.join(unknown)} (setup.bat --list shows them)")
		return [packages[n] for n in names]
	groups = (set(DEFAULT_GROUPS) | set(options.with_groups)) - set(options.without_groups)
	groups.add("core")
	return [p for p in packages.values() if p["group"] in groups]


def print_list(packages):
	for package in packages.values():
		log(f"{package['name']:<24} {package['version']:<12} {package['group']:<7} {installed_state(package):<11} {package['dest']}")


def parse(argv):
	parser = argparse.ArgumentParser(prog="setup.bat", description=__doc__)
	parser.add_argument("--with", dest="with_groups", action="append", default=[], help="also install a group (extras)")
	parser.add_argument("--without", dest="without_groups", action="append", default=[], help="skip a group (ied)")
	parser.add_argument("--only", help="install only these packages (comma-separated)")
	parser.add_argument("--force", action="store_true", help="download and extract again even when installed")
	parser.add_argument("--keep-downloads", action="store_true", help="keep the archives in vendor/_downloads")
	parser.add_argument("--no-submodules", action="store_true", help="do not run git submodule update")
	parser.add_argument("--toolchain", action="store_true", help="also build the C4 compiler and the dev toolchain")
	parser.add_argument("--add-to-path", action="store_true", help="add bin/ to the user PATH without asking")
	parser.add_argument("--no-path", action="store_true", help="never touch PATH")
	parser.add_argument("--list", action="store_true", help="show every package and whether it is installed")
	return parser.parse_args(argv)


def main(argv):
	options = parse(argv)
	packages = load_packages()
	if options.list:
		print_list(packages)
		return 0
	try:
		if not options.no_submodules and not options.only:
			update_submodules()
		for package in selected(packages, options):
			install(package, options.force, options.keep_downloads)
		if options.toolchain:
			build_toolchain()
		offer_path(options)
	except SetupError as error:
		log(f"ERROR: {error}")
		return 1
	except OSError as error:
		log(f"ERROR: {error}")
		return 1
	log("done. Next: `nuke build` builds the C4 compiler (or run setup.bat --toolchain); `nuke help` lists the commands")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
