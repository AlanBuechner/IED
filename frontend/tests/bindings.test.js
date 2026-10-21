// bindings.test.js - client bindings (docs/02 "Client bindings as built"): only declared providers and consumers, a
// call answered by the provider, a binding nobody provides (or whose plugin went away) refused, and the editor's
// language lookup behind `editor.colorize`.

import { test, assertEqual, assertThrows } from "./harness.js";
import { Bindings } from "../src/shell/bindings.js";
import { languageOf } from "/plugins/ied.editor/ui/colorize.js";

function registry(loaded) {
	return new Bindings((id) => loaded[id] || null);
}

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

test("bindings: the language of a path", () => {
	const languages = [{ id: "c", extensions: [".c", ".h"] }, { id: "markdown", extensions: [".md"] }, { id: "dockerfile", filenames: ["Dockerfile"] }];
	assertEqual(["src/a.c", "x/README.MD", "Dockerfile", "a.unknown", "Makefile"].map((p) => languageOf(languages, p)), ["c", "markdown", "dockerfile", null, null]);
});
