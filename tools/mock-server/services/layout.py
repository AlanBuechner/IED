"""Per-device-class layouts: 'layout:<wsId>:<deviceClass>' topics with revision-checked layout.set (docs/10, F-SHL-13).

The layout document belongs to the client (docs/15); the server keeps it verbatim. Revision 0 holds null: the client
starts from the default its contributions imply. The mock keeps layouts in memory only.
"""

import json

from rpc import RpcError

DEVICE_CLASSES = ("desktop", "tablet", "phone")
MAX_BYTES = 262144


class Layout:
	def __init__(self):
		self.revision = 0
		self.tree = None


class Layouts:
	def __init__(self, server):
		self.server = server
		self.items = {}
		for ws in server.workspace.items:
			for device_class in DEVICE_CLASSES:
				self._add(ws, device_class)
		server.rpc.register("layout.set", self.m_layout_set)

	def _add(self, ws_id, device_class):
		key = "layout:%s:%s" % (ws_id, device_class)
		layout = Layout()
		self.items[key] = layout
		self.server.hub.add(key, lambda: {"revision": layout.revision, "layout": layout.tree})

	async def m_layout_set(self, call, p):
		ws_id, device_class = p.str("wsId"), p.str("deviceClass")
		if ws_id not in self.server.workspace.items:
			raise RpcError("not_found", "no workspace '%s'" % ws_id)
		layout = self.items.get("layout:%s:%s" % (ws_id, device_class))
		if layout is None:
			raise RpcError("bad_request", "deviceClass must be desktop, tablet or phone")
		if "layout" not in p.raw:
			raise RpcError("bad_request", "missing param 'layout'")
		tree = p.raw["layout"]
		if tree is not None and not isinstance(tree, dict):
			raise RpcError("bad_request", "layout must be an object or null")
		expected = p.int("expectedRevision")
		if expected != layout.revision:
			raise RpcError("conflict", "the layout changed since that revision", {"revision": layout.revision})
		if len(json.dumps(tree, separators=(",", ":"))) > MAX_BYTES:
			raise RpcError("bad_request", "a layout is at most 256 KB")
		layout.tree = tree
		layout.revision += 1
		key = "layout:%s:%s" % (ws_id, device_class)
		self.server.hub.publish(key, "layout.changed", {"revision": layout.revision, "layout": layout.tree, "by": call.client_id})
		return {"revision": layout.revision}
