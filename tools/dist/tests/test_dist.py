"""Checks of tools/dist (docs/21 "Prebuilt releases on the owner's Gitea"): the publisher against a fake Gitea, the
archives' layout, the Linux installer's Python, and on Windows the PowerShell installer end to end - install, update,
uninstall - from a fake server.

	python -I -m unittest discover -s tools/dist/tests
"""

import hashlib
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading
import unittest
import urllib.parse
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.dirname(HERE)
sys.path.insert(0, DIST)

import gitea  # noqa: E402
import ied_release  # noqa: E402
import publish  # noqa: E402

OWNER = "owner"


class FakeGitea(http.server.ThreadingHTTPServer):
	"""Gitea's generic package registry as tools/dist uses it, in memory."""

	def __init__(self):
		super().__init__(("127.0.0.1", 0), Handler)
		self.files = {}       # (package, version, name) -> bytes
		self.created = {}     # (package, version) -> order of creation
		self.links = []
		self.log = []
		self.lock = threading.Lock()

	@property
	def url(self):
		return "http://127.0.0.1:%d" % self.server_address[1]


class Handler(http.server.BaseHTTPRequestHandler):
	def log_message(self, *args):
		pass

	def reply(self, status, body=b"", kind="application/octet-stream"):
		self.send_response(status)
		self.send_header("Content-Type", kind)
		self.send_header("Content-Length", str(len(body)))
		self.end_headers()
		self.wfile.write(body)

	def parts(self):
		path = urllib.parse.urlsplit(self.path).path
		return [urllib.parse.unquote(p) for p in path.strip("/").split("/")]

	def do_PUT(self):
		parts = self.parts()   # api packages owner generic package version name
		data = self.rfile.read(int(self.headers["Content-Length"]))
		server = self.server
		with server.lock:
			key = (parts[4], parts[5], parts[6])
			server.log.append(("PUT", key))
			if key in server.files:
				return self.reply(409)
			server.files[key] = data
			server.created.setdefault(key[:2], len(server.created))
		self.reply(201)

	def do_DELETE(self):
		parts = self.parts()
		server = self.server
		with server.lock:
			if len(parts) == 7:
				key = (parts[4], parts[5], parts[6])
				server.log.append(("DELETE", key))
				return self.reply(204 if server.files.pop(key, None) is not None else 404)
			version = (parts[4], parts[5])
			server.log.append(("DELETE", version))
			for key in [k for k in server.files if k[:2] == version]:
				del server.files[key]
			server.created.pop(version, None)
		self.reply(204)

	def do_POST(self):
		parts = self.parts()   # api v1 packages owner generic package - link repo
		self.server.links.append((parts[5], parts[8]))
		self.reply(201)

	def do_GET(self):
		parts = self.parts()
		server = self.server
		with server.lock:
			if parts[:2] == ["api", "packages"]:
				data = server.files.get((parts[4], parts[5], parts[6]))
				return self.reply(200, data) if data is not None else self.reply(404)
			if parts[-1] == "files":
				version = (parts[5], parts[6])
				files = [{"name": k[2], "sha256": hashlib.sha256(v).hexdigest()} for k, v in server.files.items() if k[:2] == version]
				return self.reply(200 if files else 404, json.dumps(files).encode(), "application/json")
			query = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
			wanted = query.get("q", [""])[0]
			found = [{"name": p, "version": v, "created_at": "2026-01-01T00:00:%02dZ" % order} for (p, v), order in server.created.items() if wanted in p]
			return self.reply(200, json.dumps(found).encode(), "application/json")


def serve():
	server = FakeGitea()
	threading.Thread(target=server.serve_forever, daemon=True).start()
	return server


