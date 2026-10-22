"""The GPU debugger (docs/22, docs/features/17-gpu-debugger.md): the startup project launched for capture connects its
control pipe and shows on gpu:<wsId>; Capture Next Frame writes a .flash with its sidecar and thumbnail into
.ied/local/gpu-captures, published as it lands; gpu.open starts a replay whose ied-gpu/1 requests answer through
gpu.replay, binary parts as blobs; comments land in the sidecar; a delete ends the capture's replay and moves its files
to the trash; the gpu_* AI tools answer through the gpu.query binding; refusals. Needs the native parts (nuke build flashbang) and a D3D12 device; skipped without them."""

import json
import os
import shutil
import sys
import tempfile
import unittest

from frames import FrameChecker
from iedclient import Client, RpcFailure
from server import REPO, IedServer, ServerTestCase

NATIVE = os.path.join(REPO, "build", "bin", "Development-windows-x64", "flashbang")
SAMPLE = os.path.join(NATIVE, "fb-sample-d3d12.exe")
VULKAN_SAMPLE = os.path.join(NATIVE, "fb-sample-vulkan.exe")
HAS_NATIVE = all(os.path.isfile(os.path.join(NATIVE, name)) for name in ("flashbang.dll", "flashbang-replay.exe", "fb-sample-d3d12.exe", "fb-sample-vulkan.exe"))


def write(path, value):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	with open(path, "w", encoding="utf-8", newline="") as f:
		f.write(value if isinstance(value, str) else json.dumps(value, indent="\t"))


def gpu_manifest():
	"""The plugin's manifest as shipped, without its UI module (the checks need its server side only)."""
	with open(os.path.join(REPO, "plugins", "gpu", "ied-plugin.json"), encoding="utf-8") as f:
		text = "\n".join(line for line in f.read().splitlines() if not line.lstrip().startswith("//"))
	manifest = json.loads(text)
	manifest["runtime"].pop("ui", None)
	return manifest


