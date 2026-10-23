// newproject.js - New Project (VS "Create a new project", docs/15 "New Project"): the plugins' project templates
// (project.templates, docs/03 "New projects") with a search and a language filter, then the project's name, the folder
// it goes in and whether it is a new solution or joins the open one; Create runs project.create on the server and shows
// the new workspace, or says which solution the project joined. The templates come from the server, so the start page
// offers them too. A sheet on a phone.

import { h, icon, iconFrom, replace } from "/ui-kit/dom.js";
import { projectIconSpec } from "/ui-kit/fileglyphs.js";

const NAME = /^[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}$/;
const LOCATION_KEY = "ied.newProject.location";

// The templates whose title, description, language name and tags hold every word of `text`, of `language` ("": any).
export function filterTemplates(templates, text, language) {
	const words = String(text || "").toLowerCase().split(/\s+/).filter(Boolean);
	return templates.filter((t) => {
		if (language && t.language !== language) return false;
		const said = [t.title, t.description, t.languageName, ...(t.tags || [])].join(" ").toLowerCase();
		return words.every((word) => said.includes(word));
	});
}

// The languages the filter offers, [{id, name}] in the order the templates name them.
export function templateLanguages(templates) {
	const seen = new Map();
	for (const t of templates) if (t.language && !seen.has(t.language)) seen.set(t.language, t.languageName || t.language);
	return [...seen].map(([id, name]) => ({ id, name }));
}

// Why `name` cannot name a project, or "".
export function nameProblem(name) {
	if (!name) return "Name the project";
	return NAME.test(name) ? "" : "A project's name is letters, digits, '_', '.' and '-', starting with a letter, a digit or '_'";
}

// The folder the project goes in: `location`/`name`, with '/' separators.
export function projectFolder(location, name) {
	const folder = String(location || "").replace(/\\/g, "/").replace(/\/+$/, "");
	return folder && name ? folder + "/" + name : "";
}

function remembered() {
	try {
		return localStorage.getItem(LOCATION_KEY) || "";
	}
	catch (e) {
		return "";
	}
}

function remember(location) {
	try {
		localStorage.setItem(LOCATION_KEY, location);
	}
	catch (e) {
		// a private window keeps nothing
	}
}

// Opens the dialog; `options.addTo`: start with "Add to solution" chosen when the open workspace can take the project.
export async function showNewProject(shell, options) {
	const opts = options || {};
	let listed;
	try {
		listed = await shell.client.request("project.templates", shell.wsId ? { wsId: shell.wsId } : {});
	}
	catch (e) {
		shell.toasts.show("The project templates could not be read: " + e.message, "error");
		return;
	}
	new NewProjectDialog(shell, listed, opts).open();
}

class NewProjectDialog {
	constructor(shell, listed, options) {
		this.shell = shell;
		this.templates = listed.templates || [];
		this.icons = listed.icons || [];
		this.location = remembered() || listed.location || "";
		this.addTo = Boolean(options.addTo);
		this.chosen = null;
		this.selected = 0;
		this.creating = false;
		this.previous = document.activeElement;
		this.phone = window.matchMedia("(max-width: 719px)").matches;
		this.box = h("div.popup.dialog.new-project" + (this.phone ? ".sheet" : ""), { role: "dialog", "aria-modal": "true", "aria-label": "Create a new project", on: { keydown: (event) => this.key(event) } });
		this.veil = h("div.dialog-veil", { on: { mousedown: (event) => event.target === this.veil && !this.creating && this.close() } }, this.box);
	}

	open() {
		document.body.append(this.veil);
		this.pickPage();
	}

	close() {
		this.veil.remove();
		if (this.previous && this.previous.isConnected) this.previous.focus({ preventScroll: true });
	}

	key(event) {
		event.stopPropagation();
		if (event.key === "Escape" && !this.creating) {
			event.preventDefault();
			if (this.chosen) this.pickPage();
			else this.close();
		}
	}

	workspace() {
		return this.shell.wsId ? this.shell.workspace : null;
	}

