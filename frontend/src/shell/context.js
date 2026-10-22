// context.js - context keys for when-clauses (docs/15): set by the shell and by plugin UI modules; listeners hear every
// change so enablement and menus stay current.

import { evaluateWhen } from "./when.js";

export class ContextKeys {
	constructor() {
		this.values = new Map();
		this.listeners = new Set();
	}

	get(key) {
		return this.values.get(key);
	}

	set(key, value) {
		if (this.values.get(key) === value) return;
		if (value === undefined) this.values.delete(key);
		else this.values.set(key, value);
		for (const fn of [...this.listeners]) fn(key, value);
	}

	// Whether `clause` holds now (an empty one always does; one that does not parse never does); `keys` ({key: value})
	// stand in for the context's own (a tab strip's buttons ask about its group's tab, not the focused one).
	holds(clause, keys) {
		return evaluateWhen(clause, (key) => (keys && key in keys ? keys[key] : this.values.get(key)));
	}

	onChange(fn) {
		this.listeners.add(fn);
		return () => this.listeners.delete(fn);
	}
}
