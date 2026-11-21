"""The config server (docs/35): a master with --config-root serves its folder's settings, client plugins and skills to
other IEDs through /config/v1/ with tokens of its own (reader, admin; access.json keeps their SHA-256 alone), wakes a
waiting state request when what it serves changes, takes deploys (staged files committed with their checked list), and
runs the C4 server plugins of its server-plugins/ folder, whose IED::Shared::Publish values join the served settings
and which reload when they are deployed again. A client master follows it with configServer.url and configServer.token
and mirrors it into <app data>/shared/: the Shared tier under the app's, shared plugins, shared skills; a config server
that cannot be reached or refuses the token leaves the mirror as it was and says so (the server topic, a notification)."""

import asyncio
import hashlib
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

from frames import FrameChecker
from iedclient import RpcFailure
from routed import RoutedClient
from server import EXECUTABLE_ENV, REPO, IedServer, ServerTestCase, read_lines

DEPLOY = os.path.join(REPO, "tools", "config-server", "deploy.py")
COMPILE_SECONDS = 180
SCHEMA = {
	"properties": {
		"sample.models": {"type": "object", "default": {}, "x-ied-merge": "merge", "x-ied-tiers": ["app"]},
		"sample.theme": {"enum": ["dark", "darker", "light"], "default": "dark", "x-ied-tiers": ["app"]},
		"sample.width": {"type": "integer", "default": 4},
	}
}
DEMO_SCHEMA = {"properties": {"demo.greeting": {"type": "string", "default": "hi", "x-ied-tiers": ["app"]}}}
SKILL = "---\nname: nas-deploy\ndescription: How services are deployed on the NAS.\n---\n\nUse the NAS's deploy tools.\n"

# A server plugin: publishes a setting for every IED that follows the config server, or logs why it cannot.
MODELS = r'''include "api/Plugin.ch"
include "api/Shared.ch"

namespace Models
{
	void Activate(IED::PluginContext& context)
	{
		Core::String problem;
		if (IED::Shared::Publish(Core::StringView("sample.models"), Core::StringView("{\"spark\":{\"size\":VERSION}}"), problem)) context.Log(Core::StringView("models published"));
		else context.Log(problem.View());
	}
}
'''


def write(path, text):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8", newline="") as f:
		f.write(text)


def write_json(path, value):
	write(path, json.dumps(value, indent="\t"))


def models_plugin(folder, version, broken=False):
	write_json(os.path.join(folder, "ied-plugin.json"), {
		"manifestVersion": 1, "id": "test.models", "name": "Models", "version": "1.0.%d" % version, "ied": {"api": "^1"},
		"runtime": {"kind": "c4", "entry": "Models::Activate", "servers": ["master"]}})
	code = MODELS.replace("VERSION", str(version))
	if broken:
		code += "\nnamespace Models { int32 Broken() { return missing_name; } }\n"
	write(os.path.join(folder, "code", "Models.cf"), code)
	return folder


def demo_plugin(folder, greeting_default="hi"):
	schema = json.loads(json.dumps(DEMO_SCHEMA))
	schema["properties"]["demo.greeting"]["default"] = greeting_default
	write_json(os.path.join(folder, "ied-plugin.json"), {
		"manifestVersion": 1, "id": "shared.demo", "name": "Shared demo", "version": "1.0.0", "ied": {"api": "^1"},
		"runtime": {"kind": "declarative"}, "contributes": {"settings": {"schema": "settings.schema.json", "page": "Demo"}}})
	write_json(os.path.join(folder, "settings.schema.json"), schema)
	return folder


def free_port():
	with socket.socket() as s:
		s.bind(("127.0.0.1", 0))
		return s.getsockname()[1]


class ConfigServerTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-config-")
		self.home = os.path.join(self.root, "home")
		self.config = os.path.join(self.root, "config")
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		sample = os.path.join(self.home, "plugins", "sample")
		write_json(os.path.join(sample, "ied-plugin.json"), {"manifestVersion": 1, "id": "sample", "name": "Sample", "version": "1.0.0", "ied": {"api": "^1"},
			"runtime": {"kind": "declarative"}, "contributes": {"settings": {"schema": "settings.schema.json", "page": "Sample"}}})
		write_json(os.path.join(sample, "settings.schema.json"), SCHEMA)
		self.servers = []
		self.clients = []
		self.reader = self.make_token("reader", "laptop")
		self.admin = self.make_token("admin", "deploy")

	def tearDown(self):
		for server in self.servers:
			server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncTearDown(self):
		for client in self.clients:
			self.assertEqual([], client.violations)
			client.abort()

	def make_token(self, role, name):
		made = subprocess.run([os.environ[EXECUTABLE_ENV], "--config-root", self.config, "--config-token", role, name, "--data", os.path.join(self.root, "token-data")],
			capture_output=True, text=True, timeout=60)
		self.assertEqual(0, made.returncode, made.stderr)
		token = made.stdout.strip()
		self.assertRegex(token, "^[0-9a-f]{64}$", "the token is printed once, alone on stdout")
		return token

	def start(self, name, extra=(), env=None, port=0):
		server = IedServer(data=os.path.join(self.root, name + "-data"), port=port, extra=("--home", self.home, "--app-data", os.path.join(self.root, name + "-app")) + tuple(extra), env=env)
		self.assertTrue(server.started, server.error)
		self.servers.append(server)
		return server

	def start_config(self, port=0):
		self.config_server = self.start("config", ("--config-root", self.config), port=port)
		self.url = "http://127.0.0.1:%d/" % self.config_server.port
		return self.config_server

	def start_client(self, name="client", url=None, token=None):
		app = os.path.join(self.root, name + "-app")
		settings = os.path.join(app, "settings.json")
		if not os.path.exists(settings):
			write_json(settings, {"configServer.url": url or self.url, "configServer.token": {"env": "IED_CONFIG_TOKEN"}})
		return self.start(name, env={"IED_CONFIG_TOKEN": token or self.reader}), app

	async def connect(self, server):
		# The printed launch token signs in once: each server's cookie is kept for its later sockets.
		if not getattr(server, "cookie", None):
			server.cookie = server.sign_in()
		client = await RoutedClient.connect(server.ws_url, FrameChecker(), headers={"Cookie": server.cookie})
		self.clients.append(client)
		return client

	def api(self, method, path, token=None, body=None, server=None, headers=None):
		sent = {"Authorization": "Bearer " + token} if token else {}
		sent.update(headers or {})
		if isinstance(body, (dict, list)):
			body = json.dumps(body).encode("utf-8")
		return (server or self.config_server).request(method, "/config/v1/" + path, body, sent)

	def state(self, since=None, wait=None):
		path = "state" + ("?since=%s&wait=%d" % (since, wait) if since else "")
		reply = self.api("GET", path, self.reader)
		self.assertEqual(200, reply.status, reply.text())
		return json.loads(reply.body)

	def deploy(self, kind, folder, expect=0):
		done = subprocess.run([sys.executable, "-I", DEPLOY, kind, folder, "--server", self.url, "--token", self.admin], capture_output=True, text=True, timeout=COMPILE_SECONDS + 60)
		self.assertEqual(expect, done.returncode, done.stdout + done.stderr)
		return done.stdout + done.stderr

	async def until(self, what, check, seconds=30):
		deadline = time.monotonic() + seconds
		while True:
			value = await check()
			if value:
				return value
			if time.monotonic() > deadline:
				self.fail("%s: not within %d s" % (what, seconds))
			await asyncio.sleep(0.25)

	async def values(self, client):
		state = await client.request("settings.get", {})
		return {k: (v["value"], v["source"]) for k, v in state["values"].items()}

	async def followed(self, client):
		await client.sub(["server"])
		frame = await client.next_frame("server")
		return frame["data"].get("configServer")

	async def follow_state(self, client, wanted, seconds=60):
		"""The server topic's configServer once its state is `wanted`."""
		deadline = time.monotonic() + seconds
		status = await self.followed(client)
		while not status or status["state"] != wanted:
			left = deadline - time.monotonic()
			if left <= 0:
				self.fail("the config server's state never became %s: %s" % (wanted, status))
			frame = await client.next_frame("server", left)
			if frame.get("kind") == "configServer.changed":
				status = frame["data"]
		return status

	# --- Tokens -------------------------------------------------------------------------------------------------------

	async def test_tokens_guard_the_api(self):
		self.start_config()
		with open(os.path.join(self.config, "access.json"), encoding="utf-8") as f:
			kept = f.read()
		self.assertNotIn(self.reader, kept, "access.json keeps hashes, never a token")
		self.assertEqual({"laptop": "reader", "deploy": "admin"}, {t["name"]: t["role"] for t in json.loads(kept)["tokens"]})
		made = [json.loads(line) for line in read_lines(os.path.join(self.root, "token-data", "logs", "security.log"))]
		self.assertEqual(["laptop", "deploy"], [e["name"] for e in made if e["event"] == "config-token-made"], "the security log names each token made")
		self.assertFalse(any(self.reader in json.dumps(e) for e in made), "and never holds one")
		none = self.api("GET", "state")
		self.assertEqual((401, "unauthenticated"), (none.status, json.loads(none.body)["error"]), "no token")
		self.assertIn("Bearer", none.header("WWW-Authenticate"))
		wrong = self.api("GET", "state", "0" * 64)
		self.assertEqual(401, wrong.status, "an unknown token")
		self.assertEqual(401, self.api("GET", "state", headers={"Authorization": "Basic " + self.reader}).status, "not as a bearer token")
		self.assertEqual(200, self.api("GET", "state", self.reader, headers={"Host": "nas.lan:8780"}).status, "a reader reads, whatever name the server is reached by")
		refused = self.api("PUT", "settings", self.reader, {"sample.theme": "light"})
		self.assertEqual((403, "forbidden"), (refused.status, json.loads(refused.body)["error"]), "a reader does not write")
		self.assertEqual(403, self.api("GET", "server-plugins", self.reader).status, "nor sees the server plugins")
		self.assertEqual(200, self.api("PUT", "settings", self.admin, {"sample.theme": "light"}).status, "an admin writes")
		self.assertEqual(400, self.api("PUT", "settings", self.admin, b"[1, 2").status, "settings.json is a JSON object")
		self.assertEqual({"sample.theme": "light"}, self.state()["settings"])
		self.assertEqual(401, self.config_server.request("GET", "/ws").status, "the web client still needs its own sign-in")
		replaced = self.make_token("reader", "laptop")
		await self.until("a token made while the server runs is taken", lambda: asyncio.sleep(0, self.api("GET", "state", replaced).status == 200))
		self.assertEqual(401, self.api("GET", "state", self.reader).status, "making a token again with its name revokes the old one")
		other = self.start("plain")
		self.assertEqual(404, self.api("GET", "state", self.admin, server=other).status, "a server without --config-root serves no configuration")
		security = [json.loads(line) for line in read_lines(os.path.join(self.root, "config-data", "logs", "security.log"))]
		self.assertIn(("config-settings-replaced", "deploy"), [(e["event"], e.get("by")) for e in security], "an admin's write is in the security log, by its token's name")

	# --- State and long poll --------------------------------------------------------------------------------------------

	async def test_a_waiting_state_request_wakes_on_a_change(self):
		self.start_config()
		first = self.state()
		self.assertEqual({"settings": {}, "plugins": [], "skills": []}, {k: first[k] for k in ("settings", "plugins", "skills")})
		started = time.monotonic()
		same = await asyncio.to_thread(self.state, first["version"], 1)
		self.assertEqual(first["version"], same["version"], "nothing changed: the same state once the wait is over")
		self.assertGreater(time.monotonic() - started, 0.8, "it waited")
		answer = {}

		def wait():
			answer["state"] = self.state(first["version"], 30)
			answer["after"] = time.monotonic()
		waiter = threading.Thread(target=wait)
		waiter.start()
		await asyncio.sleep(1)
		self.assertNotIn("state", answer, "still waiting")
		changed = time.monotonic()
		write(os.path.join(self.config, "settings.json"), '{\n\t// edited by hand on the NAS\n\t"sample.width": 8\n}\n')
		await asyncio.to_thread(waiter.join, 30)
		self.assertIn("state", answer)
		self.assertNotEqual(first["version"], answer["state"]["version"], "a new version")
		self.assertEqual({"sample.width": 8}, answer["state"]["settings"], "a hand edit, comments and all")
		self.assertLess(answer["after"] - changed, 5, "it woke on the change, not at the end of its wait")
		bad = time.monotonic()
		write(os.path.join(self.config, "settings.json"), '{ "sample.width": ')
		await asyncio.sleep(1.5)
		self.assertEqual({"sample.width": 8}, self.state()["settings"], "a settings.json that does not read: its last valid content is served")
		plugins = json.loads(self.api("GET", "server-plugins", self.admin).body)
		self.assertIn("settings.json is not a JSON object", plugins["settingsProblem"])
		self.assertLess(time.monotonic() - bad, 10)

	# --- Deploys and a client that follows ---------------------------------------------------------------------------

	async def test_deploys_reach_a_client_through_its_mirror(self):
		self.start_config()
		write(os.path.join(self.config, "settings.json"), json.dumps({"sample.models": {"big": {"size": 70}}, "sample.theme": "darker", "demo.greeting": "hello from the NAS"}))
		skill = os.path.join(self.root, "src", "nas-deploy")
		write(os.path.join(skill, "SKILL.md"), SKILL)
		write(os.path.join(skill, "__pycache__", "x.pyc"), "skipped")
		said = self.deploy("skill", skill)
		self.assertIn("skills/nas-deploy: 1 files, deployed", said)
		demo = demo_plugin(os.path.join(self.root, "src", "demo"))
		self.deploy("plugin", demo)
		models = models_plugin(os.path.join(self.root, "src", "models"), 1)
		said = self.deploy("server-plugin", models)
		self.assertIn("code active", said, said)
		self.assertIn('published: {"sample.models": {"spark": {"size": 1}}}', said, "the deploy prints what the plugin published")
		state = self.state()
		self.assertEqual({"big": {"size": 70}, "spark": {"size": 1}}, state["settings"]["sample.models"], "published after settings.json, by the key's merge kind")
		self.assertEqual(["shared.demo"], [p["id"] for p in state["plugins"]], "client plugins are served; server plugins are not")
		self.assertEqual("1.0.0", state["plugins"][0]["version"])
		self.assertEqual(["nas-deploy"], [s["name"] for s in state["skills"]])
		files = json.loads(self.api("GET", "skills/nas-deploy/files", self.reader).body)
		self.assertEqual(["SKILL.md"], [f["path"] for f in files], "__pycache__ was not deployed")
		self.assertEqual(SKILL.encode("utf-8"), self.api("GET", "skills/nas-deploy/file/SKILL.md", self.reader).body)
		self.assertEqual(403, self.api("GET", "server-plugins/test.models/files", self.reader).status, "a server plugin's files are no reader's")

		client, app = self.start_client()
		shared = os.path.join(app, "shared")
		watcher = await self.connect(client)
		status = await self.follow_state(watcher, "following")
		self.assertEqual((self.url, 1, 1), (status["url"], status["plugins"], status["skills"]))
		self.assertTrue(os.path.isfile(os.path.join(shared, "skills", "nas-deploy", "SKILL.md")), "the skill is mirrored")
		self.assertTrue(os.path.isfile(os.path.join(shared, "plugins", "shared.demo", "ied-plugin.json")), "the client plugin is mirrored")
		self.assertFalse(os.path.exists(os.path.join(shared, "plugins", "test.models")), "the server plugin is not")

		async def greeting():
			values = await self.values(watcher)
			return values if "demo.greeting" in values and values["demo.greeting"][1] == "shared" else None
		values = await self.until("the shared plugin and its setting", greeting)
		self.assertEqual(("hello from the NAS", "shared"), values["demo.greeting"], "a shared plugin's setting set by the config server")
		self.assertEqual(({"big": {"size": 70}, "spark": {"size": 1}}, "shared"), values["sample.models"], "the published value in the Shared tier")
		self.assertEqual(("darker", "shared"), values["sample.theme"])
		self.assertEqual((4, "default"), values["sample.width"])
		plugins = {p["id"]: p for p in (await watcher.request("plugins.list"))["plugins"]}
		self.assertEqual(("shared", "loaded"), (plugins["shared.demo"]["source"], plugins["shared.demo"]["state"]), "loaded in the shared scope, no approval of its own")
		state = await watcher.request("settings.get", {})
		self.assertEqual([], state["tiers"]["shared"]["problems"])
		await watcher.request("settings.set", {"tier": "app", "key": "sample.theme", "value": "light"})
		self.assertEqual(("light", "app"), (await self.values(watcher))["sample.theme"], "the owner's own app setting wins over the shared one")
		with self.assertRaises(RpcFailure):
			await watcher.request("settings.set", {"tier": "shared", "key": "sample.theme", "value": "dark"})
		security = [json.loads(line) for line in read_lines(os.path.join(self.root, "client-data", "logs", "security.log"))]
		self.assertIn(("shared.demo", self.url), [(e.get("plugin"), e.get("server")) for e in security if e["event"] == "shared-plugin-installed"], "each install in the security log")

		# A server plugin deployed again reloads: what it publishes now reaches the client by itself.
		said = self.deploy("server-plugin", models_plugin(models, 2))
		self.assertIn('"spark": {"size": 2}', said, said)

		async def republished():
			values = await self.values(watcher)
			return values["sample.models"][0].get("spark") == {"size": 2}
		await self.until("the redeployed plugin's value at the client", republished)
		# A build that does not compile: the deploy says why and fails; the previous build runs on.
		said = self.deploy("server-plugin", models_plugin(models, 3, broken=True), expect=1)
		self.assertIn("missing_name", said, "the deploy prints the compile diagnostics")
		self.assertIn("the previous one runs", said)
		self.assertEqual({"sample.models": {"spark": {"size": 2}}}, json.loads(self.api("GET", "server-plugins", self.admin).body)["plugins"][0]["published"], "what the running build published stays")
		# A client plugin changed and one removed: the mirror follows.
		self.deploy("plugin", demo_plugin(demo, "hey"))

		async def changed_default():
			schema = (await watcher.request("settings.schema"))["settings"]
			return schema.get("demo.greeting", {}).get("default") == "hey"
		await self.until("the redeployed client plugin", changed_default)
		removed = subprocess.run([sys.executable, "-I", DEPLOY, "server-plugin", "test.models", "--remove", "--server", self.url, "--token", self.admin], capture_output=True, text=True, timeout=60)
		self.assertEqual(0, removed.returncode, removed.stderr)
		self.assertNotIn("spark", self.state()["settings"]["sample.models"], "a server plugin removed takes what it published with it")
		self.assertEqual(200, self.api("DELETE", "skills/nas-deploy", self.admin).status)
		await self.until("the removed skill gone from the mirror", lambda: asyncio.sleep(0, not os.path.exists(os.path.join(shared, "skills", "nas-deploy"))))

	async def test_a_commit_takes_only_what_its_list_says(self):
		self.start_config()
		self.assertEqual(200, self.api("PUT", "skills/s/stage/SKILL.md", self.admin, SKILL.encode("utf-8")).status)
		self.assertEqual(200, self.api("PUT", "skills/s/stage/extra.txt", self.admin, b"x").status)
		listed = [{"path": "SKILL.md", "hash": hashlib.sha256(SKILL.encode("utf-8")).hexdigest()}]
		conflict = self.api("POST", "skills/s/commit", self.admin, listed)
		self.assertEqual(409, conflict.status, "a file staged but not listed")
		self.assertIn("extra.txt: staged, not in the list", json.loads(conflict.body)["problems"])
		wrong = self.api("POST", "skills/s/commit", self.admin, [listed[0], {"path": "extra.txt", "hash": "0" * 64}])
		self.assertEqual(409, wrong.status, "a hash that is not the staged file's")
		self.assertEqual(400, self.api("PUT", "skills/s/stage/../escape", self.admin, b"x").status, "no path out of the package")
		self.assertEqual(400, self.api("PUT", "skills/.staging/stage/x", self.admin, b"x").status, "no hidden package name")
		self.assertEqual(200, self.api("DELETE", "skills/s/stage", self.admin).status)
		self.assertEqual(200, self.api("PUT", "plugins/p/stage/ied-plugin.json", self.admin, b'{"id": "other"}').status)
		misnamed = self.api("POST", "plugins/p/commit", self.admin, [{"path": "ied-plugin.json", "hash": hashlib.sha256(b'{"id": "other"}').hexdigest()}])
		self.assertEqual(400, misnamed.status, "a plugin deployed under another id than its manifest's")
		self.assertEqual([], self.state()["skills"], "nothing was committed")
		self.assertEqual(404, self.api("DELETE", "skills/never", self.admin).status)
		self.assertEqual(404, self.api("GET", "things/x/files", self.reader).status, "no such kind")

	# --- Server plugins ------------------------------------------------------------------------------------------------

	async def test_publishing_needs_a_config_server(self):
		plugin = models_plugin(os.path.join(self.home, "plugins", "test.models"), 1)
		server = self.start("plain")
		log = os.path.join(server.data, "logs", "server.log")

		async def said():
			return os.path.exists(log) and any("IED::Shared works only on a config server" in line for line in read_lines(log))
		await self.until("the plugin's Publish refused, with why", said, COMPILE_SECONDS)
		self.assertTrue(os.path.isdir(plugin))

	# --- A config server that cannot be followed -----------------------------------------------------------------------

	async def test_an_unreachable_config_server_leaves_the_mirror_as_it_was(self):
		port = free_port()
		self.url = "http://127.0.0.1:%d/" % port
		app = os.path.join(self.root, "client-app")
		write(os.path.join(app, "shared", "settings.json"), '// mirrored before\n{"sample.theme": "darker"}\n')
		client, _ = self.start_client()
		watcher = await self.connect(client)
		status = await self.follow_state(watcher, "unreachable")
		self.assertEqual(self.url, status["url"])
		self.assertTrue(status["error"], "it says why")
		self.assertEqual(("darker", "shared"), (await self.values(watcher))["sample.theme"], "the mirror it had stays in effect")
		self.assertIn("shared", (await watcher.request("settings.get", {}))["tiers"])
		self.assertTrue(await self.notified(client, "cannot be reached"), "a notification says so")
		# The config server comes up on that port: the client follows it, its mirror replaced.
		self.start_config(port)
		write(os.path.join(self.config, "settings.json"), json.dumps({"sample.theme": "light"}))
		other = await self.connect(client)
		await self.follow_state(other, "following")

		async def light():
			return (await self.values(other))["sample.theme"] == ("light", "shared")
		await self.until("the config server's value once it is reachable", light)

	async def notified(self, server, text):
		client = await self.connect(server)
		await client.sub(["server"])
		snapshot = (await client.next_frame("server"))["data"]
		return any(text in note["text"] for note in snapshot.get("notifications", []))

	async def test_a_refused_token_is_said(self):
		self.start_config()
		client, _ = self.start_client(token="f" * 64)
		watcher = await self.connect(client)
		status = await self.follow_state(watcher, "refused")
		self.assertIn("HTTP 401", status["error"])
		self.assertTrue(await self.notified(client, "refused IED"))
		missing = os.path.join(self.root, "nokey-app")
		write_json(os.path.join(missing, "settings.json"), {"configServer.url": self.url, "configServer.token": {"secret": "configServer"}})
		second = self.start("nokey")
		other = await self.connect(second)
		status = await self.follow_state(other, "refused")
		self.assertIn("no secret 'configServer' is stored on this machine", status["error"], "a secret the store does not hold says so")

	# --- Settings > Environment > Config Server: the token in the secret store ----------------------------------------

	async def test_a_stored_token_is_followed_and_never_comes_back(self):
		self.start_config()
		write(os.path.join(self.config, "settings.json"), json.dumps({"sample.models": {"a@lan": {"size": 1}, "b@lan": {"size": 2}}, "sample.theme": "darker"}))
		app = os.path.join(self.root, "stored-app")
		write_json(os.path.join(app, "settings.json"), {"configServer.url": self.url})
		client = self.start("stored")
		watcher = await self.connect(client)
		status = await self.follow_state(watcher, "refused")
		self.assertIn("configServer.token is not set", status["error"])
		listed = await watcher.request("secrets.list", {})
		self.assertEqual((True, []), (listed["available"], listed["secrets"]))
		# What the page's Connect does: the token into the store, the setting naming it.
		await watcher.request("secrets.set", {"name": "configServer", "value": self.reader})
		await watcher.request("settings.set", {"tier": "app", "key": "configServer.token", "value": {"secret": "configServer"}})
		status = await self.follow_state(await self.connect(client), "following", 15)
		self.assertEqual([{"key": "sample.models", "members": ["a@lan", "b@lan"]}, {"key": "sample.theme"}], status["served"]["settings"], "what is shared, an object's members named")
		self.assertEqual(([], []), (status["served"]["plugins"], status["served"]["skills"]))

		async def shared_models():
			values = await self.values(watcher)
			return values.get("sample.models") == ({"a@lan": {"size": 1}, "b@lan": {"size": 2}}, "shared")
		await self.until("the config server's models at the client", shared_models)
		self.assertEqual(["configServer"], [s["name"] for s in (await watcher.request("secrets.list", {}))["secrets"]])
		feed = await self.connect(client)
		await feed.sub(["server"])
		snapshot = (await feed.next_frame("server"))["data"]
		self.assertEqual(["configServer"], [s["name"] for s in snapshot["secrets"]["secrets"]], "the server topic names the secrets")
		data = os.path.join(self.root, "stored-data")
		for path in [os.path.join(data, "auth", "secrets.json"), os.path.join(data, "logs", "security.log"), os.path.join(data, "logs", "server.log"), os.path.join(app, "settings.json")]:
			with open(path, encoding="utf-8") as f:
				self.assertNotIn(self.reader, f.read(), "the token is in no file in the clear: " + path)
		self.assertNotIn(self.reader, json.dumps(snapshot))
		security = [json.loads(line) for line in read_lines(os.path.join(data, "logs", "security.log"))]
		self.assertIn(("secret-set", "configServer"), [(e["event"], e.get("name")) for e in security], "the security log names the secret")
		# Removed: refused at once, not after the waiting request ends; the secrets.changed event says so.
		removed = await watcher.request("secrets.remove", {"name": "configServer"})
		self.assertTrue(removed["removed"])
		changed = (await feed.wait_for("server", lambda f: f.get("kind") == "secrets.changed", 5))[-1]
		self.assertEqual([], changed["data"]["secrets"])
		status = await self.follow_state(await self.connect(client), "refused", 10)
		self.assertIn("no secret 'configServer' is stored", status["error"])
		self.assertFalse((await watcher.request("secrets.remove", {"name": "configServer"}))["removed"], "removed once")
		for bad in [{"name": "a/b", "value": "x"}, {"name": "configServer", "value": ""}, {"name": "configServer", "value": "x" * 4097}]:
			with self.assertRaises(RpcFailure):
				await watcher.request("secrets.set", bad)

	async def test_a_change_of_server_is_followed_at_once(self):
		port = free_port()
		self.start_config(port)
		client, _ = self.start_client()
		await self.follow_state(await self.connect(client), "following")
		watcher = await self.connect(client)
		await asyncio.sleep(1.5)
		# Its waiting state request is out for 25 s: Disconnect stops following now.
		began = time.monotonic()
		await watcher.request("settings.reset", {"tier": "app", "key": "configServer.url"})
		await self.follow_state(await self.connect(client), "off", 8)
		self.assertLess(time.monotonic() - began, 8)
		await watcher.request("settings.set", {"tier": "app", "key": "configServer.url", "value": self.url})
		await self.follow_state(await self.connect(client), "following", 8)
		# Unreachable for a while (its pause between tries grows to 8 s), back on its port: Retry now follows it now.
		self.config_server.stop()
		self.servers.remove(self.config_server)
		await self.follow_state(await self.connect(client), "unreachable", 30)
		await asyncio.sleep(8)
		self.start_config(port)
		began = time.monotonic()
		await watcher.request("configServer.retry", {})
		await self.follow_state(await self.connect(client), "following", 6)
		self.assertLess(time.monotonic() - began, 6, "the retry did not wait out the pause")

	# --- The web page (/config/) -----------------------------------------------------------------------------------------

	def page(self, method, path, cookie=None, header=True, body=None, bearer=None):
		sent = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0 Safari/537.36 Edg/140.0"}
		if cookie:
			sent["Cookie"] = cookie
		if header:
			sent["X-IED-Config"] = "1"
		if bearer:
			sent["Authorization"] = "Bearer " + bearer
		if isinstance(body, (dict, list)):
			body = json.dumps(body).encode("utf-8")
		return self.config_server.request(method, path, body, sent)

	def test_the_page_is_served_without_a_token(self):
		self.start_config()
		index = self.config_server.request("GET", "/config/")
		self.assertEqual(200, index.status, index.text())
		self.assertIn("text/html", index.header("Content-Type"))
		self.assertIn("script-src 'self'", index.header("Content-Security-Policy") or "", "the page has the web client's content policy")
		self.assertIn("/config/config.js", index.text())
		for path in ["/config/config.js", "/config/configmodel.js", "/config/config.css"]:
			self.assertEqual(200, self.config_server.request("GET", path).status, path)
		moved = self.config_server.request("GET", "/config")
		self.assertEqual((301, "/config/"), (moved.status, moved.header("Location")))
		for path in ["/config/..%2Fsecrets.json", "/config/access.json", "/config/kit/../../x.js", "/config/nothing.js"]:
			self.assertEqual(404, self.config_server.request("GET", path).status, path)
		self.assertEqual(401, self.config_server.request("GET", "/config/v1/whoami").status, "the API still wants a token")

	def test_a_browser_signs_in_and_manages_tokens(self):
		self.start_config()
		self.assertEqual(403, self.page("POST", "/config/v1/session", bearer=self.reader, body={}).status, "a reader cannot sign a browser in")
		signed = self.page("POST", "/config/v1/session", bearer=self.admin, body={})
		self.assertEqual(200, signed.status, signed.text())
		name = json.loads(signed.body)["name"]
		self.assertRegex(name, r"^web-edge-on-windows-[0-9a-f]{6}$", "named for the browser")
		cookie_header = signed.cookies()[0]
		for part in ["Path=/config/", "HttpOnly", "SameSite=Strict", "Max-Age=31536000"]:
			self.assertIn(part, cookie_header)
		cookie = cookie_header.split(";", 1)[0]
		self.assertNotIn(self.admin, cookie, "the browser has a token of its own")
		self.assertEqual(403, self.page("GET", "/config/v1/whoami", cookie=cookie, header=False).status, "the cookie alone is refused: another site's form could send it")
		me = json.loads(self.page("GET", "/config/v1/whoami", cookie=cookie).body)
		self.assertEqual((name, "admin", True), (me["name"], me["role"], me["cookie"]))
		self.assertTrue(me["serverVersion"] and me["startedMs"] and me["root"])
		# Tokens: listed with when they were used, made (shown once), refused when misnamed, revoked.
		listed = json.loads(self.page("GET", "/config/v1/tokens", cookie=cookie).body)
		self.assertEqual(name, listed["you"])
		rows = {t["name"]: t for t in listed["tokens"]}
		self.assertEqual({"laptop", "deploy", name}, set(rows))
		self.assertIn("lastUsedMs", rows[name])
		self.assertNotIn("sha256", json.dumps(listed), "never a digest")
		made = self.page("POST", "/config/v1/tokens", cookie=cookie, body={"name": "phone", "role": "reader"})
		self.assertEqual(200, made.status, made.text())
		phone = json.loads(made.body)["token"]
		self.assertEqual(200, self.api("GET", "state", phone).status, "a made token works at once")
		self.assertEqual(400, self.page("POST", "/config/v1/tokens", cookie=cookie, body={"name": "a/b", "role": "reader"}).status)
		self.assertEqual(400, self.page("POST", "/config/v1/tokens", cookie=cookie, body={"name": "x", "role": "root"}).status)
		self.assertEqual(403, self.api("POST", "tokens", self.reader, {"name": "x", "role": "admin"}).status, "a reader makes no token")
		self.assertEqual(200, self.page("DELETE", "/config/v1/tokens/phone", cookie=cookie).status)
		self.assertEqual(401, self.api("GET", "state", phone).status, "revoked at once")
		self.assertEqual(404, self.page("DELETE", "/config/v1/tokens/phone", cookie=cookie).status)
		# A one-time link signs another browser in, once.
		link = json.loads(self.page("POST", "/config/v1/links", cookie=cookie).body)
		self.assertRegex(link["url"], r"^/config/\?link=[0-9a-f]{64}$")
		opened = self.page("GET", link["url"], header=False)
		self.assertEqual((303, "/config/"), (opened.status, opened.header("Location")))
		other = opened.cookies()[0].split(";", 1)[0]
		self.assertEqual("admin", json.loads(self.page("GET", "/config/v1/whoami", cookie=other).body)["role"])
		again = self.page("GET", link["url"], header=False)
		self.assertEqual((303, "/config/#link-expired", []), (again.status, again.header("Location"), again.cookies()), "a link works once")
		# Signing out revokes the browser's token; the last admin token cannot be revoked.
		self.assertEqual(200, self.page("DELETE", "/config/v1/session", cookie=other).status)
		self.assertEqual(401, self.page("GET", "/config/v1/whoami", cookie=other).status)
		self.assertEqual(200, self.page("DELETE", "/config/v1/session", cookie=cookie).status)
		last = self.api("DELETE", "tokens/deploy", self.admin)
		self.assertEqual(409, last.status, "nobody could manage the server")
		self.assertIn("last admin token", json.loads(last.body)["message"])
		self.assertEqual(400, self.api("DELETE", "session", self.admin).status, "signing out is a browser's")
		events = [json.loads(line) for line in read_lines(os.path.join(self.root, "config-data", "logs", "security.log"))]
		said = [(e["event"], e.get("name"), e.get("by")) for e in events if e["event"].startswith("config-")]
		self.assertIn(("config-signed-in", name, "deploy"), said)
		self.assertIn(("config-token-made", "phone", name), said)
		self.assertIn(("config-token-revoked", "phone", name), said)
		self.assertIn("a sign-in link", [by for event, _, by in said if event == "config-signed-in"], "a link's sign-in says so")
		with open(os.path.join(self.config, "access.json"), encoding="utf-8") as f:
			kept = f.read()
		self.assertNotIn(cookie.split("=", 1)[1], kept, "access.json keeps hashes alone")
