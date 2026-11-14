"""Deploys a folder to an IED config server (docs/35 "Deploying"): a server plugin, a client plugin or a skill.

	python -I tools/config-server/deploy.py server-plugin path\\to\\models-plugin
	python -I tools/config-server/deploy.py skill path\\to\\skills\\nas-deploy --watch
	python -I tools/config-server/deploy.py plugin path/to/plugin --server https://nas.lan:8780/ --token <admin token>
	python -I tools/config-server/deploy.py server-plugin spark.models --remove

The folder's files (but .git, build, __pycache__ and *.pyc) are staged on the server one by one, then committed with
their list and SHA-256s, which the server checks before it renames the package into place. A plugin's id is its
ied-plugin.json's; a skill's is its folder's name (--id names another). A server plugin's code compiles on the config
server at once: the script waits for it and prints its state, its diagnostics and what it published. --watch deploys
again on every change of the folder until Ctrl+C.

The server: --server, else the environment's IED_CONFIG_SERVER, else configServer.url of the app settings (the IED you
run here follows the same server). The admin token: --token, else IED_CONFIG_ADMIN_TOKEN (make one on the config server:
ied-server --config-root <folder> --config-token admin <name>). An https server whose certificate the system does not
trust: --cafile <PEM>, else IED_CONFIG_CAFILE. Stdlib only.

Exit code 0: deployed (and, for a server plugin, its code compiled and runs).
"""

import argparse
import hashlib
import json
import os
import ssl
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools", "pylib"))

import jsonc  # noqa: E402

KINDS = {"server-plugin": "server-plugins", "plugin": "plugins", "skill": "skills"}
SKIPPED_FOLDERS = {".git", "build", "__pycache__"}
COMPILE_SECONDS = 180


class DeployError(Exception):
	pass


def app_settings_file():
	"""Where this machine's IED keeps its app settings (src/host/Config.cf AppDataFolder)."""
	if os.name == "nt":
		return os.path.join(os.environ.get("APPDATA", ""), "IED", "settings.json")
	base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
	return os.path.join(base, "ied", "settings.json")


def app_setting(key):
	path = app_settings_file()
	if not os.path.isfile(path):
		return None
	with open(path, encoding="utf-8") as f:
		try:
			return jsonc.loads(f.read()).get(key)
		except ValueError:
			return None


def server_url(given):
	url = given or os.environ.get("IED_CONFIG_SERVER") or app_setting("configServer.url") or ""
	if not url.startswith(("http://", "https://")):
		raise DeployError("no config server: --server <url>, IED_CONFIG_SERVER, or configServer.url in %s" % app_settings_file())
	return url if url.endswith("/") else url + "/"


class Server:
	"""The config API (docs/35 "The API") with an admin token."""

	def __init__(self, url, token, cafile=None):
		self.url = url
		self.token = token
		self.context = ssl.create_default_context(cafile=cafile) if url.startswith("https://") else None

	def call(self, method, path, body=None, timeout=60):
		request = urllib.request.Request(self.url + "config/v1/" + path, data=body, method=method)
		request.add_header("Authorization", "Bearer " + self.token)
		if body is not None:
			request.add_header("Content-Type", "application/octet-stream")
		try:
			with urllib.request.urlopen(request, timeout=timeout, context=self.context) as response:
				data = response.read()
		except urllib.error.HTTPError as error:
			data = error.read()
			try:
				answer = json.loads(data)
			except ValueError:
				answer = {"message": data.decode("utf-8", "replace")}
			problems = "".join("\n  " + p for p in answer.get("problems", []))
			raise DeployError("%s %s: HTTP %d: %s%s" % (method, path, error.code, answer.get("message", ""), problems))
		except urllib.error.URLError as error:
			raise DeployError("%s cannot be reached: %s" % (self.url, error.reason))
		return json.loads(data) if data else {}


def package_files(folder):
	"""Every file of the package: (relative path with '/', absolute path), in path order."""
	found = []
	for top, folders, files in os.walk(folder):
		folders[:] = sorted(f for f in folders if f not in SKIPPED_FOLDERS)
		for name in files:
			if name.endswith(".pyc"):
				continue
			full = os.path.join(top, name)
			found.append((os.path.relpath(full, folder).replace(os.sep, "/"), full))
	return sorted(found)


def package_id(kind, folder, given):
	if given:
		return given
	if kind == "skills":
		return os.path.basename(os.path.normpath(folder))
	manifest = os.path.join(folder, "ied-plugin.json")
	if not os.path.isfile(manifest):
		raise DeployError("%s holds no ied-plugin.json: a plugin is the folder with its manifest" % folder)
	with open(manifest, encoding="utf-8") as f:
		plugin = jsonc.loads(f.read())
	if not plugin.get("id"):
		raise DeployError("%s names no id" % manifest)
	return plugin["id"]


def server_plugin(server, plugin_id):
	for plugin in server.call("GET", "server-plugins").get("plugins", []):
		if plugin["id"] == plugin_id:
			return plugin
	return None


def builds(plugin):
	return ((plugin or {}).get("code") or {}).get("builds", 0)


