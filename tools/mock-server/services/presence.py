"""The 'server' topic: workspaces, connected clients (F-SRV-24), notifications."""

import time

from rpc import RpcError

LEVELS = ("info", "warning", "error", "attention")


class Presence:
	def __init__(self, server):
		self.server = server
		self.notifications = []
		self.next_note = 1
		server.hub.add("server", self.snapshot)
		server.rpc.register("server.info", self.info)
		server.rpc.register("server.notify", self.notify)
		server.rpc.register("server.dismiss", self.dismiss)

	def snapshot(self):
		return {
			"serverVersion": self.server.version,
			"workspaces": self.server.workspace.summaries(),
			"clients": [self._client(s) for s in self.server.sessions],
			"notifications": list(self.notifications),
		}

	def _client(self, session):
		return dict(session.device, clientId=session.client_id)

	def connected(self, session):
		self.server.hub.publish("server", "device.connected", self._client(session))

	def disconnected(self, session):
		self.server.hub.publish("server", "device.disconnected", self._client(session))

	async def info(self, call, p):
		return {
			"serverVersion": self.server.version,
			"sessionEpoch": self.server.epoch,
			"proto": 1,
			"mock": True,
			"testHooks": True,
		}

	async def notify(self, call, p):
		level = p.str("level", "info", required=False)
		if level not in LEVELS:
			raise RpcError("bad_request", "level must be one of %s" % ", ".join(LEVELS))
		note = {"id": "n%d" % self.next_note, "level": level, "text": p.str("text"), "ts": int(time.time() * 1000)}
		self.next_note += 1
		self.notifications.append(note)
		self.server.hub.publish("server", "notification", note)
		return {"id": note["id"]}

	async def dismiss(self, call, p):
		note_id = p.str("id")
		before = len(self.notifications)
		self.notifications = [n for n in self.notifications if n["id"] != note_id]
		if len(self.notifications) == before:
			raise RpcError("not_found", "no notification %r" % note_id)
		self.server.hub.publish("server", "notification.dismissed", {"id": note_id})
		return {}
