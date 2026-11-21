"""Builds IED's prebuilt release for Windows and Linux from one commit, checks that each installs and runs, and
publishes it to the owner's Gitea (docs/21 "Prebuilt releases on the owner's Gitea"; tools/dist/publish.py serves it):

	python -I tools/dist/ied_release.py                      build HEAD, package, publish, check, make it latest
	python -I tools/dist/ied_release.py --commit <rev> --no-publish
	python -I tools/dist/ied_release.py --only windows       one platform (the release then has only that one)

1. A release worktree (default ../IED-release, detached at the commit; never the checkout itself, whose uncommitted
   work and other sessions' edits must not ship) with the checkout's vendor/ as a junction and the submodules copied
   at the commits the release names.
2. Windows: `nuke build C4` (a Development build refreshes the worktree's own toolchains/c4/dev), then `nuke build
   ied-server ied ied-desktop flashbang`. Linux: the same two in tools/linux/builder's container with the worktree at
   /ied, its build/ the Docker volume ied-release-linux-build and NUKE_TOOLCHAINS=/ied/build/toolchains (Linux's own
   store, apart from Windows').
3. Packages in build/release/<version>/ of this checkout: per platform the install folder (tools/install's layout:
   the programs, the web client, the plugins, Nuke with its launchers, on Windows the private Python with the web
   search services and the C/C++ tools) and the C4 `dev` toolchain with the Linux sysroot (so C4 programs link with
   nothing else installed and run on glibc 2.31); on Windows also the MSVC and Windows SDK kit the compiler links
   Windows programs with (vendor/msvc), apart because it rarely changes; and release.json.
4. Published as version `<commit date>-<commit>` without moving `latest`; then each platform's installer from the
   server installs it into a scratch folder (Windows here, Linux in a clean Debian 12 container), which must start
   the server, serve the web client, and build and run a new C4 program with Nuke; then `latest` points at it.

Exit code 0 when every step it ran passed.
"""

import argparse
import datetime
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
CHECKOUT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import gitea  # noqa: E402
import publish  # noqa: E402

PRODUCT = "ied"
NAME = "IED"
BUILDER = "ied-linux-builder"
CHECK_IMAGE = "mirror.gcr.io/library/debian:12"
VOLUME = "ied-release-linux-build"
CONFIG = "Development"
WINDOWS_PROJECTS = ("ied-server", "ied", "ied-desktop", "flashbang")
LINUX_PROJECTS = ("ied-server", "ied", "ied-desktop")
# The programs and, beside the desktop app, its icon (the windows' and the applications menu's).
LINUX_PROGRAMS = ("ied-server/ied-server", "ied-server/ied-server.c4i", "ied-server/C4Script.so", "ied/ied",
	"ied-desktop/ied-desktop", "ied-desktop/ied.png", "ied-desktop/ied.svg")
# What the desktop app loads when it starts (desktop/linux/Shell.c: WebKitGTK 4.1, else 4.0, and the GTK 3 it brings).
LINUX_DESKTOP_LIBRARIES = ("libwebkit2gtk-4.1.so.0", "libwebkit2gtk-4.0.so.37")
LINUX_DESKTOP_PACKAGES = {
	"apt": ["libwebkit2gtk-4.1-0"],
	"dnf": ["webkit2gtk4.1"],
	"pacman": ["webkit2gtk-4.1"],
	"zypper": ["libwebkit2gtk-4_1-0"],
}
# The libraries the Linux programs load (ldd): the server's TLS and its script host's LLVM dependencies, the
# toolchain's clang and ld.lld with theirs (zlib is also Tor's); python3 runs Nuke; curl is web fetch's fallback
# (docs/29: when it does not impersonate a browser).
LINUX_PACKAGES = {
	"apt": ["ca-certificates", "python3", "libssl3", "libz3-4", "libxml2", "libzstd1", "zlib1g", "libedit2", "libffi8", "curl"],
	"dnf": ["ca-certificates", "python3", "openssl-libs", "z3-libs", "libxml2", "libzstd", "zlib", "libedit", "libffi", "curl"],
	"pacman": ["ca-certificates", "python", "openssl", "z3", "libxml2", "zstd", "zlib", "libedit", "libffi", "curl"],
	"zypper": ["ca-certificates", "python3", "libopenssl3", "libz3-4", "libxml2-2", "libzstd1", "libz1", "libedit0", "libffi8", "curl"],
}
LINUX_CHECK = ("ied-server", "ied", "C4Script.so", "toolchains/c4/dev/C4", "toolchains/c4/dev/toolchain/bin/clang", "toolchains/c4/dev/toolchain/bin/ld.lld",
	"vendor/python/bin/python3.12")
