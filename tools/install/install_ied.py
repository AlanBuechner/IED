"""Installs IED from this checkout's build (docs/21 "Install shape"): the programs and what the server serves and loads
- the web client, the built-in plugins, the host API's schemas, the ui-kit, codicons and the web libraries, and the
services web search runs (the private Python, SearXNG with its packages and Tor, which setup installs into the checkout
first when they are missing, docs/29), pip for the Python terminal, the C/C++ plugin's clangd and CodeLLDB, and Nuke
with its launchers (docs/21) - copied into %LOCALAPPDATA%\\Programs\\IED (only what changed), then
`ied-desktop --register` puts IED in the Start menu and
makes IED's solution files (.iedsln, .iedfed) open in it, and the folder and its bin go on the user's PATH, so
`ied <folder>` and `nuke` work in a terminal opened afterwards.

	python -I tools/install/install_ied.py --bin build/bin/Development-windows-x64 [--to <folder>] [--no-register] [--no-path]
	python -I tools/install/install_ied.py --uninstall [--to <folder>]
	nuke install-ied                     (builds first; `nuke install-ied dist` installs an optimized build)

An installed IED that is running keeps running: a program or library in use (a running SearXNG's or Tor's too) is
renamed aside (*.old) and the new one copied in its place, and the old copies are removed by the next install once
nothing uses them. Restart IED (`ied stop`, then
open it) to run the new one. --uninstall takes IED out of the Start menu and the PATH and removes the folder (close IED
first). Exit code 0 when everything was done.
"""

import argparse
import filecmp
import json
import os
import shutil
import subprocess
import sys
import time

if os.name == "nt":
	import ctypes
	import winreg

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

# Program folder in the build -> its files the install needs (beside each other, so each finds the others).
PROGRAMS = {
	"ied-server": ("ied-server.exe", "ied-server.c4i", "C4Script.dll"),
	"ied": ("ied.exe",),
	"ied-desktop": ("ied-desktop.exe",),
}

# Each program's debug information, when the build made it (-g): beside the program, a crash report names functions
# and lines (C4/docs/library/bedrock.md "Crash reports"). One the build no longer has is removed, so a report never
# reads an older build's.
SYMBOLS = {
	"ied-server": "ied-server.pdb",
	"ied": "ied.pdb",
	"ied-desktop": "ied-desktop.pdb",
}

# The server's home (src/host/config: HomeFolder finds shared/host-api/schemas above the program), and what it skips.
TREES = (
	("frontend", ("mocks", "tests")),
	("plugins", ()),
	("shared/host-api/schemas", ()),
	("shared/ui-kit", ()),
	("vendor/web", ()),
	("third_party/codicons/src/icons", ()),
	("third_party/codicons/src/template", ()),
	("Nuke/nuke", ()),
	("Nuke/templates", ()),
	("bin", ()),
)
# The folders that go on the user's PATH: the programs', and bin, Nuke's launchers (Nuke/nuke/installed.py: the Nuke an
# install carries keeps the checkout as its home).
PATH_FOLDERS = (".", "bin")
# What a checkout's own work leaves inside those trees, at any depth, and an install leaves out: a plugin's sample built
# in place (plugins/c4/samples/*/build), a crate's cargo output (target), the bytecode of a Nuke driver Python ran.
BUILT_IN_PLACE = {"build", "target", "__pycache__"}

# The services the web plugins run (docs/29): the private Python, SearXNG's own package (its searx/ folder) and Python
# packages, and the Tor Expert Bundle; and pip, which the Python terminal's pip() runs (docs/05 "Terminal profiles").
# Each install first has setup install them into the checkout's vendor/ (only what is missing or out of date), then
# copies them; without them web search says what is missing, and web fetch uses Windows' curl.
SERVICES = (
	("vendor/python", ("__pycache__",)),
	("vendor/pip", ("__pycache__",)),
	("vendor/searxng/searx", ("__pycache__",)),
	("vendor/searxng-packages", ("__pycache__", "bin")),
	("vendor/tor", ("docs",)),
)
SERVICE_PACKAGES = ("searxng", "pip", "searxng-packages", "tor", "codelldb")

