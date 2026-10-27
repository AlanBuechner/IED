"""A tab's sockets as the web client keeps them (docs/10 "Routing"): the master's, and one per workspace server the
tab talks to, through the master's /w/<wsId>/ws. Each request and subscription goes where the web client's router
(frontend/src/ws/router.js) sends it: a method or topic in the master's routes to the master; one naming a workspace
(params.wsId, or a topic key with a wsId among its parts) to that workspace's server; anything else to the default
workspace's server - the one the tab "shows", the first open workspace unless given. The routes come from the master's
server topic, read on a socket of the router's own."""

import asyncio
import re
import urllib.parse

import server  # noqa: F401 - puts tools/pylib and tools/conformance on the path
from iedclient import DEFAULT_TIMEOUT, Client, new_id

WORKSPACE_ID = re.compile(r"^w\d+(\.\d+)?$")


def serving(ws_id):
	"""The master's workspace whose server holds `ws_id`: a plugin-opened "<wsId>.<n>" is its server's."""
	return ws_id.split(".", 1)[0]


def topic_workspace(key):
	"""The wsId a topic key names among its parts ("settings:w2", "layout:w2:phone"), or None."""
	for part in key.split(":")[1:]:
		if WORKSPACE_ID.match(part):
			return part
	return None


class RoutedClient:
	def __init__(self, base, checker, headers, client_id, device_id, device_class, timeout):
		self.base = base
		self.checker = checker
		self.headers = headers
		self.client_id = client_id
		self.device_id = device_id
		self.device_class = device_class
		self.timeout = timeout
		self.master = None
		self.feed = None
		self.workspaces = {}
		self.default = None
		self.routes = {"methods": set(), "topics": set()}
		self.listed = []
		self.watcher = None

	@classmethod
	async def connect(cls, url, checker, client_id=None, device_id=None, device_class="desktop", resume=None, proto=1, timeout=DEFAULT_TIMEOUT, headers=None, workspace=None):
		split = urllib.parse.urlsplit(url)
		router = cls("%s://%s" % (split.scheme, split.netloc), checker, headers, client_id or new_id("c"), device_id or new_id("d"), device_class, timeout)
		router.master = await Client.connect(url, checker, router.client_id, router.device_id, device_class, resume, proto, timeout, headers)
		if router.master.welcome is None:
			return router
		router.feed = await Client.connect(url, checker, new_id("c"), router.device_id, device_class, None, proto, timeout, headers)
		await router.feed.sub(["server"])
		router.take(await router.feed.next_frame("server", timeout))
		router.watcher = asyncio.ensure_future(router.watch())
		router.default = workspace or (router.listed[0] if router.listed else None)
		if router.default:
			await router.workspace(router.default)
		return router

	def take(self, frame):
		data = frame.get("data") or {}
		if frame["t"] == "snap":
			self.listed = [ws["wsId"] for ws in data.get("workspaces", [])]
			self.set_routes(data.get("routes"))
		elif frame.get("kind") == "routes.changed":
			self.set_routes(data.get("routes"))
		elif frame.get("kind") == "workspace.opened" and data.get("wsId") not in self.listed:
			self.listed.append(data["wsId"])

	def set_routes(self, routes):
		if routes:
			self.routes = {"methods": set(routes.get("methods", [])), "topics": set(routes.get("topics", []))}

	async def watch(self):
		while True:
			self.take(await self.feed.topic("server").queue.get())

	async def workspace(self, ws_id):
		"""The socket to the server of `ws_id`, opened when first needed."""
		key = serving(ws_id)
		client = self.workspaces.get(key)
		# A socket whose workspace server went away is opened again, as the web client's reconnect does.
		if client is None or client.closed.is_set():
			url = "%s/w/%s/ws" % (self.base.replace("http", "ws", 1), key)
			headers = dict(self.headers or {})
			headers.setdefault("Origin", self.base.replace("ws", "http", 1))
			client = await Client.connect(url, self.checker, self.client_id, self.device_id, self.device_class, None, 1, max(self.timeout, 30), headers)
			self.workspaces[key] = client
		return client

	@property
	def primary(self):
		"""What the socket's own state means: the default workspace's server's socket, else the master's."""
		return self.workspaces.get(serving(self.default)) if self.default else self.master

	async def target(self, method=None, params=None, topic=None):
		if method is not None and method in self.routes["methods"]:
			return self.master
		if topic is not None and topic in self.routes["topics"]:
			return self.master
		ws_id = (params or {}).get("wsId") if isinstance(params, dict) else None
		if topic is not None:
			ws_id = topic_workspace(topic)
		if ws_id:
			return await self.workspace(ws_id)
		if self.default:
			return await self.workspace(self.default)
		return self.master

	def known(self, key):
		"""The socket a topic's frames arrive on (without opening one)."""
		if key in self.routes["topics"]:
			return self.master
		ws_id = topic_workspace(key) or self.default
		return self.workspaces.get(serving(ws_id)) if ws_id else self.master

	# --- iedclient.Client's interface --------------------------------------------------------------------------------

	@property
	def welcome(self):
		return self.primary.welcome if self.primary else None

	@property
	def bye(self):
		return self.primary.bye if self.primary else self.master.bye

	@property
	def close_code(self):
		return self.primary.close_code

	@property
	def closed(self):
		return self.primary.closed

	@property
	def violations(self):
		return [v for c in self.clients() for v in c.violations]

	@property
	def pongs(self):
		return self.primary.pongs

	def clients(self):
		return [c for c in [self.master, self.feed, *self.workspaces.values()] if c is not None]

	async def request(self, method, params=None, op_id=None, timeout=DEFAULT_TIMEOUT):
		return await (await self.target(method, params)).request(method, params, op_id, timeout)

	async def request_frame(self, method, params=None, op_id=None, timeout=DEFAULT_TIMEOUT, req_id=None):
		return await (await self.target(method, params)).request_frame(method, params, op_id, timeout, req_id)

	def start_request(self, method, params=None, op_id=None):
		ws_id = (params or {}).get("wsId") if isinstance(params, dict) else None
		if method in self.routes["methods"]:
			client = self.master
		else:
			client = self.workspaces.get(serving(ws_id or self.default or "")) or self.primary or self.master
		return client.start_request(method, params, op_id)

	async def sub(self, topics, start=None, timeout=DEFAULT_TIMEOUT):
		groups = {}
		for key in topics:
			client = await self.target(topic=key)
			groups.setdefault(id(client), (client, []))[1].append(key)
		answer = {}
		for client, keys in groups.values():
			answer.update(await client.sub(keys, {k: v for k, v in (start or {}).items() if k in keys} or None, timeout))
		return answer

	async def unsub(self, topics):
		for key in topics:
			await (await self.target(topic=key)).unsub([key])

	def topic(self, key):
		return self.known(key).topic(key)

	def cursors(self):
		return self.primary.cursors()

	async def next_frame(self, key, timeout=DEFAULT_TIMEOUT):
		return await self.known(key).next_frame(key, timeout)

	async def wait_for(self, key, predicate, timeout=DEFAULT_TIMEOUT):
		return await self.known(key).wait_for(key, predicate, timeout)

	def drain(self, key):
		return self.known(key).drain(key)

	async def send(self, frame):
		await self.primary.send(frame)

	def pause_reading(self):
		for client in self.clients():
			client.pause_reading()

	def resume_reading(self):
		for client in self.clients():
			client.resume_reading()

	async def wait_closed(self, timeout=DEFAULT_TIMEOUT):
		await self.primary.wait_closed(timeout)

	def abort(self):
		if self.watcher:
			self.watcher.cancel()
		for client in self.clients():
			client.abort()

	async def close(self):
		if self.watcher:
			self.watcher.cancel()
		for client in self.clients():
			await client.close()
