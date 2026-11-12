"""Sandboxes (docs/34) against the real Docker engine: the engine and its platforms; a definition made through the
window's method and the AI's sandbox_create (its approval card a diff of sandbox.json); an image built from the catalog
(Alpine, through sandbox.registryMirror so Docker Hub's anonymous limit never fails the check), the workspace mounted
at /workspace with .ied read-only and the network off; the same C program validated natively and on emulated arm64 (a
pass/fail matrix, streamed output, the matrix kept in .ied/local); sandbox_run's exit codes and a time limit that ends
the command inside the container; terminal profiles per sandbox. Skipped without a running Docker engine."""

import asyncio
import json
import os
import shutil
import subprocess
import sys
import unittest

from server import local_state
from test_chat import REPO, ChatCase, call_step, text_step, write

TURN_SECONDS = 900
MIRROR = "mirror.gcr.io/library"

HELLO = """#include <stdio.h>
int main(void) {
\tunsigned int one = 1;
\tprintf("hello from %s, %zu-bit, %s-endian\\n", ARCH, sizeof(void*) * 8, *(unsigned char*)&one ? "little" : "big");
\treturn 0;
}
"""

COMMAND = "mkdir -p build/$IED_SANDBOX && cc -DARCH=\\\"$(uname -m)\\\" -o build/$IED_SANDBOX/hello src/hello.c && build/$IED_SANDBOX/hello"


def docker_ready():
	docker = shutil.which("docker") or os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "DockerDesktop", "resources", "bin", "docker.exe")
	try:
		return subprocess.run([docker, "version", "--format", "{{.Server.Version}}"], capture_output=True, timeout=20).returncode == 0, docker
	except (OSError, subprocess.TimeoutExpired):
		return False, docker


READY, DOCKER = docker_ready()


