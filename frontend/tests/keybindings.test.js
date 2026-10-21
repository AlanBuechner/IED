// keybindings.test.js - the keybinding service: single keys, chords and their wait, when-clauses, the last binding
// winning, text fields keeping their keys, bad keys reported, labels.

import { test, assertEqual, assertTrue } from "./harness.js";
import { Keybindings } from "../src/shell/keybindings.js";
import { Commands } from "../src/shell/commands.js";
import { ContextKeys } from "../src/shell/context.js";

class Timers {
	constructor() {
		this.pending = new Map();
		this.next = 1;
	}

	setTimeout(fn) {
		const id = this.next++;
		this.pending.set(id, fn);
		return id;
	}

	clearTimeout(id) {
		this.pending.delete(id);
	}

	fire() {
		const fns = [...this.pending.values()];
		this.pending.clear();
		fns.forEach((fn) => fn());
	}
}

function setup() {
	const context = new ContextKeys();
	const reports = [];
	const ran = [];
	const commands = new Commands(context, (message) => reports.push(message));
	for (const id of ["a", "b", "c", "format", "comment"]) {
		commands.define({ id, title: id }, "shell", "/");
		commands.handle(id, (args) => ran.push(args === undefined ? id : id + ":" + JSON.stringify(args)));
	}
	const chords = [];
	const timers = new Timers();
	const keys = new Keybindings(commands, context, { report: (m) => reports.push(m), onChord: (text) => chords.push(text), timers });
	return { context, commands, keys, ran, reports, chords, timers };
}

function press(keys, code, mods, target) {
	const m = mods || {};
	let prevented = false;
	const event = { code, ctrlKey: !!m.ctrl, shiftKey: !!m.shift, altKey: !!m.alt, metaKey: !!m.meta, target: target || document.body, preventDefault: () => (prevented = true) };
	const handled = keys.handle(event);
	return { handled, prevented };
}

async function settle() {
	await Promise.resolve();
	await Promise.resolve();
}

test("keybindings: a key runs its command with its args", async () => {
	const env = setup();
	env.keys.define({ command: "a", key: "ctrl+shift+p" }, "shell");
	env.keys.define({ command: "b", key: "f12", args: { x: 1 } }, "shell");
	assertEqual(press(env.keys, "KeyP", { ctrl: true, shift: true }), { handled: true, prevented: true });
	press(env.keys, "F12");
	await settle();
	assertEqual(env.ran, ["a", "b:{\"x\":1}"]);
	assertEqual(press(env.keys, "KeyP", { ctrl: true }), { handled: false, prevented: false }, "an unbound key passes");
});

test("keybindings: chords wait for the second key", async () => {
	const env = setup();
	env.keys.define({ command: "format", key: "ctrl+k ctrl+d" }, "shell");
	env.keys.define({ command: "comment", key: "ctrl+k ctrl+c" }, "shell");
	env.keys.define({ command: "a", key: "ctrl+k" }, "shell");
	assertTrue(press(env.keys, "KeyK", { ctrl: true }).prevented, "the first key is taken");
	assertTrue(env.chords[0].includes("Ctrl+K"), "the status bar says so");
	press(env.keys, "KeyD", { ctrl: true });
	press(env.keys, "KeyK", { ctrl: true });
	press(env.keys, "KeyC", { ctrl: true });
	await settle();
	assertEqual(env.ran, ["format", "comment"], "a chord wins over a single key of its first key");
	press(env.keys, "KeyK", { ctrl: true });
	press(env.keys, "KeyX", { ctrl: true });
	await settle();
	assertEqual(env.ran.length, 2, "a second key that is no chord runs nothing");
	assertTrue(env.chords[env.chords.length - 1].includes("is not a command"));
	press(env.keys, "KeyK", { ctrl: true });
	env.timers.fire();
	press(env.keys, "KeyD", { ctrl: true });
	await settle();
	assertEqual(env.ran.length, 2, "after the wait the chord is over");
	press(env.keys, "KeyK", { ctrl: true });
	assertTrue(press(env.keys, "Escape").prevented, "Escape ends a chord");
	press(env.keys, "KeyD", { ctrl: true });
	await settle();
	assertEqual(env.ran.length, 2);
});

test("keybindings: when-clauses and the last binding that holds", async () => {
	const env = setup();
	env.keys.define({ command: "a", key: "f5" }, "shell");
	env.keys.define({ command: "b", key: "f5", when: "debugging" }, "plugin");
	press(env.keys, "F5");
	env.context.set("debugging", true);
	press(env.keys, "F5");
	await settle();
	assertEqual(env.ran, ["a", "b"]);
	env.keys.define({ command: "c", key: "f5" }, "owner");
	press(env.keys, "F5");
	await settle();
	assertEqual(env.ran[2], "c", "a later binding wins");
	env.keys.clearPluginBindings();
	assertEqual(env.keys.all().map((b) => b.command), ["a"], "the shell's stay");
});

test("keybindings: a text field keeps plain keys, not modified ones", async () => {
	const env = setup();
	env.keys.define({ command: "a", key: "a" }, "shell");
	env.keys.define({ command: "b", key: "ctrl+s" }, "shell");
	env.keys.define({ command: "c", key: "f2" }, "shell");
	const input = document.createElement("input");
	assertEqual(press(env.keys, "KeyA", {}, input).handled, false);
	assertEqual(press(env.keys, "KeyS", { ctrl: true }, input).handled, true);
	assertEqual(press(env.keys, "F2", {}, input).handled, true);
	assertEqual(press(env.keys, "KeyA", {}, document.body).handled, true);
	await settle();
	assertEqual(env.ran, ["b", "c", "a"]);
});

test("keybindings: a key that does not parse is reported, not bound", () => {
	const env = setup();
	env.keys.define({ command: "a", key: "ctrl+banana" }, "acme");
	assertEqual(env.keys.all().length, 0);
	assertTrue(env.reports[0].includes("acme") && env.reports[0].includes("banana"));
});

test("keybindings: labels name the last binding of a command and its args", () => {
	const env = setup();
	env.keys.define({ command: "a", key: "ctrl+shift+p" }, "shell");
	env.keys.define({ command: "a", key: "ctrl+q" }, "shell");
	env.keys.define({ command: "b", key: "ctrl+alt+l", args: { id: "x" } }, "shell");
	assertEqual(env.keys.labelFor("a"), "Ctrl+Q");
	assertEqual(env.keys.labelFor("b", { id: "x" }), "Ctrl+Alt+L");
	assertEqual(env.keys.labelFor("b", { id: "y" }), "");
	assertEqual(env.keys.all().find((b) => b.command === "b").appWindowOnly, false);
	env.keys.define({ command: "c", key: "ctrl+w" }, "shell");
	assertEqual(env.keys.all().find((b) => b.command === "c").appWindowOnly, true);
});
