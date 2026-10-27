// model.js - Settings > AI > MCP Servers' pure parts (docs/04 "MCP"): what a server's state reads as, and an
// mcp.servers entry made from the Add server form's fields and back.

const NAME = /^[A-Za-z0-9_-]{1,32}$/;
const DOT = " " + String.fromCharCode(0xb7) + " ";

export const PERMISSIONS = [
	["ask", "Ask before each call, until allowed for the chat"],
	["auto", "Follow the chat's permission mode"],
	["read", "Read-only: never asks, offered in Plan mode"],
];

// The state of `server` (an entry of the mcp:<wsId> topic) as a line and a tone: ok, busy, warn, bad or idle.
export function stateLine(server, trusted) {
	if (!server) return { text: "", tone: "idle" };
	if (trusted === false) return { text: "Not started: the workspace is not trusted", tone: "warn" };
	if (!server.enabled) return { text: "Turned off", tone: "idle" };
	const tools = server.tools ? server.tools.length : 0;
	const count = tools + (tools === 1 ? " tool" : " tools");
	if (server.state === "running") {
		const who = [server.serverName, server.serverVersion].filter(Boolean).join(" ") || "a server";
		return { text: "Running: " + who + DOT + count + (server.pid ? DOT + "pid " + server.pid : ""), tone: "ok" };
	}
	if (server.state === "starting") return { text: "Starting...", tone: "busy" };
	if (server.state === "failed") return { text: server.error || "It failed", tone: "bad" };
	if (server.restartInMs !== undefined) return { text: (server.error ? server.error + DOT : "") + "starting it again in " + Math.ceil(server.restartInMs / 1000) + " s", tone: "warn" };
	if (server.toolsKnown) return { text: "Stopped: its " + count + " stay offered; the next call starts it", tone: "idle" };
	return { text: "Stopped: it starts with the next chat turn", tone: "idle" };
}

// A test's answer (mcp.test) as a line and a tone.
export function testLine(answer) {
	if (!answer) return { text: "", tone: "idle" };
	if (!answer.ok) return { text: answer.error || "It did not start", tone: "bad" };
	const who = [answer.serverName, answer.serverVersion].filter(Boolean).join(" ") || "the server";
	const tools = answer.tools.length + (answer.tools.length === 1 ? " tool" : " tools");
	return { text: who + " answered" + DOT + "MCP " + answer.protocolVersion + DOT + tools + DOT + answer.ms + " ms", tone: "ok" };
}

// The words of an arguments line: spaces separate them, double quotes keep spaces in one.
export function splitArgs(text) {
	const words = [];
	let word = "";
	let quoted = false;
	let any = false;
	for (const c of text) {
		if (c === "\"") {
			quoted = !quoted;
			any = true;
		}
		else if (c === " " && !quoted) {
			if (any) words.push(word);
			word = "";
			any = false;
		}
		else {
			word += c;
			any = true;
		}
	}
	if (any) words.push(word);
	return words;
}

export function joinArgs(words) {
	return (words || []).map((w) => (w === "" || w.includes(" ") ? "\"" + w + "\"" : w)).join(" ");
}

// "NAME=value" lines (or "Name: value" for headers) as an object; a value "env:VAR" reads IED's variable VAR.
export function readPairs(text, separator) {
	const pairs = {};
	for (const raw of text.split("\n")) {
		const line = raw.trim();
		if (!line) continue;
		const at = line.indexOf(separator);
		if (at <= 0) throw new Error("\"" + line + "\" is not " + (separator === "=" ? "NAME=value" : "Name: value"));
		const key = line.slice(0, at).trim();
		const value = line.slice(at + 1).trim();
		pairs[key] = value.startsWith("env:") ? { env: value.slice(4) } : value;
	}
	return pairs;
}

export function writePairs(pairs, separator) {
	return Object.entries(pairs || {}).map(([key, value]) => key + (separator === "=" ? "=" : ": ") + (typeof value === "object" ? "env:" + value.env : value)).join("\n");
}

// The mcp.servers entry the form's fields describe, with its name; throws when one is not usable.
export function serverFrom(fields) {
	const name = fields.name.trim();
	if (!NAME.test(name)) throw new Error("a name is 1 to 32 letters, digits, '_' and '-'");
	const entry = {};
	if (fields.kind === "url") {
		const url = fields.url.trim();
		if (!/^https?:\/\//.test(url)) throw new Error("the URL must be http://host:port/path");
		entry.url = url;
		const headers = readPairs(fields.headers || "", ":");
		if (Object.keys(headers).length) entry.headers = headers;
	}
	else {
		const command = fields.command.trim();
		if (!command) throw new Error("a command is the program IED starts (python, uvx, npx, a path)");
		entry.command = command;
		const args = splitArgs(fields.args || "");
		if (args.length) entry.args = args;
		const env = readPairs(fields.env || "", "=");
		if (Object.keys(env).length) entry.env = env;
		if ((fields.cwd || "").trim()) entry.cwd = fields.cwd.trim();
	}
	if (fields.permission && fields.permission !== "ask") entry.permission = fields.permission;
	if (fields.preload) entry.preload = true;
	if (fields.privacy) entry.privacy = fields.privacy;
	for (const key of ["timeoutSeconds", "startSeconds"]) {
		if (!String(fields[key] || "").trim()) continue;
		const value = Number(fields[key]);
		if (!Number.isInteger(value) || value < 1) throw new Error(key + " must be a whole number of seconds");
		entry[key] = value;
	}
	if (fields.enabled === false) entry.enabled = false;
	return { name, entry };
}

// The form's fields for an mcp.servers entry.
export function fieldsOf(name, entry) {
	return {
		name: name || "",
		kind: entry.url ? "url" : "command",
		command: entry.command || "",
		args: joinArgs(entry.args),
		env: writePairs(entry.env, "="),
		cwd: entry.cwd || "",
		url: entry.url || "",
		headers: writePairs(entry.headers, ":"),
		permission: entry.permission || "ask",
		preload: entry.preload === true,
		privacy: entry.privacy || "",
		timeoutSeconds: entry.timeoutSeconds || "",
		startSeconds: entry.startSeconds || "",
		enabled: entry.enabled !== false,
	};
}

// Where a server of the workspace comes from: "app" (the User settings, which this page edits) when the workspace's
// entry of its name is the User settings' own, "workspace" when a solution or project file sets it, or its plugin.
export function sourceOf(server, appServers, effective) {
	if (server.source && server.source.startsWith("plugin ")) return server.source;
	const own = appServers && appServers[server.name];
	if (!own) return "workspace";
	const used = effective && effective[server.name];
	return !used || JSON.stringify(used) === JSON.stringify(own) ? "app" : "workspace";
}
