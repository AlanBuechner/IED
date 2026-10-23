"""The control channel and the `ied` CLI (docs/10 'Local control channel', F-SRV-2, F-SRV-7): one server per pipe
name, status / url / open / stop over newline-delimited JSON-RPC 2.0, launch URLs that sign in, the CLI on top; and
the server's own log (docs/12 "Logs"), which keeps each run's start, end and fatal error."""

import json
import os
import shutil
import subprocess
import tempfile
import unittest

from frames import FrameChecker
from iedclient import Client
from server import CLI_ENV, ControlPipe, IedServer, ServerTestCase, read_lines

TREE_SERVER = "server"


def server_log(server):
	return read_lines(os.path.join(server.data, "logs", "server.log"))


class ControlTests(ServerTestCase):
	def setUp(self):
		self.folder = tempfile.mkdtemp(prefix="ied-open-")
		with open(os.path.join(self.folder, "main.cf"), "w") as f:
			f.write("int32 main() { return 0; }\n")
		self.server = IedServer()
		self.assertTrue(self.server.started, self.server.error)
		self.pipe = ControlPipe(self.server.control)

	def tearDown(self):
		self.pipe.close()
		self.server.stop()
		shutil.rmtree(self.folder, ignore_errors=True)

	def result(self, method, params=None):
		reply = self.pipe.call(method, params)
		self.assertIn("result", reply, reply)
		return reply["result"]

	def test_one_server_per_pipe_name(self):
		second = IedServer(control=self.server.control)
		try:
			self.assertFalse(second.started)
			self.assertEqual(3, second.process.returncode)
			self.assertIn("another ied-server runs", second.error)
			self.assertIn("not started: another ied-server runs", server_log(second)[-1], "the log says why it did not start")
		finally:
			second.stop()

	def test_status(self):
		status = self.result("status")
		self.assertEqual(self.server.process.pid, status["pid"])
		self.assertEqual("http://127.0.0.1:%d/" % self.server.port, status["url"])
		self.assertEqual(0, status["clients"])
		self.assertEqual(1, len(status["workspaces"]))

	def test_url_signs_in_once(self):
		url = self.result("url")["url"]
		self.assertRegex(url, r"^http://127\.0\.0\.1:%d/\?t=[0-9a-f]{64}$" % self.server.port)
		token = url.rsplit("=", 1)[1]
		self.assertEqual(303, self.server.exchange(token).status)
		self.assertEqual(401, self.server.exchange(token).status)

	async def test_open_a_file_opens_its_folder_once(self):
		client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		await client.sub([TREE_SERVER])
		await client.next_frame(TREE_SERVER)
		opened = self.result("open", {"path": os.path.join(self.folder, "main.cf"), "line": 1, "col": 5})
		self.assertEqual(("w2", "main.cf"), (opened["wsId"], opened["path"]))
		self.assertTrue(opened["url"].endswith("#open=w2/main.cf:1:5"), opened["url"])
		frames = await client.wait_for(TREE_SERVER, lambda f: f["t"] == "evt" and f["kind"] == "workspace.opened", 10)
		self.assertEqual("w2", frames[-1]["data"]["wsId"])
		again = self.result("open", {"path": self.folder.upper()})
		self.assertEqual(("w2", ""), (again["wsId"], again["path"]), "the same folder, whatever its case, is the same workspace")
		read = await client.request("fs.read", {"wsId": "w2", "path": "main.cf"})
		self.assertIn("int32 main", read["text"])
		self.assertEqual([], client.violations)
		client.abort()

	def test_errors(self):
		self.assertEqual(-32601, self.pipe.call("nope")["error"]["code"])
		self.assertEqual(-32602, self.pipe.call("open", {})["error"]["code"])
		self.assertEqual(-32000, self.pipe.call("open", {"path": os.path.join(self.folder, "missing")})["error"]["code"])
		self.assertEqual(-32700, self.pipe.send_line("not json")["error"]["code"])
		self.assertEqual(-32600, self.pipe.send_line('{"id": 1, "method": "status"}')["error"]["code"])
		self.assertIn("result", self.pipe.call("status"), "the channel still works after refusals")

	async def test_stop_tells_clients_and_ends_the_server(self):
		client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		self.assertEqual({}, self.result("stop"))
		await client.wait_closed(10)
		self.assertEqual({"t": "bye", "reason": "shutdown"}, client.bye)
		self.assertEqual(0, self.server.process.wait(15))
		lines = server_log(self.server)
		pid = "[%d] " % self.server.process.pid
		self.assertRegex(lines[0], r"^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} \[\d+\] start: ied-server ", "each line: date, time, pid")
		self.assertTrue(all(pid in line for line in lines), lines)
		self.assertIn("listening: http://127.0.0.1:%d" % self.server.port, "\n".join(lines))
		self.assertTrue(lines[-2].endswith("stopped by the control channel"), lines)
		self.assertTrue(lines[-1].endswith("exit: the HTTP server stopped"), lines)

	async def test_a_crash_leaves_its_report_in_the_log(self):
		"""An exception nothing catches on a thread of the server ends it (C4's fatal error, exit code 1): the report,
		with the thread and the stack that threw, is in logs/server.log after the run's own lines."""
		client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.server.sign_in()})
		await client.request("testhooks.crash")
		self.assertEqual(1, self.server.process.wait(30))
		client.abort()
		text = "\n".join(server_log(self.server))
		self.assertIn("[%d] C4: fatal: an exception was thrown and not caught" % self.server.process.pid, text)
		self.assertIn('"testhooks.crash"', text, "the thread's name")
		self.assertRegex(text, r"\(\S*testhooks\.cf:\d+\)", "the line that threw, named from the PDB")


