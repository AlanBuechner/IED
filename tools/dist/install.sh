#!/bin/sh
# @DIST_PRODUCT@ for Linux (x64): installs or updates the newest build published at @DIST_SERVER@ - one command, no
# root needed (system packages it lacks are installed with sudo, asked first by sudo itself):
#
#   curl -fsSL @DIST_SERVER@/api/packages/@DIST_OWNER@/generic/@DIST_PRODUCT@/latest/install.sh | sh
#
# Made by tools/dist/publish.py from tools/dist/install.sh (IED docs/21 "Prebuilt releases on the owner's Gitea").
# It reads the release's release.json, downloads the archives this machine does not have yet (each checked against its
# SHA-256), puts them into the install folder (only what changed; a running program keeps running until restarted),
# installs the system packages the programs need when ldd finds a library missing, puts the folder's programs on the
# PATH (an `env` file the shell's startup files source) and writes install.json last. Run it again to update.
#
# Set before the command (`curl ... | DIST_VERSION=... sh`):
#   DIST_VERSION      install that published version instead of the newest
#   DIST_DIR          the install folder (default: the release's, ~/.local/opt/<product>)
#   DIST_NO_PATH      1: leave the shell startup files alone
#   DIST_NO_PACKAGES  1: never install system packages (only say which are missing)
#   DIST_FORCE        1: replace an install that something else made in that folder
#   DIST_UNINSTALL    1: remove it (the folder and the startup files' line)
set -eu

DIST_SERVER="${DIST_SERVER:-@DIST_SERVER@}"
DIST_OWNER="@DIST_OWNER@"
DIST_PRODUCT="@DIST_PRODUCT@"
export DIST_SERVER DIST_OWNER DIST_PRODUCT

say() { printf '[%s] %s\n' "$DIST_PRODUCT" "$*" >&2; }

as_root() {
	if [ "$(id -u)" = 0 ]; then "$@"; elif command -v sudo >/dev/null 2>&1; then sudo "$@"; else
		say "need root for: $*  (no sudo here)"; return 1; fi
}

# Python 3 runs the installer (and Nuke, which comes with the install).
if ! command -v python3 >/dev/null 2>&1; then
	case "${DIST_NO_PACKAGES:-0}" in 0|"") ;; *) say "python3 is needed; install it and run this again"; exit 1 ;; esac
	say "installing python3"
	if command -v apt-get >/dev/null 2>&1; then
		as_root apt-get update -qq && as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends python3
	elif command -v dnf >/dev/null 2>&1; then as_root dnf install -y python3
	elif command -v pacman >/dev/null 2>&1; then as_root pacman -S --noconfirm --needed python
	elif command -v zypper >/dev/null 2>&1; then as_root zypper --non-interactive install python3
	else say "python3 is needed and no known package manager is here; install it and run this again"; exit 1; fi
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/dist-$DIST_PRODUCT.XXXXXX")"
trap 'rm -rf "$work"' EXIT INT TERM
cat > "$work/install.py" <<'PYTHON'
import hashlib, json, os, platform, re, shutil, stat, subprocess, sys, tarfile, time, urllib.request

SERVER, OWNER, PRODUCT = os.environ["DIST_SERVER"].rstrip("/"), os.environ["DIST_OWNER"], os.environ["DIST_PRODUCT"]
PLATFORM = "linux-x64"
BASE = "%s/api/packages/%s/generic/%s" % (SERVER, OWNER, PRODUCT)
MARKER = ".dist-installing"
RECORD = "install.json"
RC_MARK = "# added by the %s installer" % PRODUCT


def say(text):
	print("[%s] %s" % (PRODUCT, text), file=sys.stderr, flush=True)


def fail(text):
	say("error: " + text)
	sys.exit(1)


def flag(name):
	return os.environ.get(name, "") not in ("", "0")


def fetch(url, path=None, tries=3):
	for attempt in range(tries):
		try:
			with urllib.request.urlopen(url, timeout=120) as response:
				if path is None:
					return response.read()
				with open(path, "wb") as out:
					shutil.copyfileobj(response, out, 1 << 20)
				return None
		except Exception as error:
			if attempt == tries - 1 or getattr(error, "code", 0) == 404:
				raise
			time.sleep(2 * (attempt + 1))


