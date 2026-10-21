"""The servers the end-to-end tests drive (docs/19): the mock (tools/mock-server) and the real ied-server, each serving
frontend/, and how a browser context signs in to each."""

import os
import secrets
import shutil
import subprocess
import sys
import tempfile
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
FRONTEND = os.path.join(REPO, "frontend")
MOCK = os.path.join(REPO, "tools", "mock-server", "mock_server.py")
FIXTURE = os.path.join(HERE, "fixture", "workspace")


class Target:
	kind = ""

	def __init__(self):
		self.process = None
		self.url = None
		self.storage = None

	def stop(self):
		if self.process and self.process.poll() is None:
			self.process.terminate()
			try:
				self.process.wait(10)
			except subprocess.TimeoutExpired:
				self.process.kill()
		if self.process:
			for stream in (self.process.stdout, self.process.stderr):
				if stream:
					stream.close()

	def new_context(self, browser, **options):
		"""A browser context signed in to the server."""
		return browser.new_context(**options)


class MockTarget(Target):
	kind = "mock"

	def start(self):
		argv = [sys.executable, "-I", MOCK, "--port", "0", "--static", FRONTEND, "--quiet", "--time-scale", "0.05"]
		self.process = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
		line = self.process.stdout.readline().strip()
		if not line.startswith("LISTENING "):
			raise RuntimeError("the mock did not start: " + line)
		self.url = line.split(" ", 1)[1]
		return self


class RealTarget(Target):
	"""ied-server with test hooks, a data folder of its own, and a copy of the fixture workspace."""

	kind = "real"

	def __init__(self, executable):
		super().__init__()
		self.executable = os.path.abspath(executable)
		self.root = None
		self.launch_url = None

	def start(self):
		self.root = tempfile.mkdtemp(prefix="ied-e2e-")
		workspace = os.path.join(self.root, "e2e")
		shutil.copytree(FIXTURE, workspace)
		argv = [self.executable, "--host", "127.0.0.1", "--port", "0", "--data", os.path.join(self.root, "data"), "--control", "ied-e2e-" + secrets.token_hex(6),
			"--test-hooks", "--quiet", "--static", FRONTEND, "--workspace", workspace]
		self.process = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
		listening = self.process.stdout.readline().strip()
		opening = self.process.stdout.readline().strip()
		if not listening.startswith("LISTENING ") or not opening.startswith("OPEN "):
			raise RuntimeError("ied-server did not start: %s %s %s" % (listening, opening, self.process.stderr.read()[:2000]))
		self.url = listening.split(" ", 1)[1]
		self.launch_url = opening.split(" ", 1)[1]
		return self

	def new_context(self, browser, **options):
		"""The launch token signs in the first context; later ones carry its cookie (the same device)."""
		if self.storage is not None:
			return browser.new_context(storage_state=self.storage, **options)
		context = browser.new_context(**options)
		page = context.new_page()
		page.goto(self.launch_url)
		page.wait_for_url(lambda url: urllib.parse.urlsplit(url).query == "", timeout=15000)
		self.storage = context.storage_state()
		page.close()
		return context

	def stop(self):
		super().stop()
		if self.root:
			shutil.rmtree(self.root, ignore_errors=True)