def make_release(folder, version, archives, product="demo"):
	"""A release folder: `archives` is {file: (to, {name: bytes})}, zipped."""
	os.makedirs(folder, exist_ok=True)
	entries = []
	for name, (to, files) in archives.items():
		path = os.path.join(folder, name)
		with zipfile.ZipFile(path, "w") as z:
			for inner, data in sorted(files.items()):
				# A time from the content, as a build gives a changed file a new one: the same files make the same
				# archive, and a changed file looks changed to robocopy (size and time).
				h = hashlib.sha256(data).digest()
				z.writestr(zipfile.ZipInfo(inner, date_time=(2026, 1, 1 + h[0] % 28, h[1] % 24, h[2] % 60, (h[3] % 30) * 2)), data)
		entries.append(ied_release.archive_entry(path, to=to))
	release = {"product": product, "name": "Demo", "version": version, "commit": "c" * 40, "built": "2026-01-01T00:00:00+00:00",
		"platforms": {"windows-x64": {"folder": "%LOCALAPPDATA%\\Programs\\Demo", "archives": entries, "keep": ["toolchains"], "path": ["", "bin"]}}}
	with open(os.path.join(folder, "release.json"), "w", encoding="utf-8") as f:
		json.dump(release, f)
	return release


class PublishTests(unittest.TestCase):
	def setUp(self):
		self.server = serve()
		self.client = gitea.Gitea(self.server.url, OWNER, auth="token test")
		self.folder = tempfile.mkdtemp()

	def tearDown(self):
		self.server.shutdown()
		shutil.rmtree(self.folder, ignore_errors=True)

	def publish(self, version, files, **options):
		folder = os.path.join(self.folder, version)
		make_release(folder, version, {"demo-%s.zip" % version: ("", files), "kit.zip": ("kit", {"k.lib": b"kit"})})
		return publish.publish(self.client, folder, repo="Demo", say=lambda text: None, **options)

	def test_a_release_uploads_its_archives_then_release_json_and_points_latest_at_it(self):
		self.publish("1.0.0", {"a.txt": b"one"})
		files = self.server.files
		self.assertIn(("demo", "1.0.0", "demo-1.0.0.zip"), files)
		self.assertIn(("demo", "1.0.0", "SHA256SUMS"), files)
		self.assertEqual(json.loads(files[("demo", "latest", "release.json")])["version"], "1.0.0")
		puts = [key for method, key in self.server.log if method == "PUT" and key[1] == "1.0.0"]
		self.assertEqual(puts[-1][2], "release.json", "release.json comes last: its presence means the version is whole")
		self.assertEqual(self.server.links, [("demo", "Demo")])
		for name in ("install.ps1", "install.sh"):
			text = files[("demo", "latest", name)].decode()
			self.assertIn(self.server.url, text)
			self.assertNotIn("@DIST_", text)

	def test_an_archive_already_published_is_not_uploaded_again(self):
		self.publish("1.0.0", {"a.txt": b"one"})
		self.server.log.clear()
		self.publish("1.0.0", {"a.txt": b"one"})
		uploaded = [key[2] for method, key in self.server.log if method == "PUT"]
		self.assertNotIn("demo-1.0.0.zip", uploaded)
		self.assertNotIn("kit.zip", uploaded)

	def test_old_versions_are_pruned_but_never_latest(self):
		for version in ("1.0.0", "1.1.0", "1.2.0", "1.3.0"):
			self.publish(version, {"a.txt": version.encode()}, keep=2)
		versions = {k[1] for k in self.server.files}
		self.assertEqual(versions, {"latest", "1.2.0", "1.3.0"})
		publish.promote(self.client, "demo", "1.2.0", say=lambda text: None)
		self.publish("1.4.0", {"a.txt": b"4"}, keep=1, latest=False)
		self.assertEqual({k[1] for k in self.server.files}, {"latest", "1.2.0", "1.4.0"}, "latest's version stays")

	def test_a_release_whose_archive_differs_from_release_json_is_refused(self):
		folder = os.path.join(self.folder, "bad")
		make_release(folder, "2.0.0", {"demo.zip": ("", {"a": b"a"})})
		with open(os.path.join(folder, "demo.zip"), "ab") as f:
			f.write(b"tampered")
		with self.assertRaises(publish.PublishError):
			publish.read_release(folder)

	def test_names_are_checked(self):
		folder = os.path.join(self.folder, "names")
		release = make_release(folder, "latest", {"demo.zip": ("", {"a": b"a"})})
		self.assertEqual(release["version"], "latest")
		with self.assertRaises(publish.PublishError):
			publish.read_release(folder)

	def test_installers_have_no_unfilled_marks_and_the_powershell_one_has_crlf(self):
		ps1 = publish.render_installer("install.ps1", "http://server:1", "me", "demo").decode()
		self.assertNotIn("\n", ps1.replace("\r\n", ""))
		self.assertIn("irm http://server:1/api/packages/me/generic/demo/latest/install.ps1 | iex", ps1)
		sh = publish.render_installer("install.sh", "http://server:1", "me", "demo").decode()
		self.assertNotIn("\r", sh)
		self.assertEqual(publish.install_commands("http://server:1/", "me", "demo")["linux"], "curl -fsSL http://server:1/api/packages/me/generic/demo/latest/install.sh | sh")
		for text in (ps1, sh):
			self.assertTrue(all(ord(c) < 128 for c in text), "the installers are ASCII (PowerShell 5.1 reads them without a charset)")


