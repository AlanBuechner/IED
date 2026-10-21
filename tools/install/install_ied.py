"""Installs IED from this checkout's build (docs/21 "Install shape"): the programs and what the server serves and loads
- the web client, the built-in plugins, the host API's schemas, the ui-kit, codicons and the web libraries - copied
into %LOCALAPPDATA%\\Programs\\IED (only what changed), then `ied-desktop --register` puts IED in the Start menu.

	python -I tools/install/install_ied.py --bin build/bin/Development-windows-x64 [--to <folder>] [--no-register]
	nuke install-ied                     (builds first; `nuke install-ied dist` installs an optimized build)

An installed IED that is running keeps running: a program in use is renamed aside (*.old) and the new one copied in
its place, and the old copies are removed by the next install once nothing uses them. Restart IED (`ied stop`, then
open it) to run the new one. Exit code 0 when everything was installed.
"""

import argparse
import filecmp
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

# Program folder in the build -> its files the install needs (beside each other, so each finds the others).
PROGRAMS = {
	"ied-server": ("ied-server.exe", "ied-server.c4i", "C4Script.dll"),
	"ied": ("ied.exe",),
	"ied-desktop": ("ied-desktop.exe",),
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
)


def default_target():
	return os.path.join(os.environ["LOCALAPPDATA"], "Programs", "IED")


def same(source, target):
	return os.path.exists(target) and os.path.getsize(source) == os.path.getsize(target) and filecmp.cmp(source, target, shallow=True)


def copy_program(source, target, report):
	"""Copies a program; one in use is renamed aside first (Windows lets a running program's file be renamed)."""
	if same(source, target):
		return
	try:
		shutil.copy2(source, target)
	except PermissionError:
		aside = "%s.%d.old" % (target, int(time.time() * 1000))
		os.replace(target, aside)
		shutil.copy2(source, target)
		report["in_use"].append(os.path.basename(target))
	report["copied"] += 1


def remove_old(folder):
	for name in os.listdir(folder):
		if name.endswith(".old"):
			try:
				os.remove(os.path.join(folder, name))
			except OSError:
				pass


def mirror(source, target, skip, report):
	"""`target` made like `source` (folders in `skip` left out): changed files copied, files gone from it removed."""
	wanted = set()
	for folder, dirs, files in os.walk(source):
		relative = os.path.relpath(folder, source)
		if relative == ".":
			dirs[:] = [d for d in dirs if d not in skip]
		for name in files:
			path = os.path.normpath(os.path.join(relative, name))
			wanted.add(path)
			destination = os.path.join(target, path)
			if not same(os.path.join(folder, name), destination):
				os.makedirs(os.path.dirname(destination), exist_ok=True)
				shutil.copy2(os.path.join(folder, name), destination)
				report["copied"] += 1
	if not os.path.isdir(target):
		return
	for folder, _, files in os.walk(target, topdown=False):
		for name in files:
			path = os.path.normpath(os.path.relpath(os.path.join(folder, name), target))
			if path not in wanted:
				os.remove(os.path.join(folder, name))
				report["removed"] += 1
		if folder != target and not os.listdir(folder):
			os.rmdir(folder)


def commit():
	"""The checkout's commit, with "-dirty" when the build may hold changes not committed yet."""
	try:
		return subprocess.run(["git", "-C", REPO, "describe", "--always", "--dirty"], capture_output=True, text=True, timeout=30).stdout.strip()
	except OSError:
		return ""


def install(bin_root, target):
	report = {"copied": 0, "removed": 0, "in_use": []}
	os.makedirs(target, exist_ok=True)
	remove_old(target)
	for program, files in PROGRAMS.items():
		for name in files:
			source = os.path.join(bin_root, program, name)
			if not os.path.isfile(source):
				raise SystemExit("install-ied: %s is not built (%s)" % (name, source))
			copy_program(source, os.path.join(target, name), report)
	for tree, skip in TREES:
		source = os.path.join(REPO, tree)
		if not os.path.isdir(source):
			raise SystemExit("install-ied: %s is missing; run setup.bat (the web libraries) or check out the submodules" % tree)
		mirror(source, os.path.join(target, tree), set(skip), report)
	with open(os.path.join(target, "install.json"), "w", encoding="utf-8") as f:
		json.dump({"commit": commit(), "installed": time.strftime("%Y-%m-%dT%H:%M:%S"), "from": os.path.abspath(bin_root)}, f, indent="\t")
		f.write("\n")
	return report


def register(target):
	desktop = os.path.join(target, "ied-desktop.exe")
	code = subprocess.run([desktop, "--register"], timeout=60).returncode
	shortcut = os.path.join(os.environ["APPDATA"], "Microsoft", "Windows", "Start Menu", "Programs", "IED.lnk")
	if code != 0 or not os.path.isfile(shortcut):
		raise SystemExit("install-ied: the Start menu shortcut was not made (ied-desktop --register exited %d)" % code)
	return shortcut


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--bin", required=True, help="the build's bin folder for one configuration (build/bin/<config>-windows-x64)")
	parser.add_argument("--to", default=None, help="the install folder (default %%LOCALAPPDATA%%\\Programs\\IED)")
	parser.add_argument("--no-register", action="store_true", help="do not put IED in the Start menu")
	args = parser.parse_args(argv)
	target = os.path.abspath(args.to or default_target())
	report = install(args.bin, target)
	print("install-ied: %s - %d files copied, %d removed" % (target, report["copied"], report["removed"]))
	if report["in_use"]:
		print("install-ied: IED was running from here (%s); restart it to use the new one: `ied stop`, then open IED" % ", ".join(report["in_use"]))
	if not args.no_register:
		print("install-ied: in the Start menu as IED (%s)" % register(target))
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
