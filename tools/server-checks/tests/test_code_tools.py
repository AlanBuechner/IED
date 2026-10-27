"""The AI's code tools (docs/13 "Navigating code", docs/24 "Plugin code asks language servers"). Against a real clangd
on a small C++ project (skipped without the vendored clangd, which setup.bat installs): a file's outline, the
workspace's symbols - the servers started and indexed for the first search - a symbol's whole source, definitions,
references by the function they sit in, hover, a type's members with their declarations and modifiers (inherited ones
through the type hierarchy), the type of a variable, implementations, callers and callees, base and derived types, and
a macro. Against the fake language server: a language plugin's own requests (aiExposedRequests, the expandMacro
purpose), a file no server serves, and files nobody has open - opened on the server alone, refreshed after a write,
the oldest closed past 16, taken over by the store when a document opens."""

import asyncio
import json
import os
import shutil
import subprocess
import sys
import unittest

from test_chat import REPO, ChatCase, call_step, text_step, write

CLANGD = os.path.join(REPO, "vendor", "llvm", "bin", "clangd.exe")
FAKE_LSP = os.path.join(REPO, "tools", "server-checks", "fake_lsp.py")
C4LSP = os.path.join(REPO, "toolchains", "c4", "dev", "tools", "C4LSP", "C4LSP.exe")
BEDROCK_QUEUE = os.path.join(REPO, "toolchains", "c4", "dev", "Bedrock", "Container", "priorityqueue.ch")
NUKE = os.path.join(REPO, "nuke.bat")
TURN_SECONDS = 120

RING_CH = """// A ring of values and what it writes to.
include "bedrock.ch"

#define RING_CAPACITY 64
#define TWICE(x) ((x) * 2)

namespace Game
{
	// Something values can be written to.
	interface ISink
	{
		// Writes one value.
		void Write(int32 value);
	}

	// A ring buffer of values.
	class Ring : ISink
	{
		[operator("create")] public void Init() {}
		// Pushes one value; drops it when full.
		public void Push(int32 value);
		// How many values it holds.
		public int32 Count() const;
		public static int32 Capacity() { return RING_CAPACITY; }
		public bool Empty() const { return count == 0; }
		public void Write(int32 value);

		private int32 count;
		public static const int32 kLimit = TWICE(RING_CAPACITY);
	}

	int32 Fill(Ring& ring);
}
"""

RING_CF = """include "ring.ch"

namespace Game
{
	// Adds the value at the end.
	void Ring::Push(int32 value) {
		if (count < Capacity()) count += 1;
	}

	int32 Ring::Count() const { return count; }

	void Ring::Write(int32 value) { Push(value); }

	int32 Fill(Ring& ring) {
		ring.Push(TWICE(3));
		ring.Write(4);
		return ring.Count();
	}
}
"""

MAIN_CF = """// Arena - entry point.
include "ring.ch"

int32 main() {
	Game::Ring ring;
	return Game::Fill(ring);
}
"""

RING_TESTS_CF = """include "ring.ch"

[test("ring.push")]
static void PushCounts() {
	Game::Ring ring;
	ring.Push(1);
	assert_eq(1, ring.Count(), "one value");
}

[test("ring.full")]
static void ReportsAFailure() {
	Game::Ring ring;
	assert_eq(5, ring.Count(), "deliberately wrong");
}
"""

PRINTING_MAIN_CF = """include "ring.ch"

int32 main() {
	Game::Ring ring;
	printf("filled %d\\n", Game::Fill(ring));
	return 0;
}
"""

RING_H = """#pragma once
#define RING_CAPACITY 64
#define TWICE(x) ((x) * 2)

namespace game {

/// Something values can be written to.
class Sink {
public:
	virtual ~Sink() = default;
	/// Writes one value.
	virtual void Write(int value) = 0;
};

/// A ring buffer of ints.
class Ring : public Sink {
public:
	Ring();
	/// Pushes one value; drops it when full.
	void Push(int value);
	/// How many values it holds.
	int Count() const;
	static int Capacity() { return RING_CAPACITY; }
	inline bool Empty() const { return count == 0; }
	void Write(int value) override;

private:
	int values[RING_CAPACITY];
	int count = 0;
	static const int kLimit = TWICE(RING_CAPACITY);
};

}
"""

