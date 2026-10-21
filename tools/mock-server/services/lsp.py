"""Language servers (docs/24): the mock runs none, so 'lsp:<wsId>' holds no servers, lsp.capabilities says no profile
serves a document, lsp.request answers not_found and lsp.restart knows no profile - what the real server says for a
workspace whose plugins contribute no lspProfiles."""

from rpc import RpcError


class LanguageServers:
	def __init__(self, server):
		self.server = server
		for ws_id in server.workspace.items:
			server.hub.add("lsp:" + ws_id, lambda: {"servers": []})
		server.rpc.register("lsp.request", self.m_request)
		server.rpc.register("lsp.capabilities", self.m_capabilities)
		server.rpc.register("lsp.status", self.m_status)
		server.rpc.register("lsp.restart", self.m_restart)

	async def m_request(self, call, p):
		self.server.docs.get(p.str("docId"))
		raise RpcError("not_found", "no language server serves this document")

	async def m_capabilities(self, call, p):
		self.server.docs.get(p.str("docId"))
		return {"profile": None}

	async def m_status(self, call, p):
		self.server.workspace.get(p.str("wsId"))
		return {"servers": []}

	async def m_restart(self, call, p):
		self.server.workspace.get(p.str("wsId"))
		raise RpcError("not_found", "no language server profile " + p.str("profile"))
