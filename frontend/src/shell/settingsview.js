// settingsview.js - the settings window (docs/07 "The settings window", mockup 04): Visual Studio's Tools > Options,
// shown over the shell (settingsoverlay.js) - a tree of pages whose branches fold, the tier being edited (User, Solution,
// Project), search (its finds by page), Modified only, and on a page its settings in groups that fold, each a row with
// its effective value, the tier that set it, a control from its schema, Reset and a menu; a plugin's settingsPages draw
// their own editor under their page. Values come from settings:<wsId> (a project's from settings.get), writes are
// settings.set and settings.reset (docs/07 "Settings on the wire"); the server checks every value against its schema.

import { h, icon, replace } from "/ui-kit/dom.js";
import { MenuHost } from "/ui-kit/menu.js";
import { TIERS, controlKind, findPage, inputText, isModified, pageGroups, pageOwning, parseInput, problemsFor, searchByPage, settingTitle, settingsPages, sourceLabel } from "./settingsmodel.js";
import { KeyboardPage } from "./keyboardpage.js";
import { LanguageToolsPage } from "./languagetools.js";

const menus = new MenuHost();
const FOLDED_KEY = "ied.settings.folded";           // the groups folded, "<page id>|<group id>"
const TREE_FOLDED_KEY = "ied.settings.treeFolded";  // the tree's pages whose children are folded

function stored(key, fallback) {
	try {
		const value = localStorage.getItem(key);
		return value === null ? fallback : JSON.parse(value);
	}
	catch {
		return fallback;
	}
}

function store(key, value) {
	try {
		localStorage.setItem(key, JSON.stringify(value));
	}
	catch {
		// a private window: the choice lasts as long as the page
	}
}

export function settingsView(shell, ctx) {
	return new SettingsView(shell, ctx);
}

// The shell's own pages, drawn as a plugin's settingsPages are: Environment > Keyboard edits keyboard.bindings;
// Languages > Tools shows the workspace's language tools and installs them (docs/26 "Language tools").
function shellPages(shell) {
	const plugin = { id: "shell", base: "/", module: {
		KeyboardSettings: (ctx) => new KeyboardPage(shell, ctx),
		LanguageTools: (ctx) => (ctx.wsId ? new LanguageToolsPage(shell, ctx) : ctx.el.append(h("p.muted", null, "Open a workspace to see its language tools."))),
	} };
	return [
		{ plugin, value: { id: "shell.keyboard", page: "Environment", title: "Keyboard", view: "KeyboardSettings", keys: ["keyboard.bindings"] } },
		{ plugin, value: { id: "shell.languages", page: "Languages", title: "Tools", view: "LanguageTools", keys: [] } },
	];
}

class SettingsView {
	constructor(shell, ctx) {
		this.shell = shell;
		this.ctx = ctx;
		this.schema = {};
		this.state = { tiers: {}, projects: {}, values: {} };
		this.projectValues = null;
		this.pageId = shell.nextSettingsPage || null;
		this.tier = shell.nextSettingsTier || "app";
		this.project = shell.nextSettingsProject || null;
		shell.nextSettingsPage = null;
		shell.nextSettingsTier = null;
		shell.nextSettingsProject = null;
		shell.settingsViews.add(this);
		this.query = "";
		this.loaded = false;
		this.mounted = new Map();
		this.listeners = new Set();
		this.modifiedOnly = false;
		this.folded = new Set(stored(FOLDED_KEY, []));
		this.treeFolded = new Set(stored(TREE_FOLDED_KEY, []));
		this.searchField = h("input.input-field.set-search", { type: "search", placeholder: "Search settings (Ctrl+E)", "aria-label": "Search settings", spellcheck: "false" });
		this.searchField.addEventListener("input", () => {
			this.query = this.searchField.value;
			this.renderPage();
		});
		this.modifiedButton = h("button.btn.flat.set-modified-only", { "aria-pressed": "false", title: "Show only the settings set in some tier", on: { click: () => this.setModifiedOnly(!this.modifiedOnly) } }, icon("filter"), "Modified only");
		this.foldButtons = h("span.set-folds", null,
			h("button.ib", { title: "Expand all groups", "aria-label": "Expand all groups", on: { click: () => this.foldAll(false) } }, icon("expand-all")),
			h("button.ib", { title: "Collapse all groups", "aria-label": "Collapse all groups", on: { click: () => this.foldAll(true) } }, icon("collapse-all")));
		this.tiersEl = h("span.set-seg", { role: "radiogroup", "aria-label": "Editing tier" });
		this.where = h("span.muted.set-where");
		this.tree = h("nav.set-tree", { "aria-label": "Settings pages" });
		this.page = h("div.set-page");
		this.el = h("div.settings-window", null,
			h("div.set-top", null, this.searchField, this.modifiedButton, this.foldButtons, h("span.grow"), h("span.muted", null, "Editing tier"), this.tiersEl, this.where),
			h("div.set-body", null, this.tree, this.page));
		ctx.el.append(this.el);
		ctx.el.addEventListener("keydown", (event) => {
			if (event.ctrlKey && (event.key === "e" || event.key === "E")) {
				event.preventDefault();
				this.searchField.focus();
			}
		});
		// Without a workspace (the start page) there is the User tier alone: settings:app, the global
		// plugins' schema, and their own pages, imported here since no workspace loaded them.
		this.plugins = null;
		this.stop = shell.client.subscribe(ctx.wsId ? "settings:" + ctx.wsId : "settings:app", {
			snap: (data) => this.received(data),
			evt: (kind, data) => kind === "settings.changed" && this.received(data),
		});
		this.loadSchema();
	}