# The web search services of a Linux install (docs/29 "On Linux"): setup's linux packages, which it fetches on Windows
# into vendor/linux/, and SearXNG's own package, which serves both systems - each where a Windows install has its own:
# (source in the worktree, folder in the install, its top folders left out).
LINUX_SERVICE_PACKAGES = ("searxng", "python-linux", "searxng-packages-linux", "tor-linux")
LINUX_SERVICES = (
	("vendor/linux/python", "vendor/python", ()),
	("vendor/searxng/searx", "vendor/searxng/searx", ()),
	("vendor/linux/searxng-packages", "vendor/searxng-packages", ("bin",)),   # pip's Windows launchers
	("vendor/linux/tor", "vendor/tor", ("docs",)),
)
# What a Windows package must hold (install_ied reports what it could not find; these are the ones a release needs).
WINDOWS_REQUIRED = ("ied.exe", "ied-desktop.exe", "ied-server.exe", "ied-server.c4i", "C4Script.dll", "bin/nuke.cmd",
	"frontend/login.html", "vendor/python/python.exe", "plugins/cpp/clangd/bin/clangd.exe", "plugins/gpu/bin/flashbang.dll")


class ReleaseError(Exception):
	pass


def say(text):
	print("ied-release: %s" % text, flush=True)


def run(command, cwd=None, env=None, log=None):
	"""Runs `command`; its output goes to `log` (a file, appended) or to this console. Raises when it fails."""
	say("$ %s" % " ".join(command))
	if log:
		with open(log, "ab") as out:
			code = subprocess.run(command, cwd=cwd, env=env, stdout=out, stderr=subprocess.STDOUT).returncode
	else:
		code = subprocess.run(command, cwd=cwd, env=env).returncode
	if code != 0:
		raise ReleaseError("%s exited with %d%s" % (command[0], code, " (see %s)" % log if log else ""))


def git(*args, cwd=CHECKOUT):
	result = subprocess.run(["git"] + list(args), cwd=cwd, capture_output=True, text=True)
	if result.returncode != 0:
		raise ReleaseError("git %s: %s" % (" ".join(args), result.stderr.strip()))
	return result.stdout.strip()


def docker():
	found = shutil.which("docker")
	if found:
		return found
	desktop = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "DockerDesktop", "resources", "bin", "docker.exe")
	if os.path.isfile(desktop):
		return desktop
	raise ReleaseError("Docker is needed for the Linux build (Docker Desktop with its Linux engine)")


def docker_path(path):
	return os.path.realpath(path).replace("\\", "/")


def version_of(commit):
	stamp = int(git("log", "-1", "--format=%ct", commit))
	day = datetime.datetime.fromtimestamp(stamp, datetime.timezone.utc)
	return "%04d.%02d.%02d-%s" % (day.year, day.month, day.day, commit[:8])


# --- 1. the worktree -------------------------------------------------------------------------------------------------

def submodules(commit):
	"""[(path, gitlink commit)] of the release's submodules that are checked out (update = none ones are not)."""
	text = git("show", "%s:.gitmodules" % commit)
	found, path, skipped = [], None, False
	for line in text.splitlines() + ["[end]"]:
		line = line.strip()
		if line.startswith("["):
			if path and not skipped:
				found.append(path)
			path, skipped = None, False
		elif line.startswith("path"):
			path = line.split("=", 1)[1].strip()
		elif line.replace(" ", "") == "update=none":
			skipped = True
	return [(p, git("rev-parse", "%s:%s" % (commit, p))) for p in found]