	tile(template) {
		return iconFrom(projectIconSpec({ data: { language: template.language } }, this.icons), null, "np-tile");
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Choosing a template

	pickPage() {
		this.chosen = null;
		const languages = templateLanguages(this.templates);
		this.search = h("input.input-field.np-search", { type: "search", placeholder: "Search for templates", "aria-label": "Search for templates", spellcheck: "false", on: { input: () => this.drawList(), keydown: (event) => this.listKey(event) } });
		this.language = h("select.input-field.np-language", { "aria-label": "Language", on: { change: () => this.drawList() } },
			h("option", { value: "" }, "All languages"), ...languages.map((l) => h("option", { value: l.id }, l.name)));
		this.list = h("div.np-list", { role: "listbox", "aria-label": "Project templates" });
		this.next = h("button.btn.primary", { on: { click: () => this.choose() } }, "Next");
		replace(this.box,
			h("div.dialog-title", null, "Create a new project"),
			h("div.np-filters", null, this.search, this.language),
			this.list,
			h("div.dialog-buttons", null, h("button.btn", { on: { click: () => this.close() } }, "Cancel"), this.next));
		this.drawList();
		this.search.focus();
	}

	shown() {
		return filterTemplates(this.templates, this.search.value, this.language.value);
	}

	drawList() {
		const shown = this.shown();
		this.selected = Math.min(this.selected, Math.max(0, shown.length - 1));
		this.next.disabled = !shown.length;
		if (!this.templates.length) {
			replace(this.list, h("div.np-empty.muted", null, "No plugin offers a project template. Language plugins add theirs: C/C++, C4 and Go do."));
			return;
		}
		if (!shown.length) {
			replace(this.list, h("div.np-empty.muted", null, "No template matches."));
			return;
		}
		replace(this.list, ...shown.map((template, i) => h("div.np-row" + (i === this.selected ? ".sel" : ""), {
			role: "option", "aria-selected": i === this.selected ? "true" : "false", dataset: { template: template.key },
			on: { click: () => this.select(i), dblclick: () => this.choose() },
		}, this.tile(template),
			h("div.np-text", null,
				h("div.np-title", null, template.title),
				template.description ? h("div.np-description", null, template.description) : null,
				h("div.np-chips", null, template.languageName ? h("span.chip", null, template.languageName) : null, ...(template.tags || []).map((tag) => h("span.chip", null, tag)))))));
	}

	select(index) {
		this.selected = index;
		this.drawList();
	}

	listKey(event) {
		const shown = this.shown();
		if (event.key === "ArrowDown" || event.key === "ArrowUp") {
			event.preventDefault();
			this.select(Math.max(0, Math.min(shown.length - 1, this.selected + (event.key === "ArrowDown" ? 1 : -1))));
			const row = this.list.children[this.selected];
			if (row) row.scrollIntoView({ block: "nearest" });
		}
		else if (event.key === "Enter") {
			event.preventDefault();
			this.choose();
		}
	}

	choose() {
		const template = this.shown()[this.selected];
		if (template) this.configurePage(template);
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Configuring the project

	configurePage(template) {
		this.chosen = template;
		const ws = this.workspace();
		const canAdd = Boolean(ws && template.canAdd);
		this.name = h("input.input-field", { type: "text", value: this.suggestedName(template), "aria-label": "Project name", spellcheck: "false", autocomplete: "off", on: { input: () => this.check(), keydown: (event) => event.key === "Enter" && this.create() } });
		this.place = h("input.input-field.mono", { type: "text", value: this.location, "aria-label": "Location", spellcheck: "false", autocomplete: "off", on: { input: () => this.check(), keydown: (event) => event.key === "Enter" && this.create() } });
		this.solution = h("select.input-field", { "aria-label": "Solution", on: { change: () => this.check() } },
			h("option", { value: "new" }, "Create new solution"),
			canAdd ? h("option", { value: "add", selected: this.addTo ? "" : null }, "Add to solution '" + ws.name + "'") : null);
		this.where = h("div.np-where.muted.mono");
		this.problem = h("div.dialog-problem.c-error.np-problem");
		this.back = h("button.btn", { on: { click: () => !this.creating && this.pickPage() } }, "Back");
		this.make = h("button.btn.primary", { on: { click: () => this.create() } }, "Create");
		replace(this.box,
			h("div.dialog-title", null, "Configure your new project"),
			h("div.np-chosen", null, this.tile(template), h("b", null, template.title), template.languageName ? h("span.chip", null, template.languageName) : null, ...(template.tags || []).map((tag) => h("span.chip", null, tag))),
			h("label.np-field", null, h("span", null, "Project name"), this.name),
			h("label.np-field", null, h("span", null, "Solution"), this.solution),
			h("label.np-field", null, h("span", null, "Location"), this.place),
			this.where, this.problem,
			h("div.dialog-buttons", null, this.back, this.make));
		this.check();
		this.name.focus();
		this.name.select();
	}

	// The first free name of the template's kind: ConsoleApp1, ConsoleApp2...
	suggestedName(template) {
		const stem = template.title.replace(/[^A-Za-z0-9]/g, "") || "Project";
		const ws = this.workspace();
		const taken = new Set(((ws && ws.solution && ws.solution.projects) || []).map((p) => p.name.toLowerCase()));
		for (let n = 1; ; n++) if (!taken.has((stem + n).toLowerCase())) return stem + n;
	}

	adding() {
		return this.solution.value === "add";
	}

	check() {
		const adding = this.adding();
		const ws = this.workspace();
		this.place.disabled = adding || this.creating;
		if (adding) this.place.value = ws ? String(ws.root).replace(/\\/g, "/") : "";
		else if (this.place.value === String(ws ? ws.root : "").replace(/\\/g, "/")) this.place.value = this.location;
		const name = this.name.value.trim();
		const why = nameProblem(name) || (!adding && !/^([A-Za-z]:[\\/]|\/|\\\\)/.test(this.place.value.trim()) ? "The location is a full path of the computer IED runs on" : "");
		const folder = projectFolder(this.place.value.trim(), name);
		this.where.textContent = folder ? (adding ? "The project goes in " : "The new solution and its project go in ") + folder : "";
		this.problem.textContent = why;
		this.make.disabled = Boolean(why) || this.creating;
		return !why;
	}

	async create() {
		if (this.creating || !this.check()) return;
		const adding = this.adding();
		const params = { template: this.chosen.key, name: this.name.value.trim() };
		if (adding) params.wsId = this.shell.wsId;
		else params.location = this.place.value.trim();
		this.creating = true;
		this.name.disabled = this.solution.disabled = this.place.disabled = this.back.disabled = this.make.disabled = true;
		replace(this.make, icon("loading", "spin"), "Creating");
		let made;
		try {
			made = await this.shell.client.request("project.create", params);
		}
		catch (e) {
			this.creating = false;
			this.name.disabled = this.solution.disabled = this.back.disabled = false;
			replace(this.make, "Create");
			this.check();
			this.problem.textContent = e.message;
			return;
		}
		if (!adding) remember(params.location);
		this.close();
		if (adding) this.shell.toasts.show("Added the project " + params.name + " to the solution: " + made.path, "info");
		else {
			this.shell.toasts.show("Created " + params.name + " in " + made.path, "info");
			await this.shell.showWorkspace(made.workspace.wsId);
		}
	}
}
