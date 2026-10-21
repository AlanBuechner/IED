// bindings.js - client bindings (docs/02 "Client bindings as built"): a plugin's UI module provides the bindings its
// manifest lists with side "client", and any UI module whose manifest consumes one calls it. A call to a binding no
// loaded plugin provides rejects, so the caller degrades.

export class BindingError extends Error {
	constructor(code, message) {
		super(message);
		this.code = code;
	}
}

export class Bindings {
	// `declared(pluginId)`: the plugin's manifest `bindings` ({provides, consumes}) while it is loaded, else null.
	constructor(declared) {
		this.declared = declared;
		this.providers = new Map();   // name -> {pluginId, fn}
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
		return this.provider(name) !== null;
	}

	async call(pluginId, name, args) {
		const declared = this.declared(pluginId);
		if (!declared || !(declared.consumes || []).some((b) => b.name === name)) throw new BindingError("not_declared", "'" + pluginId + "' does not consume '" + name + "'");
		const provider = this.provider(name);
		if (!provider) throw new BindingError("not_provided", "no plugin provides '" + name + "'");
		return provider.fn(args);
	}
}
