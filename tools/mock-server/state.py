"""The mock server object: config, session epoch, hub, rpc, sessions, fixture services, simulated restart."""

import asyncio
import dataclasses
import json
import secrets
import sys

from frames import FrameChecker
from hub import Hub
from rpc import Rpc
from services import chat, debug, docs, layout, lsp, outputs, plugins, presence, tasks, terminals, testhooks, workspace

MB = 1024 * 1024


@dataclasses.dataclass
class Config:
	host: str = "127.0.0.1"
	port: int = 8765
	static_dir: str = None
	allow_origins: list = dataclasses.field(default_factory=list)
	allow_hosts: list = dataclasses.field(default_factory=list)
	time_scale: float = 1.0
	idle_timeout: float = 60.0
	outbound_budget: int = 4 * MB
	coalesce_bytes: int = 256 * 1024
	validate_frames: bool = True
	frames_log: str = None
	tree_files: int = 0
	quiet: bool = False


class Clock:
	def __init__(self, scale):
		self.scale = scale

	async def sleep(self, seconds):
		await asyncio.sleep(max(0.0, seconds * self.scale))


class FrameLog:
	def __init__(self, path):
		self.file = open(path, "a", encoding="utf-8", newline="\n") if path else None

	def write(self, direction, text):
		if self.file:
			self.file.write(text + "\n")
			self.file.flush()


class Blobs:
	def __init__(self):
		self.items = {}

	def put(self, content_type, data):
		blob_id = "b-" + secrets.token_hex(8)
		self.items[blob_id] = (content_type, data)
		return blob_id

	def get(self, blob_id):
		return self.items.get(blob_id)


class Server:
	version = "0.1.0-mock"

	def __init__(self, config):
		self.config = config
		self.epoch = _new_epoch()
		self.frames = FrameChecker()
		self.hub = Hub(self.check_outgoing)
		self.rpc = Rpc()
		self.sessions = set()
		self.blobs = Blobs()
		self.clock = Clock(config.time_scale)
		self.frame_log = FrameLog(config.frames_log)
		self.presence = presence.Presence(self)
		self.workspace = workspace.Workspaces(self)
		self.docs = docs.Documents(self)
		self.layout = layout.Layouts(self)
		self.plugins = plugins.Plugins(self)
		self.outputs = outputs.Outputs(self)
		self.lsp = lsp.LanguageServers(self)
		self.tasks = tasks.Tasks(self)
		self.debug = debug.Debugger(self)
		self.terminals = terminals.Terminals(self)
		self.chat = chat.Chats(self)
		self.testhooks = testhooks.TestHooks(self)

	def check_outgoing(self, frame):
		if self.config.validate_frames:
			problems = self.frames.problems(frame)
			if problems:
				raise AssertionError("mock produced an invalid frame %s: %s" % (json.dumps(frame)[:200], problems[0]))

	def log(self, message):
		if not self.config.quiet:
			sys.stderr.write("[mock] %s\n" % message)
			sys.stderr.flush()

	async def restart(self):
		"""Simulates a server restart: new epoch, rings and opId cache lost, durable state kept, sockets dropped."""
		for service in (self.chat, self.tasks, self.terminals, self.testhooks):
			service.on_restart()
		self.epoch = _new_epoch()
		self.hub.clear_rings()
		self.rpc.ops.clear()
		sessions = list(self.sessions)
		await asyncio.gather(*(s.shutdown("server-restart") for s in sessions), return_exceptions=True)
		self.log("simulated restart; new epoch %s" % self.epoch)


def _new_epoch():
	return "e-" + secrets.token_hex(6)