RING_CPP = """#include "ring.h"

namespace game {

Ring::Ring() {}

// Adds the value at the end.
void Ring::Push(int value) {
	if (count < Capacity()) {
		values[count] = value;
		count += 1;
	}
}

int Ring::Count() const { return count; }

void Ring::Write(int value) { Push(value); }

}
"""

GAME_CPP = """#include "ring.h"

int Fill(game::Ring& ring) {
	ring.Push(TWICE(3));
	ring.Write(4);
	return ring.Count();
}

int main() {
	game::Ring ring;
	return Fill(ring);
}
"""


class CodeToolCase(ChatCase):
	"""A chat server whose workspace a language plugin serves (`prepare` adds it), the server started again with it."""

	def setUp(self):
		super().setUp()
		self.prepare()
		self.server.stop(remove_data=False)
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)

	async def results(self, *calls, permission="ask"):
		"""Each call run in one turn; the model's results by call id."""
		chat_id, _ = await self.chat(permission=permission)
		self.model.queue(*[call_step(name, args, call_id) for call_id, name, args in calls], text_step("Done."))
		self.assertEqual("finished", (await self.turn(chat_id, timeout=TURN_SECONDS))["stop"])
		sent = self.model.requests()[-1]["body"]["messages"]
		results = {m["tool_call_id"]: m["content"] for m in sent if m["role"] == "tool"}
		dump = os.environ.get("IED_CODE_TOOLS_DUMP")
		if dump:
			with open(dump, "a", encoding="utf-8") as f:
				for call_id, name, args in calls:
					f.write("=== %s %s\n%s\n\n" % (name, json.dumps(args), results.get(call_id)))
		return results