# The C/C++ plugin's own tools (docs/21 "Bundled vs detected", docs/26 "Language tools"), copied into its folder in the
# install, where its manifest looks first: clangd from the vendored LLVM with clang's builtin headers, which it finds
# beside itself (../lib/clang/<version>/include), and CodeLLDB's adapter with its LLDB. The mirror of plugins/ keeps them.
CPP_TOOLS_FOLDERS = ("cpp/clangd", "cpp/codelldb")


def cpp_tools(repo):
	"""(source, target, is_tree) of the C/C++ plugin's tools that the checkout has, relative to it and to the install."""
	found = []
	clangd = os.path.join(repo, "vendor", "llvm", "bin", "clangd.exe")
	if os.path.isfile(clangd):
		found.append((clangd, os.path.join("plugins", "cpp", "clangd", "bin", "clangd.exe"), False))
		resources = os.path.join(repo, "vendor", "llvm", "lib", "clang")
		for version in sorted(os.listdir(resources)) if os.path.isdir(resources) else []:
			include = os.path.join(resources, version, "include")
			if os.path.isdir(include):
				found.append((include, os.path.join("plugins", "cpp", "clangd", "lib", "clang", version, "include"), True))
	codelldb = os.path.join(repo, "vendor", "codelldb")
	for part in ("adapter", "lldb"):
		if os.path.isdir(os.path.join(codelldb, part)):
			found.append((os.path.join(codelldb, part), os.path.join("plugins", "cpp", "codelldb", part), True))
	return found


def services_command(repo):
	"""setup for the services' packages: through the private Python when it is there, else setup.bat (which installs
	it first). Never touches PATH."""
	python = os.path.join(repo, "vendor", "python", "python.exe")
	options = ["--only", ",".join(SERVICE_PACKAGES), "--no-path"]
	if os.path.isfile(python):
		return [python, "-I", os.path.join(repo, "tools", "setup", "setup.py")] + options
	return ["cmd", "/d", "/c", os.path.join(repo, "setup.bat")] + options


def ensure_services(repo):
	"""Whether setup left the services installed in the checkout (it says what it did; offline it fails)."""
	return subprocess.run(services_command(repo), cwd=repo).returncode == 0


def default_target():
	return os.path.join(os.environ["LOCALAPPDATA"], "Programs", "IED")


def same(source, target):
	return os.path.exists(target) and os.path.getsize(source) == os.path.getsize(target) and filecmp.cmp(source, target, shallow=True)


def aside(path):
	"""Renames a file in use out of the way (Windows lets a running program's file, or a loaded DLL, be renamed); the
	next install removes it."""
	os.replace(path, "%s.%d.old" % (path, int(time.time() * 1000)))


def replace_file(source, target, report):
	"""Copies `source` over `target`; a target in use is renamed aside first."""
	try:
		shutil.copy2(source, target)
	except PermissionError:
		aside(target)
		shutil.copy2(source, target)
		report["in_use"].append(os.path.basename(target))
	report["copied"] += 1


def discard(path, report):
	"""Removes a file the source no longer has; one in use is renamed aside (an aside one is left for the next time)."""
	try:
		os.remove(path)
	except PermissionError:
		if path.endswith(".old"):
			return
		aside(path)
	report["removed"] += 1


def copy_program(source, target, report):
	"""Copies a program; one in use is renamed aside first."""
	if not same(source, target):
		replace_file(source, target, report)


def install_symbols(bin_root, target, report):
	"""Each program's PDB beside it when the build has one; one it has not is removed from the install."""
	for program, name in SYMBOLS.items():
		source = os.path.join(bin_root, program, name)
		installed = os.path.join(target, name)
		if os.path.isfile(source):
			copy_program(source, installed, report)
		elif os.path.isfile(installed):
			discard(installed, report)


def remove_old(folder):
	for name in os.listdir(folder):
		if name.endswith(".old"):
			try:
				os.remove(os.path.join(folder, name))
			except OSError:
				pass