	// What a request about the settings names of the workspace: nothing, without one.
	scope() {
		return this.ctx.wsId ? { wsId: this.ctx.wsId } : {};
	}

	async loadSchema() {
		try {
			this.schema = (await this.shell.client.request("settings.schema", this.scope())).settings || {};
			if (!this.ctx.wsId) this.plugins = await globalPlugins(this.shell);
		}
		catch (e) {
			this.shell.report("settings.schema: " + e.message, e);
		}
		this.loaded = true;
		this.render();
	}

	received(data) {
		this.state = { tiers: data.tiers || {}, projects: data.projects || {}, values: data.values || {} };
		if (this.tier === "project") this.loadProject();
		else this.render();
	}

	async loadProject() {
		if (!this.project) this.project = Object.keys(this.state.projects)[0] || null;
		this.projectValues = this.project ? (await this.shell.client.request("settings.get", { wsId: this.ctx.wsId, project: this.project }).catch(() => null)) : null;
		this.render();
	}

	values() {
		return this.tier === "project" && this.projectValues ? this.projectValues.values || {} : this.state.values;
	}

	pages() {
		const plugins = this.ctx.wsId ? this.shell.contributions.plugins : this.plugins || [];
		const contributed = plugins.flatMap((p) => (p.contributes.settingsPages || []).map((value) => ({ plugin: p, value })));
		return settingsPages(this.schema, shellPages(this.shell).concat(contributed));
	}

	// Shows the page `page` ("custom:shell.keyboard", "page:AI"); `tier` says which tier to edit and, for a project's
	// tier, which project (Settings... on a project in Solution Explorer, docs/07). With neither, just the page.
	show(page, tier, project) {
		if (tier) this.tier = this.ctx.wsId ? tier : "app";
		if (project) this.project = project;
		if (page) this.pageId = page;
		if (this.tier === "project" && this.ctx.wsId) this.loadProject();
		else this.render();
	}

	render() {
		if (!this.loaded) return;
		const pages = this.pages();
		if (!this.pageId || !findPage(pages, this.pageId)) this.pageId = firstPage(pages);
		this.renderTiers();
		this.renderTree(pages);
		this.renderPage();
		for (const fn of [...this.listeners]) fn();
	}