@unittest.skipUnless(os.path.exists(CLANGD), "no vendored clangd (setup.bat installs it)")
class ClangdCodeToolTests(CodeToolCase):
	def prepare(self):
		shutil.copytree(os.path.join(REPO, "plugins", "cpp"), os.path.join(self.home, "plugins", "cpp"))
		write(os.path.join(self.repo, "src", "ring.h"), RING_H)
		write(os.path.join(self.repo, "src", "ring.cpp"), RING_CPP)
		write(os.path.join(self.repo, "src", "game.cpp"), GAME_CPP)
		commands = [{"directory": self.repo, "file": os.path.join(self.repo, "src", name), "arguments": ["clang++", "-std=c++17", "-c", os.path.join("src", name)]} for name in ("ring.cpp", "game.cpp")]
		write(os.path.join(self.repo, "compile_commands.json"), json.dumps(commands))

	async def asyncSetUp(self):
		await super().asyncSetUp()
		await self.client.request("settings.set", {"tier": "app", "key": "cpp.clangd.path", "value": CLANGD})

	async def test_navigating_a_cpp_project(self):
		results = await self.results(
			("s1", "code_symbols", {"query": "Push"}),
			("o1", "code_symbols", {"path": "src/ring.h"}),
			("r1", "code_read_symbol", {"symbol": "Ring::Push"}),
			("r2", "code_read_symbol", {"symbol": "game::Ring::Push", "declaration": True}),
			("r3", "code_read_symbol", {"path": "src/ring.cpp", "line": 10}),
			("d1", "code_definition", {"path": "src/game.cpp", "line": 4, "symbol": "Push"}),
			("f1", "code_references", {"symbol": "Ring::Push"}),
			("h1", "code_hover", {"symbol": "Ring::Count"}),
			("h2", "code_hover", {"symbol": "Ring::values"}),
			("m1", "code_members", {"symbol": "Ring", "inherited": True}),
			("t1", "code_type_definition", {"path": "src/game.cpp", "line": 10, "symbol": "ring"}),
			("i1", "code_implementations", {"symbol": "Sink::Write"}),
			("c1", "code_call_hierarchy", {"symbol": "Ring::Push", "direction": "incoming", "depth": 2}),
			("c2", "code_call_hierarchy", {"symbol": "Fill", "direction": "outgoing"}),
			("y1", "code_type_hierarchy", {"symbol": "Sink", "direction": "subtypes"}),
			("x1", "code_expand_macro", {"path": "src/game.cpp", "line": 4, "symbol": "TWICE"}),
			("n1", "code_read_symbol", {"symbol": "Pop"}),
			("a1", "code_hover", {"symbol": "Write"}))
		for call_id in ("s1", "o1", "r1", "r2", "r3", "d1", "f1", "h1", "m1", "t1", "i1", "c1", "c2", "y1", "x1"):
			self.assertNotIn("did not run", results[call_id], call_id)
		self.assertIn("method game::Ring::Push  src/ring.cpp:8", results["s1"], "the servers started and indexed for the first search")
		self.assertIn("Its source, src/ring.cpp:\n7\t// Adds the value at the end.\n8\tvoid Ring::Push(int value) {", results["s1"], "a lone match comes with its definition")
		self.assertIn("class Ring", results["o1"])
		self.assertIn("Push", results["o1"])
		self.assertIn("8\tvoid Ring::Push(int value) {", results["r1"], "the definition, with its body")
		self.assertIn("13\t}", results["r1"])
		self.assertIn("7\t// Adds the value at the end.", results["r1"], "the comment above it")
		self.assertIn("Also declared at src/ring.h:20", results["r1"])
		self.assertIn("Defined at src/ring.cpp:8", results["r2"])
		self.assertIn("20\t\tvoid Push(int value);", results["r2"], "the declaration when asked")
		self.assertIn("10\t\t\tvalues[count] = value;", results["r3"], "the function holding the line")
		self.assertIn("method game::Ring::Push: void (int) - src/ring.cpp lines 8-13", results["d1"], "the definition")
		self.assertIn("src/ring.h line 20", results["d1"], "and the declaration")
		self.assertIn("src/game.cpp", results["f1"])
		self.assertIn("(in Fill): ring.Push(TWICE(3));", results["f1"], "each use with the function it is in")
		self.assertIn("(in game::Ring::Write)", results["f1"])
		self.assertIn("int Ring::Count() const", results["h1"])
		self.assertIn("How many values it holds.", results["h1"])
		self.assertIn("L23  method  public: static int Capacity()", results["m1"], "a member's declaration with its modifiers")
		self.assertIn("inline bool Empty() const  [readonly]", results["m1"], "and the modifiers its semantic tokens add")
		self.assertIn("void Write(int value) override  [virtual]", results["m1"])
		self.assertIn("L28  field  private: int values[64]\n", results["m1"], "a field has no doc summary from the hover's facts")
		self.assertIn("Pushes one value", results["m1"], "its doc comment")
		self.assertIn("from Sink (src/ring.h:8)\n", results["m1"], "inherited members")
		self.assertIn("virtual void Write(int value) = 0  [abstract]  - Writes one value.", results["m1"])
		self.assertIn("1 type definition for src/game.cpp:10:\nclass game::Ring - src/ring.h lines 16-31", results["t1"])
		self.assertIn("method game::Ring::Write: void (int) - src/ring.cpp line 17", results["i1"])
		self.assertIn("  function Fill - src/game.cpp:3 (call at line 4)\n    function main - src/game.cpp:9 (call at line 11)", results["c1"], "callers two levels up")
		self.assertIn("  method Push (game::Ring::Push) - src/ring.cpp:8 (call at line 4)", results["c2"])
		self.assertIn("class Sink - src/ring.h:8 is derived by:\n  class Ring (game::Ring) - src/ring.h:16", results["y1"])
		self.assertIn("// Expands to\n((3) * 2)", results["x1"], "clangd's hover says what the use expands to")
		self.assertIn("Defined at src/ring.h:3", results["x1"])
		self.assertIn("no symbol Pop", results["n1"])
		self.assertIn("names several symbols", results["a1"], "Sink::Write and Ring::Write")


