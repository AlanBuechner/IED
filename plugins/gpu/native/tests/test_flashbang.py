# Captures the sample programs with the capture library and replays the captures (docs/22): the files a capture leaves,
# the replayer's events, state, readbacks, timing and its ied-gpu/1 framing on stdio; the C4 sample (samples/c4) built
# with --c4 and captured through Bedrock's Gpu::Capture with manual frame bounds. A machine without a D3D12 (or Vulkan)
# GPU skips what it cannot run. Stdlib only.
#   python -I test_flashbang.py --bin build/bin/Development-windows-x64/flashbang [--c4 toolchains/c4/dev/C4.exe]
import argparse
import glob
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

BIN = ""
C4 = ""
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))


def run(args, env=None, timeout=120):
	return subprocess.run(args, capture_output=True, encoding="utf-8", errors="replace", timeout=timeout, env=env)


def replay(capture, *args, device=True):
	options = [] if device else ["--no-device"]
	result = run([os.path.join(BIN, "flashbang-replay.exe"), capture, *options, *args])
	if result.returncode != 0:
		raise AssertionError(f"flashbang-replay {' '.join(args)} failed: {result.stderr}")
	return json.loads(result.stdout)


class RpcClient:
	def __init__(self):
		self.process = subprocess.Popen([os.path.join(BIN, "flashbang-replay.exe"), "--rpc"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
		self.next = 1

	def call(self, method, params):
		message = json.dumps({"jsonrpc": "2.0", "id": self.next, "method": method, "params": params}).encode()
		self.next += 1
		self.process.stdin.write(b"Content-Length: %d\r\n\r\n" % len(message) + message)
		self.process.stdin.flush()
		while True:
			headers = {}
			while True:
				line = self.process.stdout.readline().decode().rstrip("\r\n")
				if not line:
					break
				name, _, value = line.partition(":")
				headers[name.strip().lower()] = int(value)
			body = json.loads(self.process.stdout.read(headers["content-length"]))
			binary = self.process.stdout.read(headers.get("binary-length", 0)) if headers.get("binary-length") else b""
			if "id" in body:
				return body, binary

	def close(self):
		self.call("shutdown", {})
		self.process.wait(timeout=30)
		self.process.stdin.close()
		self.process.stdout.close()


class D3D12CaptureReplay(unittest.TestCase):
	@classmethod
	def setUpClass(cls):
		cls.folder = tempfile.mkdtemp(prefix="flashbang-test-")
		env = dict(os.environ, FLASHBANG_CAPTURE_DIR=cls.folder)
		sample = run([os.path.join(BIN, "fb-sample-d3d12.exe"), "--frames", "8", "--capture-frame", "3", "--hidden", "--flashbang", os.path.join(BIN, "flashbang.dll")], env=env)
		if sample.returncode != 0:
			raise unittest.SkipTest(f"the D3D12 sample does not run here: {sample.stderr.strip()}")
		captures = glob.glob(os.path.join(cls.folder, "*.flash"))
		if len(captures) != 1:
			raise AssertionError(f"expected one capture, found {captures}; sample said {sample.stdout} {sample.stderr}")
		cls.capture = captures[0]
		cls.events = replay(cls.capture, "events", device=False)

	@classmethod
	def tearDownClass(cls):
		shutil.rmtree(cls.folder, ignore_errors=True)

	def event_named(self, name):
		for event in self.events["events"]:
			if event[6] == name:
				return event
		self.fail(f"no event {name}")

	def draws_under(self, marker):
		marker_event = self.event_named(marker)
		return [e for e in self.events["events"] if e[1] == marker_event[0] and e[2] == "draw"]

	def test_capture_leaves_its_files(self):
		base = self.capture[:-len(".flash")]
		self.assertTrue(os.path.exists(base + ".png"))
		with open(base + ".json", encoding="utf-8") as f:
			sidecar = json.load(f)
		self.assertEqual(sidecar["api"], "D3D12")
		self.assertEqual(sidecar["program"], "fb-sample-d3d12")
		self.assertEqual(sidecar["warnings"], [])
		self.assertGreater(sidecar["events"], 0)

	def test_events_follow_the_markers(self):
		frame = self.event_named("Frame")
		self.assertEqual(frame[2], "marker")
		self.assertEqual(self.event_named("Scene")[1], frame[0])
		self.assertEqual(len(self.draws_under("Cube A")), 1)
		self.assertEqual(len([e for e in self.events["events"] if e[2] == "dispatch"]), 1)
		self.assertEqual(self.events["queues"][0]["name"], "Direct queue")

	def test_state_names_bindings(self):
		draw = self.draws_under("Cube B")[0]
		state = replay(self.capture, "state", str(draw[0]), device=False)
		self.assertEqual(state["pipeline"], "Lit")
		text = json.dumps(state, ensure_ascii=False)
		dot = chr(0xB7)   # the middle dot the replayer puts between a value's parts
		self.assertIn(f"RTV {dot} HDR {dot} R16G16B16A16_FLOAT", text)
		self.assertIn(f"DSV {dot} Depth {dot} D32_FLOAT", text)
		self.assertIn(f"SRV {dot} Pattern", text)
		self.assertIn("Frame constants", text)
		# Buffer bindings say where they start and how their elements read, for the buffer view.
		items = {item["label"]: item for group in state["groups"] for item in group["items"]}
		self.assertEqual((896, "uint16 index"), (items["Index buffer"]["offset"], items["Index buffer"]["layout"]))
		self.assertEqual("float3 POSITION0; float3 NORMAL0; float2 TEXCOORD0", items["VB 0"]["layout"])

	def test_partial_replay_reads_the_target(self):
		floor = self.draws_under("Floor")[0][0]
		cube = self.draws_under("Cube C")[0][0]
		resources = {r["name"]: r for r in replay(self.capture, "resources", device=False)["resources"]}
		hdr = resources["HDR"]["id"]
		client = RpcClient()
		try:
			opened, _ = client.call("initialize", {"capture": self.capture})
			self.assertNotIn("error", opened)
			self.assertIn("adapter", opened["result"]["replayDevice"])
			# The pixel where Cube C stands: floor-colored after the floor, tinted blue after Cube C.
			before, _ = client.call("pixel", {"id": hdr, "eid": floor, "x": 880, "y": 300})
			after, _ = client.call("pixel", {"id": hdr, "eid": cube, "x": 880, "y": 300})
			self.assertNotEqual(before["result"]["values"], after["result"]["values"])
			blue = after["result"]["values"]
			self.assertGreater(blue[2], blue[0])
			texture, data = client.call("texture", {"id": hdr, "eid": cube, "maxSize": 640})
			info = texture["result"]
			self.assertEqual((info["width"], info["height"], info["step"]), (640, 360, 2))
			self.assertEqual(len(data), 640 * 360 * 16)
			r, g, b, a = struct.unpack_from("<4f", data, (180 * 640 + 440) * 16)
			self.assertGreater(b, r)
			self.assertEqual(info["nan"], 0)
			missing, _ = client.call("texture", {"id": 999999, "eid": cube})
			self.assertIn("error", missing)
		finally:
			client.close()

	def test_timing_and_statistics(self):
		timing = replay(self.capture, "timing", "2")
		self.assertEqual(timing["repeat"], 2)
		timed = {e[0] for e in timing["events"]}
		for draw in self.draws_under("Cube A") + self.draws_under("Floor"):
			self.assertIn(draw[0], timed)
		stats = {s[0]: s for s in timing["stats"]}
		tonemap = [e for e in self.events["events"] if e[2] == "draw" and e[1] == self.event_named("Tonemap")[0]][0]
		self.assertEqual(stats[tonemap[0]][8], 1280 * 720)   # psInvocations of the full-screen pass

	def test_mesh_reads_the_vertex_inputs(self):
		draw = self.draws_under("Cube A")[0][0]
		client = RpcClient()
		try:
			client.call("initialize", {"capture": self.capture})
			mesh, data = client.call("mesh", {"eid": draw})
			result = mesh["result"]
			self.assertEqual([a["name"] for a in result["attributes"]], ["POSITION", "NORMAL", "TEXCOORD"])
			self.assertEqual(result["count"], 24)
			self.assertEqual(len(result["indices"]), 36)
			self.assertEqual(len(data), 3 * 24 * 16)
			x, y, z, w = struct.unpack_from("<4f", data, 0)
			self.assertEqual({abs(x), abs(y), abs(z)}, {0.5})
		finally:
			client.close()


def read_bmp(path):
	"""A top-down 32-bit BMP's width, height and BGRA bytes."""
	with open(path, "rb") as f:
		data = f.read()
	width, height = struct.unpack_from("<ii", data, 18)
	return width, abs(height), data[54:]


class VulkanCaptureReplay(unittest.TestCase):
	"""The Vulkan sample captured through the layer (VK_INSTANCE_LAYERS), its last frame, and its replay - down to the
	presented image matching the program's own screenshot of that frame."""

	@classmethod
	def setUpClass(cls):
		cls.folder = tempfile.mkdtemp(prefix="flashbang-vk-")
		cls.screenshot = os.path.join(cls.folder, "last.bmp")
		captures = os.path.join(cls.folder, "captures")
		env = dict(os.environ, FLASHBANG_CAPTURE_DIR=captures, VK_ADD_LAYER_PATH=BIN, VK_INSTANCE_LAYERS="VK_LAYER_IED_flashbang")
		# A capture triggered in frame 6 records frame 7, the last one, which the screenshot shows too.
		sample = run([os.path.join(BIN, "fb-sample-vulkan.exe"), "--frames", "8", "--capture-frame", "6", "--hidden", "--flashbang", os.path.join(BIN, "flashbang.dll"),
			"--screenshot", cls.screenshot], env=env)
		if sample.returncode == 2:
			raise unittest.SkipTest(f"the Vulkan sample does not run here: {sample.stderr.strip()}")
		if sample.returncode != 0:
			raise AssertionError(f"the Vulkan sample failed: {sample.stdout} {sample.stderr}")
		cls.captures = glob.glob(os.path.join(captures, "*.flash"))
		if len(cls.captures) != 1:
			raise AssertionError(f"expected one capture, found {cls.captures}")
		cls.capture = cls.captures[0]
		cls.events = replay(cls.capture, "events", device=False)["events"]

	@classmethod
	def tearDownClass(cls):
		shutil.rmtree(cls.folder, ignore_errors=True)

	def event_named(self, name):
		for event in self.events:
			if event[6] == name:
				return event
		self.fail(f"no event {name}")

	def draws_under(self, marker):
		return [e for e in self.events if e[1] == self.event_named(marker)[0] and e[2] == "draw"]

	def test_capture_leaves_its_files(self):
		base = self.capture[:-len(".flash")]
		self.assertTrue(os.path.exists(base + ".png"))
		with open(base + ".json", encoding="utf-8") as f:
			sidecar = json.load(f)
		self.assertEqual(("fb-sample-vulkan", "Vulkan", 1280, 720), (sidecar["program"], sidecar["api"], sidecar["width"], sidecar["height"]))
		self.assertEqual([], sidecar["warnings"])
		self.assertEqual(len(self.events), sidecar["events"])

	def test_events_follow_the_labels(self):
		frame = self.event_named("Frame")
		self.assertEqual("marker", frame[2])
		self.assertEqual(frame[0], self.event_named("Scene")[1])
		self.assertEqual(1, len(self.draws_under("Cube A")))
		self.assertEqual(["vkCmdDispatch(32, 32, 1)"], [e[6] for e in self.events if e[2] == "dispatch"])
		self.assertEqual("vkQueuePresentKHR", self.events[-1][6])

	def test_state_names_attachments_and_descriptors(self):
		draw = self.draws_under("Cube B")[0]
		state = replay(self.capture, "state", str(draw[0]), device=False)
		self.assertEqual("Lit", state["pipeline"])
		items = {}
		for group in state["groups"]:
			for item in group["items"]:
				items[item["label"]] = item
				for child in item.get("children", []):
					items[child["label"]] = child
		dot = chr(0xB7)
		self.assertEqual((f"HDR {dot} R16G16B16A16_SFLOAT", "rt"), (items["Color 0"]["value"], items["Color 0"]["role"]))
		self.assertEqual("depth", items["Depth"]["role"])
		self.assertTrue(items["b1 COMBINED_IMAGE_SAMPLER"]["value"].startswith(f"Pattern {dot} R8G8B8A8_UNORM"))
		self.assertTrue(items["b0 UNIFORM_BUFFER"]["value"].startswith("Frame constants"))
		self.assertEqual((0, "float4"), (items["b0 UNIFORM_BUFFER"]["offset"], items["b0 UNIFORM_BUFFER"]["layout"]))
		self.assertEqual((896, "uint16 index"), (items["Index buffer"]["offset"], items["Index buffer"]["layout"]))
		self.assertEqual("float3 position; float3 normal; float2 uv", items["VB 0"]["layout"])
		self.assertEqual(["position (0)", "normal (1)", "uv (2)"], [label for label in items if label.endswith(")") and "(" in label and label[0].islower()][:3])
		self.assertIn("fs", [i.get("shader") for i in items.values()])

	def test_partial_replay_reads_the_target(self):
		floor = self.draws_under("Floor")[0][0]
		cube = self.draws_under("Cube C")[0][0]
		resources = {r["name"]: r for r in replay(self.capture, "resources", device=False)["resources"]}
		hdr = resources["HDR"]["id"]
		client = RpcClient()
		try:
			opened, _ = client.call("initialize", {"capture": self.capture})
			self.assertEqual([], opened["result"]["warnings"])
			before, _ = client.call("pixel", {"id": hdr, "eid": floor, "x": 880, "y": 300})
			after, _ = client.call("pixel", {"id": hdr, "eid": cube, "x": 880, "y": 300})
			self.assertNotEqual(before["result"]["values"], after["result"]["values"])
			blue = after["result"]["values"]
			self.assertGreater(blue[2], blue[0])
			texture, data = client.call("texture", {"id": hdr, "eid": cube, "maxSize": 640})
			info = texture["result"]
			self.assertEqual((info["width"], info["height"], info["step"]), (640, 360, 2))
			self.assertEqual(len(data), 640 * 360 * 16)
		finally:
			client.close()

	def test_replayed_frame_matches_the_program(self):
		present = self.events[-1][0]
		client = RpcClient()
		try:
			client.call("initialize", {"capture": self.capture})
			calls, _ = client.call("apiCalls", {"eid": present})
			image = calls["result"]["calls"][0]["args"]["image"]
			texture, data = client.call("texture", {"id": image, "eid": present, "maxSize": 4096})
		finally:
			client.close()
		width, height, bgra = read_bmp(self.screenshot)
		info = texture["result"]
		self.assertEqual((width, height), (info["width"], info["height"]))
		# The replay decodes B8G8R8A8 to RGBA floats; the screenshot is the program's own bytes of the same frame.
		worst, total = 0, 0
		for i in range(0, width * height, 97):
			r, g, b, a = struct.unpack_from("<4f", data, i * 16)
			bb, gg, rr = bgra[i * 4], bgra[i * 4 + 1], bgra[i * 4 + 2]
			for replayed, shown in ((r, rr), (g, gg), (b, bb)):
				difference = abs(round(replayed * 255) - shown)
				worst = max(worst, difference)
				total += difference
		self.assertLessEqual(worst, 2, "the replayed frame differs from the program's")

	def test_mesh_shader_and_timing(self):
		draw = self.draws_under("Cube A")[0][0]
		client = RpcClient()
		try:
			client.call("initialize", {"capture": self.capture})
			mesh, data = client.call("mesh", {"eid": draw})
			self.assertEqual(["position", "normal", "uv"], [a["name"] for a in mesh["result"]["attributes"]])
			self.assertEqual((24, 36), (mesh["result"]["count"], len(mesh["result"]["indices"])))
			self.assertEqual(len(data), 3 * 24 * 16)
			shader, _ = client.call("shader", {"stage": "fs", "eid": draw})
			result = shader["result"]
			self.assertEqual(["frame", "patternTexture", "checkerTexture"], [b["name"] for b in result["bindings"]])
			self.assertIn("OpEntryPoint Fragment %main \"main\"", result["disassembly"])
			timing, _ = client.call("timing", {"repeat": 2})
			stats = {s[0]: s for s in timing["result"]["stats"]}
			dispatch = [e for e in self.events if e[2] == "dispatch"][0][0]
			self.assertEqual(256 * 256, stats[dispatch][11])   # csInvocations
		finally:
			client.close()


class C4CaptureReplay(unittest.TestCase):
	"""The C4 sample: Gpu::Capture loads the library from FLASHBANG_DLL, wraps the device, and captures the one frame it
	triggers between StartFrame and EndFrame."""

	@classmethod
	def setUpClass(cls):
		if not C4:
			raise unittest.SkipTest("no C4 compiler given (--c4)")
		if not os.path.isfile(os.path.join(os.path.dirname(C4), "Bedrock", "gpu.ch")):
			raise unittest.SkipTest("the C4 toolchain predates Bedrock's Gpu module (nuke build -p C4 refreshes dev)")
		cls.folder = tempfile.mkdtemp(prefix="flashbang-c4-")
		program = os.path.join(cls.folder, "fb-sample-c4.exe")
		built = run([C4, os.path.join(REPO, "plugins", "gpu", "samples", "c4"), "-o", program], timeout=600)
		if built.returncode != 0:
			raise AssertionError(f"the C4 sample does not build: {built.stdout} {built.stderr}")
		captures = os.path.join(cls.folder, "captures")
		env = dict(os.environ, FLASHBANG_DLL=os.path.join(BIN, "flashbang.dll"), FLASHBANG_CAPTURE_DIR=captures)
		cls.sample = run([program, "--frames", "5", "--capture-frame", "2"], env=env)
		if cls.sample.returncode == 2:
			raise unittest.SkipTest(f"the C4 sample has no D3D12 device here: {cls.sample.stdout.strip()}")
		if cls.sample.returncode != 0:
			raise AssertionError(f"the C4 sample failed: {cls.sample.stdout} {cls.sample.stderr}")
		cls.captures = sorted(glob.glob(os.path.join(captures, "*.flash")))
		cls.events = replay(cls.captures[0], "events", device=False)["events"] if cls.captures else []

	@classmethod
	def tearDownClass(cls):
		if hasattr(cls, "folder"):
			shutil.rmtree(cls.folder, ignore_errors=True)

	def test_only_the_triggered_frame(self):
		self.assertEqual(1, len(self.captures), self.sample.stdout)
		said = [line[len("captured "):] for line in self.sample.stdout.splitlines() if line.startswith("captured ")]
		self.assertEqual([os.path.normcase(os.path.normpath(self.captures[0]))], [os.path.normcase(os.path.normpath(path)) for path in said], "CapturePath names it")
		with open(self.captures[0][:-len(".flash")] + ".json", encoding="utf-8") as f:
			sidecar = json.load(f)
		self.assertEqual(("fb-sample-c4", "D3D12", 3, "fb-sample-c4"), (sidecar["program"], sidecar["api"], sidecar["frame"], sidecar["comment"]))
		self.assertEqual({"frame": "even"}, sidecar["annotations"])
		self.assertNotIn("thumbnail", sidecar, "nothing was presented, so there is no picture of it")
		self.assertEqual(len(self.events), sidecar["events"])

	def test_events_end_with_the_frame(self):
		named = {e[6]: e for e in self.events}
		marker = named["Clear"]
		clears = [e for e in self.events if e[2] == "clear"]
		self.assertEqual(1, len(clears))
		self.assertEqual(marker[0], clears[0][1], "the clear sits in its marker")
		self.assertIn("Signal(Frame fence, 3)", named)
		self.assertEqual(["present", "EndFrame"], self.events[-1][2::4])

	def test_the_target_holds_the_clear_color(self):
		resources = {r["name"]: r for r in replay(self.captures[0], "resources", device=False)["resources"]}
		clear = [e for e in self.events if e[2] == "clear"][0][0]
		client = RpcClient()
		try:
			client.call("initialize", {"capture": self.captures[0]})
			pixel, _ = client.call("pixel", {"id": resources["Target"]["id"], "eid": clear, "x": 100, "y": 200})
			for got, want in zip(pixel["result"]["values"], (0.25, 0.5, 0.5, 1.0)):
				self.assertAlmostEqual(want, got, places=2)
		finally:
			client.close()


def main():
	global BIN, C4
	parser = argparse.ArgumentParser()
	parser.add_argument("--bin", required=True)
	parser.add_argument("--c4", default="", help="the C4 compiler that builds the C4 sample")
	options, rest = parser.parse_known_args()
	BIN = os.path.abspath(options.bin)
	C4 = os.path.abspath(options.c4) if options.c4 else ""
	unittest.main(argv=[sys.argv[0]] + rest, verbosity=2)


if __name__ == "__main__":
	main()
