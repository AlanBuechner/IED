// keybindings.js - the keybinding service (F-SHL-21, docs/15 "Commands and keys"): bindings from the shell and the
// plugins, then the owner's (keyboard.bindings, docs/07 "Host settings"), which win and may take default keys away;
// chords with a 2 s wait for the second key, when-clauses, and the last binding that holds winning.

import { KeyError, parseBinding, keyOfEvent, formatBinding, belongsToTextField, isBrowserReserved } from "./keys.js";
import { isTextInput } from "/ui-kit/dom.js";

const CHORD_WAIT_MS = 2000;

export class Keybindings {
	// options: {report(message), onChord(text or null), timers}
	constructor(commands, context, options) {
		this.commands = commands;
		this.context = context;
		this.report = options.report;
		this.onChord = options.onChord || (() => {});
		this.timers = options.timers || window;
		this.bindings = [];
		this.owner = [];            // the owner's bindings: after every default, so they win
		this.removals = [];         // [{command, keys|null}]: default keys the owner took away
		this.pending = null;
		this.pendingTimer = null;
		this.recording = false;     // a settings page is reading keys: the shell runs none
	}

	// The owner's keyboard.bindings: [{command, key?, when?, args?, remove?}]; an entry that does not parse is reported.
	setOwner(entries) {
		this.owner = [];
		this.removals = [];
		for (const entry of entries || []) {
			try {
				const keys = entry.key ? parseBinding(entry.key) : null;
				if (entry.remove) this.removals.push({ command: entry.command, keys });
				else if (keys) this.owner.push({ keys, command: entry.command, when: entry.when || "", args: entry.args, source: "user" });
			}
			catch (e) {
				if (!(e instanceof KeyError)) throw e;
				this.report("Your keyboard shortcut for '" + entry.command + "' is ignored: " + e.message);
			}
		}
	}

	// Whether the owner took `binding` (a default) away.
	removed(binding) {
		return this.removals.some((r) => r.command === binding.command && (!r.keys || r.keys.join(" ") === binding.keys.join(" ")));
	}

	// Every binding in effect, defaults first and the owner's last.
	active() {
		return this.bindings.filter((b) => !this.removed(b)).concat(this.owner);
	}

	// binding: {command, key, when, args}; `source`: "shell" or a plugin id. A key that does not parse is reported.
	define(binding, source) {
		try {
			const keys = parseBinding(binding.key);
			this.bindings.push({ keys, command: binding.command, when: binding.when || "", args: binding.args, source });
		}
		catch (e) {
			if (!(e instanceof KeyError)) throw e;
			this.report("Plugin '" + source + "': the keybinding for '" + binding.command + "' is ignored: " + e.message);
		}
	}

	clearPluginBindings() {
		this.bindings = this.bindings.filter((b) => b.source === "shell");
	}

	// Every binding, the owner's included, each with its label and whether the owner took it away.
	all() {
		return this.bindings.concat(this.owner).map((b) => Object.assign({ label: formatBinding(b.keys), appWindowOnly: isBrowserReserved(b.keys[0]), removed: b.source !== "user" && this.removed(b) }, b));
	}

	// The label of the binding that runs `command` with `args` (the last defined one), or "".
	labelFor(command, args) {
		const wanted = JSON.stringify(args === undefined ? null : args);
		const active = this.active();
		for (let i = active.length - 1; i >= 0; i--) {
			const b = active[i];
			if (b.command === command && JSON.stringify(b.args === undefined ? null : b.args) === wanted) return formatBinding(b.keys);
		}
		return "";
	}

	// A keydown: true when it was a binding's (or a chord's) and the event is consumed.
	handle(event) {
		const key = keyOfEvent(event);
		if (!key || this.recording) return false;
		if (this.pending) return this.secondKey(event, key);
		if (isTextInput(event.target) && belongsToTextField(key)) return false;
		if (this.holding((b) => b.keys.length === 2 && b.keys[0] === key)) {
			this.startChord(key);
			consume(event);
			return true;
		}
		const binding = this.holding((b) => b.keys.length === 1 && b.keys[0] === key);
		if (!binding) return false;
		consume(event);
		this.commands.run(binding.command, binding.args);
		return true;
	}

	// The last binding matching `test` whose when-clause holds.
	holding(test) {
		const active = this.active();
		for (let i = active.length - 1; i >= 0; i--) {
			const b = active[i];
			if (test(b) && this.context.holds(b.when)) return b;
		}
		return null;
	}

	startChord(key) {
		this.pending = key;
		this.onChord("(" + formatBinding([key]) + ") was pressed. Waiting for the second key of the chord...");
		this.timers.clearTimeout(this.pendingTimer);
		this.pendingTimer = this.timers.setTimeout(() => this.endChord(), CHORD_WAIT_MS);
	}

	secondKey(event, key) {
		const first = this.pending;
		this.endChord();
		consume(event);
		if (key === "escape") return true;
		const binding = this.holding((b) => b.keys.length === 2 && b.keys[0] === first && b.keys[1] === key);
		if (binding) this.commands.run(binding.command, binding.args);
		else this.onChord("The key combination (" + formatBinding([first, key]) + ") is not a command.", true);
		return true;
	}

	endChord() {
		this.timers.clearTimeout(this.pendingTimer);
		this.pending = null;
		this.onChord(null);
	}
}

// A key the shell takes: the browser does nothing with it, and neither does an editor in the page (the shell listens
// first, at the window, as the event goes down).
function consume(event) {
	event.preventDefault();
	if (event.stopPropagation) event.stopPropagation();
}
