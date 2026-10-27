"""The master and its workspace servers (docs/01 "Processes", docs/10 "Routing", docs/11 "Workspace servers",
docs/12 "Restart behavior"): each open workspace runs in a server process of its own; the master lists them with
their servers' states and routes; a workspace server refuses every request the master did not send; one that is killed
is started again with its id while the other workspace goes on, and its chats resume; ws.restart and ws.close."""

import asyncio
import ctypes
import http.client
import json
import os
import shutil
import signal
import tempfile
import time

from frames import FrameChecker
from iedclient import Client, RpcFailure
from routed import RoutedClient
from server import ControlPipe, IedServer, ServerTestCase, read_lines
from test_chat import ChatCase, write

import ws


def alive(pid):
	"""Whether process `pid` still runs (os.kill(pid, 0) would end it on Windows)."""
	kernel = ctypes.windll.kernel32
	handle = kernel.OpenProcess(0x1000, False, pid)   # PROCESS_QUERY_LIMITED_INFORMATION
	if not handle:
		return False
	code = ctypes.c_ulong()
	try:
		return bool(kernel.GetExitCodeProcess(handle, ctypes.byref(code))) and code.value == 259   # STILL_ACTIVE
	finally:
		kernel.CloseHandle(handle)


def await_gone(pid, timeout=15):
	deadline = time.time() + timeout
	while alive(pid):
		if time.time() > deadline:
			raise AssertionError("process %d still runs" % pid)
		time.sleep(0.2)


def status(server):
	pipe = ControlPipe(server.control)
	try:
		return pipe.call("status")["result"]
	finally:
		pipe.close()


def workspace_status(server, ws_id):
	"""A workspace server's own pipe, which only its master uses: its pid and port."""
	pipe = ControlPipe("%s-%s" % (server.control, ws_id))
	try:
		return pipe.call("status")["result"]
	finally:
		pipe.close()


def await_servers(server, count, ready=True, timeout=60, check=None):
	"""The master's workspaces once `count` of them have a server in state ready (or the state `check` accepts)."""
	deadline = time.time() + timeout
	while True:
		listed = status(server)["workspaces"]
		states = [w.get("server", {}).get("state") for w in listed]
		good = [w for w in listed if (check(w) if check else w.get("server", {}).get("state") == "ready")]
		if len(good) >= count:
			return listed
		if time.time() > deadline:
			raise AssertionError("the workspace servers did not come up: %r" % states)
		time.sleep(0.1)


class TwoWorkspaceTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-servers-")
		self.alpha = os.path.join(self.root, "alpha")
		self.beta = os.path.join(self.root, "beta")
		write(os.path.join(self.alpha, "a.txt"), "alpha\n")
		write(os.path.join(self.beta, "b.txt"), "beta\n")
		self.server = IedServer(extra=("--workspace", self.alpha, "--workspace", self.beta))
		self.assertTrue(self.server.started, self.server.error)
		self.cookie = self.server.sign_in()

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def socket(self, ws_id):
		return await Client.connect("ws://127.0.0.1:%d/w/%s/ws" % (self.server.port, ws_id), FrameChecker(), headers={"Cookie": self.cookie}, timeout=30)

	async def test_two_workspaces_run_in_two_processes(self):
		listed = await_servers(self.server, 2)
		master = status(self.server)
		self.assertEqual(["w1", "w2"], [w["wsId"] for w in listed])
		pids = [w["server"]["pid"] for w in listed]
		self.assertEqual(2, len(set(pids)), "a process each")
		self.assertNotIn(master["pid"], pids, "neither is the master")
		for w in listed:
			self.assertEqual(0, w["server"]["restarts"])
			self.assertTrue(w["server"]["log"].endswith(".log"), w)
			self.assertEqual(w["server"]["pid"], workspace_status(self.server, w["wsId"])["pid"], "the server the master names is the one on its pipe")
		alpha = await self.socket("w1")
		beta = await self.socket("w2")
		try:
			self.assertNotEqual(alpha.welcome["sessionEpoch"], beta.welcome["sessionEpoch"], "two servers, two epochs")
			self.assertEqual(["a.txt"], [e["name"] for e in (await alpha.request("fs.list", {"wsId": "w1", "path": ""}))["entries"]])
			self.assertEqual(["b.txt"], [e["name"] for e in (await beta.request("fs.list", {"wsId": "w2", "path": ""}))["entries"]])
			self.assertEqual(["w1"], [w["wsId"] for w in (await alpha.request("ws.list"))["workspaces"]], "a workspace server has its own workspace alone")
			with self.assertRaises(RpcFailure) as other:
				await alpha.request("fs.list", {"wsId": "w2", "path": ""})
			self.assertEqual("not_found", other.exception.code)
		finally:
			alpha.abort()
			beta.abort()

	async def test_the_client_is_routed_by_the_masters_routes(self):
		await_servers(self.server, 2)
		master = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		try:
			await master.sub(["server"])
			snap = (await master.next_frame("server"))["data"]
			for method in ("ws.restart", "ws.close", "ws.openRecent", "auth.devices", "plugins.setEnabled"):
				self.assertIn(method, snap["routes"]["methods"])
			self.assertIn("server", snap["routes"]["topics"])
			self.assertIn("ai.usage", snap["routes"]["methods"], "the chat plugin's code in the master")
			self.assertIn("aiusage", snap["routes"]["topics"])
			self.assertNotIn("fs.list", snap["routes"]["methods"], "a workspace's methods are its server's")
			with self.assertRaises(RpcFailure) as refused:
				await master.request("fs.list", {"wsId": "w1", "path": ""})
			self.assertEqual("not_found", refused.exception.code)
			self.assertIn("/w/w1/ws", refused.exception.message, "the master says where to ask")
		finally:
			master.abort()
		routed = await RoutedClient.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		try:
			self.assertEqual(["b.txt"], [e["name"] for e in (await routed.request("fs.list", {"wsId": "w2", "path": ""}))["entries"]], "a wsId goes to its server")
			self.assertIn("claude", await routed.request("ai.usage"), "a master route goes to the master")
			self.assertEqual({"w1", "w2"}, set(routed.workspaces), "one socket per workspace server")
		finally:
			routed.abort()

	async def test_a_workspace_server_refuses_what_the_master_did_not_send(self):
		await_servers(self.server, 2)
		port = workspace_status(self.server, "w1")["port"]
		self.assertNotEqual(self.server.port, port)
		host = "127.0.0.1:%d" % port

		def ask(method, path, headers=None):
			connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
			sent = {"Host": host}
			sent.update(headers or {})
			connection.request(method, path, headers=sent)
			reply = connection.getresponse()
			body = reply.read()
			connection.close()
			return reply.status, body

		for path in ("/", "/healthz", "/ws", "/api/blob/b1", "/w/w1/files/a.txt", "/index.html", "/api/auth/state", "/?t=" + "0" * 64):
			for headers in ({}, {"Cookie": self.cookie}, {"X-IED-Secret": "0" * 64}, {"X-IED-Secret": ""}):
				status_code, body = ask("GET", path, headers)
				self.assertEqual(403, status_code, (path, headers, body))
		self.assertEqual(403, ask("GET", "/ws?secret=" + "1" * 64 + "&device=d-x", {"Cookie": self.cookie})[0], "a guessed secret on the upgrade")
		self.assertEqual(403, ask("PUT", "/api/upload", {"Cookie": self.cookie})[0])
		upgrade = await self.raw_upgrade(port)
		self.assertEqual(403, upgrade, "a WebSocket without the master's secret")
		self.assertEqual(401, self.server.request("GET", "/w/w1/ws").status, "the master's own check comes first")
		self.assertEqual(404, self.server.request("GET", "/w/w9/files/a.txt", headers={"Cookie": self.cookie}).status, "a workspace that is not open")

	async def raw_upgrade(self, port):
		try:
			sock = await ws.connect("ws://127.0.0.1:%d/ws" % port, headers={"Cookie": self.cookie})
		except ws.HandshakeError as e:
			return e.status
		sock.abort()
		return 101

	async def test_a_killed_workspace_server_comes_back_and_the_other_goes_on(self):
		listed = await_servers(self.server, 2)
		killed = listed[0]["server"]["pid"]
		alpha = await self.socket("w1")
		beta = await self.socket("w2")
		master = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		try:
			await master.sub(["server"])
			await master.next_frame("server")
			os.kill(killed, signal.SIGTERM)
			await alpha.wait_closed(10)
			self.assertEqual(1012, alpha.close_code, "the browser's side is told its server went away")
			self.assertEqual(["b.txt"], [e["name"] for e in (await beta.request("fs.list", {"wsId": "w2", "path": ""}))["entries"]], "the other workspace goes on, on the same socket")
			frames = await master.wait_for("server", lambda f: f.get("kind") == "workspace.changed" and f["data"]["wsId"] == "w1" and f["data"]["server"]["state"] == "ready", timeout=60)
			states = [f["data"]["server"]["state"] for f in frames if f.get("kind") == "workspace.changed" and f["data"]["wsId"] == "w1"]
			self.assertEqual("restarting", states[0], states)
			back = frames[-1]["data"]["server"]
			self.assertEqual((1, signal.SIGTERM), (back["restarts"], back["exitCode"]), back)
			self.assertNotEqual(killed, back["pid"])
			self.assertTrue(beta.closed.is_set() is False, "the other workspace's socket never dropped")
			again = await self.socket("w1")
			try:
				self.assertNotEqual(alpha.welcome["sessionEpoch"], again.welcome["sessionEpoch"], "a new server, a new epoch: the client resumes by snapshot")
				self.assertEqual(["a.txt"], [e["name"] for e in (await again.request("fs.list", {"wsId": "w1", "path": ""}))["entries"]])
				self.assertEqual(["w1"], [w["wsId"] for w in (await again.request("ws.list"))["workspaces"]], "it keeps its id")
			finally:
				again.abort()
			log = "\n".join(read_lines(back["log"]))
			self.assertIn("restarted after: its last run ended: code %d" % signal.SIGTERM, log)
			master_log = "\n".join(read_lines(os.path.join(self.server.data, "logs", "server.log")))
			self.assertIn("workspace server w1 exited unasked (code %d)" % signal.SIGTERM, master_log)
		finally:
			alpha.abort()
			beta.abort()
			master.abort()

	async def test_a_crashed_workspace_server_leaves_its_report_and_comes_back(self):
		"""An exception nothing catches ends a workspace server (C4's fatal error, exit code 1): its report is in its own
		log, the master's says how it ended, and the master starts it again; the master itself runs on."""
		await_servers(self.server, 2)
		alpha = await self.socket("w1")
		try:
			await alpha.request("testhooks.crash")
			await alpha.wait_closed(30)
		finally:
			alpha.abort()
		listed = await_servers(self.server, 1, check=lambda w: w["wsId"] == "w1" and w["server"]["state"] == "ready" and w["server"]["restarts"] == 1)
		back = [w for w in listed if w["wsId"] == "w1"][0]["server"]
		self.assertEqual(1, back["exitCode"])
		self.assertIsNone(self.server.process.poll(), "the master runs on")
		log = "\n".join(read_lines(back["log"]))
		self.assertIn("C4: fatal: an exception was thrown and not caught", log, "the crash report, in the workspace server's own log")
		self.assertIn('"testhooks.crash"', log)
		self.assertIn("restarted after: its last run ended: code 1", log)

	async def test_three_exits_within_a_minute_leave_it_failed_until_restarted(self):
		for attempt in range(3):
			listed = await_servers(self.server, 2)
			os.kill(listed[0]["server"]["pid"], signal.SIGTERM)
			if attempt < 2:
				await_servers(self.server, 1, check=lambda w: w["wsId"] == "w1" and w["server"]["state"] == "ready" and w["server"]["restarts"] == attempt + 1)
		failed = await_servers(self.server, 1, check=lambda w: w["wsId"] == "w1" and w["server"]["state"] == "failed")
		w1 = [w for w in failed if w["wsId"] == "w1"][0]["server"]
		self.assertIn("code %d" % signal.SIGTERM, w1["problem"])
		self.assertEqual(503, self.server.request("GET", "/w/w1/files/a.txt", headers={"Cookie": self.cookie}).status, "a failed server is not waited for")
		master = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		try:
			summary = await master.request("ws.restart", {"wsId": "w1"})
			self.assertEqual("w1", summary["wsId"])
		finally:
			master.abort()
		await_servers(self.server, 2)

	async def test_restart_and_close(self):
		listed = await_servers(self.server, 2)
		first = listed[1]["server"]["pid"]
		master = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		try:
			await master.sub(["server"])
			await master.next_frame("server")
			await master.request("ws.restart", {"wsId": "w2"})
			frames = await master.wait_for("server", lambda f: f.get("kind") == "workspace.changed" and f["data"]["wsId"] == "w2" and f["data"]["server"]["state"] == "ready", timeout=60)
			back = frames[-1]["data"]["server"]
			self.assertNotEqual(first, back["pid"])
			self.assertEqual(1, back["restarts"])
			self.assertNotIn("problem", back, "a restart someone asked for is no failure")
			await master.request("ws.close", {"wsId": "w2"})
			await master.wait_for("server", lambda f: f.get("kind") == "workspace.closed" and f["data"]["wsId"] == "w2", timeout=30)
			self.assertEqual(["w1"], [w["wsId"] for w in (await master.request("ws.list"))["workspaces"]])
			self.assertEqual(404, self.server.request("GET", "/w/w2/files/b.txt", headers={"Cookie": self.cookie}).status)
			await_gone(back["pid"])
			master.drain("server")
			await master.sub(["server"])
			recent = (await master.wait_for("server", lambda f: f["t"] == "snap"))[-1]["data"]["recent"]
			folded = lambda path: os.path.normcase(os.path.realpath(path))
			self.assertIn(folded(self.beta), [folded(r["path"]) for r in recent], "a closed workspace stays among the recent projects")
		finally:
			master.abort()

	def test_stop_ends_every_workspace_server(self):
		listed = await_servers(self.server, 2)
		pids = [w["server"]["pid"] for w in listed]
		pipe = ControlPipe(self.server.control)
		pipe.call("stop")
		pipe.close()
		self.server.process.wait(30)
		for pid in pids:
			await_gone(pid)
		self.assertIn("stopped (code 0)", "\n".join(read_lines(os.path.join(self.server.data, "logs", "server.log"))), "each was asked and stopped")


