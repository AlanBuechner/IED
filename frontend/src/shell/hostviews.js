// hostviews.js - the shell's own tabs ("shell:<name>"): Plugins (the plugin host's state, docs/02), Settings
// (settingsview.js, docs/07; Keyboard Shortcuts is its Environment > Keyboard page) and About.

import { h, icon, iconFrom, replace } from "/ui-kit/dom.js";
import { formatBinding } from "./keys.js";
import { settingsView } from "./settingsview.js";

const VIEWS = {
	keys: { title: "Keyboard Shortcuts", icon: "$(record-keys)", view: (shell, ctx) => {
		shell.nextSettingsPage = "custom:shell.keyboard";
		return settingsView(shell, ctx);
	} },
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
// Plugins

const STATE_CHIPS = { loaded: "ok", disabled: "", failed: "err", needsApproval: "warn", overridden: "", untrusted: "warn" };
const CODE_CHIPS = { active: "ok", compiling: "info", idle: "", failed: "err" };

// A c4 plugin's code (docs/26 section 2): its state, and its diagnostics, its fault or a failed new build.
function codeRow(code) {
	if (!code) return null;
	return h("div.plugin-code", null,
		h("span.chip" + (CODE_CHIPS[code.state] ? "." + CODE_CHIPS[code.state] : ""), { title: "Built " + code.builds + " time(s)" + (code.cached ? "; this build was linked from the compiled-script cache" : "") }, "code " + code.state),
		code.problem ? h("pre.plugin-code-problem" + (code.state === "failed" ? ".c-error" : ".c-warn"), null, code.problem) : null);
}

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
			codeRow(p.code),
			p.contributes.length ? h("div.muted", null, "Contributes: " + p.contributes.join(", ")) : null,
			p.permissions.length ? h("div.muted", null, "Permissions: " + p.permissions.join(", ")) : null);
	}

	// Rendered again on every change (a plugin's code compiling, starting, failing): where the page was scrolled stays.
	function render() {
		const page = list.closest(".page");
		const top = page ? page.scrollTop : 0;
		replace(list, ...plugins.map(card));
		if (!plugins.length) list.append(h("div.muted", null, "No plugins are installed."));
		if (page) page.scrollTop = top;
	}

	// The global plugins and the workspace's own (.ied/plugins/ and the solution's pins, docs/26 section 7).
	function refresh() {
		shell.client.request("plugins.list", ctx.wsId ? { wsId: ctx.wsId } : {})
			.then((data) => {
				plugins = data.plugins;
				render();
			})
			.catch((e) => shell.report("plugins.list: " + e.message, e));
	}

	const changed = { snap: refresh, evt: (kind) => kind === "plugins.changed" && refresh() };
	const stops = [shell.client.subscribe("plugins", changed)];
	if (ctx.wsId) stops.push(shell.client.subscribe("ws:" + ctx.wsId, { evt: changed.evt }));
	ctx.el.append(h("div.page", null,
		h("div.page-head", null, h("h1", null, "Plugins"), h("button.btn", { on: { click: () => act("plugins.reload", {}) } }, icon("refresh"), "Reload")),
		list));
	return { dispose: () => stops.forEach((stop) => stop()) };
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
