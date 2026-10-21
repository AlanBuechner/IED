// bindings.test.js - client bindings (docs/02 "Client bindings as built"): only declared providers and consumers, a
// call answered by the provider, a binding nobody provides (or whose plugin went away) refused, a server binding
// reached through plugins.call (docs/26 section 4), and the editor's language lookup behind `editor.colorize`.

import { test, assertEqual, assertThrows } from "./harness.js";
import { Bindings } from "../src/shell/bindings.js";
import { Commands } from "../src/shell/commands.js";
import { languageOf } from "/plugins/ied.editor/ui/colorize.js";

function registry(loaded) {
	return new Bindings((id) => loaded[id] || null);
}

test("bindings: an event reaches the plugins listening while they are loaded", () => {
	const loaded = {
		editor: { provides: [{ name: "editor.scrolled", version: "1", kind: "event", side: "client" }] },
		markdown: { consumes: [{ name: "editor.scrolled", version: "1", optional: true }] },
		other: {},
	};
	const bindings = registry(loaded);
	const heard = [];
	const stop = bindings.on("markdown", "editor.scrolled", (data) => heard.push(data.line));
	assertThrows(() => bindings.on("other", "editor.scrolled", () => {}), Error, "a listener declares what it consumes");
	assertThrows(() => bindings.emit("other", "editor.scrolled", {}), Error, "only its provider emits it");
	bindings.emit("editor", "editor.scrolled", { path: "a.md", line: 3 });
	delete loaded.markdown;
	bindings.emit("editor", "editor.scrolled", { path: "a.md", line: 4 });
	loaded.markdown = { consumes: [{ name: "editor.scrolled", version: "1" }] };
	bindings.emit("editor", "editor.scrolled", { path: "a.md", line: 5 });
	stop();
	bindings.emit("editor", "editor.scrolled", { path: "a.md", line: 6 });
	assertEqual(heard, [3, 5], "not while its plugin is gone, nor once stopped");
});

test("bindings: declared providers and consumers", async () => {
	const loaded = {
		editor: { provides: [{ name: "editor.colorize", version: "1", kind: "function", side: "client" }] },
		git: { consumes: [{ name: "editor.colorize", version: "1", optional: true }] },
		other: {},
	};
	const bindings = registry(loaded);
	assertThrows(() => bindings.provide("other", "editor.colorize", () => 1), Error, "only the plugin that declares it provides it");
	assertEqual(bindings.has("editor.colorize"), false);
	let refused = null;
	await bindings.call("git", "editor.colorize", {}).catch((e) => { refused = e.code; });
	assertEqual(refused, "not_provided", "nobody provides it yet");
	bindings.provide("editor", "editor.colorize", ({ lines }) => lines.map((l) => "<b>" + l + "</b>"));
	assertEqual(await bindings.call("git", "editor.colorize", { lines: ["a"] }), ["<b>a</b>"]);
	await bindings.call("other", "editor.colorize", {}).catch((e) => { refused = e.code; });
	assertEqual(refused, "not_declared", "a caller declares what it consumes");
	delete loaded.editor;
	assertEqual(bindings.has("editor.colorize"), false, "a provider whose plugin is gone");
});

test("bindings: a server binding answers through the server, also for a command that names it", async () => {
	const asked = [];
	const server = { provides: (name) => name === "probe.echo", call: async (name, args) => { asked.push([name, args]); return { said: args.text }; } };
	const bindings = new Bindings((id) => ({ ui: { consumes: [{ name: "probe.echo", version: "1" }] } })[id] || null, server);
	assertEqual(bindings.has("probe.echo"), true);
	assertEqual(await bindings.call("ui", "probe.echo", { text: "hi" }), { said: "hi" });
	let refused = null;
	await bindings.callServer("probe.other", {}).catch((e) => { refused = e.code; });
	assertEqual(refused, "not_provided");
	const commands = new Commands({ holds: () => true }, (message) => { refused = message; });
	commands.bindings = bindings;
	commands.define({ id: "probe.say", title: "Say", binding: "probe.echo" }, "probe");
	commands.define({ id: "probe.none", title: "None", binding: "probe.missing" }, "probe");
	assertEqual(commands.isEnabled("probe.say"), true);
	assertEqual(await commands.run("probe.say", { text: "yo" }), { said: "yo" });
	assertEqual(asked, [["probe.echo", { text: "hi" }], ["probe.echo", { text: "yo" }]]);
	assertEqual(commands.isEnabled("probe.none"), false, "no plugin provides it");
});

test("bindings: the language of a path", () => {
	const languages = [{ id: "c", extensions: [".c", ".h"] }, { id: "markdown", extensions: [".md"] }, { id: "dockerfile", filenames: ["Dockerfile"] }];
	assertEqual(["src/a.c", "x/README.MD", "Dockerfile", "a.unknown", "Makefile"].map((p) => languageOf(languages, p)), ["c", "markdown", "dockerfile", null, null]);
});