class ArchiveTests(unittest.TestCase):
	def setUp(self):
		self.folder = tempfile.mkdtemp()

	def tearDown(self):
		shutil.rmtree(self.folder, ignore_errors=True)

	def tree(self):
		source = os.path.join(self.folder, "tree")
		os.makedirs(os.path.join(source, "bin"))
		with open(os.path.join(source, "ied-server"), "wb") as f:
			f.write(b"\x7fELF" + b"\0" * 60)
		with open(os.path.join(source, "bin", "nuke"), "wb") as f:
			f.write(b"#!/bin/sh\nexec python3 nuke\n")
		with open(os.path.join(source, "README.md"), "wb") as f:
			f.write(b"text\n")
		return source

	def test_a_linux_archive_makes_programs_executable_and_the_rest_not(self):
		path = os.path.join(self.folder, "a.tar.gz")
		ied_release.write_tar(path, [(self.tree(), "")])
		with tarfile.open(path) as archive:
			modes = {m.name: m.mode for m in archive.getmembers()}
			owners = {(m.uid, m.gid) for m in archive.getmembers()}
		self.assertEqual(modes, {"ied-server": 0o755, "bin/nuke": 0o755, "README.md": 0o644})
		self.assertEqual(owners, {(0, 0)})

	def test_the_same_files_make_the_same_zip(self):
		source = self.tree()
		first, second = os.path.join(self.folder, "1.zip"), os.path.join(self.folder, "2.zip")
		ied_release.write_zip(first, [(source, ""), (os.path.join(source, "bin"), "extra/")])
		ied_release.write_zip(second, [(source, ""), (os.path.join(source, "bin"), "extra/")])
		self.assertEqual(publish.sha256_of(first), publish.sha256_of(second), "an unchanged kit is not uploaded or downloaded again")
		with zipfile.ZipFile(first) as z:
			self.assertEqual(z.namelist(), ["README.md", "ied-server", "bin/nuke", "extra/nuke"])


def linux_installer_python():
	"""The Python the Linux installer runs, without its last line (main())."""
	with open(os.path.join(DIST, "install.sh"), encoding="utf-8") as f:
		text = f.read()
	body = text.split("<<'PYTHON'\n", 1)[1].split("\nPYTHON\n", 1)[0]
	assert body.rstrip().endswith("main()")
	return body.rstrip()[:-len("main()")]