def main_checkout():
	"""The repository's main working tree - where setup put vendor/ and the submodules are checked out - also when this
	script runs from another worktree (whose submodules are plain copies)."""
	common = git("rev-parse", "--path-format=absolute", "--git-common-dir")
	return os.path.dirname(os.path.normpath(common))


def prepare_worktree(worktree, commit):
	if not os.path.isdir(worktree):
		git("worktree", "add", "--detach", worktree, commit)
	else:
		if git("status", "--porcelain", "--untracked-files=no", cwd=worktree):
			raise ReleaseError("%s has changes; a release builds only committed work" % worktree)
		git("checkout", "-q", "--detach", commit, cwd=worktree)
	home = main_checkout()
	vendor = os.path.join(worktree, "vendor")
	if not os.path.exists(vendor):
		subprocess.run(["cmd", "/d", "/c", "mklink", "/J", vendor, os.path.join(home, "vendor")], check=True, capture_output=True)
	for path, gitlink in submodules(commit):
		source = os.path.join(home, path)
		have = git("rev-parse", "HEAD", cwd=source) if os.path.exists(os.path.join(source, ".git")) else ""
		if have != gitlink:
			raise ReleaseError("the checkout's %s is at %s, the release names %s: `git submodule update %s` first" % (path, have[:10] or "nothing", gitlink[:10], path))
		code = subprocess.run(["robocopy", source, os.path.join(worktree, path), "/MIR", "/XD", ".git", "/XF", ".git", "/NFL", "/NDL", "/NJH", "/NJS", "/NP"], capture_output=True).returncode
		if code >= 8:
			raise ReleaseError("copying the submodule %s failed (robocopy %d)" % (path, code))


# --- 2. the builds ---------------------------------------------------------------------------------------------------

def nuke_environment():
	env = dict(os.environ)
	env.pop("NUKE_TOOLCHAINS", None)   # the worktree's own store: its `dev` is the compiler built from the release
	return env


def build_windows(worktree, jobs, logs):
	log = os.path.join(logs, "windows.log")
	for projects in (("C4",), WINDOWS_PROJECTS):
		# By its whole path: with NoDefaultCurrentDirectoryInExePath set, cmd does not look in the current folder.
		run(["cmd", "/d", "/c", os.path.join(worktree, "nuke.bat"), "build"] + list(projects) + ["-c", CONFIG, "-j", str(jobs)], cwd=worktree, env=nuke_environment(), log=log)


def linux_container(worktree, extra_mounts=(), workdir="/ied"):
	return [docker(), "run", "--rm", "--platform", "linux/amd64", "-v", docker_path(worktree) + ":/ied", "-v", VOLUME + ":/ied/build"] + \
		[a for m in extra_mounts for a in ("-v", m)] + ["-w", workdir, "-e", "PYTHONDONTWRITEBYTECODE=1", "-e", "NUKE_TOOLCHAINS=/ied/build/toolchains", BUILDER]


def build_linux(worktree, jobs, logs):
	if subprocess.run([docker(), "image", "inspect", BUILDER], capture_output=True).returncode != 0:
		run([docker(), "build", "-t", BUILDER, os.path.join(worktree, "tools", "linux", "builder")])
	log = os.path.join(logs, "linux.log")
	try:
		for projects in (("C4",), LINUX_PROJECTS):
			run(linux_container(worktree) + ["python3", "Nuke/nuke", "build"] + list(projects) + ["-c", CONFIG, "-j", str(jobs), "--system", "linux"], log=log)
	finally:
		# Nuke refreshes the IED solution from where this build's store is (build/toolchains, inside the workspace):
		# a build artifact of the release worktree, never part of the release.
		git("checkout", "--", ".", cwd=worktree)


# --- 3. the packages -------------------------------------------------------------------------------------------------

def sha256_of(path):
	return publish.sha256_of(path)


def archive_entry(path, sha256=None, to=""):
	return {"file": os.path.basename(path), "to": to, "size": os.path.getsize(path), "sha256": sha256 or sha256_of(path)}


def files_under(folder):
	for root, dirs, files in os.walk(folder):
		dirs.sort()
		for name in sorted(files):
			path = os.path.join(root, name)
			yield path, os.path.relpath(path, folder).replace("\\", "/")