	renderTiers() {
		const solution = this.state.tiers.solution;
		replace(this.tiersEl, ...TIERS.filter(([id]) => id !== "solution" || solution).filter(([id]) => id !== "project" || Object.keys(this.state.projects).length).map(([id, label]) => h("button" + (this.tier === id ? ".on" : ""), {
			role: "radio", "aria-checked": this.tier === id ? "true" : "false", dataset: { tier: id },
			on: { click: () => this.setTier(id) },
		}, icon(id === "app" ? "account" : id === "solution" ? "library" : "project"), label)));
		const projects = Object.keys(this.state.projects);
		const select = this.tier === "project" && projects.length > 1 ? h("select.input-field", { "aria-label": "Project", on: { change: (event) => {
			this.project = event.target.value;
			this.loadProject();
		} } }, ...projects.map((name) => h("option", Object.assign({ value: name }, name === this.project ? { selected: true } : {}), name))) : null;
		const file = this.state.tiers[this.tier === "app" ? "app" : "solution"];
		replace(this.where, this.tier === "project" ? "Project " : "", select || (this.tier === "project" ? this.project || "" : ""), file && this.tier !== "project" ? h("span.mono", { title: file.file }, shortFile(file.file)) : null);
	}

	setTier(tier) {
		this.tier = tier;
		if (tier === "project") this.loadProject();
		else this.render();
	}

	renderTree(pages) {
		const go = (node) => {
			this.pageId = node.id;
			this.query = "";
			this.searchField.value = "";
			this.render();
			this.page.scrollTop = 0;
		};
		const row = (node, depth, parent) => {
			const branch = depth === 0 && node.children.length > 0;
			const folded = branch && this.treeFolded.has(node.id) && !node.children.some((c) => c.id === this.pageId);
			const chevron = branch ? h("span.set-twisty", { title: folded ? "Expand" : "Collapse", on: { click: (event) => {
				event.stopPropagation();
				this.toggleTree(node.id);
			} } }, icon(folded ? "chevron-right" : "chevron-down")) : h("span.set-twisty");
			const modified = this.modifiedCount(node.keys);
			return h("button.set-node" + (node.id === this.pageId ? ".sel" : ""), {
				style: { "--d": String(depth) }, dataset: { page: node.id }, "aria-expanded": branch ? String(!folded) : null,
				on: {
					click: () => go(node),
					keydown: (event) => {
						if (!branch) return;
						if (event.key === "ArrowLeft" && !folded) this.toggleTree(node.id);
						else if (event.key === "ArrowRight" && folded) this.toggleTree(node.id);
					},
				},
			}, chevron, h("span.name", null, node.title),
			this.problemCount(node.keys) ? h("span.meta", null, icon("warning", "c-warn")) : modified ? h("span.meta.set-count", { title: modified + " set" }, String(modified)) : null);
		};
		replace(this.tree, ...pages.flatMap((p) => {
			const folded = this.treeFolded.has(p.id) && !p.children.some((c) => c.id === this.pageId);
			return [row(p, 0), ...(folded ? [] : p.children.map((c) => row(c, 1, p)))];
		}));
	}

	toggleTree(id) {
		if (this.treeFolded.has(id)) this.treeFolded.delete(id);
		else this.treeFolded.add(id);
		store(TREE_FOLDED_KEY, [...this.treeFolded]);
		this.renderTree(this.pages());
	}

	modifiedCount(keys) {
		const values = this.values();
		return keys.reduce((n, key) => n + (isModified(values, key) ? 1 : 0), 0);
	}

	setModifiedOnly(on) {
		this.modifiedOnly = on;
		this.modifiedButton.classList.toggle("on", on);
		this.modifiedButton.setAttribute("aria-pressed", on ? "true" : "false");
		this.renderPage();
	}

	// Every group of the page shown folded, or none.
	foldAll(fold) {
		const node = findPage(this.pages(), this.pageId);
		if (!node) return;
		for (const group of pageGroups(node.keys, this.schema)) {
			const id = node.id + "|" + group.id;
			if (fold) this.folded.add(id);
			else this.folded.delete(id);
		}
		store(FOLDED_KEY, [...this.folded]);
		this.renderPage();
	}

	toggleGroup(id) {
		if (this.folded.has(id)) this.folded.delete(id);
		else this.folded.add(id);
		store(FOLDED_KEY, [...this.folded]);
		this.renderPage();
	}

	problemCount(keys) {
		return keys.reduce((n, key) => n + problemsFor(this.state.tiers, key).length, 0);
	}

