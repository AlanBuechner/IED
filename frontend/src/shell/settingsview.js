// settingsview.js - the settings window (docs/07 "The settings window", mockup 04): Visual Studio's Tools > Options as
// a tab - a tree of pages, the tier being edited (User, Solution, Project), search, and a row per setting with its
// effective value, the tier that set it, a control from its schema and Reset; a plugin's settingsPages draw their own
// editor under their page. Values come from settings:<wsId> (a project's from settings.get), writes are settings.set and
// settings.reset (docs/07 "Settings on the wire"); the server checks every value against its schema.

import { h, icon, replace } from "/ui-kit/dom.js";
import { TIERS, controlKind, findPage, inputText, matches, parseInput, problemsFor, settingTitle, settingsPages, sourceLabel } from "./settingsmodel.js";
import { KeyboardPage } from "./keyboardpage.js";

export function settingsView(shell, ctx) {
	return new SettingsView(shell, ctx);
}

// The shell's own pages, drawn as a plugin's settingsPages are: Environment > Keyboard edits keyboard.bindings.
function shellPages(shell) {
	const plugin = { id: "shell", base: "/", module: { KeyboardSettings: (ctx) => new KeyboardPage(shell, ctx) } };
	return [{ plugin, value: { id: "shell.keyboard", page: "Environment", title: "Keyboard", view: "KeyboardSettings", keys: ["keyboard.bindings"] } }];
}

class SettingsView {
	constructor(shell, ctx) {
		this.shell = shell;
		this.ctx = ctx;
		this.schema = {};
		this.state = { tiers: {}, projects: {}, values: {} };
		this.projectValues = null;
		this.tier = "app";
		this.project = null;
		this.pageId = shell.nextSettingsPage || null;
		shell.nextSettingsPage = null;
		shell.settingsViews.add(this);
		this.query = "";
		this.loaded = false;
		this.mounted = new Map();
		this.listeners = new Set();
		this.searchField = h("input.input-field.set-search", { placeholder: "Search settings", "aria-label": "Search settings", spellcheck: "false" });
		this.searchField.addEventListener("input", () => {
			this.query = this.searchField.value;
			this.renderPage();
		});
		this.tiersEl = h("span.set-seg", { role: "radiogroup", "aria-label": "Editing tier" });
		this.where = h("span.muted.set-where");
		this.tree = h("nav.set-tree", { "aria-label": "Settings pages" });
		this.page = h("div.set-page");
		this.el = h("div.settings-window", null,
			h("div.set-top", null, this.searchField, h("span.muted", null, "Editing tier"), this.tiersEl, this.where),
			h("div.set-body", null, this.tree, this.page));
		ctx.el.append(this.el);
		ctx.el.addEventListener("keydown", (event) => {
			if (event.ctrlKey && (event.key === "e" || event.key === "E")) {
				event.preventDefault();
				this.searchField.focus();
			}
		});
		this.stop = shell.client.subscribe("settings:" + ctx.wsId, {
			snap: (data) => this.received(data),
			evt: (kind, data) => kind === "settings.changed" && this.received(data),
		});
		this.loadSchema();
	}

	async loadSchema() {
		try {
			this.schema = (await this.shell.client.request("settings.schema", { wsId: this.ctx.wsId })).settings || {};
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
		const contributed = this.shell.contributions.plugins.flatMap((p) => (p.contributes.settingsPages || []).map((value) => ({ plugin: p, value })));
		return settingsPages(this.schema, shellPages(this.shell).concat(contributed));
	}

	// Shows the page `id` ("custom:shell.keyboard", "page:AI").
	showPage(id) {
		this.pageId = id;
		this.render();
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
		const row = (node, depth) => h("button.set-node" + (node.id === this.pageId ? ".sel" : ""), {
			style: { "--d": String(depth) }, dataset: { page: node.id },
			on: { click: () => {
				this.pageId = node.id;
				this.query = "";
				this.searchField.value = "";
				this.render();
			} },
		}, h("span.name", null, node.title), this.problemCount(node.keys) ? h("span.meta", null, icon("warning", "c-warn")) : null);
		replace(this.tree, ...pages.flatMap((p) => [row(p, 0), ...p.children.map((c) => row(c, 1))]));
	}

	problemCount(keys) {
		return keys.reduce((n, key) => n + problemsFor(this.state.tiers, key).length, 0);
	}

	renderPage() {
		const pages = this.pages();
		if (this.query.trim()) {
			const found = Object.keys(this.schema).sort().filter((key) => matches(key, this.schema[key], this.query));
			replace(this.page, h("div.set-crumbs", null, found.length + (found.length === 1 ? " setting matches" : " settings match")), ...found.map((key) => this.row(key)));
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
		replace(this.page, h("div.set-crumbs", null, parent ? parent.title + " \u203a " + node.title : node.title), h("h1", null, node.title),
			...node.keys.map((key) => this.row(key)), node.children.length && !node.keys.length ? h("p.muted", null, "Choose a page under " + node.title + ".") : null);
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
		return h("div.set-row", { dataset: { key } },
			h("div.set-label", null, h("span.set-title", null, settingTitle(key)), h("span.mono.muted", null, key)),
			entry.schema && entry.schema.description ? h("div.set-desc", null, entry.schema.description) : null,
			h("div.set-edit", null, control,
				h("span.set-source" + (current.source !== "default" ? ".set" : ""), { title: "The tier that sets the value in effect" }, sourceLabel(current.source)),
				editingSet && allowed ? h("button.btn.flat", { title: "Remove it from the " + sourceLabel(this.tier) + " tier", on: { click: () => this.clear(key) } }, icon("discard"), "Reset") : null,
				allowed ? null : h("span.muted", null, "set in: " + (entry.tiers || []).map(sourceLabel).join(", "))),
			message,
			...problems.map((p) => h("div.set-message.c-error", null, sourceLabel(p.tier) + " file, line " + p.line + ": " + p.message)));
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
		const apply = () => {
			if (field.value === inputText(kind, value)) return;
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
		const params = { tier: tier || this.tier, wsId: this.ctx.wsId, key, value };
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
		const params = { tier: tier || this.tier, wsId: this.ctx.wsId, key };
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
