// contributions.js - what the loaded plugins of a workspace contribute to the shell (docs/02, docs/15 "Composition"):
// activities, tool windows, commands, menus, keybindings and status bar items, registered with the shell's registries;
// and each plugin's UI module, imported once per page with activate(ied) called once.

const DOCKS = new Set(["left", "right", "bottom", "document"]);

// The shell's own dock host, there only while no plugin contributes one: the shell's tabs (Plugins, Keyboard
// Shortcuts) always have a place, so a person can turn plugins back on.
const SHELL_PLUGIN = { id: "shell", source: "builtin", contributes: {}, ui: null, base: "/", module: null };
export const HOME_ACTIVITY = { id: "shell.home", title: "Home", icon: "$(home)", order: 1000, plugin: SHELL_PLUGIN };

export class Contributions {
	// `shell` gives commands, keybindings, menus, report(), apiFor(plugin) and the page's module cache.
	constructor(shell) {
		this.shell = shell;
		this.plugins = [];
		this.activities = [];
		this.toolWindows = [];
		this.statusItems = [];
		this.toolbarItems = [];
		this.panel = null;
	}

	// Reads the contributions of `wsId` and registers them; resolves once every UI module is imported and activated.
	async load(wsId) {
		const answer = await this.shell.client.request("plugins.contributions", { wsId });
		this.plugins = answer.plugins.map((p) => ({
			id: p.id, source: p.source, contributes: p.contributes || {}, ui: p.ui || null,
			base: p.source === "project" ? "/w/" + encodeURIComponent(wsId) + "/plugins/" + encodeURIComponent(p.id) + "/" : "/plugins/" + encodeURIComponent(p.id) + "/",
			module: null,
		}));
		this.register();
		await Promise.all(this.plugins.map((plugin) => this.importModule(plugin)));
	}

	register() {
		const { commands, keybindings, menus } = this.shell;
		commands.clearPluginDefinitions();
		keybindings.clearPluginBindings();
		menus.clearPluginItems();
		this.activities = [];
		this.toolWindows = [];
		this.statusItems = [];
		this.toolbarItems = [];
		this.panel = null;
		for (const plugin of this.plugins) {
			const c = plugin.contributes;
			for (const spec of c.commands || []) commands.define(spec, plugin.id, plugin.base);
			for (const binding of c.keybindings || []) keybindings.define(binding, plugin.id);
			for (const [menuId, items] of Object.entries(c.menus || {})) for (const item of items) menus.add(menuId, item, plugin.id);
			for (const a of c.activities || []) this.activities.push(Object.assign({ plugin, order: 100 }, a));
			for (const t of c.toolWindows || []) this.toolWindows.push(Object.assign({ plugin, activity: "ied.editor" }, t, { dock: DOCKS.has(t.defaultDock) ? t.defaultDock : "bottom" }));
			for (const s of c.statusBarItems || []) this.statusItems.push(Object.assign({ plugin, priority: 0 }, s));
			for (const t of c.toolbarItems || []) this.toolbarItems.push(Object.assign({ plugin, priority: 0 }, t));
			if (!this.panel && (c.panels || []).length) this.panel = Object.assign({ plugin }, c.panels[0]);
		}
		if (!this.activities.some((a) => !a.view)) this.activities.push(HOME_ACTIVITY);
		this.activities.sort((a, b) => a.order - b.order || a.title.localeCompare(b.title));
	}

	async importModule(plugin) {
		if (!plugin.ui) return;
		const cache = this.shell.modules;
		if (!cache.has(plugin.ui)) cache.set(plugin.ui, this.activate(plugin));
		const loaded = await cache.get(plugin.ui);
		plugin.module = loaded.module;
		plugin.error = loaded.error;
	}

	// Imports a UI module and calls its activate(ied); a failure is reported and kept, so its views say why.
	async activate(plugin) {
		try {
			const module = await import(plugin.ui);
			if (typeof module.activate === "function") await module.activate(this.shell.apiFor(plugin));
			return { module, error: null };
		}
		catch (e) {
			this.shell.report("Plugin '" + plugin.id + "': its UI module failed to load: " + (e && e.message ? e.message : e), e);
			return { module: null, error: e };
		}
	}

	activity(id) {
		return this.activities.find((a) => a.id === id) || null;
	}

	toolWindow(id) {
		return this.toolWindows.find((t) => t.id === id) || null;
	}

	toolsOf(activityId) {
		return this.toolWindows.filter((t) => t.activity === activityId && this.shell.context.holds(t.when));
	}

	plugin(id) {
		return this.plugins.find((p) => p.id === id) || null;
	}
}
