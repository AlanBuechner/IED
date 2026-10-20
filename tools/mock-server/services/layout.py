"""Per-device-class layouts: 'layout:<wsId>:<deviceClass>' topics with revision-checked layout.set (F-SRV-23/25).

The layout tree is opaque to the server; the defaults below are only a plausible starting point for the frontend.
"""

from rpc import RpcError

DEVICE_CLASSES = ("desktop", "tablet", "phone")


def default_layout(device_class):
	if device_class == "phone":
		return {"kind": "stack", "views": ["chat", "explorer", "editor", "output"], "active": "chat"}
	return {
		"kind": "split", "dir": "row", "sizes": [0.2, 0.55, 0.25],
		"children": [
			{"kind": "tabs", "views": ["explorer", "git"], "active": "explorer"},
			{"kind": "split", "dir": "column", "sizes": [0.7, 0.3], "children": [
				{"kind": "tabs", "views": ["editor"], "active": "editor"},
				{"kind": "tabs", "views": ["output", "errors", "terminal"], "active": "output"},
			]},
			{"kind": "tabs", "views": ["chat"], "active": "chat"},
		],
	}


class Layout:
	def __init__(self, device_class):
		self.revision = 1
		self.tree = default_layout(device_class)


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
		layout = Layout(device_class)
		self.items[key] = layout
		self.server.hub.add(key, lambda: {"revision": layout.revision, "layout": layout.tree})

	async def m_layout_set(self, call, p):
		key = "layout:%s:%s" % (p.str("wsId"), p.str("deviceClass"))
		layout = self.items.get(key)
		if layout is None:
			raise RpcError("not_found", "no layout %r" % key)
		expected = p.int("expectedRevision")
		if expected != layout.revision:
			raise RpcError("conflict", "layout changed since revision %d" % expected, {"revision": layout.revision})
		layout.tree = p.obj("layout")
		layout.revision += 1
		self.server.hub.publish(key, "layout.changed", {"revision": layout.revision, "layout": layout.tree, "by": call.client_id})
		return {"revision": layout.revision}