def write_zip(path, parts):
	"""A zip of `parts` ([(folder, prefix)]), entries in name order, so the same files make the same archive."""
	temporary = path + ".tmp"
	with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as archive:
		for folder, prefix in parts:
			for file, relative in files_under(folder):
				archive.write(file, prefix + relative)
	os.replace(temporary, path)


def is_program(path):
	with open(path, "rb") as f:
		head = f.read(4)
	return head == b"\x7fELF" or head[:2] == b"#!"


def write_tar(path, parts):
	"""A tar.gz of `parts` ([(folder, prefix)]) for Linux: programs (ELF, scripts) 0755, the rest 0644, owned by root."""
	temporary = path + ".tmp"
	with tarfile.open(temporary, "w:gz", compresslevel=6, format=tarfile.PAX_FORMAT) as archive:
		for folder, prefix in parts:
			for file, relative in files_under(folder):
				info = archive.gettarinfo(file, prefix + relative)
				info.mode = 0o755 if is_program(file) else 0o644
				info.uid = info.gid = 0
				info.uname = info.gname = ""
				with open(file, "rb") as f:
					archive.addfile(info, f)
	os.replace(temporary, path)


def install_module(worktree):
	sys.path.insert(0, os.path.join(worktree, "tools", "install"))
	import install_ied
	return install_ied


def stage_windows(worktree, stage):
	python = os.path.join(worktree, "vendor", "python", "python.exe")
	bin_root = os.path.join(worktree, "build", "bin", "%s-windows-x64" % CONFIG)
	run([python if os.path.isfile(python) else sys.executable, "-I", os.path.join(worktree, "tools", "install", "install_ied.py"),
		"--bin", bin_root, "--to", stage, "--no-register", "--no-path"])
	record = os.path.join(stage, "install.json")
	if os.path.exists(record):
		os.remove(record)   # the installer writes the target machine's own
	missing = [p for p in WINDOWS_REQUIRED if not os.path.isfile(os.path.join(stage, *p.split("/")))]
	if missing:
		raise ReleaseError("the Windows package lacks %s" % ", ".join(missing))


def stage_linux(worktree, stage, programs):
	"""The Linux install folder: the programs from the Linux build, the trees an install has (install_ied's), and the
	Linux web search services (LINUX_SERVICES), which setup first installs into the worktree's vendor/ when they are
	missing or out of date."""
	install_ied = install_module(worktree)
	report = {"copied": 0, "removed": 0, "in_use": [], "missing": []}
	os.makedirs(stage, exist_ok=True)
	for name in os.listdir(programs):
		install_ied.copy_program(os.path.join(programs, name), os.path.join(stage, name), report)
	for tree, skip in install_ied.TREES:
		install_ied.mirror(os.path.join(worktree, tree), os.path.join(stage, tree), set(skip), report, anywhere=install_ied.BUILT_IN_PLACE)
	python = os.path.join(worktree, "vendor", "python", "python.exe")
	run([python if os.path.isfile(python) else sys.executable, "-I", os.path.join(worktree, "tools", "setup", "setup.py"),
		"--only", ",".join(LINUX_SERVICE_PACKAGES), "--no-path"], cwd=worktree)
	for source, target, skip in LINUX_SERVICES:
		folder = os.path.join(worktree, *source.split("/"))
		if not os.path.isdir(folder):
			raise ReleaseError("the Linux services lack %s (setup did not install it)" % source)
		install_ied.mirror(folder, os.path.join(stage, *target.split("/")), set(skip), report, anywhere={"__pycache__"})
	say("Linux stage: %d copied, %d removed" % (report["copied"], report["removed"]))


