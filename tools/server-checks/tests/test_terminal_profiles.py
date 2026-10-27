"""Terminal profiles (docs/05 "Terminal profiles"): term.profiles lists what the plugins contribute - the terminal's own
shells and the Python plugin's Python - with whether each can start and the default; term.create runs a profile with its
variables expanded; terminal.defaultProfile chooses what New Terminal runs; a profile that cannot start or does not exist
is refused. The Python terminal runs IED's own Python (vendor/python) in the workspace's folder, and its pip() installs
into IED's packages folder for it - here a wheel made on the spot, so nothing is downloaded."""

import base64
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
import asyncio
import time
import zipfile

from frames import FrameChecker
from iedclient import RpcFailure
from routed import RoutedClient
from server import REPO, IedServer, ServerTestCase

SHELLS = ["pwsh", "powershell", "cmd", "gitbash"]


def write(path, text):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8", newline="") as f:
		f.write(text)


def loose(text):
	"""Text compared loosely: lower case, '/' separators, no line breaks or escape sequences (a pseudo console's)."""
	return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text).replace("\r", "").replace("\n", "").replace("\\", "/").lower()


def path_text(path):
	return loose(os.path.realpath(path))


def record_line(name, data):
	digest = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode("ascii")
	return "%s,sha256=%s,%d" % (name, digest, len(data))


def make_wheel(folder):
	"""A wheel of one module, iedprobe (ANSWER = "ied-probe-answer"), that pip installs without an index."""
	files = {
		"iedprobe.py": b'ANSWER = "ied-probe-answer"\n',
		"iedprobe-1.0.dist-info/METADATA": b"Metadata-Version: 2.1\nName: iedprobe\nVersion: 1.0\n",
		"iedprobe-1.0.dist-info/WHEEL": b"Wheel-Version: 1.0\nGenerator: ied-checks\nRoot-Is-Purelib: true\nTag: py3-none-any\n",
	}
	record = "\n".join([record_line(name, data) for name, data in files.items()] + ["iedprobe-1.0.dist-info/RECORD,,", ""])
	path = os.path.join(folder, "iedprobe-1.0-py3-none-any.whl")
	with zipfile.ZipFile(path, "w") as wheel:
		for name, data in files.items():
			wheel.writestr(name, data)
		wheel.writestr("iedprobe-1.0.dist-info/RECORD", record)
	return path


class TerminalProfileTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-profiles-")
		self.folder = os.path.join(self.root, "Proj")
		os.makedirs(self.folder)
		self.server = IedServer(extra=("--workspace", self.folder))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		self.ws = (await self.client.request("ws.list"))["workspaces"][0]["wsId"]

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def profiles(self):
		return await self.client.request("term.profiles", {"wsId": self.ws})

	async def output_until(self, topic, text, timeout=30):
		seen = []
		def has(frame):
			data = frame.get("data", {})
			seen.append(data.get("data", "") if frame.get("kind") == "data" else data.get("scrollback", ""))
			return text in "".join(seen)
		await self.client.wait_for(topic, has, timeout=timeout)
		return "".join(seen)

	async def open(self, params):
		term = await self.client.request("term.create", dict({"wsId": self.ws, "cols": 250, "rows": 40}, **params))
		topic = "term:" + term["termId"]
		await self.client.sub([topic])
		return term, topic

	async def test_the_profiles_the_plugins_contribute(self):
		answer = await self.profiles()
		ids = [p["id"] for p in answer["profiles"]]
		self.assertEqual(SHELLS, [i for i in ids if i in SHELLS], "the terminal's own, in its order")
		python = [p for p in answer["profiles"] if p["id"] == "python"][0]
		self.assertEqual(("Python", "$(python)", "ied.python", True), (python["title"], python["icon"], python["plugin"], python["available"]))
		cmd = [p for p in answer["profiles"] if p["id"] == "cmd"][0]
		self.assertTrue(cmd["available"], cmd)
		usable = [p["id"] for p in answer["profiles"] if p["available"]]
		self.assertEqual([i for i in ("pwsh", "powershell", "cmd") if i in usable][0], answer["default"], "the first shell there is")
		self.assertEqual("", answer["setting"])

	async def test_the_python_terminal_runs_ieds_python_and_pip(self):
		term, topic = await self.open({"profile": "python"})
		self.assertEqual(("Python", "python"), (term["title"], term["profile"]))
		await self.output_until(topic, "IED's own Python")
		await self.client.request("term.input", {"termId": term["termId"], "data": "6*7\r"})
		await self.output_until(topic, "42")
		await self.client.request("term.input", {"termId": term["termId"], "data": "import os; print('cwd=' + os.getcwd().lower())\r"})
		await self.output_until(topic, "cwd=" + os.path.realpath(self.folder).lower())
		wheel = make_wheel(self.root)
		await self.client.request("term.input", {"termId": term["termId"], "data": 'pip("install --no-index --no-deps %s")\r' % wheel.replace("\\", "/")})
		await self.output_until(topic, "Successfully installed iedprobe", timeout=120)
		await self.client.request("term.input", {"termId": term["termId"], "data": "import iedprobe; print(iedprobe.ANSWER)\r"})
		await self.output_until(topic, "ied-probe-answer")
		packages = os.path.join(self.server.data, "plugins", "ied.python", "packages")
		self.assertTrue(os.path.isfile(os.path.join(packages, "iedprobe.py")), "into IED's packages folder for the Python terminal")
		self.assertFalse(os.path.exists(os.path.join(REPO, "vendor", "python", "iedprobe.py")), "never into IED's own Python")
		await self.client.request("term.input", {"termId": term["termId"], "data": "pip install requests\r"})
		await self.output_until(topic, 'call pip("install requests")')
		await self.client.request("term.input", {"termId": term["termId"], "data": "exit()\r"})
		exit = (await self.client.wait_for(topic, lambda f: f.get("kind") == "exit", timeout=20))[-1]["data"]
		self.assertEqual(0, exit["code"])

	async def test_the_default_profile_and_refusals(self):
		await self.client.request("settings.set", {"tier": "app", "key": "terminal.defaultProfile", "value": "python"})
		answer = await self.profiles()
		self.assertEqual(("python", "python"), (answer["default"], answer["setting"]))
		term, topic = await self.open({})
		self.assertEqual("python", term["profile"], "New Terminal runs the default")
		await self.output_until(topic, "IED's own Python")
		await self.client.request("term.close", {"termId": term["termId"]})
		with self.assertRaises(RpcFailure) as unknown:
			await self.client.request("term.create", {"wsId": self.ws, "profile": "nothing"})
		self.assertEqual("not_found", unknown.exception.code)
		given, _ = await self.open({"argv": [sys.executable, "-I", "-c", "print('given')"]})
		self.assertEqual("", given["profile"], "a program given as it is runs no profile")

	async def test_a_plugins_profile_with_its_variables(self):
		write(os.path.join(self.folder, ".ied", "plugins", "test.term", "ied-plugin.json"), json.dumps({
			"manifestVersion": 1, "id": "test.term", "name": "Terminal probe", "version": "1.0.0", "publisher": "checks",
			"ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
			"contributes": {"terminalProfiles": [
				{"id": "ghost", "title": "Ghost", "argv": ["C:/nowhere/ghost.exe"]},
				{"id": "probe", "title": "Probe", "icon": "$(beaker)", "argv": [sys.executable, "-I", "-c", "import os; print('probe ' + os.getcwd() + ' ' + os.environ['PROBE_DIR'] + ' end', flush=True); input()"],
					"env": {"PROBE_DIR": "${pluginDir}"}, "cwd": "${workspaceDir}/sub"},
				{"id": "hidden", "title": "Hidden", "argv": ["cmd"], "when": "workspaceContains.nothing-here"},
			]},
		}))
		os.makedirs(os.path.join(self.folder, "sub"))
		await self.client.request("ws.setTrust", {"wsId": self.ws, "trusted": True})
		deadline = time.monotonic() + 20
		answer = await self.profiles()
		while "probe" not in [p["id"] for p in answer["profiles"]] and time.monotonic() < deadline:
			await asyncio.sleep(0.2)
			answer = await self.profiles()
		ids = [p["id"] for p in answer["profiles"]]
		self.assertNotIn("hidden", ids, "a profile whose when does not hold is not offered")
		ghost = [p for p in answer["profiles"] if p["id"] == "ghost"][0]
		self.assertEqual((False, "C:/nowhere/ghost.exe was not found"), (ghost["available"], ghost["problem"]))
		with self.assertRaises(RpcFailure) as refused:
			await self.client.request("term.create", {"wsId": self.ws, "profile": "ghost"})
		self.assertEqual("unavailable", refused.exception.code)
		await self.client.request("settings.set", {"tier": "app", "key": "terminal.defaultProfile", "value": "ghost"})
		answer = await self.profiles()
		self.assertNotEqual("ghost", answer["default"], "a default that cannot start falls back to a shell")
		term, topic = await self.open({"profile": "probe"})
		self.assertEqual(("Probe", "probe"), (term["title"], term["profile"]))
		text = loose(await self.output_until(topic, " end"))
		self.assertIn(path_text(os.path.join(self.folder, "sub")), text, "its folder, from ${workspaceDir}")
		self.assertIn(path_text(os.path.join(self.folder, ".ied", "plugins", "test.term")), text, "its environment, with ${pluginDir}")