def mirror(source, target, skip, report, anywhere=(), keep=()):
	"""`target` made like `source` (folders in `skip` left out at its top, those in `anywhere` at any depth): changed
	files copied, files gone from it removed - but those under `keep` (target-relative folders, '/'-separated), which
	something else puts there. Names compare as the file system does (on Windows, whatever their case), and a file
	the source spells in another case takes the source's spelling."""
	wanted = {}
	for folder, dirs, files in os.walk(source):
		relative = os.path.relpath(folder, source)
		dirs[:] = [d for d in dirs if d not in anywhere and not (relative == "." and d in skip)]
		for name in files:
			path = os.path.normpath(os.path.join(relative, name))
			wanted[os.path.normcase(path)] = path
			destination = os.path.join(target, path)
			if not same(os.path.join(folder, name), destination):
				os.makedirs(os.path.dirname(destination), exist_ok=True)
				replace_file(os.path.join(folder, name), destination, report)
	if not os.path.isdir(target):
		return
	kept = tuple(os.path.normpath(k) + os.sep for k in keep)
	for folder, _, files in os.walk(target, topdown=False):
		for name in files:
			path = os.path.normpath(os.path.relpath(os.path.join(folder, name), target))
			spelled = wanted.get(os.path.normcase(path))
			if spelled is None and not path.startswith(kept):
				discard(os.path.join(folder, name), report)
			elif spelled is not None and spelled != path:
				respell(os.path.join(target, path), os.path.join(target, spelled))
		if folder != target and not os.listdir(folder):
			os.rmdir(folder)


def respell(path, spelled):
	"""Renames `path` to `spelled`, the same name in another case (through a name of its own: Windows keeps the old
	spelling when a rename changes only the case)."""
	passing = path + ".respelling"
	os.replace(path, passing)
	os.replace(passing, spelled)


def install_cpp_tools(repo, target, report):
	"""The C/C++ plugin's tools into its folder of the install; what the checkout lacks is reported (clangd always comes
	with the LLVM setup installs; CodeLLDB is setup's codelldb package, else the plugin offers its one-click install)."""
	tools = cpp_tools(repo)
	for source, relative, is_tree in tools:
		destination = os.path.join(target, relative)
		if is_tree:
			mirror(source, destination, set(), report)
		elif not same(source, destination):
			os.makedirs(os.path.dirname(destination), exist_ok=True)
			replace_file(source, destination, report)
	for name, folder in (("clangd", "plugins/cpp/clangd"), ("CodeLLDB", "plugins/cpp/codelldb")):
		if not any(relative.replace(os.sep, "/").startswith(folder) for _, relative, _ in tools):
			report["missing"].append(name + " (vendor/llvm, vendor/codelldb)")


def checkout_state(repo):
	"""(commit, dirty): the checkout's HEAD and whether a tracked file has changes not committed (untracked files, a
	session's scratch, do not count); ("", False) without git."""
	try:
		head = subprocess.run(["git", "-C", repo, "rev-parse", "HEAD"], capture_output=True, text=True, timeout=30).stdout.strip()
		changed = subprocess.run(["git", "-C", repo, "status", "--porcelain", "--untracked-files=no"], capture_output=True, text=True, timeout=60).stdout
		return head, bool(changed.strip())
	except OSError:
		return "", False


def install_record(repo, bin_root, head, dirty, now_ms):
	"""install.json: the checkout an install came from (`source`, forward slashes, so an installed IED finds it), its
	commit and whether it had uncommitted changes, when, and the build folder it was copied from."""
	return {"source": os.path.abspath(repo).replace("\\", "/"), "commit": head, "dirty": dirty, "installedMs": now_ms, "bin": os.path.abspath(bin_root).replace("\\", "/")}


def write_record(target, record):
	"""Written last and whole (renamed into place), so an install.json says the install is complete."""
	temporary = os.path.join(target, "install.json.tmp")
	with open(temporary, "w", encoding="utf-8") as f:
		json.dump(record, f, indent="\t")
		f.write("\n")
	os.replace(temporary, os.path.join(target, "install.json"))


