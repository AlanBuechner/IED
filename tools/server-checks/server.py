"""A real ied-server for one test: its own data folder, the launch token it prints, and plain HTTP calls to it."""

import asyncio
import http.client
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import urllib.parse

EXECUTABLE_ENV = "IED_SERVER_CHECKS_SERVER"
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
FRONTEND = os.path.join(REPO, "frontend")


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
	"""Starts `ied-server --test-hooks` on a free port (or `port`) with a data folder of its own (or `data`)."""

	def __init__(self, data=None, port=0, host="127.0.0.1", static=True, extra=()):
		self.owns_data = data is None
		self.data = data or tempfile.mkdtemp(prefix="ied-checks-")
		argv = [os.environ[EXECUTABLE_ENV], "--host", host, "--port", str(port), "--data", self.data, "--test-hooks", "--quiet"]
		if static:
			argv += ["--static", FRONTEND]
		self.process = subprocess.Popen(argv + list(extra), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
		self.listening = self.process.stdout.readline().strip()
		self.open_line = self.process.stdout.readline().strip()
		if not self.listening.startswith("LISTENING "):
			self.process.wait(10)
			self.error = self.process.stderr.read()
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
		if self.process.poll() is None:
			self.process.terminate()
			self.process.wait(10)
		self.process.stdout.close()
		self.process.stderr.close()
		if remove_data if remove_data is not None else self.owns_data:
			shutil.rmtree(self.data, ignore_errors=True)


def read_lines(path):
	with open(path, encoding="utf-8") as f:
		return [line for line in f.read().split("\n") if line]


sys.path.insert(0, os.path.join(REPO, "tools", "pylib"))
sys.path.insert(0, os.path.join(REPO, "tools", "conformance"))
