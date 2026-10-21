"""Tasks (docs/06 "Protocol and state"): a project's offer is its presets' variables, tasks and toolbar under its own file's;
selections persist per machine; a run streams its steps' output to run:<runId> and the output source, turns matched
lines into diagnostics, follows onlyIf, saves dirty documents first, refuses unknown variables and a second `single`
run, and cancel kills the step's whole process tree. An untrusted workspace runs nothing; a folder workspace gets the
presets whose `detect` matches."""

import asyncio
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

from frames import FrameChecker
from iedclient import Client, RpcFailure
from server import REPO, IedServer, ServerTestCase

PY = sys.executable


def write(path, value):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8", newline="") as f:
		f.write(value if isinstance(value, str) else json.dumps(value, indent="\t"))


def python(code):
	return [PY, "-I", "-c", code]


FAKE_PLUGIN = {
	"manifestVersion": 1, "id": "fake.tasks", "name": "Fake tasks", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
	"contributes": {
		"settings": {"schema": "settings.schema.json"},
		"problemMatchers": [
			{"id": "fake", "source": "fake", "patterns": [{"regexp": "^(.+?) \\((\\d+):(\\d+)\\) (error|warning): (.*)$", "file": 1, "line": 2, "column": 3, "severity": 4, "message": 5}]},
			{"id": "fake-src", "source": "fake-src", "fileLocation": {"kind": "search", "roots": ["${projectRoot}/src"]},
				"patterns": [{"regexp": "^(.+?):(\\d+): (.*)$", "file": 1, "line": 2, "message": 3}]}],
		"taskPresets": [{
			"id": "fake.make", "label": "Fake make", "detect": "fake.mk",
			"variables": {"config": {"kind": "dropdown", "values": ["Debug", "Development"], "default": "Development", "scope": "solution"}},
			"tasks": {
				"build": {"slot": "build", "steps": [{"run": python("print('building ${config}')"), "problemMatcher": "fake"}]},
				"clean": {"steps": [{"run": python("print('preset clean')")}]}},
			"toolbar": ["config", "|", "build"]},
			{"id": "fake.derived", "label": "Derived", "extends": "fake.make", "problemMatcher": "fake", "toolbar": ["config", "|", "lint"],
				"tasks": {"lint": {"steps": [{"run": python("print('src/a.c (1:1) warning: lint says hi')")}]}}},
			{"id": "fake.loop", "label": "Loop", "extends": "fake.loop"}]}}


class TaskTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-tasks-")
		self.home = os.path.join(self.root, "home")
		self.repo = os.path.join(self.root, "Game")
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		write(os.path.join(self.home, "plugins", "fake", "ied-plugin.json"), FAKE_PLUGIN)
		write(os.path.join(self.home, "plugins", "fake", "settings.schema.json"), {"properties": {"fake.greeting": {"type": "string", "default": "hello"}}})
		write(os.path.join(self.repo, "src", "a.c"), "int a;\nint b;\nint c;\n")
		write(os.path.join(self.repo, "hello.cmd"), "@echo hello %~1\r\n")
		write(os.path.join(self.repo, "Game.ied.proj"), {"version": 1, "name": "Game", "root": ".", "kind": "executable", "presets": ["fake.make"], "tasks": self.tasks()})
		write(os.path.join(self.repo, "Game.ied.sln"), {"version": 1, "name": "Game", "projects": [{"file": "Game.ied.proj"}]})
		self.server = IedServer(extra=("--home", self.home, "--workspace", os.path.join(self.repo, "Game.ied.sln")))
		self.assertTrue(self.server.started, self.server.error)

	def tasks(self):
		marker = os.path.join(self.root, "grandchild.pid").replace("\\", "/")
		grandchild = "import subprocess,sys,time; p=subprocess.Popen([sys.executable,'-c','import time; time.sleep(120)']); open('%s','w').write(str(p.pid)); print('ready', flush=True); time.sleep(120)" % marker
		return {
			"clean": {"label": "Clean it", "steps": [{"run": python("print('project clean')")}]},
			"check": {"steps": [{"run": python("import sys; print('src/a.c (2:5) error: no b'); print('src/missing.c (1:1) warning: gone'); sys.stderr.write('src/a.c (3:1) warning: c\\n')"), "problemMatcher": "fake"}]},
			"checkSrc": {"steps": [{"run": python("print('a.c:3: named from src')"), "problemMatcher": "fake-src"}]},
			"shell": {"steps": [{"run": ".\\hello.cmd \"big world\"& echo \"kept & quoted\""}]},
			"chain": {"steps": [
				{"run": python("import sys; print('first'); sys.exit(3)")},
				{"run": python("print('skipped')")},
				{"run": python("print('cleanup')"), "onlyIf": "always"},
				{"task": "clean", "onlyIf": "always"}]},
			"forever": {"concurrency": "single", "steps": [{"run": python(grandchild)}]},
			"sleepy": {"concurrency": "parallel", "steps": [{"run": python("import time; time.sleep(60)  # " + self.orphan_marker())}]},
			"typo": {"steps": [{"run": python("print('${nope}')")}]},
			"show": {"steps": [{"run": python("print(open('src/a.c').read())")}]},
			"greet": {"steps": [{"run": python("print('${config:fake.greeting} from ${config}')")}]}}

	def orphan_marker(self):
		return "ied-step-" + os.path.basename(self.root)

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		await self.connect()

	async def connect(self):
		self.cookie = self.server.sign_in()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		await self.client.sub(["tasks:w1", "diag:w1"])
		await self.client.next_frame("tasks:w1")
		await self.client.next_frame("diag:w1")

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def run_task(self, **params):
		started = await self.client.request("task.run", dict({"wsId": "w1"}, **params))
		topic = started["topic"]
		await self.client.sub([topic])
		frames = await self.client.wait_for(topic, lambda f: f.get("kind") == "run.end" or (f["t"] == "snap" and f["data"]["status"] in ("succeeded", "failed", "cancelled")), timeout=60)
		return started["runId"], frames

	def text_of(self, frames):
		return "".join(f["data"]["text"] for f in frames if f.get("kind") == "output") + "".join(f["data"].get("tail", "") for f in frames if f["t"] == "snap")

	async def test_the_offer_is_the_presets_under_the_project(self):
		offer = await self.client.request("task.list", {"wsId": "w1"})
		self.assertEqual(("Game", "Game", ["Game"]), (offer["project"], offer["startupProject"], offer["projects"]))
		self.assertEqual([("config", "Development", "solution", ["Debug", "Development"])], [(v["key"], v["value"], v["scope"], v["values"]) for v in offer["variables"]])
		tasks = {t["key"]: t for t in offer["tasks"]}
		self.assertEqual("Clean it", tasks["clean"]["label"], "the project's task replaced the preset's")
		self.assertEqual("build", tasks["build"]["slot"], "the preset's task stays")
		self.assertEqual(["config", "|", "build"], offer["toolbar"])
		self.assertEqual([], offer["problems"])

	async def test_a_preset_extends_another(self):
		"""docs/26: a preset reads its base first and overrides it by key; its problemMatcher reaches every step that names
		none, the base's included; a preset that extends itself is a problem, not a loop."""
		write(os.path.join(self.repo, "Lint.ied.proj"), {"version": 1, "name": "Lint", "root": ".", "kind": "other", "presets": ["fake.derived", "fake.loop"]})
		write(os.path.join(self.repo, "Game.ied.sln"), {"version": 1, "name": "Game", "projects": [{"file": "Game.ied.proj"}, {"file": "Lint.ied.proj"}]})
		offer = None
		for _ in range(50):
			try:
				offer = await self.client.request("task.list", {"wsId": "w1", "project": "Lint"})
				break
			except RpcFailure:
				await asyncio.sleep(0.1)
		tasks = {t["key"]: t for t in offer["tasks"]}
		self.assertEqual(["build", "clean", "lint"], sorted(tasks), "the base's tasks and its own")
		self.assertEqual(["config", "|", "lint"], offer["toolbar"], "its toolbar replaced the base's")
		self.assertEqual(["the task preset fake.loop extends itself"], offer["problems"])
		_, frames = await self.run_task(task="lint", project="Lint")
		frame = (await self.client.wait_for("diag:w1", lambda f: f.get("kind") == "diag.set" and f["data"]["source"] == "fake/Lint", timeout=10))[-1]["data"]
		self.assertEqual("lint says hi", frame["items"][0]["message"], "the preset's matcher on its step")

	async def test_selections_persist(self):
		chosen = await self.client.request("task.select", {"wsId": "w1", "key": "config", "value": "Debug"})
		self.assertEqual("Debug", chosen["variables"][0]["value"])
		changed = (await self.client.wait_for("tasks:w1", lambda f: f.get("kind") == "tasks.changed"))[-1]["data"]
		self.assertEqual("Debug", changed["offer"]["variables"][0]["value"], "every client hears of it")
		with open(os.path.join(self.repo, ".ied", "local", "selections.json"), encoding="utf-8") as f:
			self.assertEqual({"config": "Debug"}, json.load(f)["solution"])
		with self.assertRaises(RpcFailure) as refused:
			await self.client.request("task.select", {"wsId": "w1", "key": "config", "value": "Release"})
		self.assertEqual("bad_request", refused.exception.code)
		_, frames = await self.run_task(slot="build")
		self.assertIn("building Debug", self.text_of(frames), "a run uses the selection")

	async def test_a_run_streams_output_and_problems(self):
		await self.client.sub(["out:w1:build"])
		await self.client.next_frame("out:w1:build")
		run_id, frames = await self.run_task(task="check")
		end = frames[-1]["data"]
		self.assertEqual(("succeeded", 0), (end["status"], end["exitCode"]))
		self.assertIn("src/a.c (2:5) error: no b", self.text_of(frames))
		problems = {}
		while len(problems.get("src/a.c", ("", []))[1]) < 2:
			frame = (await self.client.wait_for("diag:w1", lambda f: f.get("kind") == "diag.set", timeout=10))[-1]["data"]
			problems[frame["path"]] = (frame["source"], frame["items"])
		source, items = problems["src/a.c"]
		self.assertEqual("fake/Game", source, "the matcher's source in the project")
		self.assertEqual([(1, 1, 4, "no b"), (2, 2, 0, "c")], sorted((i["severity"], i["range"]["start"]["line"], i["range"]["start"]["character"], i["message"]) for i in items), "both streams, 0-based")
		self.assertNotIn("src/missing.c", problems, "a file that is not there is not a diagnostic")
		appended = (await self.client.wait_for("out:w1:build", lambda f: f.get("kind") == "append" and "no b" in f["data"]["text"]))
		self.assertTrue(appended)
		runs = (await self.client.request("task.runs", {"wsId": "w1"}))["runs"]
		self.assertEqual((run_id, "check", "succeeded"), (runs[0]["runId"], runs[0]["task"], runs[0]["status"]))

	async def test_a_window_that_comes_back_sees_how_the_last_run_ended(self):
		snapshot = (await self.client.request("task.list", {"wsId": "w1"}))
		await self.run_task(task="chain")
		other = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		try:
			await other.sub(["tasks:w1"])
			data = (await other.next_frame("tasks:w1"))["data"]
			self.assertEqual(([], "chain", "failed"), (data["runs"], data["ended"]["task"], data["ended"]["status"]))
			self.assertEqual(snapshot["project"], data["offer"]["project"])
			self.assertEqual([], other.violations)
		finally:
			other.abort()

	async def test_a_setting_reaches_a_step(self):
		"""${config:key} is a setting's effective value; ${config} stays the configuration variable."""
		_, frames = await self.run_task(task="greet")
		self.assertIn("hello from Development", self.text_of(frames))
		await self.client.request("settings.set", {"tier": "solution", "wsId": "w1", "key": "fake.greeting", "value": "hi"})
		_, frames = await self.run_task(task="greet")
		self.assertIn("hi from Development", self.text_of(frames))

	async def test_a_shell_string_runs_as_written(self):
		_, frames = await self.run_task(task="shell")
		text = self.text_of(frames)
		self.assertIn("hello big world", text, "a batch file with a quoted argument")
		self.assertIn('"kept & quoted"', text, "quotes reach the shell as written")

	async def test_a_matcher_finds_files_under_its_roots(self):
		await self.run_task(task="checkSrc")
		frame = (await self.client.wait_for("diag:w1", lambda f: f.get("kind") == "diag.set" and f["data"]["source"] == "fake-src/Game", timeout=10))[-1]["data"]
		self.assertEqual(("src/a.c", 2, "named from src"), (frame["path"], frame["items"][0]["range"]["start"]["line"], frame["items"][0]["message"]))

	async def test_a_failed_step_skips_what_needs_success(self):
		_, frames = await self.run_task(task="chain")
		end = [f for f in frames if f.get("kind") == "run.end"][-1]["data"]
		self.assertEqual(("failed", 3), (end["status"], end["exitCode"]))
		steps = {f["data"]["step"]: f["data"]["status"] for f in frames if f.get("kind") == "step.end"}
		self.assertEqual({0: "failed", 1: "skipped", 2: "ok", 3: "ok"}, steps, "always steps run; a nested task's step too")
		text = self.text_of(frames)
		self.assertTrue("cleanup" in text and "project clean" in text and "skipped" not in text, text)

	async def test_cancel_kills_the_whole_tree(self):
		started = await self.client.request("task.run", {"wsId": "w1", "task": "forever"})
		await self.client.sub([started["topic"]])
		await self.client.wait_for(started["topic"], lambda f: f.get("kind") == "output" and "ready" in f["data"]["text"], timeout=30)
		with self.assertRaises(RpcFailure) as refused:
			await self.client.request("task.run", {"wsId": "w1", "task": "forever"})
		self.assertEqual("conflict", refused.exception.code, "a single task runs once")
		with open(os.path.join(self.root, "grandchild.pid"), encoding="utf-8") as f:
			grandchild = int(f.read())
		await self.client.request("task.cancel", {"runId": started["runId"]})
		end = (await self.client.wait_for(started["topic"], lambda f: f.get("kind") == "run.end", timeout=20))[-1]["data"]
		self.assertEqual("cancelled", end["status"])
		for _ in range(50):
			listed = subprocess.run(["tasklist", "/FI", "PID eq %d" % grandchild], capture_output=True, text=True).stdout
			if str(grandchild) not in listed:
				break
			await asyncio.sleep(0.1)
		self.assertNotIn(str(grandchild), listed, "the step's own child died with it")

	def steps_alive(self):
		"""The ids of the sleepy task's steps still on the system, suspended ones too."""
		query = "Get-CimInstance Win32_Process -Filter \"Name like 'python%'\" | Where-Object { $_.CommandLine -like '*" + self.orphan_marker() + "*' } | ForEach-Object { $_.ProcessId }"
		listed = subprocess.run(["powershell", "-NoProfile", "-Command", query], capture_output=True, text=True).stdout
		return [int(line) for line in listed.split() if line.strip().isdigit()]

	async def test_a_server_killed_while_starting_steps_leaves_none_behind(self):
		"""BUG-309: a step's process is created inside its job, so a server that dies while it starts steps takes every
		one of them along; none is left suspended or running on its own."""
		try:
			for round in range(16):
				if round:
					self.client.abort()
					self.server.stop()
					self.server = IedServer(extra=("--home", self.home, "--workspace", os.path.join(self.repo, "Game.ied.sln")))
					self.assertTrue(self.server.started, self.server.error)
					await self.connect()
				starts = [asyncio.ensure_future(self.client.request("task.run", {"wsId": "w1", "task": "sleepy"})) for _ in range(24)]
				for _ in range(3):
					await asyncio.sleep(0)
				sent = time.perf_counter()
				while time.perf_counter() - sent < 0.002 * round:
					pass
				self.server.process.kill()
				self.server.process.wait(10)
				await asyncio.gather(*starts, return_exceptions=True)
				for _ in range(50):
					alive = self.steps_alive()
					if not alive:
						break
					await asyncio.sleep(0.1)
				self.assertEqual([], alive, "round %d: the steps died with the server" % round)
		finally:
			for pid in self.steps_alive():
				subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)

	async def test_unknown_variables_and_untrusted_workspaces_are_refused(self):
		with self.assertRaises(RpcFailure) as refused:
			await self.client.request("task.run", {"wsId": "w1", "task": "typo"})
		self.assertEqual("bad_request", refused.exception.code)
		self.assertIn("${nope}", refused.exception.message)
		with self.assertRaises(RpcFailure) as missing:
			await self.client.request("task.run", {"wsId": "w1", "slot": "debug"})
		self.assertEqual("not_found", missing.exception.code)
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": False})
		with self.assertRaises(RpcFailure) as untrusted:
			await self.client.request("task.run", {"wsId": "w1", "task": "clean"})
		self.assertEqual("forbidden", untrusted.exception.code)

	async def test_dirty_documents_are_saved_first(self):
		doc = await self.client.request("doc.open", {"wsId": "w1", "path": "src/a.c"})
		await self.client.request("doc.edit", {"docId": doc["docId"], "baseVersion": doc["version"], "changes": [{"range": {"start": {"line": 0, "col": 0}, "end": {"line": 0, "col": 0}}, "text": "// edited\n"}]})
		_, frames = await self.run_task(task="show")
		self.assertIn("// edited", self.text_of(frames), "the step read the saved file")
		with open(os.path.join(self.repo, "src", "a.c"), encoding="utf-8") as f:
			self.assertTrue(f.read().startswith("// edited"))


class FolderTaskTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-tasks-folder-")
		self.home = os.path.join(self.root, "home")
		self.folder = os.path.join(self.root, "tool")
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		write(os.path.join(self.home, "plugins", "fake", "ied-plugin.json"), FAKE_PLUGIN)
		write(os.path.join(self.home, "plugins", "fake", "settings.schema.json"), {"properties": {"fake.greeting": {"type": "string", "default": "hello"}}})
		write(os.path.join(self.folder, "fake.mk"), "all:\n")
		self.server = IedServer(extra=("--home", self.home, "--workspace", self.folder))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def test_a_folder_gets_the_presets_it_matches(self):
		offer = await self.client.request("task.list", {"wsId": "w1"})
		self.assertEqual(("tool", ["build", "clean"]), (offer["project"], sorted(t["key"] for t in offer["tasks"])))