def install(bin_root, target):
	report = {"copied": 0, "removed": 0, "in_use": [], "missing": []}
	os.makedirs(target, exist_ok=True)
	remove_old(target)
	for program, files in PROGRAMS.items():
		for name in files:
			source = os.path.join(bin_root, program, name)
			if not os.path.isfile(source):
				raise SystemExit("install-ied: %s is not built (%s)" % (name, source))
			copy_program(source, os.path.join(target, name), report)
	install_symbols(bin_root, target, report)
	for tree, skip in TREES:
		source = os.path.join(REPO, tree)
		if not os.path.isdir(source):
			raise SystemExit("install-ied: %s is missing; run setup.bat (the web libraries) or check out the submodules" % tree)
		mirror(source, os.path.join(target, tree), set(skip), report, anywhere=BUILT_IN_PLACE, keep=CPP_TOOLS_FOLDERS if tree == "plugins" else ())
	if not ensure_services(REPO):
		print("install-ied: setup could not install the web search services (see above); installing without what is missing")
	for tree, skip in SERVICES:
		source = os.path.join(REPO, tree)
		if os.path.isdir(source):
			mirror(source, os.path.join(target, tree), set(skip), report)
		else:
			report["missing"].append(tree)
	install_cpp_tools(REPO, target, report)
	head, dirty = checkout_state(REPO)
	write_record(target, install_record(REPO, bin_root, head, dirty, int(time.time() * 1000)))
	return report


def shortcut_path():
	return os.path.join(os.environ["APPDATA"], "Microsoft", "Windows", "Start Menu", "Programs", "IED.lnk")


def register(target):
	desktop = os.path.join(target, "ied-desktop.exe")
	code = subprocess.run([desktop, "--register"], timeout=60).returncode
	if code != 0 or not os.path.isfile(shortcut_path()):
		raise SystemExit("install-ied: the Start menu shortcut was not made (ied-desktop --register exited %d)" % code)
	return shortcut_path()


def same_folder(entry, folder):
	return os.path.normcase(os.path.normpath(os.path.expandvars(entry.strip().strip('"')))) == os.path.normcase(os.path.normpath(folder))


def with_folder(path_value, folder):
	"""The PATH value with `folder` added last; None when it is there already."""
	entries = [e for e in path_value.split(";") if e.strip()]
	if any(same_folder(e, folder) for e in entries):
		return None
	return ";".join(entries + [folder])


def without_folder(path_value, folder):
	"""The PATH value without `folder`; None when it was not there."""
	entries = [e for e in path_value.split(";") if e.strip()]
	kept = [e for e in entries if not same_folder(e, folder)]
	return None if len(kept) == len(entries) else ";".join(kept)


def path_folders(target):
	return [os.path.normpath(os.path.join(target, folder)) for folder in PATH_FOLDERS]


def with_folders(path_value, folders, change):
	"""The PATH value with `change` (with_folder or without_folder) applied for each of `folders`; None when none
	changed it."""
	changed = path_value
	for folder in folders:
		changed = change(changed, folder) or changed
	return None if changed == path_value else changed


def change_user_path(change, folders):
	"""Applies `change` (with_folder or without_folder) for each of `folders` to the user's PATH (HKCU, Environment);
	true when it changed. Programs started afterwards see it: Explorer is told, as Settings' Environment Variables
	dialog tells it."""
	with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment", 0, winreg.KEY_READ | winreg.KEY_SET_VALUE) as key:
		try:
			value, kind = winreg.QueryValueEx(key, "Path")
		except FileNotFoundError:
			value, kind = "", winreg.REG_EXPAND_SZ
		changed = with_folders(value, folders, change)
		if changed is None:
			return False
		winreg.SetValueEx(key, "Path", 0, kind if kind in (winreg.REG_SZ, winreg.REG_EXPAND_SZ) else winreg.REG_EXPAND_SZ, changed)
	result = ctypes.c_size_t()
	# HWND_BROADCAST, WM_SETTINGCHANGE, SMTO_ABORTIFHUNG, at most 5 s.
	ctypes.windll.user32.SendMessageTimeoutW(0xFFFF, 0x001A, 0, "Environment", 0x0002, 5000, ctypes.byref(result))
	return True