def copy_linux_outputs(worktree, out):
	"""The Linux programs and the Linux toolchain (with the sysroot) out of the build volume, into `out`."""
	programs = os.path.join(out, "linux-programs")
	shutil.rmtree(programs, ignore_errors=True)
	os.makedirs(programs)
	sysroot = os.path.join(worktree, "vendor", "sysroots", "linux-x64")
	if not os.path.isfile(os.path.join(sysroot, "sysroot.txt")):
		raise ReleaseError("no Linux sysroot in vendor/sysroots/linux-x64 (python -I tools/setup/linux_sysroot.py makes it)")
	bin_root = "/ied/build/bin/%s-linux-x64" % CONFIG
	script = "\n".join([
		"set -e",
		"cp %s /out/linux-programs/" % " ".join("%s/%s" % (bin_root, p) for p in LINUX_PROGRAMS),
		"rm -rf /tmp/c4 && cp -a /ied/build/toolchains/c4/dev /tmp/c4",
		"if [ -n \"$(find /tmp/c4 -type l -lname '/*')\" ]; then echo 'the toolchain has links out of itself'; exit 1; fi",
		# The sysroot comes from Windows (every file executable there): headers and libraries, none a program.
		"mkdir -p /tmp/c4/sysroots && cp -r /sysroot /tmp/c4/sysroots/linux-x64",
		"find /tmp/c4/sysroots -type f -exec chmod 0644 {} + && find /tmp/c4/sysroots -type d -exec chmod 0755 {} +",
		"tar --sort=name --owner=0 --group=0 --numeric-owner -C /tmp/c4 -cf /tmp/c4.tar .",
		"gzip -n -6 -c /tmp/c4.tar > /out/c4.tar.gz",
	])
	run(linux_container(worktree, ("%s:/out" % docker_path(out), "%s:/sysroot:ro" % docker_path(sysroot))) + ["sh", "-c", script])
	return programs, os.path.join(out, "c4.tar.gz")


def release_record(version, commit, windows, linux):
	platforms = {}
	if windows:
		platforms["windows-x64"] = {
			"folder": "%LOCALAPPDATA%\\Programs\\IED",
			"archives": windows,
			"keep": ["toolchains", ".cache"],
			"path": ["", "bin"],
			"register": [["ied-desktop.exe", "--register"]],
			"unregister": [["ied-desktop.exe", "--unregister"]],
		}
	if linux:
		platforms["linux-x64"] = {
			"folder": "~/.local/opt/ied",
			"archives": linux,
			"keep": ["toolchains", ".cache"],
			"path": ["", "bin"],
			"check": list(LINUX_CHECK),
			"packages": LINUX_PACKAGES,
			"glibc": "2.36",
			# The desktop app (docs/27 "Linux"): on a machine with a desktop, its WebKitGTK and the applications menu's entry.
			"register": [["ied-desktop", "--register"]],
			"unregister": [["ied-desktop", "--unregister"]],
			"desktopLibraries": [list(LINUX_DESKTOP_LIBRARIES)],
			"desktopPackages": LINUX_DESKTOP_PACKAGES,
			"hint": "IED is in your applications menu; in a terminal `ied` opens this folder in it (over SSH: `ied url --no-browser` prints a sign-in link)",
		}
	return {"product": PRODUCT, "name": NAME, "version": version, "commit": commit,
		"built": datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat(), "platforms": platforms}


def package(worktree, out, version, commit, platforms):
	folder = os.path.join(out, version)
	os.makedirs(folder, exist_ok=True)
	windows = linux = None
	if "windows" in platforms:
		stage = os.path.join(out, "stage", "windows")
		stage_windows(worktree, stage)
		app = os.path.join(folder, "ied-%s-windows-x64.zip" % version)
		say("packing %s" % os.path.basename(app))
		write_zip(app, [(stage, "")])
		c4 = os.path.join(folder, "c4-%s-windows-x64.zip" % version)
		say("packing %s" % os.path.basename(c4))
		write_zip(c4, [(os.path.join(worktree, "toolchains", "c4", "dev"), ""), (os.path.join(worktree, "vendor", "sysroots", "linux-x64"), "sysroots/linux-x64/")])
		msvc = os.path.join(folder, "msvc-%s-windows-x64.zip" % version)
		say("packing %s" % os.path.basename(msvc))
		write_zip(msvc, [(os.path.join(worktree, "vendor", "msvc"), "")])
		windows = [archive_entry(app), archive_entry(c4, to="toolchains/c4/dev"), archive_entry(msvc, to="vendor/msvc")]
	if "linux" in platforms:
		programs, c4_tar = copy_linux_outputs(worktree, out)
		stage = os.path.join(out, "stage", "linux")
		stage_linux(worktree, stage, programs)
		app = os.path.join(folder, "ied-%s-linux-x64.tar.gz" % version)
		say("packing %s" % os.path.basename(app))
		write_tar(app, [(stage, "")])
		c4 = os.path.join(folder, "c4-%s-linux-x64.tar.gz" % version)
		os.replace(c4_tar, c4)
		linux = [archive_entry(app), archive_entry(c4, to="toolchains/c4/dev")]
	release = release_record(version, commit, windows, linux)
	with open(os.path.join(folder, publish.RELEASE), "w", encoding="utf-8", newline="\n") as f:
		json.dump(release, f, indent="\t")
		f.write("\n")
	publish.read_release(folder)   # checks it as the publisher will
	return folder