@unittest.skipUnless(READY, "no running Docker engine")
class SandboxTests(ChatCase):
	def setUp(self):
		super().setUp()
		for plugin in ("sandbox", "terminal"):
			shutil.copytree(os.path.join(REPO, "plugins", plugin), os.path.join(self.home, "plugins", plugin))
		write(os.path.join(self.repo, "src", "hello.c"), HELLO)
		self.server.stop(remove_data=False)
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)

	def tearDown(self):
		label = "label=ied.workspace=" + self.repo.replace("\\", "/")
		for kind, listing in (("rm", ["ps", "-aq"]), ("rmi", ["images", "-q"])):
			found = subprocess.run([DOCKER] + listing + ["--filter", label], capture_output=True, text=True).stdout.split()
			if found:
				subprocess.run([DOCKER, kind, "-f"] + found, capture_output=True)
		super().tearDown()

	async def asyncSetUp(self):
		await super().asyncSetUp()
		await self.client.request("settings.set", {"tier": "app", "key": "sandbox.registryMirror", "value": MIRROR})
		await self.client.request("settings.set", {"tier": "app", "key": "sandbox.idleStopMinutes", "value": 0})
		await self.client.sub(["sandbox:w1"])

	async def snapshot(self):
		return await self.client.request("ied.sandbox/state", {"wsId": "w1"})

	async def until(self, predicate, timeout=TURN_SECONDS):
		"""The topic's frames until a "changed" one whose data `predicate` accepts; its data."""
		frames = await self.client.wait_for("sandbox:w1", lambda f: f.get("kind") == "changed" and predicate(f["data"]), timeout=timeout)
		return frames[-1]["data"]

	async def results(self, *calls, permission="auto"):
		chat_id, _ = await self.chat(permission=permission)
		self.model.queue(*[call_step(name, args, call_id) for call_id, name, args in calls], text_step("Done."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=TURN_SECONDS))["stop"])
		sent = self.model.requests()[-1]["body"]["messages"]
		return {m["tool_call_id"]: m["content"] for m in sent if m["role"] == "tool"}

	async def test_the_window_validates_natively_and_on_emulated_arm64(self):
		state = await self.snapshot()
		engine = (await self.until(lambda d: d["engine"]["state"] == "ready", timeout=60))["engine"]
		platforms = [p["platform"] for p in engine["platforms"]]
		self.assertIn("linux/arm64", platforms, "Docker Desktop emulates arm64")
		self.assertIn({"platform": "linux/386", "native": True}, engine["platforms"], "32-bit x86 runs on a 64-bit x86 engine itself")
		self.assertEqual([], state["sandboxes"])
		catalog = await self.client.request("ied.sandbox/catalog", {"wsId": "w1"})
		alpine = [s for s in catalog["systems"] if s["id"] == "alpine"][0]
		version = [v for v in alpine["versions"] if v["default"]][0]
		self.assertEqual(MIRROR + "/" + version["image"], version["pull"], "official images come through the mirror")
		self.assertIn("linux/arm64", [p["platform"] for p in version["platforms"] if p["runs"]])
		self.assertIn("build", [p["id"] for p in catalog["presets"]])

		for name, platform in (("alpine", ""), ("alpine-arm64", "linux/arm64")):
			made = await self.client.request("ied.sandbox/create", {"wsId": "w1", "name": name, "os": "alpine", "platform": platform, "packages": ["gcc", "musl-dev"], "validate": COMMAND})
			self.assertEqual(name, made["name"])
		with open(os.path.join(self.repo, ".ied", "sandbox", "alpine-arm64", "sandbox.json"), encoding="utf-8") as f:
			definition = json.load(f)
		self.assertEqual(("alpine", "linux/arm64", "none"), (definition["os"], definition["platform"], definition["network"]))
		with self.assertRaises(Exception):
			await self.client.request("ied.sandbox/create", {"wsId": "w1", "name": "alpine", "os": "alpine"})
		listed = await self.until(lambda d: len(d["sandboxes"]) == 2, timeout=30)
		self.assertTrue(all(not box["imageBuilt"] for box in listed["sandboxes"]))

		await self.client.request("ied.sandbox/validate", {"wsId": "w1"})
		done = await self.until(lambda d: d["validation"] and not d["validation"]["running"] and d["validation"]["rows"])
		rows = {row["sandbox"]: row for row in done["validation"]["rows"]}
		self.assertEqual({"alpine": "pass", "alpine-arm64": "pass"}, {k: v["result"] for k, v in rows.items()}, rows)
		frames = self.client.drain("sandbox:w1")
		outputs = "".join(f["data"].get("text", "") for f in frames if f.get("kind") == "output")
		final = await self.snapshot()
		texts = "\n".join(run.get("output", "") for run in final["runs"]) + outputs
		self.assertIn("hello from x86_64, 64-bit, little-endian", texts)
		self.assertIn("hello from aarch64, 64-bit, little-endian", texts, "the arm64 build ran under emulation")
		self.assertTrue(os.path.isfile(os.path.join(self.repo, "build", "alpine-arm64", "hello")), "the build wrote into the mounted workspace")
		boxes = {box["name"]: box for box in final["sandboxes"]}
		self.assertTrue(boxes["alpine-arm64"]["imageBuilt"] and boxes["alpine-arm64"]["emulated"])
		self.assertEqual("running", boxes["alpine"]["container"])
		with open(os.path.join(local_state(self.data, self.repo), "sandbox", "validation.json"), encoding="utf-8") as f:
			self.assertEqual(2, len(json.load(f)["rows"]), "the matrix outlives the server")
		profiles = (await self.client.request("term.profiles", {"wsId": "w1"}))["profiles"]
		self.assertIn("sandbox-alpine-arm64", [p["id"] for p in profiles], "a terminal profile per sandbox")

		await self.client.request("ied.sandbox/control", {"wsId": "w1", "name": "alpine", "action": "stop"})
		await self.until(lambda d: [b for b in d["sandboxes"] if b["name"] == "alpine" and b["container"] == "stopped"], timeout=60)

	async def test_the_ai_creates_runs_and_validates(self):
		results = await self.results(
			("l1", "sandbox_list", {}),
			("k1", "sandbox_catalog", {"query": "alpine"}),
			("c1", "sandbox_create", {"name": "alp", "os": "alpine", "packages": ["gcc", "musl-dev"], "validate": COMMAND, "description": "musl check"}),
			("r1", "sandbox_run", {"name": "alp", "command": "cat /etc/alpine-release && pwd && ls src && mkdir -p build && echo out > build/probe.txt && touch .ied/nope; echo exit-was-$?"}),
			("r2", "sandbox_run", {"name": "alp", "command": "wget -q -T 3 -O /dev/null http://example.com; echo net-$?"}),
			("r3", "sandbox_run", {"name": "alp", "command": "echo failing >&2; exit 3"}),
			("r4", "sandbox_run", {"name": "alp", "command": "sleep 300", "timeout_s": 3}),
			("r5", "sandbox_run", {"name": "alp", "command": "ps -o args | grep -c '[s]leep 300'; echo $IED_SANDBOX $IED_PLATFORM"}),
			("v1", "sandbox_validate", {}),
			("n1", "sandbox_run", {"name": "nosuch", "command": "true"}))
		self.assertIn("Docker: ready", results["l1"])
		self.assertIn("No sandboxes in this workspace yet", results["l1"])
		self.assertIn("alpine - Alpine Linux (apk)", results["k1"])
		self.assertIn("build: a C and C++ compiler and make (", results["k1"], "a query lists the presets' packages")
		self.assertIn("Wrote .ied/sandbox/alp/sandbox.json", results["c1"])
		self.assertIn("built the image", results["r1"], "the first run builds and starts it")
		self.assertIn("/workspace\n", results["r1"])
		self.assertIn("hello.c", results["r1"])
		self.assertIn("exit-was-1", results["r1"], ".ied is read-only inside")
		self.assertTrue(os.path.isfile(os.path.join(self.repo, "build", "probe.txt")), results["r1"])
		self.assertNotIn("net-0", results["r2"], "the network is off")
		self.assertIn("exit 3", results["r3"])
		self.assertIn("failing", results["r3"])
		self.assertIn("ran out of time", results["r4"])
		self.assertIn("\n0\n", results["r5"], "the timed-out command was ended inside the container")
		self.assertIn("alp linux/amd64", results["r5"], "a run knows its sandbox and platform")
		self.assertIn("Validation: 1 of 1 passed", results["v1"])
		self.assertIn("no sandbox named nosuch; the workspace has alp", results["n1"])
		system = self.model.requests()[-1]["body"]["messages"][0]["content"]
		self.assertIn("sandbox_* tools check that this workspace's program works", system, "a workspace without sandboxes hears of them")
		chat_id, _ = await self.chat(permission="auto")
		self.model.queue(text_step("ok"))
		await self.turn(chat_id)
		system = self.model.requests()[-1]["body"]["messages"][0]["content"]
		self.assertIn("This workspace's sandboxes (Docker; the workspace is /workspace inside each): alp (alpine)", system, "a new chat lists them")

	async def test_creating_asks_with_the_file_as_a_diff(self):
		chat_id, topic = await self.chat(permission="ask")
		self.model.queue(call_step("sandbox_create", {"name": "deb", "os": "debian:12", "platform": "linux/386", "presets": ["build"], "network": "full"}, "c1"), text_step("Done."))
		await self.client.request("chat.send", {"chatId": chat_id, "text": "Make a sandbox"})
		request = (await self.client.wait_for(topic, lambda f: f.get("kind") == "approval.request", timeout=60))[-1]["data"]
		text = json.dumps(request)
		self.assertIn("Create sandbox deb (debian:12, linux/386, network on)", text)
		self.assertIn("+++ b/.ied/sandbox/deb/sandbox.json", text)
		self.assertIn('\\"platform\\": \\"linux/386\\"', text)
		await self.client.request("chat.approve", {"chatId": chat_id, "approvalId": request["approvalId"], "decision": "deny"})
		await self.client.wait_for(topic, lambda f: f.get("kind") == "turn.end", timeout=60)
		self.assertFalse(os.path.exists(os.path.join(self.repo, ".ied", "sandbox", "deb")), "denied: nothing written")


if __name__ == "__main__":
	unittest.main()
