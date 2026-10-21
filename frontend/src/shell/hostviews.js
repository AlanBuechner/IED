// hostviews.js - the shell's own tabs ("shell:<name>"): Keyboard Shortcuts (the full table from the registries, docs/15),
// Plugins (the plugin host's state, docs/02), Settings (settingsview.js, docs/07) and About.

import { h, icon, iconFrom, replace } from "/ui-kit/dom.js";
import { formatBinding } from "./keys.js";
import { settingsView } from "./settingsview.js";

const VIEWS = {
	keys: { title: "Keyboard Shortcuts", icon: "$(record-keys)", view: keysView },
	plugins: { title: "Plugins", icon: "$(extensions)", view: pluginsView },
	settings: { title: "Settings", icon: "$(settings-gear)", view: (shell, ctx) => settingsView(shell, ctx) },
	about: { title: "About IED", icon: "$(info)", view: aboutView },
};

export function registerShellViews(shell) {
	shell.views.registerKind("shell", {
		view: (ctx) => {
			const spec = VIEWS[ctx.ref.slice("shell:".length)];
			if (!spec) throw new Error("there is no shell view '" + ctx.ref + "'");
			return spec.view(shell, ctx);
		},
		title: (name) => (VIEWS[name] ? VIEWS[name].title : name),
		icon: (name) => (VIEWS[name] ? VIEWS[name].icon : null),
	});
}

// ---------------------------------------------------------------------------------------------------------------
// Keyboard Shortcuts

function bindingRows(shell) {
	const rows = shell.keybindings.all().map((b) => ({
		command: commandLabel(shell, b.command, b.args),
		keys: b.label, when: b.when, source: b.source, appWindowOnly: b.appWindowOnly, conflict: false, chord: b.keys.join(" "),
	}));
	const byKey = new Map();
	for (const row of rows) {
		const key = row.chord + "\u0000" + row.when;
		byKey.set(key, (byKey.get(key) || 0) + 1);
	}
	for (const row of rows) row.conflict = byKey.get(row.chord + "\u0000" + row.when) > 1;
	return rows.sort((a, b) => a.command.localeCompare(b.command));
}

// "View: Solution Explorer" for shell.showView {id}, else the command's label with its arguments.
function commandLabel(shell, command, args) {
	const id = args && args.id;
	const tool = command === "shell.showView" && id ? shell.contributions.toolWindow(id) : null;
	const activity = command === "shell.showActivity" && id ? shell.contributions.activity(id) : null;
	if (tool || activity) return "View: " + (tool || activity).title;
	return shell.commands.label(command) + (args === undefined ? "" : " " + JSON.stringify(args));
}

function keysView(shell, ctx) {
	const filter = h("input.input-field", { type: "search", placeholder: "Search keyboard shortcuts", "aria-label": "Search keyboard shortcuts" });
	const body = h("tbody");
	const note = h("div.note", null, "Keys the browser keeps (Ctrl+W, Ctrl+T, Ctrl+N, Ctrl+Tab...) work only in an installed app window. When two bindings share a key and a when-clause, the later one wins.");

	function render() {
		const words = filter.value.toLowerCase().split(/\s+/).filter(Boolean);
		const rows = bindingRows(shell).filter((row) => words.every((w) => (row.command + " " + row.keys + " " + row.source).toLowerCase().includes(w)));
		replace(body, ...rows.map((row) => h("tr", null,
			h("td", null, row.command),
			h("td.mono", null, row.keys, row.appWindowOnly ? h("span.chip.warn", { title: "The browser keeps this key in a tab" }, "app window only") : null, row.conflict ? h("span.chip.err", { title: "Another binding has this key and when-clause" }, "conflict") : null),
			h("td.mono.muted", null, row.when),
			h("td.muted", null, row.source))));
	}

	filter.addEventListener("input", render);
	const stop = shell.commands.onChange(render);
	ctx.el.append(h("div.page", null, h("div.page-head", null, h("h1", null, "Keyboard Shortcuts"), filter), note,
		h("table.grid", null, h("thead", null, h("tr", null, h("th", null, "Command"), h("th", null, "Keys"), h("th", null, "When"), h("th", null, "Source"))), body)));
	render();
	return { dispose: stop, focus: () => filter.focus() };
}