class LinuxInstallerTests(unittest.TestCase):
	def setUp(self):
		self.folder = tempfile.mkdtemp()
		self.saved = dict(os.environ)
		os.environ.update(DIST_SERVER="http://server", DIST_OWNER="me", DIST_PRODUCT="demo")
		self.code = {}
		exec(compile(linux_installer_python(), "install.sh:python", "exec"), self.code)

	def tearDown(self):
		os.environ.clear()
		os.environ.update(self.saved)
		shutil.rmtree(self.folder, ignore_errors=True)

	def write(self, root, files):
		for name, text in files.items():
			path = os.path.join(root, *name.split("/"))
			os.makedirs(os.path.dirname(path), exist_ok=True)
			with open(path, "w") as f:
				f.write(text)

	def listing(self, root):
		found = {}
		for folder, _, files in os.walk(root):
			for name in files:
				path = os.path.join(folder, name)
				with open(path) as f:
					found[os.path.relpath(path, root).replace(os.sep, "/")] = f.read()
		return found

	def test_mirror_replaces_and_removes_but_keeps_what_it_is_told(self):
		target = os.path.join(self.folder, "ied").replace(os.sep, "/")
		source = os.path.join(self.folder, "new").replace(os.sep, "/")
		self.write(target, {"ied-server": "old", "gone.txt": "x", "plugins/a/old.js": "x", "toolchains/c4/0.1.0/C4": "kept",
			"toolchains/c4/dev/C4": "other archive", "env": "PATH", "install.json": "{}", ".dist-installing": ""})
		self.write(source, {"ied-server": "new", "plugins/a/new.js": "y", "frontend/login.html": "<html>"})
		mirror = self.code["mirror"]
		mirror(source, target, [target + "/toolchains", target + "/env"])
		self.assertEqual(self.listing(target), {"ied-server": "new", "plugins/a/new.js": "y", "frontend/login.html": "<html>",
			"toolchains/c4/0.1.0/C4": "kept", "toolchains/c4/dev/C4": "other archive", "env": "PATH", "install.json": "{}", ".dist-installing": ""})

	def test_the_env_file_puts_each_folder_on_the_path_once(self):
		text = self.code["env_text"](["/home/me/.local/opt/ied", "/home/me/.local/opt/ied/bin"])
		self.assertEqual(len(re.findall(r'PATH="/home/me/\.local/opt/ied(/bin)?:\$\{PATH\}"', text)), 2)
		self.assertIn("export PATH", text)

	def test_versions_compare_as_numbers(self):
		version_tuple = self.code["version_tuple"]
		self.assertGreater(version_tuple("glibc 2.36"), version_tuple("2.4"))
		self.assertLess(version_tuple("glibc 2.31"), version_tuple("2.36"))