def sha256_of(path):
	digest = hashlib.sha256()
	with open(path, "rb") as f:
		for block in iter(lambda: f.read(1 << 20), b""):
			digest.update(block)
	return digest.hexdigest()


def inside(folder, relative):
	return os.path.join(folder, *relative.split("/")) if relative else folder


def key_of(to):
	return to or "."


def under(path, folder):
	return os.path.normpath(path).startswith(os.path.normpath(folder).rstrip(os.sep) + os.sep)


def read_record(folder):
	try:
		with open(os.path.join(folder, RECORD), encoding="utf-8") as f:
			return json.load(f)
	except (OSError, ValueError):
		return None


def write_text(path, text, mode=0o644):
	temporary = path + ".tmp"
	with open(temporary, "w", encoding="utf-8") as f:
		f.write(text)
	os.chmod(temporary, mode)
	os.replace(temporary, path)


def version_tuple(text):
	return tuple(int(n) for n in re.findall(r"\d+", text)[:3])


def extract(archive, folder):
	with tarfile.open(archive) as tar:
		for member in tar.getmembers():
			name = member.name
			if name.startswith("/") or ".." in name.split("/") or (member.islnk() and (member.linkname.startswith("/") or ".." in member.linkname.split("/"))):
				fail("%s holds an unsafe path: %s" % (os.path.basename(archive), name))
		if hasattr(tarfile, "data_filter"):
			tar.extractall(folder, filter="tar")
		else:
			tar.extractall(folder)


def mirror(source, target, keep):
	"""`target` made like `source`: each file moved into place by a rename (a running program keeps its old file), then
	what `source` did not have removed - except under `keep` (whole paths) and the install's own files."""
	target = os.path.normpath(target)
	keep = [os.path.normpath(k) for k in keep]
	wanted = set()
	for folder, dirs, files in os.walk(source):
		relative = os.path.relpath(folder, source)
		destination_folder = target if relative == "." else os.path.join(target, relative)
		if os.path.islink(destination_folder) or (os.path.exists(destination_folder) and not os.path.isdir(destination_folder)):
			os.remove(destination_folder)
		os.makedirs(destination_folder, exist_ok=True)
		for name in files + [d for d in dirs if os.path.islink(os.path.join(folder, d))]:
			path = os.path.join(destination_folder, name)
			wanted.add(path)
			if os.path.isdir(path) and not os.path.islink(path):
				shutil.rmtree(path)
			os.replace(os.path.join(folder, name), path)
		dirs[:] = [d for d in dirs if not os.path.islink(os.path.join(folder, d))]
		wanted.add(destination_folder)
	for folder, dirs, files in os.walk(target, topdown=False):
		if any(folder == k or under(folder, k) for k in keep):
			continue
		for name in files + [d for d in dirs if os.path.islink(os.path.join(folder, d))]:
			path = os.path.join(folder, name)
			if path in wanted or any(path == k for k in keep) or (folder == target and name in (MARKER, RECORD)):
				continue
			os.remove(path)
		if folder != target and folder not in wanted and not os.listdir(folder):
			os.rmdir(folder)


def missing_libraries(folder, programs):
	"""{program: [library, ...]} of what ldd cannot find."""
	missing = {}
	for relative in programs:
		path = inside(folder, relative)
		if not os.path.isfile(path):
			continue
		try:
			output = subprocess.run(["ldd", path], capture_output=True, text=True, timeout=60).stdout
		except (OSError, subprocess.TimeoutExpired):
			return {}
		lost = sorted(set(line.split("=>")[0].strip() for line in output.splitlines() if "not found" in line))
		if lost:
			missing[relative] = lost
	return missing


def package_manager():
	for name in ("apt-get", "dnf", "pacman", "zypper"):
		if shutil.which(name):
			return name
	return None


def as_root(command):
	if os.geteuid() == 0:
		return subprocess.run(command).returncode == 0
	if not shutil.which("sudo"):
		return False
	return subprocess.run(["sudo"] + command).returncode == 0


