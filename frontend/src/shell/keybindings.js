// keybindings.js - the keybinding service (F-SHL-21, docs/15 "Commands and keys"): bindings from the shell and the
// plugins, chords with a 2 s wait for the second key, when-clauses, and the last binding that holds winning.

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
		this.pending = null;
		this.pendingTimer = null;
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

	all() {
		return this.bindings.map((b) => Object.assign({ label: formatBinding(b.keys), appWindowOnly: isBrowserReserved(b.keys[0]) }, b));
	}

	// The label of the binding that runs `command` with `args` (the last defined one), or "".
	labelFor(command, args) {
		const wanted = JSON.stringify(args === undefined ? null : args);
		for (let i = this.bindings.length - 1; i >= 0; i--) {
			const b = this.bindings[i];
			if (b.command === command && JSON.stringify(b.args === undefined ? null : b.args) === wanted) return formatBinding(b.keys);
		}
		return "";
	}

	// A keydown: true when it was a binding's (or a chord's) and the event is consumed.
	handle(event) {
		const key = keyOfEvent(event);
		if (!key) return false;
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
		for (let i = this.bindings.length - 1; i >= 0; i--) {
			const b = this.bindings[i];
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
