# Captures the sample programs with the capture library and replays the captures (docs/22): the files a capture leaves,
# the replayer's events, state, readbacks, timing and its ied-gpu/1 framing on stdio. A machine without a D3D12 (or
# Vulkan) GPU skips what it cannot run. Stdlib only.
#   python -I test_flashbang.py --bin build/bin/Development-windows-x64/flashbang
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


def main():
	global BIN
	parser = argparse.ArgumentParser()
	parser.add_argument("--bin", required=True)
	options, rest = parser.parse_known_args()
	BIN = os.path.abspath(options.bin)
	unittest.main(argv=[sys.argv[0]] + rest, verbosity=2)


if __name__ == "__main__":
	main()