@unittest.skipUnless(sys.platform == "win32" and HAS_NATIVE, "the GPU debugger's native parts are not built (nuke build flashbang)")
class GpuTests(ServerTestCase):
	def setUp(self):
		self.root = tempfile.mkdtemp(prefix="ied-gpu-")
		self.home = os.path.join(self.root, "home")
		self.repo = os.path.join(self.root, "Scene")
		shutil.copytree(os.path.join(REPO, "shared", "host-api", "schemas"), os.path.join(self.home, "shared", "host-api", "schemas"))
		plugin = os.path.join(self.home, "plugins", "gpu")
		os.makedirs(plugin)
		shutil.copy(os.path.join(REPO, "plugins", "gpu", "settings.schema.json"), plugin)
		shutil.copytree(os.path.join(REPO, "plugins", "gpu", "skills"), os.path.join(plugin, "skills"))
		write(os.path.join(plugin, "ied-plugin.json"), gpu_manifest())
		launch = {"type": "native", "request": "launch", "program": SAMPLE.replace("\\", "/"), "args": ["--hidden"], "cwd": "${projectRoot}"}
		vulkan = dict(launch, program=VULKAN_SAMPLE.replace("\\", "/"))
		write(os.path.join(self.repo, "Scene.ied.proj"), {"version": 1, "name": "Scene", "root": ".", "kind": "executable",
			"debug": {"default": "Launch", "configurations": {"Launch": launch, "Vulkan": vulkan}}})
		write(os.path.join(self.repo, "Scene.ied.sln"), {"version": 1, "name": "Scene", "projects": [{"file": "Scene.ied.proj"}]})
		self.server = IedServer(extra=("--home", self.home, "--workspace", os.path.join(self.repo, "Scene.ied.sln")))
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		self.server.stop()
		shutil.rmtree(self.root, ignore_errors=True)

	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.cookie = self.server.sign_in()
		self.client = await Client.connect(self.server.ws_url, FrameChecker(), headers={"Cookie": self.cookie})
		await self.client.request("settings.set", {"tier": "app", "key": "gpu.nativeFolder", "value": NATIVE.replace("\\", "/")})
		await self.client.sub(["gpu:w1"])
		self.snapshot = (await self.client.next_frame("gpu:w1"))["data"]

	async def asyncTearDown(self):
		self.assertEqual([], self.client.violations)
		self.client.abort()

	async def targets(self, predicate, timeout=30):
		frames = await self.client.wait_for("gpu:w1", lambda f: f.get("kind") == "targets" and any(predicate(t) for t in f["data"]["targets"]), timeout=timeout)
		return next(t for t in frames[-1]["data"]["targets"] if predicate(t))

	async def capture_frame(self, configuration="Launch", api="D3D12"):
		"""Launches the startup project for capture, captures one frame and stops it: the new capture."""
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		target = await self.client.request("gpu.launch", {"wsId": "w1", "configuration": configuration})
		self.assertEqual("running", target["state"])
		# The program says which APIs it uses once it has made its device, after it connects.
		connected = await self.targets(lambda t: t["id"] == target["id"] and t["connected"] and t["apis"])
		self.assertEqual([api], connected["apis"])
		await self.client.request("gpu.capture", {"wsId": "w1", "targetId": target["id"]})
		frames = await self.client.wait_for("gpu:w1", lambda f: f.get("kind") == "captures" and f["data"]["captures"], timeout=60)
		capture = frames[-1]["data"]["captures"][0]
		await self.client.request("gpu.stop", {"wsId": "w1", "targetId": target["id"]})
		await self.targets(lambda t: t["id"] == target["id"] and t["state"] == "exited")
		return capture

	async def replay(self, session, method, params=None):
		return await self.client.request("gpu.replay", {"sessionId": session, "method": method, "params": params or {}})

	async def test_launch_capture_and_replay(self):
		self.assertEqual([], self.snapshot["captures"])
		self.assertEqual(".ied/local/gpu-captures", self.snapshot["captureFolder"])
		capture = await self.capture_frame()
		folder = os.path.join(self.repo, ".ied", "local", "gpu-captures")
		for extension in (".flash", ".json", ".png"):
			self.assertTrue(os.path.isfile(os.path.join(folder, capture["id"] + extension)), extension)
		meta = capture["meta"]
		self.assertEqual(("fb-sample-d3d12", "D3D12", 1280, 720), (meta["program"], meta["api"], meta["width"], meta["height"]))
		self.assertEqual("/w/w1/files/.ied/local/gpu-captures/" + capture["id"] + ".png", capture["thumbnail"])
		self.assertEqual(200, self.server.request("GET", capture["thumbnail"], headers={"Cookie": self.cookie}).status)

		opened = await self.client.request("gpu.open", {"wsId": "w1", "captureId": capture["id"]})
		self.assertEqual("ready", opened["state"])
		self.assertEqual("D3D12", opened["summary"]["api"])
		again = await self.client.request("gpu.open", {"wsId": "w1", "captureId": capture["id"]})
		self.assertEqual(opened["sessionId"], again["sessionId"], "a second open joins the replay")
		session = opened["sessionId"]

		events = (await self.replay(session, "events"))["result"]
		self.assertEqual(["eid", "parent", "kind", "hidden", "last", "queue", "name"], events["columns"])
		self.assertEqual(meta["events"], len(events["events"]))
		names = [e[6] for e in events["events"] if e[2] == "marker" and not e[3]]
		self.assertEqual(["Frame", "Generate pattern", "Scene", "Floor", "Cube A", "Cube B", "Cube C", "Hidden", "Tonemap"], names)
		last_draw = [e[0] for e in events["events"] if e[2] == "draw"][-1]

		state = (await self.replay(session, "state", {"eid": last_draw}))["result"]
		self.assertEqual(last_draw, state["eid"])
		resources = (await self.replay(session, "resources"))["result"]["resources"]
		hdr = next(r for r in resources if r["name"] == "HDR")
		reply = await self.replay(session, "texture", {"id": hdr["id"], "eid": last_draw, "maxSize": 256})
		texture = reply["result"]
		self.assertEqual(("R16G16B16A16_FLOAT", "rgba32f", 1280, 720), (texture["format"], texture["encoding"], texture["srcWidth"], texture["srcHeight"]))
		self.assertTrue(reply["blob"])
		blob = self.server.request("GET", "/api/blob/" + reply["blob"], headers={"Cookie": self.cookie})
		self.assertEqual(200, blob.status)
		self.assertEqual(texture["width"] * texture["height"] * 16, len(blob.body))

		with self.assertRaises(RpcFailure) as caught:
			await self.replay(session, "texture", {"id": 999999, "eid": last_draw})
		self.assertEqual("bad_request", caught.exception.code)

		await self.client.request("gpu.comment", {"wsId": "w1", "captureId": capture["id"], "text": "cubes mid-turn"})
		with open(os.path.join(folder, capture["id"] + ".json"), encoding="utf-8") as f:
			self.assertEqual("cubes mid-turn", json.load(f)["comment"])

		await self.client.sub(["gpurs:" + session])
		await self.client.request("gpu.delete", {"wsId": "w1", "captureId": capture["id"]})
		ended = await self.client.wait_for("gpurs:" + session, lambda f: f.get("kind") == "state" and f["data"]["state"] in ("ended", "failed"), timeout=20)
		self.assertEqual("ended", ended[-1]["data"]["state"])
		self.assertFalse(os.path.exists(os.path.join(folder, capture["id"] + ".flash")))
		frames = await self.client.wait_for("gpu:w1", lambda f: f.get("kind") == "captures" and not f["data"]["captures"], timeout=20)
		self.assertEqual([], frames[-1]["data"]["captures"])

	async def test_vulkan_through_the_layer(self):
		# Launch for Capture puts the layer on the program's loader path (VK_ADD_LAYER_PATH, VK_INSTANCE_LAYERS).
		capture = await self.capture_frame("Vulkan", "Vulkan")
		self.assertEqual(("fb-sample-vulkan", "Vulkan"), (capture["meta"]["program"], capture["meta"]["api"]))
		opened = await self.client.request("gpu.open", {"wsId": "w1", "captureId": capture["id"]})
		self.assertEqual(("ready", "Vulkan"), (opened["state"], opened["summary"]["api"]))
		session = opened["sessionId"]
		events = (await self.replay(session, "events"))["result"]["events"]
		self.assertIn("Cube A", [e[6] for e in events])
		present = events[-1]
		self.assertEqual("present", present[2])
		resources = {r["name"]: r["id"] for r in (await self.replay(session, "resources"))["result"]["resources"]}
		reply = await self.replay(session, "texture", {"id": resources["HDR"], "eid": present[0], "maxSize": 320})
		self.assertEqual(("R16G16B16A16_SFLOAT", 320, 180), (reply["result"]["format"], reply["result"]["width"], reply["result"]["height"]))
		blob = self.server.request("GET", "/api/blob/" + reply["blob"], headers={"Cookie": self.cookie})
		self.assertEqual(320 * 180 * 16, len(blob.body))
		await self.client.request("gpu.close", {"sessionId": session})

	async def test_the_ai_reads_captures_through_the_binding(self):
		capture = await self.capture_frame()
		async def ask(tool, **args):
			return (await self.client.request("plugins.call", {"name": "gpu.query", "wsId": "w1", "args": {"wsId": "w1", "tool": tool, "args": args}}))["result"]
		async def text(tool, **args):
			return (await ask(tool, **args))["text"]
		self.assertIn(capture["id"] + " | fb-sample-d3d12 | D3D12", await text("captures"))
		events = await text("events")
		self.assertIn("marker  Cube A", events)
		draws = [line for line in (await text("events", filter="kind:draw")).splitlines() if line[:1].isdigit()]
		self.assertTrue(draws and all("  draw  " in line for line in draws), draws)
		last_draw = int(draws[-1].split()[0])
		cube = int(draws[2].split()[0])   # Cube B: Floor, Cube A, Cube B, Cube C, Hidden, Tonemap

		state = await text("state", eid=cube)
		self.assertIn(f"Pipeline state at EID {cube}, pipeline Lit", state)
		self.assertIn(", rt]", state, "what the draw writes is marked")
		self.assertIn("layout float3 POSITION0; float3 NORMAL0; float2 TEXCOORD0", state)
		vb = int(state.split("VB 0: ")[1].split("[#")[1].split(",")[0])
		self.assertIn("+0: ", await text("buffer", eid=cube, resource=vb, size=64))
		shader = await text("shader", eid=cube, stage="ps")
		self.assertIn("ps shader of pipeline Lit", shader)
		self.assertIn("HDR", await text("resources"))

		texture = await ask("texture", eid=cube)
		self.assertIn("1280x720 R16G16B16A16_FLOAT", texture["text"])
		self.assertIn("NaN 0, Inf 0", texture["text"])
		self.assertTrue(os.path.isfile(os.path.join(self.repo, texture["image"])), texture["image"])
		self.assertIn(f"after EID {cube}: ", await text("pixel", eid=cube, x=640, y=360))
		self.assertIn("none after the frame's last action", await text("scan"))
		timing = await text("timing", repeat=2)
		self.assertIn("GPU time of the frame: ", timing)
		self.assertIn("Cube A", timing)
		self.assertIn(f" | {last_draw} | ", await text("timing", by="event", repeat=2, top=50))
		with self.assertRaises(RpcFailure):
			await ask("state")
		with self.assertRaises(RpcFailure):
			await ask("events", capture="nope")

	async def test_the_ai_captures_a_frame(self):
		async def capture(**args):
			return (await self.client.request("plugins.call", {"name": "gpu.query", "wsId": "w1", "args": {"wsId": "w1", "tool": "capture", "args": args}}))["result"]["text"]
		with self.assertRaises(RpcFailure) as caught:
			await capture()
		self.assertIn("not trusted", str(caught.exception))
		await self.client.request("ws.setTrust", {"wsId": "w1", "trusted": True})
		text = await capture(configuration="Vulkan")
		self.assertTrue(text.startswith("Captured fb-sample-vulkan_"), text)
		self.assertIn("the program is stopped", text)
		self.assertIn("marker  Cube A", text, "the new capture's events follow")
		captures = await self.client.wait_for("gpu:w1", lambda f: f.get("kind") == "captures" and f["data"]["captures"], timeout=30)
		self.assertEqual("Vulkan", captures[-1]["data"]["captures"][0]["meta"]["api"])
		targets = await self.client.request("gpu.status", {"wsId": "w1"})
		self.assertTrue(all(t["state"] == "exited" for t in targets["targets"]), targets["targets"])

	async def test_refusals(self):
		with self.assertRaises(RpcFailure) as caught:
			await self.client.request("gpu.launch", {"wsId": "w1"})
		self.assertEqual("forbidden", caught.exception.code, "nothing runs in an untrusted workspace")
		with self.assertRaises(RpcFailure) as caught:
			await self.client.request("gpu.open", {"wsId": "w1", "captureId": "nope"})
		self.assertEqual("not_found", caught.exception.code)
		with self.assertRaises(RpcFailure) as caught:
			await self.client.request("gpu.capture", {"wsId": "w1", "targetId": "t99"})
		self.assertEqual("not_found", caught.exception.code)
		with self.assertRaises(RpcFailure) as caught:
			await self.client.request("gpu.replay", {"sessionId": "rs99", "method": "events", "params": {}})
		self.assertEqual("not_found", caught.exception.code)