def running_from(folder):
	"""The image paths of the processes running a program of `folder` (those this user may look at)."""
	kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
	kernel32.OpenProcess.restype = ctypes.c_void_p
	kernel32.OpenProcess.argtypes = (ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32)
	kernel32.QueryFullProcessImageNameW.argtypes = (ctypes.c_void_p, ctypes.c_uint32, ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_uint32))
	kernel32.CloseHandle.argtypes = (ctypes.c_void_p,)
	ids = (ctypes.c_uint32 * 8192)()
	used = ctypes.c_uint32()
	if not kernel32.K32EnumProcesses(ids, ctypes.sizeof(ids), ctypes.byref(used)):
		return []
	inside = os.path.normcase(os.path.abspath(folder)) + os.sep
	found = []
	for pid in ids[:used.value // ctypes.sizeof(ctypes.c_uint32)]:
		handle = kernel32.OpenProcess(0x1000, 0, pid)   # PROCESS_QUERY_LIMITED_INFORMATION
		if not handle:
			continue
		name = ctypes.create_unicode_buffer(1024)
		size = ctypes.c_uint32(len(name))
		if kernel32.QueryFullProcessImageNameW(handle, 0, name, ctypes.byref(size)) and os.path.normcase(name.value).startswith(inside):
			found.append(name.value)
		kernel32.CloseHandle(handle)
	return found


def remove_install(target):
	"""Removes an install folder, never while a program of it runs (a running program cannot be deleted, so the folder
	would be left half removed)."""
	if not os.path.isfile(os.path.join(target, "install.json")):
		raise SystemExit("install-ied: %s is not an IED install (no install.json); left as it is" % target)
	running = running_from(target)
	if running:
		raise SystemExit("install-ied: IED is running from %s (%s); close its windows and `ied stop`, then uninstall again" % (target, ", ".join(sorted(set(os.path.basename(p) for p in running)))))
	shutil.rmtree(target)


def uninstall(target):
	desktop = os.path.join(target, "ied-desktop.exe")
	if os.path.isfile(desktop):
		subprocess.run([desktop, "--unregister"], timeout=60)
	if os.path.isfile(shortcut_path()):
		raise SystemExit("install-ied: the Start menu shortcut is still there: %s" % shortcut_path())
	print("install-ied: out of the PATH" if change_user_path(without_folder, path_folders(target)) else "install-ied: it was not on the PATH")
	if os.path.isdir(target):
		remove_install(target)
		print("install-ied: removed %s" % target)
	return 0


def restart_note(report, running):
	"""What to say of an IED running from the install folder (`running`: its programs' paths): a program that was
	replaced, or anything else that changed - a running server read its plugins' manifests and the schemas when it
	started, and a desktop app reloading its page reaches that same server. None when no restart is needed."""
	if report["in_use"]:
		return "install-ied: IED was running from here (%s); restart it to use the new one: `ied stop`, then open IED" % ", ".join(report["in_use"])
	if running and (report["copied"] or report["removed"]):
		names = ", ".join(sorted(set(os.path.basename(p) for p in running)))
		return "install-ied: IED is running from here (%s) with the plugins it read when it started; restart it to use what changed: `ied stop`, then open IED" % names
	return None


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--bin", help="the build's bin folder for one configuration (build/bin/<config>-windows-x64)")
	parser.add_argument("--to", default=None, help="the install folder (default %%LOCALAPPDATA%%\\Programs\\IED)")
	parser.add_argument("--no-register", action="store_true", help="do not put IED in the Start menu")
	parser.add_argument("--no-path", action="store_true", help="do not put the install folder and its bin on the user's PATH")
	parser.add_argument("--uninstall", action="store_true", help="take IED out of the Start menu, the file types and the PATH, and remove the folder")
	args = parser.parse_args(argv)
	target = os.path.abspath(args.to or default_target())
	if args.uninstall:
		return uninstall(target)
	if not args.bin:
		parser.error("--bin is needed to install")
	report = install(args.bin, target)
	print("install-ied: %s - %d files copied, %d removed" % (target, report["copied"], report["removed"]))
	if report["missing"]:
		print("install-ied: without %s: web search and Settings > Languages say what is missing (run setup.bat, then install again)" % ", ".join(report["missing"]))
	note = restart_note(report, running_from(target) if os.name == "nt" else [])
	if note:
		print(note)
	if not args.no_register:
		print("install-ied: in the Start menu as IED (%s)" % register(target))
	if not args.no_path:
		added = change_user_path(with_folder, path_folders(target))
		print("install-ied: on your PATH; terminals opened from now on run `ied` and `nuke`" if added else "install-ied: already on your PATH")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