# --- 4. the checks: the published release installs, starts, serves and builds a C4 program ----------------------------

def free_name():
	return "ied-release-check-%d" % os.getpid()


def check_windows(client, version, scratch):
	"""Installs the published version with its installer into a scratch folder (no PATH, no Start menu) and runs it."""
	folder = os.path.join(scratch, "IED")
	installer = os.path.join(scratch, "install.ps1")
	os.makedirs(scratch, exist_ok=True)
	with open(installer, "wb") as f:
		f.write(publish.render_installer("install.ps1", client.server, client.owner, PRODUCT))
	env = dict(os.environ, DIST_VERSION=version, DIST_DIR=folder, DIST_NO_PATH="1", DIST_NO_REGISTER="1")
	env.pop("NUKE_TOOLCHAINS", None)
	run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", installer], env=env)
	with open(os.path.join(folder, "install.json"), encoding="utf-8") as f:
		if json.load(f).get("version") != version:
			raise ReleaseError("the Windows install's install.json does not name %s" % version)
	control = free_name()
	data = os.path.join(scratch, "data")
	server = subprocess.Popen([os.path.join(folder, "ied-server.exe"), "--control", control, "--port", "0", "--data", data,
		"--app-data", os.path.join(scratch, "app"), "--static", os.path.join(folder, "frontend")], stdout=subprocess.PIPE, text=True)
	try:
		url = None
		deadline = time.time() + 60
		while time.time() < deadline and url is None:
			line = server.stdout.readline()
			if not line:
				break
			if line.startswith("LISTENING"):
				url = line.split(None, 1)[1].strip()
		if not url:
			raise ReleaseError("the installed Windows server never said LISTENING")
		with urllib.request.urlopen(url.rstrip("/") + "/login.html", timeout=30) as response:
			if response.status != 200:
				raise ReleaseError("the installed Windows server answered login.html with %d" % response.status)
		say("Windows: the installed server serves the web client at %s" % url)
	finally:
		subprocess.run([os.path.join(folder, "ied.exe"), "stop", "--control", control], capture_output=True, timeout=60)
		try:
			server.wait(30)
		except subprocess.TimeoutExpired:
			server.kill()
	project = os.path.join(scratch, "hello")
	os.makedirs(project)
	nuke = [os.path.join(folder, "bin", "nuke.cmd")]
	run(["cmd", "/d", "/c"] + nuke + ["init", "hello"], cwd=project, env=env)
	run(["cmd", "/d", "/c"] + nuke + ["build"], cwd=project, env=env)
	program = os.path.join(project, "build", "bin", "%s-windows-x64" % CONFIG, "hello", "hello.exe")
	output = subprocess.run([program, "one"], capture_output=True, text=True, timeout=60).stdout
	if "1 argument" not in output:
		raise ReleaseError("the C4 program the Windows install built printed %r" % output)
	say("Windows: Nuke and the installed toolchain built and ran a new C4 program")


