// contributions.js - what the loaded plugins of a workspace contribute to the shell (docs/02, docs/15 "Composition"):
// activities, tool windows, commands, menus, keybindings and status bar items, registered with the shell's registries;
// and each plugin's UI module, imported once per page with activate(ied) called once. The file and project icons they
// describe are worked out by `icons`.

import { Icons } from "./icons.js";

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
		this.icons = new Icons((point) => this.of(point));
		this.listeners = new Set();
		this.imported = new Map();   // a UI module's URL -> {module, error}, once imported
	}

	// `fn()` each time the contributions are read again or cleared, once every UI module is activated; returns the
	// function that stops it.
	on(fn) {
		this.listeners.add(fn);
		return () => this.listeners.delete(fn);
	}

	// Reads the contributions of `wsId` and registers them; resolves once every UI module is imported and activated.
	async load(wsId) {
		const answer = await this.shell.client.request("plugins.contributions", { wsId });
		// A module imported before keeps serving its views at once: one shown while the others import (a reload after a
		// trust or plugin change) must not find it missing.
		this.plugins = answer.plugins.map((p) => {
			const known = p.ui ? this.imported.get(p.ui) : null;
			return {
				id: p.id, source: p.source, contributes: p.contributes || {}, ui: p.ui || null,
				base: p.source === "project" ? "/w/" + encodeURIComponent(wsId) + "/plugins/" + encodeURIComponent(p.id) + "/" : "/plugins/" + encodeURIComponent(p.id) + "/",
				module: known ? known.module : null, error: known ? known.error : null, loaded: Boolean(known) || !p.ui,
			};
		});
		this.register();
		await Promise.all(this.plugins.map((plugin) => this.importModule(plugin)));
		this.changed();
	}

	changed() {
		for (const fn of [...this.listeners]) {
			try {
				fn();
			}
			catch (e) {
				this.shell.report("A change of the plugins' contributions was not taken: " + (e && e.message ? e.message : e), e);
			}
		}
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
		this.icons.clear();
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

	// No workspace shown (the start page): none of a workspace's plugins stays registered, as on a page that never
	// showed one.
	clear() {
		this.plugins = [];
		this.register();
		this.activities = [];
		this.changed();
	}

	async importModule(plugin) {
		if (!plugin.ui) return;
		const cache = this.shell.modules;
		if (!cache.has(plugin.ui)) cache.set(plugin.ui, this.activate(plugin));
		const loaded = await cache.get(plugin.ui);
		this.imported.set(plugin.ui, loaded);
		plugin.module = loaded.module;
		plugin.error = loaded.error;
		plugin.loaded = true;
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

	// Where layout mode `mode`'s default puts tool window `tool` (docs/15 "Window layouts"): {dock, group, size, autoHide,
	// open}, from its `modes.<mode>` over its own fields.
	placementOf(tool, mode) {
		const own = tool.modes && tool.modes[mode] ? tool.modes[mode] : {};
		const size = own.size !== undefined ? own.size : tool.defaultSize;
		return {
			dock: DOCKS.has(own.dock) ? own.dock : tool.dock,
			group: own.group !== undefined ? own.group : tool.dockGroup || null,
			size: Number.isFinite(size) && size > 0 && size < 1 ? size : null,
			autoHide: own.autoHide !== undefined ? own.autoHide === true : tool.autoHide === true,
			open: own.open !== undefined ? own.open !== false : tool.open !== false,
		};
	}

	// The layout modes, in load order: [{id, title, when}].
	layoutModes() {
		return this.of("layoutModes").map((entry) => entry.value).filter((mode) => mode && typeof mode.id === "string");
	}

	plugin(id) {
		return this.plugins.find((p) => p.id === id) || null;
	}

	// A contribution point's entries over every plugin, in load order: [{plugin: {id, base}, value}].
	of(point) {
		return this.plugins.flatMap((p) => {
			const value = p.contributes[point];
			if (value === undefined) return [];
			return (Array.isArray(value) ? value : [value]).map((entry) => ({ plugin: { id: p.id, base: p.base }, value: entry }));
		});
	}
}
