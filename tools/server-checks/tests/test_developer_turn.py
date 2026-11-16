"""One nuke run in IED's checkout at a time across IED's windows (docs/28 "One nuke run at a time"): each window's
workspace server runs its chats' ied_rebuild and ied_gates, and the master runs Rebuild and relaunch; two at once would
write the same build folders and refresh the dev toolchain under each other. Here another window holds the checkout's
turn (its machine lock, IED::MachineLock: a claimed pipe name and a note saying what holds it): a chat's ied_gates waits
for it and says what it waited for, and so does the master's rebuild, in its log."""

import ctypes
import hashlib
import os
import shutil
import subprocess
import tempfile
import threading

from test_chat import ChatCase, call_step
from test_developer import BUILD_SECONDS, SourceCase, commit_all, fake_nuke, make_source_home

ERROR_INVALID_HANDLE = ctypes.c_void_p(-1).value


class HeldTurn:
	"""The checkout's nuke turn held from outside the server, as another IED window's server holds it: the pipe name
	(developer plugin's NukeTurn, MachineLockPipe) claimed as its first instance, and the note saying what holds it."""

	def __init__(self, checkout, what):
		name = "ied.developer.nuke:" + checkout.replace("\\", "/").lower().rstrip("/")
		self.pipe = "ied-lock-" + hashlib.sha256(name.encode("utf-8")).hexdigest()[:24]
		kernel = ctypes.windll.kernel32
		kernel.CreateNamedPipeW.restype = ctypes.c_void_p
		kernel.CreateNamedPipeW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32,
			ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
		first_instance_duplex = 0x00080000 | 0x00000003
		self.handle = kernel.CreateNamedPipeW("\\\\.\\pipe\\" + self.pipe, first_instance_duplex, 0, 255, 4096, 4096, 0, None)
		assert self.handle not in (None, ERROR_INVALID_HANDLE), "the turn could not be held (error %d)" % kernel.GetLastError()
		self.note = os.path.join(tempfile.gettempdir(), "ied-locks", self.pipe + ".txt")
		os.makedirs(os.path.dirname(self.note), exist_ok=True)
		with open(self.note, "w") as f:
			f.write("%s (process %d)" % (what, os.getpid()))

	def release(self):
		if self.handle is None:
			return
		try:
			os.remove(self.note)
		except OSError:
			pass
		ctypes.windll.kernel32.CloseHandle(ctypes.c_void_p(self.handle))
		self.handle = None

	def release_after(self, seconds):
		timer = threading.Timer(seconds, self.release)
		timer.start()
		return timer


class ToolTurnTests(SourceCase):
	def start_server(self):
		server = super().start_server()
		fake_nuke(self.home, "echo gates ran\nexit /b 0\n")
		return server

	async def test_ied_gates_waits_for_another_windows_run_and_says_so(self):
		held = HeldTurn(self.home, "ied_rebuild from a chat in Elsewhere")
		timer = held.release_after(3)
		try:
			chat_id, _ = await self.chat(mode="agent", permission="auto")
			self.model.queue(call_step("ied_gates", {}, "g1"), call_step("finish", {"summary": "gated", "verification": "ran"}, "f1"))
			await self.turn(chat_id, "run the gates")
		finally:
			timer.join()
			held.release()
		result = self.tool_results()["g1"]
		self.assertIn("(Waited ", result, "it waited for the other window's run")
		self.assertIn("for ied_rebuild from a chat in Elsewhere (process %d) - another IED window - before this run started." % os.getpid(), result)
		self.assertIn("nuke gates succeeded", result, "then ran: %s" % result)
		self.assertIn("gates ran", result)

	async def test_a_free_turn_is_taken_at_once_and_said_nowhere(self):
		chat_id, _ = await self.chat(mode="agent", permission="auto")
		self.model.queue(call_step("ied_gates", {}, "g1"), call_step("finish", {"summary": "gated", "verification": "ran"}, "f1"))
		await self.turn(chat_id, "run the gates")
		result = self.tool_results()["g1"]
		self.assertNotIn("Waited", result)
		self.assertIn("nuke gates succeeded", result)


class RebuildTurnTests(ChatCase):
	def start_server(self):
		make_source_home(self.home)
		shutil.rmtree(os.path.join(self.home, ".git"))
		subprocess.run(["git", "init", "-q", self.home], check=True, capture_output=True)
		fake_nuke(self.home, "echo compiling ied-server\nexit /b 0\n")
		commit_all(self.home, "first")
		return super().start_server()

	async def test_the_rebuild_waits_for_another_windows_run_in_its_log(self):
		await self.client.sub(["ied.developer/state"])
		await self.client.next_frame("ied.developer/state")
		held = HeldTurn(self.home, "ied_gates from a chat in Elsewhere")
		try:
			await self.client.request("ied.developer/rebuild", {})
			frames = await self.client.wait_for("ied.developer/state", lambda f: f.get("kind") == "build.lines" and any("waiting for" in line for line in f["data"]["lines"]), timeout=BUILD_SECONDS)
			waiting = [line for f in frames if f.get("kind") == "build.lines" for line in f["data"]["lines"] if "waiting for" in line]
			self.assertEqual(["waiting for ied_gates from a chat in Elsewhere (process %d) to end" % os.getpid()], waiting)
			lines = [line for f in frames if f.get("kind") == "build.lines" for line in f["data"]["lines"]]
			self.assertNotIn("nuke install-ied", lines, "nothing ran while the other held the checkout")
		finally:
			held.release()
		frames = await self.client.wait_for("ied.developer/state", lambda f: f.get("kind") == "changed" and f["data"]["build"]["state"] not in ("idle", "running"), timeout=BUILD_SECONDS)
		end = frames[-1]["data"]["build"]
		self.assertEqual(("succeeded", 0), (end["state"], end["exitCode"]))
		self.assertIn("nuke install-ied", end["lines"], "it ran once the turn was free")