	renderPage() {
		const pages = this.pages();
		const values = this.values();
		const shown = (key) => !this.modifiedOnly || isModified(values, key);
		this.foldButtons.hidden = Boolean(this.query.trim());
		if (this.query.trim()) {
			// What the search finds, by page: a page's head opens it, a key a plugin's own page edits links there.
			const found = searchByPage(pages, this.schema, this.query).map((hit) => Object.assign(hit, { keys: hit.keys.filter(shown) })).filter((hit) => hit.keys.length || (hit.page.view && !this.modifiedOnly));
			const count = found.reduce((n, hit) => n + hit.keys.length, 0);
			replace(this.page, h("div.set-crumbs", null, count + (count === 1 ? " setting matches" : " settings match") + (this.modifiedOnly ? " (modified only)" : "")),
				...found.map((hit) => h("section.set-found", { dataset: { page: hit.page.id } },
					h("button.set-found-head", { on: { click: () => this.openPage(hit.page.id) } }, hit.parent ? hit.parent.title + " \u203a " + hit.page.title : hit.page.title, icon("arrow-right")),
					...hit.keys.map((key) => {
						const owner = pageOwning(pages, key);
						return owner ? this.pageLink(key, owner) : this.row(key);
					}))));
			return;
		}
		const node = findPage(pages, this.pageId);
		if (!node) {
			replace(this.page, h("p.muted", null, "No plugin declares settings."));
			return;
		}
		if (node.view) {
			this.mountCustom(node);
			return;
		}
		const parent = pages.find((p) => p.children.includes(node));
		const groups = pageGroups(node.keys.filter(shown), this.schema);
		const sections = groups.length > 1 || (groups.length === 1 && groups[0].title !== "General")
			? groups.map((group) => this.groupSection(node, group))
			: groups.flatMap((group) => group.keys.map((key) => this.row(key)));
		const empty = this.modifiedOnly && node.keys.length && !groups.length ? h("p.muted", null, "Nothing on this page is set: every setting here has its default.") : null;
		replace(this.page, h("div.set-crumbs", null, parent ? parent.title + " \u203a " + node.title : node.title), h("h1", null, node.title),
			...sections, empty,
			node.children.length && !node.keys.length ? h("div.set-children", null, h("p.muted", null, "Pages under " + node.title + ":"),
				...node.children.map((child) => h("button.btn.flat", { on: { click: () => this.openPage(child.id) } }, icon("chevron-right"), child.title))) : null);
	}

	// A group of a page's settings, folded as the owner left it on this device: its head says how many it has, and how
	// many of them are set.
	groupSection(node, group) {
		const id = node.id + "|" + group.id;
		const folded = this.folded.has(id);
		const modified = this.modifiedCount(group.keys);
		return h("section.set-sec" + (folded ? ".folded" : ""), { dataset: { group: group.id } },
			h("button.set-sec-head", { "aria-expanded": folded ? "false" : "true", on: { click: () => this.toggleGroup(id) } },
				icon(folded ? "chevron-right" : "chevron-down"), h("span.set-sec-title", null, group.title),
				h("span.meta", null, group.keys.length + (group.keys.length === 1 ? " setting" : " settings") + (modified ? " \u00b7 " + modified + " set" : ""))),
			folded ? null : h("div.set-sec-rows", null, ...group.keys.map((key) => this.row(key))));
	}

	openPage(id) {
		this.pageId = id;
		this.query = "";
		this.searchField.value = "";
		this.render();
		this.page.scrollTop = 0;
	}

	focus() {
		this.searchField.focus();
	}

	// A setting a plugin's own page edits: found here, set there, where the page decides whether it can be.
	pageLink(key, node) {
		const entry = this.schema[key] || { schema: {} };
		return h("div.set-row", { dataset: { key } },
			h("div.set-label", null, h("span.set-title", null, settingTitle(key)), h("span.mono.muted", null, key)),
			entry.schema && entry.schema.description ? h("div.set-desc", null, entry.schema.description) : null,
			h("div.set-edit", null, h("button.btn", { on: { click: () => {
				this.pageId = node.id;
				this.query = "";
				this.searchField.value = "";
				this.render();
			} } }, "Set on the " + node.title + " page")));
	}