def settled(server, plugin_id, before):
	"""The server plugin once its code built again (or failed to load at all), at most COMPILE_SECONDS."""
	deadline = time.monotonic() + COMPILE_SECONDS
	plugin = None
	while time.monotonic() < deadline:
		plugin = server_plugin(server, plugin_id)
		code = (plugin or {}).get("code") or {}
		if plugin and plugin["state"] != "loaded":
			return plugin
		if code and not code.get("building") and code.get("state") not in ("idle", "compiling") and builds(plugin) > before:
			return plugin
		time.sleep(0.5)
	raise DeployError("%s did not compile within %d s: %s" % (plugin_id, COMPILE_SECONDS, json.dumps(plugin)))


def report(plugin):
	"""Prints a server plugin's state; true when the deployed code runs (not a build before it that did compile)."""
	code = plugin.get("code") or {}
	print("  plugin %s %s: %s, code %s (build %s)" % (plugin["id"], plugin.get("version", ""), plugin["state"], code.get("state", "none"), code.get("builds", 0)))
	for problem in plugin.get("problems", []):
		print("  problem: " + problem)
	if plugin.get("diagnostics"):
		print("  " + plugin["diagnostics"].replace("\n", "\n  "))
	if plugin.get("published"):
		print("  published: " + json.dumps(plugin["published"]))
	return plugin["state"] == "loaded" and code.get("state") == "active" and not plugin.get("diagnostics")


def deploy(server, kind, folder, plugin_id):
	files = package_files(folder)
	if not files:
		raise DeployError("%s holds no file" % folder)
	before = builds(server_plugin(server, plugin_id)) if kind == "server-plugins" else 0
	base = "%s/%s/" % (kind, urllib.parse.quote(plugin_id, safe=""))
	server.call("DELETE", base + "stage")
	listed = []
	for relative, full in files:
		with open(full, "rb") as f:
			data = f.read()
		server.call("PUT", base + "stage/" + urllib.parse.quote(relative, safe="/"), data)
		listed.append({"path": relative, "hash": hashlib.sha256(data).hexdigest()})
	answer = server.call("POST", base + "commit", json.dumps(listed).encode("utf-8"))
	print("%s/%s: %d files, %s, server version %s" % (kind, plugin_id, len(files), "unchanged" if answer.get("unchanged") else "deployed", answer.get("version", "")[:12]))
	if kind != "server-plugins":
		return True
	plugin = server_plugin(server, plugin_id) if answer.get("unchanged") else settled(server, plugin_id, before)
	if not plugin:
		raise DeployError("the server lists no server plugin %s after its deploy" % plugin_id)
	return report(plugin)


def snapshot(folder):
	state = {}
	for relative, full in package_files(folder):
		try:
			info = os.stat(full)
			state[relative] = (info.st_mtime_ns, info.st_size)
		except OSError:
			pass
	return state


def watch(server, kind, folder, plugin_id):
	print("watching %s (Ctrl+C ends)" % folder)
	seen = snapshot(folder)
	while True:
		time.sleep(0.5)
		now = snapshot(folder)
		if now == seen:
			continue
		time.sleep(0.3)
		seen = snapshot(folder)
		try:
			deploy(server, kind, folder, plugin_id)
		except DeployError as error:
			print("deploy failed: %s" % error)


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("kind", choices=sorted(KINDS), help="what the folder is")
	parser.add_argument("folder", help="the package's folder (with --remove: its id)")
	parser.add_argument("--server", help="the config server's URL (default: IED_CONFIG_SERVER, else configServer.url)")
	parser.add_argument("--token", help="an admin token (default: IED_CONFIG_ADMIN_TOKEN)")
	parser.add_argument("--cafile", help="a PEM file of CA certificates for an https server (default: IED_CONFIG_CAFILE)")
	parser.add_argument("--id", help="the package's id (default: the plugin manifest's id, a skill's folder name)")
	parser.add_argument("--watch", action="store_true", help="deploy again on every change of the folder")
	parser.add_argument("--remove", action="store_true", help="remove the package FOLDER names instead")
	args = parser.parse_args(argv)
	try:
		token = args.token or os.environ.get("IED_CONFIG_ADMIN_TOKEN")
		if not token:
			raise DeployError("no admin token: --token, or IED_CONFIG_ADMIN_TOKEN (ied-server --config-root <folder> --config-token admin <name> makes one)")
		server = Server(server_url(args.server), token, args.cafile or os.environ.get("IED_CONFIG_CAFILE"))
		kind = KINDS[args.kind]
		if args.remove:
			answer = server.call("DELETE", "%s/%s" % (kind, urllib.parse.quote(args.id or args.folder, safe="")))
			print("%s/%s removed, server version %s" % (kind, answer.get("id", ""), answer.get("version", "")[:12]))
			return 0
		folder = os.path.abspath(args.folder)
		if not os.path.isdir(folder):
			raise DeployError("%s is not a folder" % folder)
		plugin_id = package_id(kind, folder, args.id)
		ok = deploy(server, kind, folder, plugin_id)
		if args.watch:
			watch(server, kind, folder, plugin_id)
		return 0 if ok else 1
	except DeployError as error:
		print("deploy: %s" % error, file=sys.stderr)
		return 1
	except KeyboardInterrupt:
		return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
