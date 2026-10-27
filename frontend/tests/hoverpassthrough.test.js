// hoverpassthrough.test.js - in a real Monaco: the link key held anywhere in the page marks the editor so its hovers let
// the pointer through; released, or the window left, they are hovers again. On a Mac the key is Cmd, not Ctrl.

import { test, assertEqual } from "./harness.js";
import { loadMonaco } from "/plugins/ied.editor/ui/monaco.js";
import { HELD_CLASS, holdsLinkKey, letHoversPassWhileLinking } from "/plugins/ied.editor/ui/hoverpassthrough.js";

function fakeIed() {
	return { contributions: Object.assign(() => [], { on: () => () => {} }), report: () => {}, workspace: { wsId: "", root: "" }, commands: { run: () => {} } };
}

async function editorOver(text) {
	const monaco = await loadMonaco(fakeIed());
	const host = document.createElement("div");
	host.style.cssText = "position: fixed; left: 0; top: 0; width: 400px; height: 120px; visibility: hidden";
	document.body.append(host);
	const editor = monaco.editor.create(host, { value: text });
	return { editor, dispose: () => { editor.dispose(); host.remove(); } };
}

function key(type, init) {
	window.dispatchEvent(new KeyboardEvent(type, Object.assign({ key: "Control" }, init)));
}

test("hover pass-through: held while Ctrl is down, released with it or when the window is left", async () => {
	const { editor, dispose } = await editorOver("alpha beta\n");
	const stop = letHoversPassWhileLinking(editor, false);
	const held = () => editor.getDomNode().classList.contains(HELD_CLASS);
	try {
		assertEqual(held(), false, "not before");
		key("keydown", { ctrlKey: true });
		assertEqual(held(), true, "Ctrl down");
		key("keyup", { ctrlKey: false });
		assertEqual(held(), false, "Ctrl up");
		key("keydown", { ctrlKey: true });
		window.dispatchEvent(new Event("blur"));
		assertEqual(held(), false, "the window left with Ctrl down: no key-up will come");
		key("keydown", { ctrlKey: true });
		stop();
		assertEqual(held(), false, "stopped: the editor's hovers are plain again");
		key("keydown", { ctrlKey: true });
		assertEqual(held(), false, "and the keys no longer count");
	}
	finally {
		dispose();
	}
});

test("hover pass-through: an editor whose document arrives later is marked too", async () => {
	const monaco = await loadMonaco(fakeIed());
	const host = document.createElement("div");
	host.style.cssText = "position: fixed; left: 0; top: 0; width: 400px; height: 120px; visibility: hidden";
	document.body.append(host);
	const editor = monaco.editor.create(host, {});
	const stop = letHoversPassWhileLinking(editor, false);
	const model = monaco.editor.createModel("alpha\n", "plaintext");
	try {
		key("keydown", { ctrlKey: true });
		editor.setModel(model);
		assertEqual(editor.getDomNode().classList.contains(HELD_CLASS), true, "the node Monaco made for the model, as an editor view opens its document");
		key("keyup", { ctrlKey: false });
		assertEqual(editor.getDomNode().classList.contains(HELD_CLASS), false);
	}
	finally {
		stop();
		editor.dispose();
		model.dispose();
		host.remove();
	}
});

test("hover pass-through: the link key is Cmd on a Mac", () => {
	assertEqual([holdsLinkKey({ ctrlKey: true, metaKey: false }, true), holdsLinkKey({ ctrlKey: false, metaKey: true }, true)], [false, true]);
	assertEqual([holdsLinkKey({ ctrlKey: true, metaKey: false }, false), holdsLinkKey({ ctrlKey: false, metaKey: true }, false)], [true, false]);
});
