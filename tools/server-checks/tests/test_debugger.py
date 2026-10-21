"""The debugger (docs/25): a plugin's adapter started for a project's debug configuration, after its pre-launch task;
the workspace's breakpoints given to it before configurationDone and to every session when they change, bound and
verified per session; a stop arriving with its frames in workspace paths and 0-based lines (a file outside the
workspace as a named file: URI); revision-checked controls; inspection through the adapter; output to the Debug
source; the session outliving its client; refusals; an adapter that crashes."""

import json
import os
import shutil
import sys
import tempfile

from frames import FrameChecker
from iedclient import Client, RpcFailure
from server import REPO, IedServer, ServerTestCase

FAKE_DAP = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "fake_dap.py")
GAME = "line one\n\nline three\nline four\nline five\n"


def write(path, value):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8", newline="") as f:
		f.write(value if isinstance(value, str) else json.dumps(value, indent="\t"))


class DebuggerTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-dbg-")
		self.home = os.path.join(self.root, "home")
		self.repo = os.path.join(self.root, "Game")
		self.log = os.path.join(self.root, "dap.jsonl")
		self.outside = os.path.join(self.root, "outside", "lib.txt")
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		write(os.path.join(self.home, "plugins", "fake", "ied-plugin.json"), self.plugin())
		write(os.path.join(self.repo, "src", "game.txt"), GAME)
		write(self.outside, "a\nb\nc\nd\n")
		write(os.path.join(self.repo, "Game.iedproj"), {"version": 1, "name": "Game", "root": ".", "kind": "executable", "tasks": self.tasks(), "debug": self.debug()})
		write(os.path.join(self.repo, "Game.iedsln"), {"version": 1, "name": "Game", "projects": [{"file": "Game.iedproj"}]})
		self.server = IedServer(extra=("--home", self.home, "--workspace", os.path.join(self.repo, "Game.iedsln")))
		self.assertTrue(self.server.started, self.server.error)

	def plugin(self):
		return {"manifestVersion": 1, "id": "fake.dap", "name": "Fake debugger", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
			"contributes": {"dapProviders": [{"type": "fake", "label": "Fake", "command": sys.executable, "args": ["-I", FAKE_DAP, self.log]},
				{"type": "faketcp", "label": "Fake over TCP", "command": sys.executable, "args": ["-I", FAKE_DAP, self.log, "--connect", "${port}"], "transport": "tcp"}]}}

	def tasks(self):
		return {"build": {"steps": [{"run": [sys.executable, "-I", "-c", "print('built')"]}]},
			"broken": {"steps": [{"run": [sys.executable, "-I", "-c", "import sys; sys.exit(2)"]}]}}

	def debug(self):
		launch = {"type": "fake", "request": "launch", "program": "${projectRoot}/src/game.txt", "outside": self.outside.replace("\\", "/"), "exitCode": 7}
		return {"default": "Launch", "configurations": {
			"Launch": launch,
			"Entry": dict(launch, stopOnEntry=True),
			"Built": dict(launch, preLaunchTask="build"),
			"Broken": dict(launch, preLaunchTask="broken"),
			"Refused": dict(launch, refuse=True),
			"Tcp": dict(launch, type="faketcp", stopOnEntry=True),
			"Unknown": {"type": "nope", "request": "launch"}}}

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.cookie = self.server.sign_in()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		await self.client.sub(["dbg:w1", "bp:w1"])
		await self.client.next_frame("dbg:w1")
		await self.client.next_frame("bp:w1")

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def start(self, configuration=None):
		started = await self.client.request("dbg.start", {"wsId": "w1", "configuration": configuration} if configuration else {"wsId": "w1"})
		await self.client.sub([started["topic"]])
		return started["sessionId"], started["topic"]

	async def stopped(self, topic):
		return (await self.client.wait_for(topic, lambda f: f.get("kind") == "dbg.stopped", timeout=20))[-1]["data"]

	async def ended(self, topic):
		frames = await self.client.wait_for(topic, lambda f: f.get("kind") == "dbg.state" and f["data"]["state"] in ("ended", "failed"), timeout=20)
		return frames[-1]["data"]

	def requests(self):
		with open(self.log, encoding="utf-8") as f:
			return [json.loads(line) for line in f if line.strip() and "command" in line]

	async def test_breakpoints_bind_and_the_program_stops_there(self):
		await self.client.request("bp.set", {"wsId": "w1", "path": "src/game.txt", "breakpoints": [{"line": 1}, {"line": 40}]})
		_, topic = await self.start()
		stop = await self.stopped(topic)
		self.assertEqual(("stopped", "breakpoint"), (stop["session"]["state"], stop["session"]["stop"]["reason"]))
		top, caller = stop["frames"]
		self.assertEqual(("src/game.txt", 2, 0), (top["source"]["path"], top["line"], top["column"]), "the empty line's breakpoint bound to the next, 0-based")
		self.assertTrue(caller["source"]["uri"].startswith("file:///"), caller)
		opened = await self.client.request("doc.open", {"wsId": "w1", "uri": caller["source"]["uri"]})
		self.assertEqual((True, caller["source"]["uri"]), (opened["readOnly"], opened["path"]), "a frame outside the workspace opens, read-only")
		bound = (await self.client.wait_for("bp:w1", lambda f: f.get("kind") == "bp.bound", timeout=10))[-1]["data"]
		self.assertEqual([(1, True, 2), (40, False, 40)], [(b["line"], b["verified"], b["boundLine"]) for b in bound["breakpoints"]])
		commands = [r["command"] for r in self.requests()]
		self.assertEqual(["initialize", "launch", "setBreakpoints", "setExceptionBreakpoints", "configurationDone"], commands[:5])
		filters = [r for r in self.requests() if r["command"] == "setExceptionBreakpoints"][0]["arguments"]["filters"]
		self.assertEqual(["uncaught"], filters, "the adapter's default filters")
		launch = [r for r in self.requests() if r["command"] == "launch"][0]["arguments"]
		self.assertEqual(os.path.join(self.repo, "src", "game.txt").replace("\\", "/").lower(), launch["program"].replace("\\", "/").lower(), "the configuration's ${projectRoot}")

	async def test_an_adapter_over_tcp(self):
		session, topic = await self.start("Tcp")
		stop = await self.stopped(topic)
		self.assertEqual(("entry", "src/game.txt"), (stop["session"]["stop"]["reason"], stop["frames"][0]["source"]["path"]))
		await self.client.request("dbg.stop", {"sessionId": session})
		end = await self.ended(topic)
		self.assertEqual("ended", end["state"])

	async def test_controls_are_revision_checked(self):
		session, topic = await self.start("Entry")
		stop = await self.stopped(topic)
		revision = stop["session"]["revision"]
		self.assertEqual("entry", stop["session"]["stop"]["reason"])
		await self.client.request("dbg.next", {"sessionId": session, "threadId": 1, "revision": revision})
		with self.assertRaises(RpcFailure) as twice:
			await self.client.request("dbg.next", {"sessionId": session, "threadId": 1, "revision": revision})
		self.assertEqual("conflict", twice.exception.code, "two devices pressing Step at once step once")
		stepped = await self.stopped(topic)
		self.assertEqual(("step", 1), (stepped["session"]["stop"]["reason"], stepped["frames"][0]["line"]))
		self.assertGreater(stepped["session"]["revision"], revision + 1)
		with self.assertRaises(RpcFailure) as running:
			await self.client.request("dbg.pause", {"sessionId": session})
		self.assertEqual("conflict", running.exception.code, "pause wants a running program")

	async def test_inspection_asks_the_adapter(self):
		session, topic = await self.start("Entry")
		await self.stopped(topic)
		trace = await self.client.request("dbg.stackTrace", {"sessionId": session, "threadId": 1})
		self.assertEqual(["main", "caller"], [f["name"] for f in trace["stackFrames"]])
		scopes = await self.client.request("dbg.scopes", {"sessionId": session, "frameId": 1})
		self.assertEqual("Locals", scopes["scopes"][0]["name"])
		variables = await self.client.request("dbg.variables", {"sessionId": session, "variablesReference": scopes["scopes"][0]["variablesReference"]})
		self.assertEqual(("line", "1"), (variables["variables"][0]["name"], variables["variables"][0]["value"]))
		value = await self.client.request("dbg.evaluate", {"sessionId": session, "frameId": 1, "expression": "where", "context": "hover"})
		self.assertEqual("where = 1", value["result"])
		with self.assertRaises(RpcFailure) as unknown:
			await self.client.request("dbg.request", {"sessionId": session, "command": "readMemory", "arguments": {}})
		self.assertEqual("plugin_error", unknown.exception.code)

	async def test_output_goes_to_the_debug_source_and_the_session_ends(self):
		await self.client.sub(["out:w1:debug"])
		await self.client.next_frame("out:w1:debug")
		_, topic = await self.start()
		end = await self.ended(topic)
		self.assertEqual(("ended", 7), (end["state"], end["exitCode"]))
		printed = await self.client.wait_for("out:w1:debug", lambda f: f.get("kind") == "append" and "hello from game.txt" in f["data"]["text"], timeout=10)
		self.assertTrue(printed)
		sessions = (await self.client.request("dbg.sessions", {"wsId": "w1"}))["sessions"]
		self.assertEqual("ended", sessions[-1]["state"])

	async def test_the_pre_launch_task_runs_first(self):
		_, topic = await self.start("Built")
		end = await self.ended(topic)
		self.assertEqual("ended", end["state"])
		runs = (await self.client.request("task.runs", {"wsId": "w1"}))["runs"]
		self.assertEqual(("build", "succeeded"), (runs[0]["task"], runs[0]["status"]))
		_, broken = await self.start("Broken")
		failed = await self.ended(broken)
		self.assertEqual("failed", failed["state"])
		self.assertIn("pre-launch task broken failed", failed["problem"])

	async def test_refusals(self):
		offer = await self.client.request("task.list", {"wsId": "w1"})
		self.assertEqual(("Launch", ["Launch", "Entry", "Built", "Broken", "Refused", "Tcp", "Unknown"]), (offer["debug"]["default"], offer["debug"]["configurations"]), "the offer names them")
		with self.assertRaises(RpcFailure) as unknown:
			await self.client.request("dbg.start", {"wsId": "w1", "configuration": "Unknown"})
		self.assertEqual("not_found", unknown.exception.code)
		with self.assertRaises(RpcFailure) as missing:
			await self.client.request("dbg.start", {"wsId": "w1", "configuration": "Nope"})
		self.assertEqual("not_found", missing.exception.code)
		_, topic = await self.start("Refused")
		failed = await self.ended(topic)
		self.assertIn("could not be started", failed["problem"])
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": False})
		with self.assertRaises(RpcFailure) as untrusted:
			await self.client.request("dbg.start", {"wsId": "w1"})
		self.assertEqual("forbidden", untrusted.exception.code)

	async def test_breakpoints_persist_and_reach_a_running_session(self):
		session, topic = await self.start("Entry")
		await self.stopped(topic)
		await self.client.request("bp.set", {"wsId": "w1", "path": "src/game.txt", "breakpoints": [{"line": 3, "condition": "x > 1"}, {"line": 4, "enabled": False}]})
		bound = (await self.client.wait_for("bp:w1", lambda f: f.get("kind") == "bp.bound", timeout=10))[-1]["data"]
		self.assertEqual([(3, False)], [(b["line"], b["verified"]) for b in bound["breakpoints"]], "a disabled one is not sent")
		self.assertIn("takes no conditions", bound["breakpoints"][0]["message"])
		with open(os.path.join(self.repo, ".ied", "local", "breakpoints.json"), encoding="utf-8") as f:
			saved = json.load(f)["files"]["src/game.txt"]
		self.assertEqual([3, 4], [b["line"] for b in saved])
		listed = await self.client.request("bp.list", {"wsId": "w1"})
		self.assertEqual("src/game.txt", listed["files"][0]["path"])
		await self.client.request("watch.set", {"wsId": "w1", "expressions": ["line", " total "]})
		changed = (await self.client.wait_for("bp:w1", lambda f: f.get("kind") == "watch.changed", timeout=10))[-1]["data"]
		self.assertEqual(["line", "total"], changed["expressions"])
		with open(os.path.join(self.repo, ".ied", "local", "breakpoints.json"), encoding="utf-8") as f:
			self.assertEqual(["line", "total"], json.load(f)["watches"], "kept with the breakpoints")
		with self.assertRaises(RpcFailure) as blank:
			await self.client.request("watch.set", {"wsId": "w1", "expressions": [" "]})
		self.assertEqual("bad_request", blank.exception.code)
		with self.assertRaises(RpcFailure) as escape:
			await self.client.request("bp.set", {"wsId": "w1", "path": "../x.txt", "breakpoints": []})
		self.assertEqual("bad_request", escape.exception.code)

	async def test_a_session_outlives_its_client(self):
		_, topic = await self.start("Entry")
		await self.stopped(topic)
		self.client.abort()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		await self.client.sub([topic])
		data = (await self.client.next_frame(topic))["data"]
		self.assertEqual(("stopped", "src/game.txt"), (data["session"]["state"], data["frames"][0]["source"]["path"]))

	async def test_an_adapter_that_crashes_ends_its_session(self):
		session, topic = await self.start("Entry")
		await self.stopped(topic)
		with self.assertRaises(RpcFailure):
			await self.client.request("dbg.request", {"sessionId": session, "command": "fake/crash"})
		end = await self.ended(topic)
		self.assertEqual("ended", end["state"])
		with self.assertRaises(RpcFailure) as gone:
			await self.client.request("dbg.next", {"sessionId": session})
		self.assertEqual("conflict", gone.exception.code)