@unittest.skipUnless(os.path.exists(C4LSP), "no dev C4 toolchain (nuke build makes it)")
class C4CodeToolTests(CodeToolCase):
	"""A C4 project as `nuke init --ied` makes one and `nuke build` describes it to C4LSP, the toolchain's `c4` plugin
	pinned by the solution."""

	def prepare(self):
		def nuke(*words):
			done = subprocess.run(["cmd", "/c", NUKE] + list(words), cwd=self.repo, capture_output=True, text=True)
			self.assertEqual(0, done.returncode, done.stdout + done.stderr)
		nuke("init", "--toolchain", "dev", "--ied")
		shutil.rmtree(os.path.join(self.repo, "src", "tests"), ignore_errors=True)   # the checks' own tests, not the template's example
		shutil.copytree(os.path.join(REPO, "plugins", "nuke"), os.path.join(self.home, "plugins", "nuke"))
		write(os.path.join(self.repo, "src", "ring.ch"), RING_CH)
		write(os.path.join(self.repo, "src", "ring.cf"), RING_CF)
		write(os.path.join(self.repo, "src", "main.cf"), MAIN_CF)
		nuke("build")

	async def test_building_testing_and_running_through_build_run(self):
		await self.client.request("settings.set", {"tier": "app", "key": "nuke.command", "value": os.path.join(REPO, "bin", "nuke.cmd")})
		write(os.path.join(self.repo, "src", "tests", "ring_tests.cf"), RING_TESTS_CF)
		write(os.path.join(self.repo, "src", "main.cf"), PRINTING_MAIN_CF)
		results = await self.results(
			("b1", "build_run", {}),
			("t1", "build_run", {"task": "test"}),
			("r1", "build_run", {"task": "run"}), permission="auto")
		self.assertIn("Build succeeded", results["b1"], results["b1"])
		self.assertNotIn("filled", results["b1"], "a build's output is not shown when it succeeds")
		self.assertIn("Test failed", results["t1"], results["t1"])
		self.assertIn("src/tests/ring_tests.cf:13:1: error: deliberately wrong", results["t1"], "the failed assertion as a problem with its place")
		self.assertIn("1 passed, 1 failed", results["t1"], "the end of the output: the summary")
		self.assertIn("Run succeeded", results["r1"], results["r1"])
		self.assertIn("filled 2", results["r1"], "the program's output")

	async def test_navigating_a_c4_project(self):
		results = await self.results(
			("s1", "code_symbols", {"query": "Push"}),
			("s2", "code_symbols", {"query": "ISink"}),
			("o1", "code_symbols", {"path": "src/ring.ch"}),
			("r1", "code_read_symbol", {"symbol": "Ring::Push"}),
			("r2", "code_read_symbol", {"symbol": "Game::Ring::Push", "declaration": True}),
			("r3", "code_read_symbol", {"path": "src/ring.cf", "line": 7}),
			("d1", "code_definition", {"path": "src/ring.cf", "line": 15, "symbol": "Push"}),
			("f1", "code_references", {"path": "src/ring.cf", "line": 15, "symbol": "Push"}),
			("f2", "code_references", {"symbol": "Ring::Push"}),
			("h1", "code_hover", {"symbol": "Ring::Count"}),
			("m1", "code_members", {"symbol": "Ring", "inherited": True}),
			("c1", "code_call_hierarchy", {"symbol": "Ring::Push", "direction": "incoming", "depth": 2}),
			("c2", "code_call_hierarchy", {"symbol": "Fill", "direction": "outgoing"}),
			("y1", "code_type_hierarchy", {"symbol": "ISink", "direction": "subtypes"}),
			("x1", "code_expand_macro", {"path": "src/ring.cf", "line": 15, "symbol": "TWICE"}),
			("t1", "code_type_definition", {"path": "src/main.cf", "line": 5, "symbol": "ring"}),
			("i1", "code_implementations", {"symbol": "ISink::Write"}),
			("p1", "code_read_symbol", {"symbol": "TWICE"}),
			("b1", "code_symbols", {"query": "PriorityQueue", "kind": "class"}),
			("b2", "fs_read", {"path": BEDROCK_QUEUE, "limit": 5}),
			("g1", "code_symbols", {"path": "src"}))
		self.assertIn("method Game::Ring::Push  src/ring.cf:6\nmethod Game::Ring::Push  src/ring.ch:21\n", results["s1"], "the project's first, Bedrock's after")
		self.assertNotIn("Its source", results["s1"], "a name several symbols have is only listed")
		self.assertIn("1 symbol for \"ISink\"\ninterface Game::ISink  src/ring.ch:10\nIts source, src/ring.ch:\n9\t\t// Something values can be written to.\n10\t\tinterface ISink\n", results["s2"], "the only one, with its source")
		self.assertIn("function TWICE: macro  [5]", results["o1"], "a macro is a symbol")
		self.assertIn("  class Ring: class : ISink  [16-30]", results["o1"])
		self.assertIn("    method Capacity: static int32 ()  [24]", results["o1"], "a member's kind and signature")
		self.assertIn("    field kLimit: static const int32  [29]", results["o1"])
		self.assertIn("5\t\t// Adds the value at the end.\n6\t\tvoid Ring::Push(int32 value) {", results["r1"], "the definition with its comment")
		self.assertIn("Also declared at src/ring.ch:21", results["r1"])
		self.assertIn("20\t\t\t// Pushes one value; drops it when full.\n21\t\t\tpublic void Push(int32 value);", results["r2"], "the declaration when asked")
		self.assertNotIn("19\t", results["r2"], "the declaration above it is not an annotation of it")
		self.assertIn("Defined at src/ring.cf:6", results["r2"])
		self.assertIn("7\t\t\tif (count < Capacity()) count += 1;", results["r3"], "the function holding the line")
		self.assertIn("method Game::Ring::Push: void (int32 value) - src/ring.cf lines 5-8", results["d1"])
		self.assertIn("4 references to src/ring.cf:15 in 2 files", results["f1"])
		self.assertIn("  12 (in Game::Ring::Write): void Ring::Write(int32 value) { Push(value); }", results["f1"])
		self.assertIn("  21 (in Game::Ring::Push): public void Push(int32 value);", results["f1"])
		self.assertNotIn("did not list", results["f1"], "nothing unlisted when the server found them all")
		self.assertIn("4 references to Game::Ring::Push in 2 files", results["f2"], "asked from the declaration too")
		self.assertNotIn("did not list", results["f2"])
		self.assertIn("1 type definition for src/main.cf:5:\nclass Game::Ring", results["t1"], "a variable's type")
		self.assertIn("method Game::Ring::Write", results["i1"], "the method that implements it, not its class")
		self.assertIn("#define TWICE(x) ((x) * 2)", results["p1"], "a macro read by name")
		self.assertIn(BEDROCK_QUEUE, results["b1"], "a library's file shown by its absolute path")
		self.assertTrue(results["b2"].startswith("1\t"), "and fs_read takes that path: %s" % results["b2"][:200])
		self.assertIn("src is a folder", results["g1"])
		self.assertIn("public int32 Game::Ring::Count() const", results["h1"])
		self.assertIn("How many values it holds.", results["h1"])
		self.assertIn("  L24  method  public static int32 Capacity()", results["m1"], "static")
		self.assertIn("  L29  field  public static const int32 kLimit = ((64) * 2)", results["m1"], "static const with its value")
		self.assertIn("  L19  constructor  [operator(\"create\")] public void Init()", results["m1"], "annotations")
		self.assertIn("  L23  method  public int32 Count() const  - How many values it holds.", results["m1"], "const and the doc summary")
		self.assertIn("from ISink (src/ring.ch:10)\n  L13  method  void Write(int32 value)  - Writes one value.", results["m1"], "inherited")
		self.assertIn("  method Write (Game::Ring::Write) - src/ring.cf:12 (call at line 12)\n    function Fill (Game::Fill) - src/ring.cf:14 (call at line 16)", results["c1"])
		self.assertIn("function main - src/main.cf:4 (call at line 6)", results["c1"], "callers two levels up")
		self.assertIn("  method Count (Game::Ring::Count) - src/ring.cf:10 (call at line 17)", results["c2"])
		self.assertIn("interface ISink (Game::ISink) - src/ring.ch:10 is derived by:\n  class Ring (Game::Ring) - src/ring.ch:17", results["y1"])
		self.assertIn("TWICE at src/ring.cf:15 expands to:\n```\n((3) * 2)\n```", results["x1"], "c4/expandMacro")