// ---------------------------------------------------------------------------------------------------------------
// Plugins

const STATE_CHIPS = { loaded: "ok", disabled: "", failed: "err", needsApproval: "warn", overridden: "", untrusted: "warn" };

function pluginsView(shell, ctx) {
	const list = h("div.plugin-list");
	let plugins = [];

	function act(method, params) {
		shell.client.request(method, params).catch((e) => shell.report(method + ": " + e.message, e));
	}

	function card(p) {
		const canToggle = p.state === "loaded" || p.state === "disabled";
		return h("div.plugin-card", { dataset: { plugin: p.id } },
			h("div.plugin-head", null,
				h("b", null, p.name || p.id), h("span.muted", null, p.id + " " + p.version),
				h("span.chip" + (STATE_CHIPS[p.state] ? "." + STATE_CHIPS[p.state] : ""), null, p.state),
				h("span.chip", null, p.source), h("span.chip", null, p.kind),
				h("span.grow"),
				p.state === "needsApproval" ? h("button.btn", { on: { click: () => act("plugins.approve", { id: p.id }) } }, "Approve") : null,
				canToggle ? h("button.btn", { on: { click: () => act("plugins.setEnabled", { id: p.id, enabled: p.state === "disabled" }) } }, p.state === "disabled" ? "Enable" : "Disable") : null),
			p.overriddenBy ? h("div.muted", null, "Overridden by the " + p.overriddenBy + " plugin of this id.") : null,
			p.problems.length ? h("ul.problems", null, p.problems.map((problem) => h("li.c-error", null, problem))) : null,
			p.contributes.length ? h("div.muted", null, "Contributes: " + p.contributes.join(", ")) : null,
			p.permissions.length ? h("div.muted", null, "Permissions: " + p.permissions.join(", ")) : null);
	}

	function render() {
		replace(list, ...plugins.map(card));
		if (!plugins.length) list.append(h("div.muted", null, "No plugins are installed."));
	}

	const stop = shell.client.subscribe("plugins", {
		snap: (data) => {
			plugins = data.plugins;
			render();
		},
		evt: (kind, data) => {
			if (kind !== "plugins.changed") return;
			plugins = data.plugins;
			render();
		},
	});
	ctx.el.append(h("div.page", null,
		h("div.page-head", null, h("h1", null, "Plugins"), h("button.btn", { on: { click: () => act("plugins.reload", {}) } }, icon("refresh"), "Reload")),
		list));
	return { dispose: stop };
}

// ---------------------------------------------------------------------------------------------------------------
// About

function aboutView(shell, ctx) {
	const facts = h("dl.facts");
	function fact(term, value) {
		facts.append(h("dt", null, term), h("dd.mono", null, value));
	}
	ctx.el.append(h("div.page", null, h("div.page-head", null, iconFrom("$(code)", "/", "lg"), h("h1", null, "IED")),
		h("p", null, "An IDE served by a server you own: chats, builds and editors live on the server; this page is one view of them."),
		facts));
	shell.client.request("server.info", {}).then((info) => {
		fact("Server", info.serverVersion + (info.mock ? " (mock)" : ""));
		fact("Protocol", String(info.proto));
		fact("Session epoch", info.sessionEpoch);
		fact("This tab", shell.client.clientId);
		fact("This device", shell.identity.deviceId + " (" + shell.deviceClass + ")");
		fact("Keys", formatBinding(["ctrl+shift+p"]) + " opens the command palette");
	}, (e) => shell.report("server.info: " + e.message, e));
	return null;
}