LINUX_CHECK_SCRIPT = r"""
set -eu
apt-get update -qq >/dev/null && apt-get install -y -qq --no-install-recommends curl ca-certificates >/dev/null
DIST_VERSION="$VERSION" sh /dist/install.sh
. "$HOME/.local/opt/ied/env"
ied-server --help >/dev/null 2>&1 || true
url="$(ied url --no-browser)"
case "$url" in http*) ;; *) echo "ied url printed: $url"; exit 1 ;; esac
base="$(printf '%s' "$url" | sed -E 's#^(https?://[^/]+).*#\1#')"
curl -fsS -o /dev/null "$base/login.html"
ied status | head -3
ied stop
mkdir /tmp/hello && cd /tmp/hello
nuke init hello
nuke build
./build/bin/Development-linux-x64/hello/hello one | grep -q "1 argument"
echo "LINUX-CHECK-PASSED"
"""


def check_linux(client, version, scratch):
	"""Installs the published version with its installer in a clean Debian 12 container (as root, so it installs the
	system packages itself), then signs in through `ied url`, fetches the web client, and builds and runs a C4 program."""
	dist = os.path.join(scratch, "dist")
	os.makedirs(dist, exist_ok=True)
	with open(os.path.join(dist, "install.sh"), "wb") as f:
		f.write(publish.render_installer("install.sh", client.server, client.owner, PRODUCT))
	with open(os.path.join(dist, "check.sh"), "w", encoding="utf-8", newline="\n") as f:
		f.write(LINUX_CHECK_SCRIPT)
	result = subprocess.run([docker(), "run", "--rm", "--platform", "linux/amd64", "-e", "VERSION=" + version, "-v", docker_path(dist) + ":/dist:ro",
		CHECK_IMAGE, "sh", "/dist/check.sh"], capture_output=True, text=True)
	sys.stdout.write(result.stdout[-4000:])
	sys.stderr.write(result.stderr[-4000:])
	if result.returncode != 0 or "LINUX-CHECK-PASSED" not in result.stdout:
		raise ReleaseError("the Linux install check failed (exit %d)" % result.returncode)
	say("Linux: installed in Debian 12, served the web client, built and ran a C4 program")


DESKTOP_CHECK_IMAGE = "ied-dist-desktop-check"
LINUX_DESKTOP_CHECK_SCRIPT = r"""
set -eu
Xvfb :99 -screen 0 1600x1000x24 >/tmp/xvfb.log 2>&1 &
export DISPLAY=:99
sleep 2
eval "$(dbus-launch --sh-syntax)"
DIST_VERSION="$VERSION" sh /dist/install.sh
desktop-file-validate "$HOME/.local/share/applications/ied.desktop"
test "$(xdg-mime query default application/x-ied-solution)" = ied.desktop
. "$HOME/.local/opt/ied/env"
# WebKit's sandbox needs user namespaces, which a container does not give.
export WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1
ied-desktop >/tmp/desktop.out 2>&1 &
timeout 90 sh -c 'until xdotool search --onlyvisible --name "IED" >/dev/null 2>&1; do sleep 1; done'
sleep 15
import -window root /out/desktop.png
ied status | grep -q '"clients":1' || { ied status; cat /tmp/desktop.out; exit 1; }
echo "LINUX-DESKTOP-CHECK-PASSED"
"""


def check_linux_desktop(client, version, scratch, logs):
	"""Installs the published version in a clean Debian 12 with a virtual display: the installer must bring WebKitGTK
	and put IED in the applications menu (an entry desktop-file-validate passes, IED's files opening in it), and the
	desktop app must open a window showing IED, signed in to the server it started. Its screenshot goes beside the logs."""
	dist = os.path.join(scratch, "dist")
	os.makedirs(dist, exist_ok=True)
	with open(os.path.join(dist, "install.sh"), "wb") as f:
		f.write(publish.render_installer("install.sh", client.server, client.owner, PRODUCT))
	with open(os.path.join(dist, "check.sh"), "w", encoding="utf-8", newline="\n") as f:
		f.write(LINUX_DESKTOP_CHECK_SCRIPT)
	run([docker(), "build", "-q", "-t", DESKTOP_CHECK_IMAGE, os.path.join(HERE, "desktop-check")])
	result = subprocess.run([docker(), "run", "--rm", "--platform", "linux/amd64", "-e", "VERSION=" + version, "-v", docker_path(dist) + ":/dist:ro",
		"-v", docker_path(logs) + ":/out", DESKTOP_CHECK_IMAGE, "sh", "/dist/check.sh"], capture_output=True, text=True)
	sys.stdout.write(result.stdout[-4000:])
	sys.stderr.write(result.stderr[-4000:])
	if result.returncode != 0 or "LINUX-DESKTOP-CHECK-PASSED" not in result.stdout:
		raise ReleaseError("the Linux desktop check failed (exit %d)" % result.returncode)
	say("Linux: the desktop app opened IED from the applications menu's install (screenshot: %s)" % os.path.join(logs, "desktop.png"))


