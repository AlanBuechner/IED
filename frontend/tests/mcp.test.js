// mcp.test.js - Settings > AI > MCP Servers' pure parts (plugins/mcp, docs/04 "MCP"): what a server's state and a test
// read as, the Add server form's fields as an mcp.servers entry and back, and where a server comes from.

import { test, assertEqual } from "./harness.js";
import { fieldsOf, joinArgs, readPairs, serverFrom, sourceOf, splitArgs, stateLine, testLine } from "/plugins/ied.mcp/ui/model.js";
import { resourceMention } from "/plugins/ied.chat/ui/mentions.js";

const DOT = " " + String.fromCharCode(0xb7) + " ";

test("mcp: a server's state as a line and a tone", () => {
	const tools = [{ name: "a" }, { name: "b" }];
	assertEqual(stateLine({ enabled: true, state: "running", serverName: "Blender", serverVersion: "1.0", pid: 42, tools }), { text: "Running: Blender 1.0" + DOT + "2 tools" + DOT + "pid 42", tone: "ok" });
	assertEqual(stateLine({ enabled: true, state: "starting", tools: [] }).tone, "busy");
	assertEqual(stateLine({ enabled: true, state: "failed", error: "initialize: no answer", tools: [] }), { text: "initialize: no answer", tone: "bad" });
	assertEqual(stateLine({ enabled: true, state: "stopped", restartInMs: 1500, error: "exited with code 3", tools }), { text: "exited with code 3" + DOT + "starting it again in 2 s", tone: "warn" });
	assertEqual(stateLine({ enabled: true, state: "stopped", toolsKnown: true, tools }).text, "Stopped: its 2 tools stay offered; the next call starts it");
	assertEqual(stateLine({ enabled: true, state: "stopped", tools: [] }).text, "Stopped: it starts with the next chat turn");
	assertEqual(stateLine({ enabled: false, state: "stopped", tools }).text, "Turned off");
	assertEqual(stateLine({ enabled: true, state: "running", tools }, false).tone, "warn", "an untrusted workspace starts nothing");
	assertEqual(stateLine(null), { text: "", tone: "idle" });
});

test("mcp: a test's answer", () => {
	assertEqual(testLine({ ok: true, serverName: "fake", serverVersion: "1.2.3", protocolVersion: "2025-06-18", tools: [{}, {}, {}], ms: 120 }),
		{ text: "fake 1.2.3 answered" + DOT + "MCP 2025-06-18" + DOT + "3 tools" + DOT + "120 ms", tone: "ok" });
	assertEqual(testLine({ ok: false, error: "there is no program x on PATH", tools: [] }), { text: "there is no program x on PATH", tone: "bad" });
});

test("mcp: arguments and pairs", () => {
	assertEqual(splitArgs("-I \"C:/My Tools/server.py\" --port 9876"), ["-I", "C:/My Tools/server.py", "--port", "9876"]);
	assertEqual(splitArgs("a \"\" b"), ["a", "", "b"], "an empty word kept");
	assertEqual(joinArgs(["-I", "C:/My Tools/s.py", ""]), "-I \"C:/My Tools/s.py\" \"\"");
	assertEqual(readPairs("PORT=9876\nTOKEN=env:BLENDER_TOKEN\n\n", "="), { PORT: "9876", TOKEN: { env: "BLENDER_TOKEN" } });
	assertEqual(readPairs("Authorization: env:TOKEN", ":"), { Authorization: { env: "TOKEN" } });
	let refused = "";
	try {
		readPairs("no separator", "=");
	}
	catch (e) {
		refused = e.message;
	}
	assertEqual(refused, "\"no separator\" is not NAME=value");
});

test("mcp: the form's fields as an entry and back", () => {
	const made = serverFrom({ name: "blender", kind: "command", command: "python", args: "-I C:/tools/blender.py", env: "PORT=9876", cwd: "", url: "", headers: "", permission: "ask", preload: false, privacy: "", timeoutSeconds: "300", startSeconds: "", enabled: true });
	assertEqual(made, { name: "blender", entry: { command: "python", args: ["-I", "C:/tools/blender.py"], env: { PORT: "9876" }, timeoutSeconds: 300 } }, "only what differs from the defaults");
	const web = serverFrom({ name: "engine", kind: "url", url: "http://127.0.0.1:7801/mcp", headers: "Authorization: env:TOKEN", permission: "read", preload: true, privacy: "local", timeoutSeconds: "", startSeconds: "", enabled: false });
	assertEqual(web.entry, { url: "http://127.0.0.1:7801/mcp", headers: { Authorization: { env: "TOKEN" } }, permission: "read", preload: true, privacy: "local", enabled: false });
	const back = fieldsOf("blender", made.entry);
	assertEqual([back.kind, back.args, back.env, back.permission, back.timeoutSeconds], ["command", "-I C:/tools/blender.py", "PORT=9876", "ask", 300]);
	for (const [fields, message] of [
		[{ name: "a b", kind: "command", command: "x" }, "a name is 1 to 32 letters, digits, '_' and '-'"],
		[{ name: "a", kind: "command", command: " " }, "a command is the program IED starts (python, uvx, npx, a path)"],
		[{ name: "a", kind: "url", url: "ftp://x" }, "the URL must be http://host:port/path"],
		[{ name: "a", kind: "command", command: "x", timeoutSeconds: "1.5" }, "timeoutSeconds must be a whole number of seconds"],
	]) {
		let said = "";
		try {
			serverFrom(fields);
		}
		catch (e) {
			said = e.message;
		}
		assertEqual(said, message);
	}
});

test("mcp: a resource as a composer mention", () => {
	assertEqual(resourceMention({ server: "blender", uri: "blender://scene/Cube", name: "Cube", description: "the cube" }),
		{ kind: "mcp", ref: "blender blender://scene/Cube", label: "mcp:blender/Cube", detail: "the cube" });
	assertEqual(resourceMention({ server: "fake", uri: "fake://pixel", name: "pixel" }).detail, "fake://pixel", "its URI when it says nothing more");
});

test("mcp: where a server comes from", () => {
	const app = { blender: { command: "python" } };
	assertEqual(sourceOf({ name: "blender", source: "settings" }, app, { blender: { command: "python" } }), "app");
	assertEqual(sourceOf({ name: "blender", source: "settings" }, app, { blender: { command: "uvx" } }), "workspace", "a solution's entry of the name wins");
	assertEqual(sourceOf({ name: "engine", source: "settings" }, app, {}), "workspace");
	assertEqual(sourceOf({ name: "engine", source: "plugin game.engine" }, app, {}), "plugin game.engine");
});
