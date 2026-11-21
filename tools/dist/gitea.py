"""A Gitea server's generic package registry, which tools/dist publishes prebuilt releases to (docs/21 "Prebuilt
releases on the owner's Gitea"): `<server>/api/packages/<owner>/generic/<package>/<version>/<file>`.

Downloads need no sign-in when the owner's packages are visible to everyone (a public user or organization); uploads,
deletions and listings do. The server and owner come from the command line, else DIST_SERVER / DIST_OWNER, else the
checkout's `.ied/local/dist.json` (per machine, never committed: a LAN address stays out of the repository):

	{"server": "http://<host>:<port>", "owner": "<user or organization>", "repo": "<repository the packages link to>"}

Sign-in: DIST_TOKEN (a Gitea access token with package write access), else what git already has for that server
(`git credential fill`, e.g. Git Credential Manager's), so publishing needs no secret of its own.
"""

import base64
import http.client
import json
import os
import subprocess
import time
import urllib.parse

CONFIG_FILE = os.path.join(".ied", "local", "dist.json")
TIMEOUT = 600   # seconds per socket operation: an upload of a few hundred MB over a LAN


class GiteaError(Exception):
	pass


def load_config(checkout, server=None, owner=None, repo=None):
	"""{server, owner, repo}: the arguments, else DIST_SERVER / DIST_OWNER / DIST_REPO, else `.ied/local/dist.json`."""
	stored = {}
	path = os.path.join(checkout, CONFIG_FILE)
	if os.path.isfile(path):
		with open(path, encoding="utf-8") as f:
			stored = json.load(f)
	server = server or os.environ.get("DIST_SERVER") or stored.get("server")
	owner = owner or os.environ.get("DIST_OWNER") or stored.get("owner")
	repo = repo or os.environ.get("DIST_REPO") or stored.get("repo")
	if not server or not owner:
		raise GiteaError("no package server: pass --server and --owner, set DIST_SERVER and DIST_OWNER, or write %s "
			'({"server": "http://<host>:<port>", "owner": "<user>", "repo": "<repository>"})' % path)
	return {"server": server.rstrip("/"), "owner": owner, "repo": repo}


def credential_header(server):
	"""The Authorization header for `server`: DIST_TOKEN, else git's stored credentials for it."""
	token = os.environ.get("DIST_TOKEN")
	if token:
		return "token " + token
	parts = urllib.parse.urlsplit(server)
	query = "protocol=%s\nhost=%s\n\n" % (parts.scheme, parts.netloc)
	environment = dict(os.environ, GIT_TERMINAL_PROMPT="0", GCM_INTERACTIVE="never")
	try:
		result = subprocess.run(["git", "credential", "fill"], input=query, capture_output=True, text=True, env=environment, timeout=120)
	except (OSError, subprocess.TimeoutExpired) as error:
		raise GiteaError("git credential fill for %s failed: %s" % (server, error)) from None
	fields = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
	if result.returncode != 0 or not fields.get("username") or not fields.get("password"):
		raise GiteaError("no credentials for %s: set DIST_TOKEN to a Gitea access token, or sign git in to it (git clone "
			"or push once there)" % server)
	pair = "%s:%s" % (fields["username"], fields["password"])
	return "Basic " + base64.b64encode(pair.encode("utf-8")).decode("ascii")