@unittest.skipUnless(os.name == "nt", "the PowerShell installer runs on Windows")
class WindowsInstallerTests(unittest.TestCase):
	"""install.ps1 end to end against a fake server: a first install, an update, an uninstall."""

	def setUp(self):
		self.server = serve()
		self.client = gitea.Gitea(self.server.url, OWNER, auth="token test")
		self.folder = tempfile.mkdtemp()
		self.target = os.path.join(self.folder, "Demo")
		self.installer = os.path.join(self.folder, "install.ps1")
		with open(self.installer, "wb") as f:
			f.write(publish.render_installer("install.ps1", self.server.url, OWNER, "demo"))

	def tearDown(self):
		self.server.shutdown()
		shutil.rmtree(self.folder, ignore_errors=True)

	def release(self, version, app, kit=None):
		folder = os.path.join(self.folder, "release-" + version)
		archives = {"demo-%s.zip" % version: ("", app)}
		if kit is not None:
			archives["kit.zip"] = ("vendor/kit", kit)
		make_release(folder, version, archives)
		publish.publish(self.client, folder, say=lambda text: None, keep=0)

	def install(self, **env):
		environment = {k: v for k, v in os.environ.items() if not k.startswith("DIST_")}
		environment.update(DIST_DIR=self.target, DIST_NO_PATH="1", DIST_NO_REGISTER="1", **env)
		result = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", self.installer], env=environment, capture_output=True, text=True, timeout=300)
		return result

	def files(self):
		found = set()
		for folder, _, names in os.walk(self.target):
			for name in names:
				found.add(os.path.relpath(os.path.join(folder, name), self.target).replace(os.sep, "/"))
		return found

	def test_install_update_and_uninstall(self):
		self.release("1.0.0", {"demo.exe": b"v1", "plugins/a/old.js": b"x", "bin/tool.cmd": b"@echo off"}, kit={"k.lib": b"kit"})
		first = self.install()
		self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
		self.assertEqual(self.files(), {"demo.exe", "plugins/a/old.js", "bin/tool.cmd", "vendor/kit/k.lib", "demo-update.cmd", "install.json"})
		with open(os.path.join(self.target, "install.json"), encoding="utf-8") as f:
			record = json.load(f)
		self.assertEqual(record["version"], "1.0.0")
		self.assertEqual(set(record["dist"]["archives"]), {".", "vendor/kit"})
		os.makedirs(os.path.join(self.target, "toolchains", "c4", "0.1.0"))
		with open(os.path.join(self.target, "toolchains", "c4", "0.1.0", "C4.exe"), "wb") as f:
			f.write(b"kept")

		self.release("1.1.0", {"demo.exe": b"v2", "plugins/a/new.js": b"y", "bin/tool.cmd": b"@echo off"}, kit={"k.lib": b"kit"})
		second = self.install()
		self.assertEqual(second.returncode, 0, second.stdout + second.stderr)
		self.assertIn("kit.zip: already installed", second.stdout + second.stderr)
		self.assertEqual(self.files(), {"demo.exe", "plugins/a/new.js", "bin/tool.cmd", "vendor/kit/k.lib", "demo-update.cmd", "install.json", "toolchains/c4/0.1.0/C4.exe"})
		with open(os.path.join(self.target, "demo.exe"), "rb") as f:
			self.assertEqual(f.read(), b"v2")

		pinned = self.install(DIST_VERSION="1.0.0")
		self.assertEqual(pinned.returncode, 0, pinned.stdout + pinned.stderr)
		self.assertIn("plugins/a/old.js", self.files())

		gone = self.install(DIST_UNINSTALL="1")
		self.assertEqual(gone.returncode, 0, gone.stdout + gone.stderr)
		self.assertFalse(os.path.exists(self.target))

	def test_a_running_program_is_renamed_aside_and_replaced(self):
		system32 = os.path.join(os.environ["SystemRoot"], "System32")
		with open(os.path.join(system32, "PING.EXE"), "rb") as f:
			ping = f.read()
		with open(os.path.join(system32, "where.exe"), "rb") as f:
			where = f.read()
		self.release("1.0.0", {"demo.exe": ping})
		self.assertEqual(self.install().returncode, 0)
		running = subprocess.Popen([os.path.join(self.target, "demo.exe"), "-n", "60", "127.0.0.1"], stdout=subprocess.DEVNULL)
		try:
			self.release("1.1.0", {"demo.exe": where})
			result = self.install()
			self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
			self.assertIn("was running (demo)", result.stdout)
			with open(os.path.join(self.target, "demo.exe"), "rb") as f:
				self.assertEqual(f.read(), where, "the new program is in place while the old one still runs")
			self.assertEqual(len([n for n in os.listdir(self.target) if n.startswith("demo.exe.") and n.endswith(".old")]), 1)
			self.assertIsNone(running.poll(), "the running copy kept running")
		finally:
			running.kill()
			running.wait()

	def test_a_folder_another_install_made_is_left_alone(self):
		self.release("1.0.0", {"demo.exe": b"v1"})
		os.makedirs(self.target)
		with open(os.path.join(self.target, "install.json"), "w") as f:
			json.dump({"source": "C:/work/IED", "commit": "abc"}, f)
		result = self.install()
		self.assertNotEqual(result.returncode, 0)
		self.assertIn("DIST_FORCE", result.stdout + result.stderr)
		self.assertEqual(self.files(), {"install.json"})

	def test_a_damaged_download_changes_nothing(self):
		self.release("1.0.0", {"demo.exe": b"v1"})
		key = ("demo", "1.0.0", "demo-1.0.0.zip")
		self.server.files[key] = self.server.files[key][:-1] + b"!"
		result = self.install()
		self.assertNotEqual(result.returncode, 0)
		self.assertIn("SHA-256", result.stdout + result.stderr)
		self.assertNotIn("demo.exe", self.files())


if __name__ == "__main__":
	unittest.main()