	// A plugin's own page, mounted once and kept while the window lives.
	mountCustom(node) {
		let entry = this.mounted.get(node.id);
		if (!entry) {
			const el = h("div.set-custom", { dataset: { page: node.id } });
			const factory = node.plugin.module && node.plugin.module[node.view];
			entry = { el, instance: null };
			this.mounted.set(node.id, entry);
			if (typeof factory !== "function") el.append(h("p.c-error", null, "The plugin " + node.plugin.id + " has no view " + node.view + "."));
			else {
				try {
					entry.instance = factory({ el, ied: this.shell.apiFor(node.plugin), wsId: this.ctx.wsId, settings: this.api(node) }) || null;
				}
				catch (e) {
					el.append(h("p.c-error", null, "This page failed: " + e.message));
					this.shell.report("settings page " + node.id + ": " + e.message, e);
				}
			}
		}
		replace(this.page, entry.el);
	}

	// What a plugin's page reads and writes; `tier` defaults to the one being edited.
	api(node) {
		return {
			keys: node.keys,
			schema: (key) => this.schema[key] || null,
			value: (key) => this.values()[key] || { value: this.schema[key] ? this.schema[key].default : null, source: "default" },
			tier: () => this.tier,
			set: (key, value, tier) => this.write(key, value, tier),
			reset: (key, tier) => this.clear(key, tier),
			// The window's own row for `key` (its control, source, Reset and problems), for a page that draws some keys
			// its own way and leaves the others as every page shows them; drawn again by the page on a change.
			row: (key) => this.row(key),
			onChange: (fn) => {
				this.listeners.add(fn);
				return () => this.listeners.delete(fn);
			},
		};
	}

	// --- Rows --------------------------------------------------------------------------------------------------------

	row(key) {
		const entry = this.schema[key] || { schema: {}, tiers: [] };
		const current = this.values()[key] || { value: entry.default, source: "default" };
		const allowed = (entry.tiers || []).includes(this.tier);
		const editingSet = current.source === this.tier;
		const kind = controlKind(entry.schema);
		const message = h("div.set-message.c-error", { hidden: true });
		const control = this.control(key, kind, entry.schema, current.value, allowed, message);
		const problems = problemsFor(this.state.tiers, key);
		const modified = current.source && current.source !== "default";
		return h("div.set-row" + (modified ? ".modified" : ""), { dataset: { key }, on: { contextmenu: (event) => this.rowMenu(event, key, current, editingSet && allowed) } },
			h("div.set-label", null, h("span.set-title", null, settingTitle(key)), h("span.mono.muted", null, key)),
			entry.schema && entry.schema.description ? h("div.set-desc", null, entry.schema.description) : null,
			h("div.set-edit", null, control,
				h("span.set-source" + (current.source !== "default" ? ".set" : ""), { title: "The tier that sets the value in effect" }, sourceLabel(current.source)),
				editingSet && allowed ? h("button.btn.flat", { title: "Remove it from the " + sourceLabel(this.tier) + " tier", on: { click: () => this.clear(key) } }, icon("discard"), "Reset") : null,
				allowed ? null : h("span.muted", null, "set in: " + (entry.tiers || []).map(sourceLabel).join(", "))),
			message,
			...problems.map((p) => h("div.set-message.c-error", null, sourceLabel(p.tier) + " file, line " + p.line + ": " + p.message)));
	}

	// A row's menu: copy its id or its value as JSON, Reset it in the tier being edited.
	rowMenu(event, key, current, resettable) {
		if (event.target.closest("input, textarea, select")) return;
		event.preventDefault();
		menus.show({ at: event, label: settingTitle(key), rows: [
			{ kind: "item", label: "Copy Setting ID", id: "id", enabled: true },
			{ kind: "item", label: "Copy Value as JSON", id: "value", enabled: true },
			{ kind: "separator" },
			{ kind: "item", label: "Reset in " + sourceLabel(this.tier), id: "reset", enabled: resettable },
		], pick: (row) => {
			if (row.id === "id") this.copy(key);
			else if (row.id === "value") this.copy(JSON.stringify(current.value === undefined ? null : current.value, null, 2));
			else if (row.id === "reset") this.clear(key);
		} });
	}

	async copy(text) {
		try {
			await navigator.clipboard.writeText(text);
		}
		catch (e) {
			this.shell.report("The clipboard refused the text: " + e.message, e);
		}
	}