@unittest.skipUnless(os.environ.get(CLI_ENV), "needs --cli")
class CliTests(ServerTestCase):
	def setUp(self):
		self.server = IedServer()
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()

	def ied(self, *arguments, control=None, env=None):
		argv = [os.environ[CLI_ENV], "--control", control or self.server.control] + list(arguments)
		return subprocess.run(argv, capture_output=True, text=True, timeout=60, env=env)

	def test_status_url_and_open(self):
		status = self.ied("status")
		self.assertEqual(0, status.returncode, status.stderr)
		self.assertEqual(self.server.process.pid, json.loads(status.stdout)["result"]["pid"])
		url = self.ied("--no-browser", "url")
		self.assertRegex(url.stdout.strip(), r"^http://127\.0\.0\.1:%d/\?t=[0-9a-f]{64}$" % self.server.port)
		folder = tempfile.mkdtemp(prefix="ied-cli-")
		try:
			with open(os.path.join(folder, "a.txt"), "w") as f:
				f.write("x\n")
			opened = self.ied("--no-browser", os.path.join(folder, "a.txt") + ":3")
			self.assertEqual(0, opened.returncode, opened.stderr)
			self.assertTrue(opened.stdout.strip().endswith("#open=w2/a.txt:3"), opened.stdout)
		finally:
			shutil.rmtree(folder, ignore_errors=True)

	def test_no_server(self):
		gone = self.ied("status", control="ied-checks-nobody")
		self.assertEqual(1, gone.returncode)
		self.assertIn("no ied-server runs", gone.stderr)

	def test_usage(self):
		bad = self.ied("--bogus")
		self.assertEqual(2, bad.returncode)
		self.assertIn("usage: ied", bad.stderr)

	def test_starts_a_server_when_none_runs_and_stops_it(self):
		home = tempfile.mkdtemp(prefix="ied-cli-home-")
		env = dict(os.environ, LOCALAPPDATA=home)
		control = "ied-checks-started-" + os.path.basename(home)
		try:
			url = self.ied("--no-browser", "url", control=control, env=env)
			self.assertEqual(0, url.returncode, url.stderr)
			self.assertRegex(url.stdout.strip(), r"^http://127\.0\.0\.1:\d+/\?t=[0-9a-f]{64}$")
			self.assertTrue(os.path.exists(os.path.join(home, "IED", "auth")), "the started server keeps its state under LOCALAPPDATA")
			self.assertEqual(0, self.ied("status", control=control, env=env).returncode)
			self.assertEqual(0, self.ied("stop", control=control, env=env).returncode)
			self.assertEqual(1, self.ied("status", control=control, env=env).returncode, "stopped")
		finally:
			self.ied("stop", control=control, env=env)
			shutil.rmtree(home, ignore_errors=True)
