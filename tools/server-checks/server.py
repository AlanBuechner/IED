"""A real ied-server for one test: its own data folder, the launch token it prints, and plain HTTP calls to it. A
server a test did not stop (its setUp failed after starting it, so its tearDown never ran) is stopped when the checks
end, never left running."""

import asyncio
import atexit
import hashlib
import http.client
import json
import os
import secrets
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.parse

EXECUTABLE_ENV = "IED_SERVER_CHECKS_SERVER"
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
FRONTEND = os.path.join(REPO, "frontend")
RUNNING = set()


def trust_key(path):
	"""The server's TrustKey: a folder's path compared without ASCII case or separator kind, no trailing slash."""
	key = path.replace("\\", "/").lower()
	return key.rstrip("/") if len(key) > 1 else key


def local_state(data, folder):
	"""Where a server with `data` keeps a folder workspace's per-machine state (docs/07): its app data, never the
	folder. The workspace's ".ied/local/..." paths name files here."""
	key = hashlib.sha256(trust_key(os.path.realpath(folder)).encode("utf-8")).hexdigest()[:16]
	return os.path.join(data, "app", "workspaces", key)


def read_json_file(path, seconds=3.0):
	"""A JSON file the server writes, read as it stands: the server replaces such files whole (write beside, then move),
	and on Windows a read that meets the move gets a sharing error for a moment, so it is tried again."""
	deadline = time.monotonic() + seconds
	while True:
		try:
			with open(path, encoding="utf-8") as f:
				return json.load(f)
		except PermissionError:
			if time.monotonic() > deadline:
				raise
			time.sleep(0.05)


def folder_key(folder):
	"""The key of a folder's local state and of its workspace server's files (docs/01 "Storage")."""
	return hashlib.sha256(trust_key(os.path.realpath(folder)).encode("utf-8")).hexdigest()[:16]


def server_state(data, folder):
	"""The workspace server's own folder for the workspace at `folder` (its plugins' state, its script cache)."""
	return os.path.join(data, "servers", folder_key(folder))


def workspace_log(data, folder):
	"""The workspace server's log for the workspace at `folder` (docs/12 "Logs")."""
	return os.path.join(data, "logs", "workspace-%s.log" % folder_key(folder))


def workspace_file(data, folder, path):
	"""The file a folder workspace's relative `path` names, as the server's Workspace::At: ".ied/local/..." in its local
	state, anything else in the folder."""
	if path == ".ied/local" or path.startswith(".ied/local/"):
		return os.path.join(local_state(data, folder), *path[len(".ied/local/"):].split("/"))
	return os.path.join(folder, *path.split("/"))


def remove_tree(path):
	"""Removes a folder a test made, git's read-only object files included (rmtree alone leaves them, and the folder)."""
	def writable(function, target, error):
		os.chmod(target, stat.S_IWRITE)
		function(target)
	if os.path.isdir(path):
		shutil.rmtree(path, onexc=writable)


@atexit.register
def stop_leftovers():
	for server in list(RUNNING):
		server.stop()


class ServerTestCase(unittest.IsolatedAsyncioTestCase):
	"""The tests make blocking HTTP calls on purpose: asyncio's debug mode need not report each one as slow."""

	async def asyncSetUp(self):
		asyncio.get_running_loop().slow_callback_duration = 60


class Reply:
	def __init__(self, status, headers, body):
		self.status = status
		self.headers = headers          # [(name, value)] as sent
		self.body = body

	def header(self, name):
		for key, value in self.headers:
			if key.lower() == name.lower():
				return value
		return None

	def cookies(self):
		return [value for key, value in self.headers if key.lower() == "set-cookie"]

	def text(self):
		return self.body.decode("utf-8", "replace")