	control(key, kind, schema, value, allowed, message) {
		const disabled = allowed ? {} : { disabled: true };
		const commit = async (next) => {
			message.hidden = true;
			const error = await this.write(key, next);
			if (error) {
				message.textContent = error;
				message.hidden = false;
			}
		};
		if (kind === "boolean") {
			return h("input.set-check", Object.assign({ type: "checkbox", "aria-label": key, on: { change: (event) => commit(event.target.checked) } }, value ? { checked: true } : {}, disabled));
		}
		if (kind === "enum") {
			return h("select.input-field", Object.assign({ "aria-label": key, on: { change: (event) => commit(JSON.parse(event.target.value)) } }, disabled),
				...schema.enum.map((option) => h("option", Object.assign({ value: JSON.stringify(option) }, option === value ? { selected: true } : {}), String(option))));
		}
		const field = kind === "json"
			? h("textarea.input-field.set-json", Object.assign({ "aria-label": key, spellcheck: "false", rows: String(Math.min(12, inputText(kind, value).split("\n").length + 1)) }, disabled))
			: h("input.input-field", Object.assign({ "aria-label": key, type: kind === "string" ? "text" : "number", spellcheck: "false" }, disabled));
		field.value = inputText(kind, value);
		let sent = null;   // the text last sent: the blur after an Enter does not send a refused value again
		const apply = () => {
			if (field.value === inputText(kind, value) || field.value === sent) return;
			sent = field.value;
			try {
				commit(parseInput(kind, field.value));
			}
			catch (e) {
				message.textContent = e.message;
				message.hidden = false;
			}
		};
		field.addEventListener("keydown", (event) => {
			event.stopPropagation();
			if (event.key === "Enter" && (kind !== "json" || event.ctrlKey)) {
				event.preventDefault();
				apply();
			}
			else if (event.key === "Escape") field.value = inputText(kind, value);
		});
		field.addEventListener("blur", apply);
		return field;
	}

	// The error to show, or null when it was written.
	async write(key, value, tier) {
		const params = Object.assign({ tier: tier || this.tier, key, value }, this.scope());
		if ((tier || this.tier) === "project") params.project = this.project;
		try {
			await this.shell.client.request("settings.set", params);
			if (this.tier === "project") this.loadProject();
			return null;
		}
		catch (e) {
			return refusal(e);
		}
	}

	async clear(key, tier) {
		const params = Object.assign({ tier: tier || this.tier, key }, this.scope());
		if ((tier || this.tier) === "project") params.project = this.project;
		try {
			await this.shell.client.request("settings.reset", params);
			if (this.tier === "project") this.loadProject();
			return null;
		}
		catch (e) {
			this.shell.toasts.show(e.message, "error");
			return e.message;
		}
	}

	dispose() {
		this.stop();
		this.shell.settingsViews.delete(this);
		for (const entry of this.mounted.values()) if (entry.instance && entry.instance.dispose) entry.instance.dispose();
	}
}

// The global plugins with settings pages of their own, for the settings without a workspace: their contributions as the
// server lists them for no workspace, each UI module imported but not activated - activating it waits for a workspace,
// whose window imports the same module again and activates it then.
async function globalPlugins(shell) {
	const answer = await shell.client.request("plugins.contributions", {});
	const plugins = answer.plugins.filter((p) => p.ui && ((p.contributes || {}).settingsPages || []).length)
		.map((p) => ({ id: p.id, source: p.source, contributes: p.contributes, ui: p.ui, base: "/plugins/" + encodeURIComponent(p.id) + "/", module: null }));
	await Promise.all(plugins.map(async (plugin) => {
		try {
			plugin.module = await import(plugin.ui);
		}
		catch (e) {
			shell.report("Plugin '" + plugin.id + "': its settings pages did not load: " + e.message, e);
		}
	}));
	return plugins;
}

// A refused write as the row shows it: the server's reason and the schema's problems ("/: must be at least 1").
export function refusal(error) {
	const problems = error.data && Array.isArray(error.data.problems) ? error.data.problems : [];
	return problems.length ? error.message + ": " + problems.join("; ") : error.message;
}

// The first page that shows something: a page with settings, else its first child.
function firstPage(pages) {
	if (!pages.length) return null;
	return pages[0].keys.length || !pages[0].children.length ? pages[0].id : pages[0].children[0].id;
}

function shortFile(path) {
	const parts = (path || "").split(/[\\/]/);
	return parts.slice(-2).join("/");
}
