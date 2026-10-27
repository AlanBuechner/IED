"""The servers the end-to-end tests drive (docs/19): the mock (tools/mock-server) and the real ied-server, each serving
frontend/, and how a browser context signs in to each."""

import json
import os
import secrets
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
FRONTEND = os.path.join(REPO, "frontend")
MOCK = os.path.join(REPO, "tools", "mock-server", "mock_server.py")
FAKE_MODEL = os.path.join(REPO, "tools", "fake-model", "fake_model.py")
FIXTURE = os.path.join(HERE, "fixture", "workspace")


def make_repository(folder):
	"""The fixture copy as a git repository with some history for the Git activity: the files on main, tagged v1, and
	a branch with a commit of its own."""
	def git(*args):
		subprocess.run(["git", "-c", "core.autocrlf=false", *args], cwd=folder, check=True, capture_output=True)
	git("init", "-q", "-b", "main")
	git("config", "user.name", "e2e")
	git("config", "user.email", "e2e@example.com")
	git("add", ".")
	git("commit", "-q", "-m", "Fixture: the workspace")
	git("tag", "v1")
	git("switch", "-q", "-c", "feature/ideas")
	with open(os.path.join(folder, "docs", "ideas.md"), "w", encoding="utf-8", newline="") as f:
		f.write("# Ideas" + chr(10))
	git("add", ".")
	git("commit", "-q", "-m", "Docs: ideas")
	git("switch", "-q", "main")


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
		self.model = None
		self.model_url = None

	def start(self):
		self.model = subprocess.Popen([sys.executable, "-I", FAKE_MODEL, "--port", "0", "--time-scale", "0.05"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
		self.model_url = self.model.stdout.readline().strip().split(" ", 1)[1]
		self.root = tempfile.mkdtemp(prefix="ied-e2e-")
		workspace = os.path.join(self.root, "e2e")
		shutil.copytree(FIXTURE, workspace)
		make_repository(workspace)
		self.workspace = workspace
		# The machine's own Claude Code CLI stays out: a server that finds it signed in sets Claude up as the default group
		# by itself, minutes into a run, and the chats lose the fake model (docs/04 "Setting it up").
		app = os.path.join(self.root, "data", "app")
		os.makedirs(app)
		with open(os.path.join(app, "settings.json"), "w", encoding="utf-8") as f:
			json.dump({"ai.claude.command": [os.path.join(self.root, "no-claude", "claude.exe")]}, f)
		self.control = "ied-e2e-" + secrets.token_hex(6)
		argv = [self.executable, "--host", "127.0.0.1", "--port", "0", "--data", os.path.join(self.root, "data"), "--control", self.control,
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

	def control_call(self, method, params=None):
		"""One request on the master's control pipe (docs/10 "Local control channel"): its result."""
		request = {"jsonrpc": "2.0", "id": 1, "method": method}
		if params is not None:
			request["params"] = params
		with open("\\\\.\\pipe\\" + self.control, "r+b", buffering=0) as pipe:
			pipe.write((json.dumps(request) + "\n").encode("utf-8"))
			data = b""
			while not data.endswith(b"\n"):
				chunk = pipe.read(65536)
				if not chunk:
					break
				data += chunk
		return json.loads(data)["result"]

	def open_folder(self, name):
		"""A second workspace beside the fixture's, opened as `ied <folder>` opens it (its own workspace server)."""
		folder = os.path.join(self.root, name)
		os.makedirs(os.path.join(folder, "src"), exist_ok=True)
		with open(os.path.join(folder, "README.md"), "w", encoding="utf-8", newline="") as f:
			f.write("# " + name + chr(10) + chr(10) + "A second workspace, in a server of its own." + chr(10))
		with open(os.path.join(folder, "src", "tool.py"), "w", encoding="utf-8", newline="") as f:
			f.write("print('tool')" + chr(10))
		return self.control_call("open", {"path": folder})["wsId"]

	def queue_model(self, *scripts):
		"""Scripts for the fake model's next requests (tools/fake-model/README.md)."""
		request = urllib.request.Request(self.model_url + "/_fake/queue", data=json.dumps({"scripts": list(scripts)}).encode(), headers={"Content-Type": "application/json"})
		urllib.request.urlopen(request, timeout=10).read()

	def model_requests(self):
		"""What the fake model was sent, oldest first."""
		with urllib.request.urlopen(self.model_url + "/_fake/requests", timeout=10) as reply:
			return json.loads(reply.read())

	def stop(self):
		super().stop()
		if self.model:
			self.model.kill()
			self.model.wait(10)
			self.model.stdout.close()
		if self.root:
			shutil.rmtree(self.root, ignore_errors=True)
