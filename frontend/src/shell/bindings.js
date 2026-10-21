// bindings.js - bindings in the browser (docs/02 "Client bindings as built"): a plugin's UI module provides the
// bindings its manifest lists with side "client", and any UI module whose manifest consumes one calls it - or a server
// binding, which a plugin's C4 code answers through plugins.call (docs/26 section 4). A call to a binding no loaded
// plugin provides rejects, so the caller degrades. An event binding (kind "event") has no answer: its provider emits
// and every consumer listening hears it.

export class BindingError extends Error {
	constructor(code, message) {
		super(message);
		this.code = code;
	}
}

export class Bindings {
	// `declared(pluginId)`: the plugin's manifest `bindings` ({provides, consumes}) while it is loaded, else null.
	// `server`: {provides(name): whether a loaded plugin declares the server binding, call(name, args): its answer}.
	constructor(declared, server) {
		this.declared = declared;
		this.server = server || null;
		this.providers = new Map();   // name -> {pluginId, fn}
		this.listeners = new Map();   // event name -> Set of {pluginId, fn}
	}

	provide(pluginId, name, fn) {
		const declared = this.declared(pluginId);
		const listed = declared && (declared.provides || []).some((b) => b.name === name && b.side === "client");
		if (!listed) throw new BindingError("not_declared", "'" + pluginId + "' does not declare the client binding '" + name + "' in bindings.provides");
		this.providers.set(name, { pluginId, fn });
	}

	// The provider of `name` while its plugin is loaded.
	provider(name) {
		const provider = this.providers.get(name);
		return provider && this.declared(provider.pluginId) ? provider : null;
	}

	has(name) {
		return this.provider(name) !== null || this.onServer(name);
	}

	onServer(name) {
		return this.server !== null && this.server.provides(name);
	}

	// A server binding's answer, for a caller that is not a plugin (a command that names a `binding`).
	callServer(name, args) {
		if (!this.onServer(name)) return Promise.reject(new BindingError("not_provided", "no plugin provides the server binding '" + name + "'"));
		return this.server.call(name, args || {});
	}

	// An event to the plugins listening: only from the plugin that declares it (kind "event", side "client").
	emit(pluginId, name, data) {
		const declared = this.declared(pluginId);
		const listed = declared && (declared.provides || []).some((b) => b.name === name && b.side === "client" && b.kind === "event");
		if (!listed) throw new BindingError("not_declared", "'" + pluginId + "' does not declare the client event '" + name + "' in bindings.provides");
		for (const listener of [...(this.listeners.get(name) || [])]) {
			if (!this.declared(listener.pluginId)) continue;
			try {
				listener.fn(data);
			}
			catch (e) {
				console.error("IED: a listener of '" + name + "' failed", e);
			}
		}
	}

	// Listens to an event the plugin consumes; returns the function that stops it.
	on(pluginId, name, fn) {
		const declared = this.declared(pluginId);
		if (!declared || !(declared.consumes || []).some((b) => b.name === name)) throw new BindingError("not_declared", "'" + pluginId + "' does not consume '" + name + "'");
		if (!this.listeners.has(name)) this.listeners.set(name, new Set());
		const listener = { pluginId, fn };
		this.listeners.get(name).add(listener);
		return () => this.listeners.get(name).delete(listener);
	}

	async call(pluginId, name, args) {
		const declared = this.declared(pluginId);
		if (!declared || !(declared.consumes || []).some((b) => b.name === name)) throw new BindingError("not_declared", "'" + pluginId + "' does not consume '" + name + "'");
		const provider = this.provider(name);
		if (provider) return provider.fn(args);
		if (this.onServer(name)) return this.server.call(name, args || {});
		throw new BindingError("not_provided", "no plugin provides '" + name + "'");
	}
}