class IedServer:
	"""Starts `ied-server --test-hooks` on a free port (or `port`) with a data folder of its own (or `data`); another copy
	of the server with `executable`."""

	def __init__(self, data=None, port=0, host="127.0.0.1", static=True, extra=(), control=None, executable=None):
		self.owns_data = data is None
		self.data = data or tempfile.mkdtemp(prefix="ied-checks-")
		self.control = control or "ied-checks-" + secrets.token_hex(6)
		argv = [executable or os.environ[EXECUTABLE_ENV], "--host", host, "--port", str(port), "--data", self.data, "--control", self.control, "--test-hooks", "--quiet"]
		if static:
			argv += ["--static", FRONTEND]
		self.process = subprocess.Popen(argv + list(extra), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
		RUNNING.add(self)
		self.listening = self.process.stdout.readline().strip()
		self.open_line = self.process.stdout.readline().strip()
		if not self.listening.startswith("LISTENING "):
			self.process.wait(10)
			self.error = "exit %s, first line %r: %s" % (self.process.returncode, self.listening, self.process.stderr.read())
			self.port = None
			return
		self.error = ""
		url = urllib.parse.urlsplit(self.listening.split(" ", 1)[1])
		self.host, self.port = url.hostname, url.port
		self.launch_url = self.open_line.split(" ", 1)[1] if self.open_line.startswith("OPEN ") else None

	@property
	def started(self):
		return self.port is not None

	@property
	def cookie_name(self):
		return "ied-%d" % self.port

	@property
	def ws_url(self):
		return "ws://%s:%d/ws" % (self.host, self.port)

	def request(self, method, path, body=None, headers=None):
		connection = http.client.HTTPConnection(self.host, self.port, timeout=30)
		sent = {"Host": "%s:%d" % (self.host, self.port)}
		sent.update(headers or {})
		connection.request(method, path, body=body, headers=sent)
		response = connection.getresponse()
		reply = Reply(response.status, response.getheaders(), response.read())
		connection.close()
		return reply

	def with_cookie(self, cookie, extra=None):
		headers = {"Cookie": cookie}
		headers.update(extra or {})
		return headers

	def exchange(self, token, path="/"):
		return self.request("GET", "%s?t=%s" % (path, token))

	def sign_in(self):
		"""Spends the printed launch token: the session cookie as "name=value"."""
		url = urllib.parse.urlsplit(self.launch_url)
		reply = self.exchange(urllib.parse.parse_qs(url.query)["t"][0])
		assert reply.status == 303, "launch token refused: %d" % reply.status
		return reply.cookies()[0].split(";", 1)[0]

	def stop(self, remove_data=None):
		RUNNING.discard(self)
		if self.process.poll() is None:
			self.process.terminate()
			self.process.wait(10)
		self.process.stdout.close()
		self.process.stderr.close()
		if remove_data if remove_data is not None else self.owns_data:
			shutil.rmtree(self.data, ignore_errors=True)


CLI_ENV = "IED_SERVER_CHECKS_CLI"
DESKTOP_ENV = "IED_SERVER_CHECKS_DESKTOP"


def pipe_path(name):
	return "\\\\.\\pipe\\" + name


class ControlPipe:
	"""The server's control channel (docs/10): one JSON-RPC 2.0 request per line."""

	def __init__(self, name):
		self.pipe = open(pipe_path(name), "r+b", buffering=0)
		self.ids = 0

	def call(self, method, params=None):
		self.ids += 1
		request = {"jsonrpc": "2.0", "id": self.ids, "method": method}
		if params is not None:
			request["params"] = params
		return self.send_line(json.dumps(request))

	def send_line(self, line):
		self.pipe.write((line + "\n").encode("utf-8"))
		data = b""
		while not data.endswith(b"\n"):
			chunk = self.pipe.read(65536)
			if not chunk:
				break
			data += chunk
		return json.loads(data)

	def close(self):
		self.pipe.close()


def read_lines(path):
	with open(path, encoding="utf-8") as f:
		return [line for line in f.read().split("\n") if line]


sys.path.insert(0, os.path.join(REPO, "tools", "pylib"))
sys.path.insert(0, os.path.join(REPO, "tools", "conformance"))