def install_packages(packages):
	manager = package_manager()
	key = {"apt-get": "apt", "dnf": "dnf", "pacman": "pacman", "zypper": "zypper"}.get(manager)
	names = (packages or {}).get(key) or []
	if not manager or not names:
		return False
	say("installing system packages: %s (%s)" % (" ".join(names), manager))
	if manager == "apt-get":
		as_root(["apt-get", "update", "-qq"])
		# env, not sudo's environment: sudo drops the caller's variables.
		return as_root(["env", "DEBIAN_FRONTEND=noninteractive", "apt-get", "install", "-y", "-qq", "--no-install-recommends"] + names)
	if manager == "dnf":
		return as_root(["dnf", "install", "-y"] + names)
	if manager == "pacman":
		return as_root(["pacman", "-S", "--noconfirm", "--needed"] + names)
	return as_root(["zypper", "--non-interactive", "install"] + names)


def startup_files():
	home = os.path.expanduser("~")
	files = [os.path.join(home, ".profile"), os.path.join(home, ".bashrc")]
	for name in (".bash_profile", ".bash_login", ".zshenv", ".zshrc"):
		if os.path.exists(os.path.join(home, name)):
			files.append(os.path.join(home, name))
	if os.path.basename(os.environ.get("SHELL", "")) == "zsh" and os.path.join(home, ".zshenv") not in files:
		files.append(os.path.join(home, ".zshenv"))
	return files


def change_startup(env_file, add):
	line = '. "%s" %s' % (env_file, RC_MARK)
	changed = []
	for path in startup_files():
		try:
			with open(path, encoding="utf-8") as f:
				lines = f.read().splitlines()
		except FileNotFoundError:
			lines = []
		kept = [l for l in lines if not l.endswith(RC_MARK)]
		new = kept + [line] if add else kept
		if new != lines and (add or os.path.exists(path)):
			write_text(path, "\n".join(new) + "\n", stat.S_IMODE(os.stat(path).st_mode) if os.path.exists(path) else 0o644)
			changed.append(path)
	return changed


def env_text(folders):
	parts = []
	for folder in reversed(folders):
		parts.append('case ":${PATH}:" in *":%s:"*) ;; *) PATH="%s:${PATH}" ;; esac' % (folder, folder))
	return "# %s's programs on the PATH (written by its installer; sourced by the shell's startup files)\n%s\nexport PATH\n" % (PRODUCT, "\n".join(parts))


def running_from(folder):
	names = set()
	for pid in os.listdir("/proc"):
		if pid.isdigit():
			try:
				exe = os.readlink("/proc/%s/exe" % pid)
			except OSError:
				continue
			if under(exe.replace(" (deleted)", ""), folder):
				names.add(os.path.basename(exe.replace(" (deleted)", "")))
	return sorted(names)