# --- the command -------------------------------------------------------------------------------------------------------

def main(argv):
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--commit", default="HEAD", help="what to release (default HEAD of this checkout)")
	parser.add_argument("--worktree", default=os.path.join(os.path.dirname(CHECKOUT), "IED-release"), help="the release worktree")
	parser.add_argument("--only", choices=("windows", "linux"), help="build and publish one platform")
	parser.add_argument("--skip-build", action="store_true", help="package what the worktree built last (at the same commit)")
	parser.add_argument("--skip-package", action="store_true", help="publish and check the release folder made last for this commit")
	parser.add_argument("--no-publish", action="store_true", help="build and package only")
	parser.add_argument("--no-check", action="store_true", help="publish without the install checks (and make it latest)")
	parser.add_argument("--no-latest", action="store_true", help="publish and check, but leave latest where it is")
	parser.add_argument("--keep", type=int, default=3, help="published versions kept (default 3)")
	parser.add_argument("-j", "--jobs", type=int, default=3, help="parallel compiles (default 3)")
	parser.add_argument("--linux-volume", default=VOLUME, help="the Docker volume of the Linux build (default %s)" % VOLUME)
	args = parser.parse_args(argv)
	platforms = (args.only,) if args.only else ("windows", "linux")
	globals()["VOLUME"] = args.linux_volume
	try:
		client = None
		if not args.no_publish:
			config = gitea.load_config(CHECKOUT)
			client = gitea.Gitea(config["server"], config["owner"])
		commit = git("rev-parse", args.commit + "^{commit}")
		version = version_of(commit)
		worktree = os.path.abspath(args.worktree)
		out = os.path.join(CHECKOUT, "build", "release")
		logs = os.path.join(out, version)
		os.makedirs(logs, exist_ok=True)
		say("IED %s from %s in %s" % (version, commit[:10], worktree))
		if args.skip_package:
			folder = os.path.join(out, version)
			publish.read_release(folder)
		else:
			if not args.skip_build:
				prepare_worktree(worktree, commit)
				if "windows" in platforms:
					build_windows(worktree, args.jobs, logs)
				if "linux" in platforms:
					build_linux(worktree, args.jobs, logs)
			elif git("rev-parse", "HEAD", cwd=worktree) != commit:
				raise ReleaseError("--skip-build: the worktree is not at %s" % commit[:10])
			folder = package(worktree, out, version, commit, platforms)
		say("packaged in %s" % folder)
		if args.no_publish:
			return 0
		publish.publish(client, folder, config["repo"], keep=0, latest=False, say=say)
		if not args.no_check:
			scratch = tempfile.mkdtemp(prefix="ied-release-check-")
			if "windows" in platforms:
				check_windows(client, version, os.path.join(scratch, "windows"))
			if "linux" in platforms:
				check_linux(client, version, os.path.join(scratch, "linux"))
				check_linux_desktop(client, version, os.path.join(scratch, "linux-desktop"), logs)
			shutil.rmtree(scratch, ignore_errors=True)
		if not args.no_latest:
			publish.promote(client, PRODUCT, version, say=say)
			publish.prune(client, PRODUCT, args.keep, protect={version}, say=say)
		for system, line in publish.install_commands(client.server, client.owner, PRODUCT).items():
			print("%s: %s" % (system, line))
	except (ReleaseError, gitea.GiteaError, publish.PublishError, OSError, subprocess.SubprocessError) as error:
		print("ied-release: %s" % error, file=sys.stderr)
		return 1
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
