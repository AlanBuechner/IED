"""The tools a model builds, runs and looks things up with, on a C4 project (docs/13 "As built"): docs_search and
docs_read over a plugin's aiDocs, a plugin's aiHints and the source line under each build error, program_run, and
fs_edit saying where a missed old text differs from the file."""

import glob
import json
import os
import shutil
import subprocess
import unittest

from test_chat import REPO, write
from test_code_tools import CodeToolCase, NUKE, PRINTING_MAIN_CF, RING_CF, RING_CH

GUIDE_MD = """# Demo library

What the library offers.

## Frobnicator

A Frobnicator turns widgets into gadgets, one call per batch.

    int64 Frobnicate(int64 widgets)

## Other

Nothing to see.
"""

BROKEN_MAIN_CF = """include "ring.ch"

int32 main() {
	auto filled = 3;
	return filled;
}
"""

DEMO_PLUGIN = {"manifestVersion": 1, "id": "demo-docs", "name": "Demo docs", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
	"contributes": {"aiDocs": [{"id": "demo", "title": "Demo library reference", "folder": "docs"}],
		"aiHints": [{"match": "type not found: auto", "hint": "write the type; there is no auto"}]}}


@unittest.skipUnless(os.path.exists(os.path.join(REPO, "toolchains", "c4", "dev", "C4.exe")), "no dev C4 toolchain (nuke build makes it)")
class WorkToolTests(CodeToolCase):
	def prepare(self):
		def nuke(*words):
			done = subprocess.run(["cmd", "/c", NUKE] + list(words), cwd=self.repo, capture_output=True, text=True)
			self.assertEqual(0, done.returncode, done.stdout + done.stderr)
		nuke("init", "--toolchain", "dev", "--ied")
		shutil.copytree(os.path.join(REPO, "plugins", "nuke"), os.path.join(self.home, "plugins", "nuke"))
		plugin = os.path.join(self.home, "plugins", "demo-docs")
		write(os.path.join(plugin, "ied-plugin.json"), json.dumps(DEMO_PLUGIN))
		write(os.path.join(plugin, "docs", "guide.md"), GUIDE_MD)
		write(os.path.join(self.repo, "src", "ring.ch"), RING_CH)
		write(os.path.join(self.repo, "src", "ring.cf"), RING_CF)
		write(os.path.join(self.repo, "src", "main.cf"), PRINTING_MAIN_CF)
		nuke("build")

	def program(self):
		built = [p for p in glob.glob(os.path.join(self.repo, "build", "bin", "*", "*", "*.exe")) if not p.endswith("-tests.exe")]
		self.assertEqual(1, len(built), built)
		return os.path.relpath(built[0], self.repo).replace("\\", "/")

	async def test_reference_docs_of_a_plugin(self):
		results = await self.results(
			("s1", "docs_search", {"query": "Frobnicate widgets"}),
			("r1", "docs_read", {"path": "demo/guide.md", "heading": "Frobnicator"}),
			("r2", "docs_read", {"path": "demo/guide.md"}),
			("r3", "docs_read", {"path": "demo/missing.md"}))
		self.assertIn("demo/guide.md", results["s1"], results["s1"])
		self.assertIn("Frobnicator", results["s1"], "the section that matches")
		self.assertIn("int64 Frobnicate(int64 widgets)", results["r1"], "the section's text")
		self.assertNotIn("Nothing to see", results["r1"], "only that section")
		self.assertIn("Other", results["r2"], "a path alone lists the headings")
		self.assertIn("missing.md", results["r3"], results["r3"])

	async def test_running_a_built_program(self):
		results = await self.results(
			("p1", "program_run", {"program": self.program(), "args": ["one", "two words"]}),
			("p2", "program_run", {"program": self.program(), "repeat": 3}),
			("p3", "program_run", {"program": "build/bin/no-such.exe"}), permission="auto")
		self.assertIn("exit 0 after", results["p1"], results["p1"])
		self.assertIn("CPU", results["p1"], "what it used, read once it ended")
		self.assertIn("peak memory", results["p1"])
		self.assertIn("--- stdout", results["p1"])
		self.assertIn("filled 2", results["p1"], "its output")
		self.assertIn("--- stderr: nothing", results["p1"], "the streams apart")
		self.assertIn("median", results["p2"], "repeated runs for timing")
		self.assertIn("did not start", results["p3"], results["p3"])

	async def test_a_task_update_before_any_task(self):
		results = await self.results(("u1", "task_update", {"id": "t1", "status": "in_progress"}))
		self.assertIn("the chat has no tasks yet", results["u1"], results["u1"])
		self.assertIn("tasks_set(", results["u1"], "the call to make instead (a model asked five times with the old empty list)")

	async def test_errors_with_their_source_lines_and_hints(self):
		await self.client.request("settings.set", {"tier": "app", "key": "nuke.command", "value": os.path.join(REPO, "bin", "nuke.cmd")})
		write(os.path.join(self.repo, "src", "main.cf"), BROKEN_MAIN_CF)
		results = await self.results(
			("b1", "build_run", {}),
			("e1", "fs_edit", {"path": "src/main.cf", "edits": [{"old": "\tauto filled = 3;\n\treturn filed;", "new": "\tint32 filled = 3;\n\treturn filled;"}]}),
			("e2", "fs_edit", {"path": "src/main.cf", "edits": [{"old": "func main() { os.Exit(0) }", "new": "x"}]}), permission="auto")
		self.assertIn("src/main.cf:4:2: error: type not found: auto", results["b1"], results["b1"])
		self.assertIn("    4 | auto filled = 3;", results["b1"], "the line the error names, under it")
		self.assertIn("hint: write the type; there is no auto", results["b1"], "the plugin's hint for it")
		self.assertIn("line 5: the file agrees with the start of old", results["e1"], results["e1"])
		self.assertIn("then has `led;", results["e1"], "what the file has where the old text left it")
		self.assertIn("where old has `ed;`", results["e1"])
		self.assertIn("no part of it is in the file", results["e2"], results["e2"])
