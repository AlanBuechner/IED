// commands.js - the command registry (F-SHL-20): what a command is comes from a contribution (or the shell), what it
// does from a handler a UI module (or the shell) registers. Everything clickable runs a command; a failure is reported,
// never swallowed.

export class Commands {
	// `context`: ContextKeys; `report(message, detail)`: shows a failure to the person.
	constructor(context, report) {
		this.context = context;
		this.report = report;
		this.definitions = new Map();
		this.handlers = new Map();
		this.listeners = new Set();
	}

	// spec: {id, title, category, icon, enablement, binding, task}; `source`: "shell" or a plugin id; `base`: the URL an
	// icon path is relative to.
	define(spec, source, base) {
		this.definitions.set(spec.id, Object.assign({}, spec, { source, base }));
		this.changed();
	}

	handle(id, fn) {
		this.handlers.set(id, fn);
		this.changed();
	}

	// Forgets every definition from plugins (contributions are about to be read again); handlers stay.
	clearPluginDefinitions() {
		for (const [id, definition] of [...this.definitions]) if (definition.source !== "shell") this.definitions.delete(id);
		this.changed();
	}

	get(id) {
		return this.definitions.get(id) || null;
	}

	all() {
		return [...this.definitions.values()];
	}

	label(id) {
		const definition = this.get(id);
		if (!definition) return id;
		return definition.category ? definition.category + ": " + definition.title : definition.title;
	}

	isEnabled(id) {
		const definition = this.get(id);
		if (!definition || !this.handlers.has(id)) return false;
		return this.context.holds(definition.enablement);
	}

	// Runs a command; resolves to what its handler returned, or undefined after reporting why it could not run.
	async run(id, args) {
		const definition = this.get(id);
		if (!definition) return this.fail("There is no command '" + id + "'.");
		if (!this.context.holds(definition.enablement)) return this.fail("'" + this.label(id) + "' is not available now.");
		const handler = this.handlers.get(id);
		if (!handler) return this.fail(this.unhandled(definition));
		try {
			return await handler(args);
		}
		catch (e) {
			return this.fail("'" + this.label(id) + "' failed: " + (e && e.message ? e.message : String(e)), e);
		}
	}

	unhandled(definition) {
		if (definition.binding) return "'" + this.label(definition.id) + "' calls the server binding '" + definition.binding + "', and server bindings arrive with plugin runtimes (M11).";
		if (definition.task) return "'" + this.label(definition.id) + "' runs the task '" + definition.task + "', and tasks arrive with the build loop (M3).";
		return "'" + this.label(definition.id) + "' has no handler: its plugin's UI module did not register one.";
	}

	fail(message, error) {
		this.report(message, error);
		return undefined;
	}

	onChange(fn) {
		this.listeners.add(fn);
		return () => this.listeners.delete(fn);
	}

	changed() {
		for (const fn of [...this.listeners]) fn();
	}
}