class ResumeAfterKillTests(ChatCase):
	"""A chat whose workspace server is killed mid-turn goes on by itself once the master has started it again (docs/12
	"Restart behavior"), while a second workspace's server never notices."""

	def start_server(self):
		self.other = os.path.join(self.root, "Other")
		write(os.path.join(self.other, "readme.txt"), "other\n")
		return IedServer(data=self.data, extra=("--home", self.home, "--workspace", self.repo, "--workspace", self.other))

	async def test_a_chat_resumes_after_its_workspace_server_is_killed(self):
		chat_id, topic = await self.chat(mode="agent")
		await self.use_model({}, script="long-stream")
		await self.client.request("chat.send", {"chatId": chat_id, "text": "work"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "segment.delta", timeout=20)
		other = await self.client.workspace("w2")
		before = other.welcome["sessionEpoch"]
		killed = [w for w in status(self.server)["workspaces"] if w["wsId"] == "w1"][0]["server"]["pid"]
		os.kill(killed, signal.SIGTERM)
		await_servers(self.server, 1, check=lambda w: w["wsId"] == "w1" and w["server"]["state"] == "ready" and w["server"]["restarts"] == 1)
		self.assertEqual(["readme.txt"], [e["name"] for e in (await other.request("fs.list", {"wsId": "w2", "path": ""}))["entries"]])
		self.assertEqual(before, other.welcome["sessionEpoch"], "the other workspace's server did not restart")
		self.assertFalse(other.closed.is_set())
		deadline = time.time() + 30
		while True:
			history = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"]
			if len(history) == 2:
				break
			self.assertLess(time.time(), deadline, "the cut-off turn did not go on: %r" % [t["stop"] for t in history])
			await asyncio.sleep(0.25)
		self.assertEqual("interrupted", history[0]["stop"])
		self.assertEqual("interrupted", history[1]["user"].get("resumed"), "IED started it, after its server came back")
		await self.client.request("chat.stop", {"chatId": chat_id})