class FakeServerCodeToolTests(CodeToolCase):
	def prepare(self):
		self.log = os.path.join(self.root, "fake-lsp.jsonl")
		plugin = os.path.join(self.home, "plugins", "fake")
		os.makedirs(plugin)
		shutil.copy(FAKE_LSP, plugin)
		requests = [{"method": "fake/expand", "purpose": "expandMacro", "description": "What a macro use expands to."}, {"method": "fake/echo", "description": "Its params back."}]
		write(os.path.join(plugin, "ied-plugin.json"), json.dumps({"manifestVersion": 1, "id": "fake", "name": "Fake", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
			"contributes": {"languages": [{"id": "fake", "extensions": [".fk"]}],
				"lspProfiles": [{"id": "fake-lsp", "command": sys.executable, "args": ["-I", "${pluginDir}/fake_lsp.py", self.log], "selector": {"languages": ["fake"]}, "aiExposedRequests": requests}]}}))
		write(os.path.join(self.repo, "a.fk"), "use MAKE_IT here\n")
		write(os.path.join(self.repo, "b.fk"), "MAKE_ITS\nagain MAKE_IT(1)\n// MAKE_IT in a comment\nsay(\"MAKE_IT in a string\")\nMAKE_IT alone\n")
		for i in range(18):
			write(os.path.join(self.repo, "f%d.fk" % i), "word %d\n" % i)

	def received(self, method):
		with open(self.log, encoding="utf-8") as f:
			return [m for m in map(json.loads, f) if m.get("method") == method]

	def uris(self, method):
		return [m["params"]["textDocument"]["uri"].rsplit("/", 1)[-1] for m in self.received(method)]

	async def test_a_language_plugins_own_requests(self):
		results = await self.results(
			("x1", "code_expand_macro", {"path": "a.fk", "line": 1, "symbol": "MAKE_IT"}),
			("u1", "code_custom", {"method": "fake/echo", "path": "a.fk", "line": 1, "symbol": "here", "params": {"extra": 1}}),
			("u2", "code_custom", {"method": "fake/nope", "path": "a.fk"}),
			("n1", "code_hover", {"path": "src/main.c", "line": 1}),
			("r1", "code_references", {"path": "a.fk", "line": 1, "symbol": "MAKE_IT"}))
		self.assertIn("MAKE_IT at a.fk:1 expands to:", results["x1"], "the request its profile names for the purpose")
		self.assertIn("make_it(1)", results["x1"])
		self.assertIn('"extra":1', results["u1"], "the params given")
		self.assertIn('"position":{"line":0,"character":12}', results["u1"], "and the place, at the symbol on the line")
		self.assertIn("does not expose fake/nope; it exposes fake/expand, fake/echo", results["u2"])
		self.assertIn("no language server serves src/main.c", results["n1"])
		self.assertIn("0 references to a.fk:1", results["r1"])
		self.assertIn("did not list these lines that use MAKE_IT", results["r1"], "a server that finds none: the lines using it, to check")
		self.assertIn("  b.fk:2: again MAKE_IT(1)\n", results["r1"])
		self.assertNotIn("MAKE_ITS", results["r1"], "whole words only")
		for noise in ("in a comment", "in a string", "MAKE_IT alone"):
			self.assertNotIn(noise, results["r1"], "only uses as code: called, or reached through . -> ::")
		self.assertEqual(["a.fk"], self.uris("textDocument/didOpen"), "opened on the server once for the asking")

	async def test_files_nobody_has_open(self):
		calls = [("h%d" % i, "code_hover", {"path": "f%d.fk" % i, "line": 1}) for i in range(17)]
		calls += [("w1", "fs_write", {"path": "f16.fk", "content": "changed\n"}), ("h99", "code_hover", {"path": "f16.fk", "line": 1})]
		results = await self.results(*calls, permission="acceptEdits")
		self.assertIn("at 0:0", results["h0"], "each answered")
		self.assertEqual(["f%d.fk" % i for i in range(17)], self.uris("textDocument/didOpen"), "each opened on the server for the asking")
		self.assertEqual(["f0.fk"], self.uris("textDocument/didClose"), "past 16 the oldest is closed")
		changes = self.received("textDocument/didChange")
		self.assertEqual(["f16.fk"], [m["params"]["textDocument"]["uri"].rsplit("/", 1)[-1] for m in changes], "a file written since is sent again")
		self.assertEqual("changed\n", changes[0]["params"]["contentChanges"][0]["text"])
		await self.client.request("doc.open", {"wsId": "w1", "path": "f15.fk"})
		for _ in range(50):
			if self.uris("textDocument/didClose").count("f15.fk") == 1 and self.uris("textDocument/didOpen").count("f15.fk") == 2:
				break
			await asyncio.sleep(0.1)
		self.assertEqual(["f0.fk", "f15.fk"], self.uris("textDocument/didClose"), "a document that opens takes the file over")
		self.assertEqual(2, self.uris("textDocument/didOpen").count("f15.fk"))


