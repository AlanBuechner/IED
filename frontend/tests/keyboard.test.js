// keyboard.test.js - the owner's keyboard shortcuts (docs/07 "Host settings"): they win over the defaults, can take a
// default key away, and the Keyboard page's rows, conflicts and edits of keyboard.bindings.

import { test, assertEqual } from "./harness.js";
import { Keybindings } from "../src/shell/keybindings.js";
import { changed, commandRows, conflicts, withDefaults, withKey, withoutKey } from "../src/shell/keyboardmodel.js";

function registry() {
	const ran = [];
	const commands = { run: (command, args) => ran.push(command + (args ? JSON.stringify(args) : "")) };
	const context = { holds: (when) => !when || when === "doc" };
	const keys = new Keybindings(commands, context, { report: (message) => ran.push("report: " + message), timers: { setTimeout: () => 0, clearTimeout: () => {} } });
	keys.define({ command: "edit.duplicate", key: "ctrl+d", when: "doc" }, "ied.editor");
	keys.define({ command: "edit.find", key: "ctrl+f" }, "ied.editor");
	return { keys, ran };
}

function press(keys, key, target) {
	const [name, ...mods] = key.split("+").reverse();
	const event = { code: name.length === 1 ? "Key" + name.toUpperCase() : name, key: name, ctrlKey: mods.includes("ctrl"), shiftKey: mods.includes("shift"), altKey: mods.includes("alt"), metaKey: false, target: target || null, preventDefault() {}, stopPropagation() {} };
	return keys.handle(event);
}

test("keyboard: the owner's bindings win and take defaults away", () => {
	const { keys, ran } = registry();
	keys.setOwner([{ command: "edit.selectWord", key: "ctrl+d", when: "doc" }, { command: "edit.find", remove: true }, { command: "bad", key: "ctrl+nope" }]);
	assertEqual(ran.length === 1 && ran[0].startsWith("report: Your keyboard shortcut for 'bad' is ignored"), true, "a key that does not parse is said: " + ran);
	ran.length = 0;
	press(keys, "ctrl+d");
	press(keys, "ctrl+f");
	assertEqual(ran, ["edit.selectWord"], "the owner's Ctrl+D runs; Ctrl+F was taken off");
	const all = keys.all();
	assertEqual(all.filter((b) => b.removed).map((b) => b.command), ["edit.find"]);
	assertEqual(keys.labelFor("edit.find"), "", "a removed default has no label");
	keys.recording = true;
	assertEqual(press(keys, "ctrl+d"), false, "while a page records keys the shell runs none");
});

test("keyboard: the page's rows and edits", () => {
	const { keys } = registry();
	const label = (command) => ({ "edit.duplicate": "Edit: Duplicate", "edit.find": "Edit: Find", "edit.sort": "Edit: Sort Lines" })[command] || command;
	let entries = withKey([], "edit.sort", undefined, "ctrl+k ctrl+s", "doc");
	keys.setOwner(entries);
	let rows = commandRows([{ id: "edit.duplicate" }, { id: "edit.find" }, { id: "edit.sort" }], keys.all(), label);
	assertEqual(rows.map((r) => [r.label, r.keys.map((k) => k.label)]), [["Edit: Duplicate", ["Ctrl+D"]], ["Edit: Find", ["Ctrl+F"]], ["Edit: Sort Lines", ["Ctrl+K, Ctrl+S"]]]);
	assertEqual([changed(rows[0]), changed(rows[2])], [false, true]);
	assertEqual(conflicts(rows, "ctrl+d", "doc", rows[2]), ["Edit: Duplicate"], "Ctrl+D with the same when-clause");
	assertEqual(conflicts(rows, "ctrl+d", "", rows[2]), [], "a different when-clause is no conflict");
	entries = withoutKey(entries, "edit.find", undefined, rows[1].keys[0]);
	assertEqual(entries[1], { command: "edit.find", key: "ctrl+f", remove: true }, "a default is taken off");
	entries = withoutKey(entries, "edit.sort", undefined, rows[2].keys[0]);
	assertEqual(entries.length, 1, "the owner's own key goes");
	assertEqual(withDefaults(entries, "edit.find", undefined), [], "Reset brings the defaults back");
});