def main():
	if platform.machine() not in ("x86_64", "AMD64", "amd64"):
		fail("this build is for x86-64 Linux; this machine is %s" % platform.machine())
	version = os.environ.get("DIST_VERSION") or "latest"
	try:
		release = json.loads(fetch("%s/%s/release.json" % (BASE, version)))
	except Exception as error:
		fail("no release '%s' of %s at %s: %s" % (version, PRODUCT, SERVER, error))
	target = release.get("platforms", {}).get(PLATFORM)
	if not target:
		fail("%s %s has no %s build" % (PRODUCT, release.get("version"), PLATFORM))
	name = release.get("name") or PRODUCT
	folder = os.path.abspath(os.path.expanduser(os.path.expandvars(os.environ.get("DIST_DIR") or target["folder"])))
	path_folders = [inside(folder, p) for p in target.get("path", [])]
	env_file = os.path.join(folder, "env")
	current = read_record(folder)

	if os.path.isdir(folder) and os.listdir(folder):
		ours = (current and current.get("dist")) or os.path.exists(os.path.join(folder, MARKER))
		if not ours and not flag("DIST_FORCE"):
			fail("%s holds something this installer did not make; DIST_FORCE=1 replaces it, DIST_DIR=<folder> installs elsewhere" % folder)

	if flag("DIST_UNINSTALL"):
		if not os.path.isdir(folder):
			say("%s is not installed in %s" % (name, folder))
			return
		running = running_from(folder)
		if running:
			fail("%s is running from %s (%s); stop it (ied stop) and run this again" % (name, folder, ", ".join(running)))
		for path in change_startup(env_file, add=False):
			say("took the PATH line out of %s" % path)
		shutil.rmtree(folder)
		say("removed %s from %s" % (name, folder))
		return

	least = target.get("glibc")
	if least:
		have = (os.confstr("CS_GNU_LIBC_VERSION") or "") if hasattr(os, "confstr") else ""
		if not have.startswith("glibc ") or version_tuple(have) < version_tuple(least):
			fail("%s %s needs glibc %s or newer (Debian 12, Ubuntu 24.04 and later); this machine has %s" % (name, release["version"], least, have or "no glibc"))

	say("%s %s (commit %s) into %s" % (name, release["version"], (release.get("commit") or "")[:10], folder))
	installed = dict(((current or {}).get("dist") or {}).get("archives") or {})
	archives = target["archives"]
	work = os.environ["DIST_WORK"]
	os.makedirs(folder, exist_ok=True)
	open(os.path.join(folder, MARKER), "w").close()

	wanted = []
	for archive in archives:
		destination = inside(folder, archive["to"])
		if installed.get(key_of(archive["to"])) == archive["sha256"] and os.path.isdir(destination):
			say("%s: already installed" % archive["file"])
			continue
		path = os.path.join(work, archive["file"])
		say("downloading %s (%.0f MB)" % (archive["file"], archive["size"] / 1048576))
		fetch("%s/%s/%s" % (BASE, release["version"], archive["file"]), path)
		if sha256_of(path) != archive["sha256"]:
			fail("%s does not match its SHA-256 in release.json; nothing was changed" % archive["file"])
		wanted.append(archive)

	others = [inside(folder, a["to"]) for a in archives]

	def keep_of(archive):
		destination = inside(folder, archive["to"])
		keep = [o for o in others if o != destination and under(o, destination)]
		if not archive["to"]:
			keep += [inside(folder, k) for k in target.get("keep", []) if k]
			keep += [env_file]
		return keep

	staging = folder.rstrip("/") + ".staging"
	shutil.rmtree(staging, ignore_errors=True)
	try:
		os.remove(os.path.join(folder, RECORD))
	except FileNotFoundError:
		pass
	for index, archive in enumerate(wanted):
		unpacked = os.path.join(staging, str(index))
		os.makedirs(unpacked)
		say("installing %s" % archive["file"])
		extract(os.path.join(work, archive["file"]), unpacked)
		os.remove(os.path.join(work, archive["file"]))
		mirror(unpacked, inside(folder, archive["to"]), keep_of(archive))
		installed[key_of(archive["to"])] = archive["sha256"]
	shutil.rmtree(staging, ignore_errors=True)

	if path_folders:
		write_text(env_file, env_text(path_folders))
		update = os.path.join(path_folders[0], "%s-update" % PRODUCT)
		here = 'DIST_DIR="%s" ' % folder if os.environ.get("DIST_DIR") else ""   # an install elsewhere updates itself
		write_text(update, "#!/bin/sh\n# Updates %s from %s (written by its installer).\ncurl -fsSL %s/latest/install.sh | %ssh\n" % (name, SERVER, BASE, here), 0o755)

	checks = target.get("check", [])
	missing = missing_libraries(folder, checks)
	if missing and not flag("DIST_NO_PACKAGES") and install_packages(target.get("packages")):
		missing = missing_libraries(folder, checks)
	for program, libraries in sorted(missing.items()):
		say("warning: %s cannot find %s; install them with your package manager" % (program, ", ".join(libraries)))

	if path_folders and not flag("DIST_NO_PATH"):
		for path in change_startup(env_file, add=True):
			say("PATH line added to %s" % path)

	record = {"product": PRODUCT, "name": name, "version": release["version"], "commit": release.get("commit"),
		"built": release.get("built"), "installedMs": int(time.time() * 1000),
		"dist": {"server": SERVER, "owner": OWNER, "platform": PLATFORM,
			"archives": dict((key_of(a["to"]), installed[key_of(a["to"])]) for a in archives if key_of(a["to"]) in installed)}}
	write_text(os.path.join(folder, RECORD), json.dumps(record, indent="\t") + "\n")
	try:
		os.remove(os.path.join(folder, MARKER))
	except FileNotFoundError:
		pass
	say("installed %s %s in %s" % (name, release["version"], folder))
	running = running_from(folder)
	if running and wanted:
		say("%s was running (%s): restart it to use this version (ied stop, then ied)" % (name, ", ".join(running)))
	if path_folders:
		say('open a new terminal (or run: . "%s"), then: %s' % (env_file, target.get("hint") or PRODUCT))
		say("update later with: %s-update" % PRODUCT)


main()
PYTHON
DIST_WORK="$work" python3 "$work/install.py"