@unittest.skipUnless(os.path.exists(C4LSP), "no dev toolchain with C4LSP (nuke build makes it)")
class C4AfterAiWriteTests(CodeToolCase):
	"""docs/24 "After an AI edit" with the real C4LSP and this checkout's c4 plugin (plugins/c4, the built-in, which
	asks for both and finds the dev toolchain in the store as it does on a machine): a C4 file the AI writes comes back
	formatted, and an error in it is in the result."""

	def prepare(self):
		def nuke(*words):
			done = subprocess.run(["cmd", "/c", NUKE] + list(words), cwd=self.repo, capture_output=True, text=True)
			self.assertEqual(0, done.returncode, done.stdout + done.stderr)
		nuke("init", "--toolchain", "dev", "--ied")
		shutil.rmtree(os.path.join(self.repo, "src", "tests"), ignore_errors=True)
		shutil.copytree(os.path.join(REPO, "plugins", "nuke"), os.path.join(self.home, "plugins", "nuke"))
		write(os.path.join(self.repo, "src", "ring.ch"), RING_CH)
		write(os.path.join(self.repo, "src", "ring.cf"), RING_CF)
		write(os.path.join(self.repo, "src", "main.cf"), MAIN_CF)
		nuke("build")
		# This checkout's built-in c4 plugin in place of the toolchain's copy (which may be older), finding the dev
		# toolchain - its tools - through NUKE_TOOLCHAINS, as the workspace's nuke.toml pins it.
		solution = [os.path.join(self.repo, name) for name in os.listdir(self.repo) if name.endswith(".iedsln")][0]
		with open(solution, encoding="utf-8") as f:
			pinned = json.load(f)
		pinned.pop("localPlugins", None)
		write(solution, json.dumps(pinned))
		plugin = os.path.join(self.home, "plugins", "c4")
		for name in ("ied-plugin.json", "settings.schema.json", "toolchain-contributions.json", "grammars", "skills"):
			source = os.path.join(REPO, "plugins", "c4", name)
			if os.path.isdir(source):
				shutil.copytree(source, os.path.join(plugin, name))
			else:
				os.makedirs(plugin, exist_ok=True)
				shutil.copy2(source, plugin)
		store = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(C4LSP)))))
		saved = os.environ.get("NUKE_TOOLCHAINS")
		os.environ["NUKE_TOOLCHAINS"] = store
		self.addCleanup(lambda: os.environ.pop("NUKE_TOOLCHAINS", None) if saved is None else os.environ.__setitem__("NUKE_TOOLCHAINS", saved))

	async def test_a_c4_write_is_formatted_and_checked(self):
		await self.client.request("settings.set", {"tier": "app", "key": "ai.edits.diagnosticsWaitMs", "value": 10000})
		results = await self.results(
			("s1", "code_symbols", {"query": "Push"}),
			("w1", "fs_write", {"path": "src/twice.cf", "content": "namespace Game\n{\nint32   Twice(int32 x)   {return x*2;}\n}\n"}),
			("w2", "fs_write", {"path": "src/broken.cf", "content": "namespace Game\n{\n\tint32 Broken() { return missingName; }\n}\n"}),
			permission="auto")
		with open(os.path.join(self.repo, "src", "twice.cf"), encoding="utf-8") as f:
			formatted = f.read()
		self.assertIn("Formatted by C4LSP", results["w1"], results["w1"])
		self.assertNotEqual("namespace Game\n{\nint32   Twice(int32 x)   {return x*2;}\n}\n", formatted, "C4LSP formatted it")
		self.assertIn("Twice", formatted, "and kept the code")
		self.assertIn("Language server (C4LSP):", results["w1"], results["w1"])
		self.assertIn("missingName", results["w2"], results["w2"])
		self.assertIn("error", results["w2"])