class Gitea:
	def __init__(self, server, owner, auth=None):
		self.server = server.rstrip("/")
		self.owner = owner
		self._auth = auth            # the Authorization header; looked up on first use when None

	def _header(self):
		if self._auth is None:
			self._auth = credential_header(self.server)
		return self._auth

	def request(self, method, path, body=None, data=None, length=None, content_type=None, ok=(200, 201, 204), signed=True):
		"""(status, bytes) of `method path`; a status outside `ok` raises GiteaError. `data` is bytes or a file, sent
		with its length (http.client: urllib sends a file chunked, which not every server or proxy takes)."""
		parts = urllib.parse.urlsplit(self.server)
		connection = (http.client.HTTPSConnection if parts.scheme == "https" else http.client.HTTPConnection)(parts.netloc, timeout=TIMEOUT)
		if body is not None:
			data = json.dumps(body).encode("utf-8")
			content_type = "application/json"
		try:
			connection.putrequest(method, parts.path.rstrip("/") + path)
			if signed:
				connection.putheader("Authorization", self._header())
			if data is not None:
				connection.putheader("Content-Type", content_type or "application/octet-stream")
			if data is not None or method in ("PUT", "POST"):
				connection.putheader("Content-Length", str(length if length is not None else len(data) if data is not None else 0))
			connection.endheaders()
			if isinstance(data, (bytes, bytearray)):
				connection.send(data)
			elif data is not None:
				for block in iter(lambda: data.read(1 << 20), b""):
					connection.send(block)
			response = connection.getresponse()
			status, payload = response.status, response.read()
		except (OSError, http.client.HTTPException) as error:
			raise GiteaError("%s %s%s: %s" % (method, self.server, path, error)) from None
		finally:
			connection.close()
		if status not in ok:
			raise GiteaError("%s %s%s: HTTP %d %s" % (method, self.server, path, status, payload[:300].decode("utf-8", "replace")))
		return status, payload

	def package_path(self, package, version, name=None):
		path = "/api/packages/%s/generic/%s/%s" % (_quote(self.owner), _quote(package), _quote(version))
		return path + ("/" + _quote(name) if name else "")

	def download_url(self, package, version, name):
		return self.server + self.package_path(package, version, name)

	def files(self, package, version):
		"""{name: sha256} of a published version; empty when there is none."""
		status, payload = self.request("GET", "/api/v1/packages/%s/generic/%s/%s/files" % (_quote(self.owner), _quote(package), _quote(version)), ok=(200, 404))
		if status == 404:
			return {}
		return {f["name"]: f.get("sha256", "") for f in json.loads(payload)}

	def upload(self, package, version, name, path):
		"""Uploads the file at `path` as `name` (a file already there of that name is replaced)."""
		size = os.path.getsize(path)
		for attempt in range(3):
			try:
				with open(path, "rb") as f:
					status, _ = self.request("PUT", self.package_path(package, version, name), data=f, length=size, ok=(201, 409))
				if status == 201:
					return
				self.delete_file(package, version, name)
			except GiteaError:
				if attempt == 2:
					raise
				time.sleep(2 * (attempt + 1))
		raise GiteaError("%s/%s/%s: the server kept a file of that name" % (package, version, name))

	def upload_bytes(self, package, version, name, data):
		status, _ = self.request("PUT", self.package_path(package, version, name), data=data, ok=(201, 409))
		if status == 409:
			self.delete_file(package, version, name)
			self.request("PUT", self.package_path(package, version, name), data=data, ok=(201,))

	def delete_file(self, package, version, name):
		self.request("DELETE", self.package_path(package, version, name), ok=(200, 204, 404))

	def delete_version(self, package, version):
		self.request("DELETE", self.package_path(package, version), ok=(200, 204, 404))

	def versions(self, package):
		"""[(version, created)] of a package, newest first ("created" as the server writes it, ISO 8601)."""
		found, page = [], 1
		while True:
			query = urllib.parse.urlencode({"type": "generic", "q": package, "page": page, "limit": 50})
			_, payload = self.request("GET", "/api/v1/packages/%s?%s" % (_quote(self.owner), query))
			batch = json.loads(payload)
			found += [(p["version"], p.get("created_at", "")) for p in batch if p.get("name") == package]
			if len(batch) < 50:
				break
			page += 1
		return sorted(found, key=lambda item: item[1], reverse=True)

	def link(self, package, repo):
		"""Links the package to a repository of the owner (its Packages tab); already linked is fine."""
		self.request("POST", "/api/v1/packages/%s/generic/%s/-/link/%s" % (_quote(self.owner), _quote(package), _quote(repo)), ok=(200, 201, 204, 400, 409))


def _quote(text):
	return urllib.parse.quote(text, safe="")