class AfterAiWriteTests(CodeToolCase):
	"""docs/24 "After an AI edit": a write of a file whose language plugin asks for it (formatOnAiWrite,
	diagnosticsOnAiWrite) is formatted by its server - positions converted from the server's UTF-8 - and its result
	carries what the server then says of the file and of the others; information, never a failure of the call. A
	language whose plugin asks for neither gets neither, and the owner turns both off (ai.edits.*)."""

	def prepare(self):
		self.log = os.path.join(self.root, "fake-lsp.jsonl")
		plugin = os.path.join(self.home, "plugins", "fake")
		os.makedirs(plugin)
		shutil.copy(FAKE_LSP, plugin)
		command = {"command": sys.executable, "args": ["-I", "${pluginDir}/fake_lsp.py", self.log]}
		write(os.path.join(plugin, "ied-plugin.json"), json.dumps({"manifestVersion": 1, "id": "fake", "name": "Fake", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
			"contributes": {"languages": [{"id": "fake", "extensions": [".fk"]}, {"id": "quiet", "extensions": [".qt"]}],
				"lspProfiles": [dict(command, id="fake-lsp", label="Fake", selector={"languages": ["fake"]}, formatOnAiWrite=True, diagnosticsOnAiWrite=True),
					dict(command, id="quiet-lsp", label="Quiet", selector={"languages": ["quiet"]})]}}))

	def received(self, method):
		"""What the server got of `method`; nothing when it never started."""
		if not os.path.exists(self.log):
			return []
		with open(self.log, encoding="utf-8") as f:
			return [m for m in map(json.loads, f) if m.get("method") == method]

	async def test_a_write_is_formatted_and_told_what_its_server_says(self):
		accent = chr(0xE9)   # two bytes in UTF-8, one UTF-16 unit: the server's columns differ from the file's
		await self.client.request("settings.set", {"tier": "app", "key": "ai.edits.diagnosticsWaitMs", "value": 600})
		results = await self.results(
			("w0", "fs_write", {"path": "b.fk", "content": "bad one\n"}),
			("w1", "fs_write", {"path": "a.fk", "content": accent + " messy bad\n\tmeh\nfine\n"}),
			("e1", "fs_edit", {"path": "a.fk", "edits": [{"old": "bad", "new": "good"}]}),
			("w2", "fs_write", {"path": "q.qt", "content": "messy bad\n"}),
			("w3", "fs_write", {"path": "s.fk", "content": "silent bad\n"}),
			permission="auto")
		self.assertIn("a.fk: +3 -0 lines, saved\nFormatted by Fake: it changed 1 of the lines you wrote", results["w1"])
		self.assertIn("Language server (Fake): 1 error, 1 warning in this file - what the code says now, not a failure of this call", results["w1"])
		self.assertIn("  line 1:8: error: bad word\n  line 2:2: warning: dull word", results["w1"], "after formatting, in the file's own columns")
		self.assertIn("Other files it finds problems in: b.fk (1 error).", results["w1"], "a multi-file edit's other files")
		with open(os.path.join(self.repo, "a.fk"), encoding="utf-8") as f:
			self.assertEqual(accent + " tidy good\n\tmeh\nfine\n", f.read(), "formatted, the UTF-8 edit put at its UTF-16 place, then edited")
		self.assertEqual({"tabSize": 4, "insertSpaces": False}, self.received("textDocument/formatting")[0]["params"]["options"], "the file's own indentation: tabs")
		self.assertIn("Language server (Fake): 0 errors, 1 warning in this file", results["e1"])
		self.assertNotIn("Formatted", results["e1"], "nothing left to format")
		self.assertNotIn("Language server", results["w2"], "its plugin asks for neither")
		self.assertNotIn("Formatted", results["w2"])
		with open(os.path.join(self.repo, "q.qt"), encoding="utf-8") as f:
			self.assertEqual("messy bad\n", f.read())
		self.assertIn("Language server (Fake): nothing new about this file within 600 ms", results["w3"], "a server that says nothing in time")
		chat_id = (await self.client.request("chat.list", {"wsId": "w1"}))["chats"][0]["chatId"]
		turn = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"][-1]
		self.assertEqual(["ok"] * 5, [s["state"] for s in turn["segments"] if s["type"] == "tool"], "errors in the code are no failed call")

	async def test_the_owner_turns_them_off(self):
		await self.client.request("settings.set", {"tier": "app", "key": "ai.edits.formatOnWrite", "value": False})
		await self.client.request("settings.set", {"tier": "app", "key": "ai.edits.diagnosticsOnWrite", "value": False})
		results = await self.results(("w1", "fs_write", {"path": "a.fk", "content": "messy bad\n"}), permission="auto")
		self.assertEqual("a.fk: +1 -0 lines, saved", results["w1"])
		with open(os.path.join(self.repo, "a.fk"), encoding="utf-8") as f:
			self.assertEqual("messy bad\n", f.read())
		self.assertEqual([], self.received("textDocument/formatting"))
