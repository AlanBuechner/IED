// config.js - the config server's own page (docs/35 "The web page"): sign in once per browser with an admin token (or a
// one-time link), then see and manage what the server shares - its settings (where each comes from, settings.json
// edited in place), the server plugins with their code and what they published, the client plugins and skills with
// their files, and the tokens (made, shown once, revoked; the IEDs that follow). It follows the state with the waiting
// state request, so a deploy or a server plugin's publish shows as it happens. The server holds everything: this page
// keeps nothing but which section is open.

import { h, replace } from "/config/kit/dom.js";
import { ago, codeLook, compact, following, settingRows, short, tokenNameProblem, tokenRows } from "/config/configmodel.js";

const SECTIONS = [
	["overview", "Overview", "home"],
	["settings", "Settings", "settings"],
	["server-plugins", "Server plugins", "server-process"],
	["plugins", "Plugins", "extensions"],
	["skills", "Skills", "book"],
	["tokens", "Tokens", "key"],
];
const SECTION_KEY = "ied.config.section";

const app = document.getElementById("app");
const model = {
	me: null,            // whoami
	state: null,         // the served state
	serverPlugins: null, // {plugins, settingsProblem}
	settingsText: null,  // settings.json as written
	tokens: null,        // {tokens, you}
	made: null,          // a token just made: {name, role, token}
	link: null,          // a sign-in link just made: {url, expiresMs}
	files: {},           // "<kind>/<id>": [{path, size, hash}] once asked for
	skillText: {},       // a skill's SKILL.md once asked for
	open: new Set(),     // the rows shown open: "<kind>/<id>"
	error: "",
};
let section = stored(SECTION_KEY) || "overview";
let settingsDraft = null;   // the settings editor's text while it differs from the file
let settingsMessage = { text: "", accent: "" };
let pollVersion = "";
let tokensTimer = 0;

function stored(key) {
	try {
		return localStorage.getItem(key);
	}
	catch {
		return null;
	}
}

function store(key, value) {
	try {
		localStorage.setItem(key, value);
	}
	catch {
		// a private window: the section is remembered while the page lives
	}
}

function icon(name, extra) {
	const el = document.createElement("span");
	el.className = extra ? "ci " + extra : "ci";
	el.style.setProperty("--ci", "url(/config/icons/" + encodeURIComponent(name) + ".svg)");
	el.setAttribute("aria-hidden", "true");
	return el;
}

// The config API: the browser's cookie (with the header that proves this page sent it), or a bearer token to sign in.
async function api(method, path, body, bearer) {
	const headers = { "X-IED-Config": "1" };
	if (bearer) headers.Authorization = "Bearer " + bearer;
	if (body !== undefined) headers["Content-Type"] = typeof body === "string" ? "text/plain" : "application/json";
	const answer = await fetch("/config/v1/" + path, { method, headers, credentials: "same-origin", body: body === undefined ? undefined : typeof body === "string" ? body : JSON.stringify(body) });
	const text = await answer.text();
	let data = null;
	try {
		data = text ? JSON.parse(text) : null;
	}
	catch {
		data = text;
	}
	if (!answer.ok) {
		const problems = data && Array.isArray(data.problems) ? ": " + data.problems.join("; ") : "";
		const error = new Error(((data && data.message) || answer.status + " " + answer.statusText) + problems);
		error.status = answer.status;
		throw error;
	}
	return data;
}

// --- Signing in ------------------------------------------------------------------------------------------------------

async function start() {
	try {
		model.me = await api("GET", "whoami");
	}
	catch (e) {
		if (e.status === 401 || e.status === 403) return signIn();
		return failed(e);
	}
	if (model.me.role !== "admin") return signIn("This browser's token reads only: sign in with an admin token to manage the server.");
	await reloadAll();
	follow();
	tokensTimer = setInterval(() => reloadTokens().then(render), 15000);
}

function signIn(note) {
	app.removeAttribute("aria-busy");
	const expired = location.hash === "#link-expired";
	const field = h("input.input-field.cfg-token", { type: "password", id: "cfg-admin", autocomplete: "off", spellcheck: "false", placeholder: "Admin token", "aria-label": "Admin token" });
	const message = h("div.cfg-message.c-error", { role: "alert", hidden: !expired && !note }, expired ? "That sign-in link has expired or was used already." : note || "");
	const button = h("button.btn.primary", { type: "submit" }, "Sign in");
	const form = h("form.cfg-signin", { on: { submit: async (event) => {
		event.preventDefault();
		const token = field.value.trim();
		if (!token) return;
		button.disabled = true;
		message.hidden = true;
		try {
			await api("POST", "session", {}, token);
			field.value = "";
			history.replaceState(null, "", "/config/");
			app.setAttribute("aria-busy", "true");
			await start();
		}
		catch (e) {
			message.textContent = e.status === 401 ? "The server does not know that token." : e.status === 403 ? "That token reads only: sign in with an admin token." : e.message;
			message.hidden = false;
			button.disabled = false;
		}
	} } },
		h("h1", null, "IED Config Server"),
		h("p.muted", null, "Sign in with an admin token once: this browser then gets a token of its own, kept in a cookie for a year and listed under Tokens, where it can be revoked."),
		h("label.cfg-label", { for: "cfg-admin" }, "Admin token"), field, message, h("div.cfg-actions", null, button),
		h("p.muted.cfg-small", null, "No token at hand? A browser that is signed in makes a one-time sign-in link under Tokens; ", h("code", null, "deploy.py signin --open"), " makes one with this machine's admin token."));
	replace(app, h("main.cfg-signin-page", null, form));
	field.focus();
}

function failed(e) {
	app.removeAttribute("aria-busy");
	replace(app, h("main.cfg-signin-page", null, h("div.cfg-signin", null, h("h1", null, "IED Config Server"), h("p.c-error", null, "The server did not answer: " + e.message), h("button.btn", { on: { click: () => location.reload() } }, "Try again"))));
}

async function signOut() {
	try {
		await api("DELETE", "session");
	}
	catch {
		// gone already: the sign-in shows either way
	}
	clearInterval(tokensTimer);
	model.me = null;
	signIn();
}

// --- Loading ---------------------------------------------------------------------------------------------------------

async function reloadAll() {
	try {
		const [state, serverPlugins, settingsText, tokens] = await Promise.all([api("GET", "state"), api("GET", "server-plugins"), api("GET", "settings"), api("GET", "tokens")]);
		Object.assign(model, { state, serverPlugins, settingsText: typeof settingsText === "string" ? settingsText : JSON.stringify(settingsText, null, "\t"), tokens, error: "" });
		pollVersion = state.version;
	}
	catch (e) {
		if (e.status === 401) return signIn("This browser's token was revoked: sign in again.");
		model.error = e.message;
	}
	render();
}

async function reloadTokens() {
	try {
		model.tokens = await api("GET", "tokens");
	}
	catch (e) {
		if (e.status === 401) {
			clearInterval(tokensTimer);
			signIn("This browser's token was revoked: sign in again.");
		}
	}
}

// The waiting state request, again and again: a new version reloads what the page shows.
async function follow() {
	while (model.me) {
		try {
			const state = await api("GET", "state?since=" + encodeURIComponent(pollVersion) + "&wait=25");
			if (state.version !== pollVersion) {
				pollVersion = state.version;
				const [serverPlugins, settingsText] = await Promise.all([api("GET", "server-plugins"), api("GET", "settings")]);
				Object.assign(model, { state, serverPlugins, settingsText: typeof settingsText === "string" ? settingsText : JSON.stringify(settingsText, null, "\t"), error: "" });
				model.files = {};
				render();
			}
		}
		catch (e) {
			if (e.status === 401) {
				model.me = null;
				return signIn("This browser's token was revoked: sign in again.");
			}
			model.error = "The server did not answer (" + e.message + "); trying again.";
			render();
			await new Promise((resolve) => setTimeout(resolve, 5000));
		}
	}
}

// --- The page --------------------------------------------------------------------------------------------------------

function render() {
	if (!model.me || !model.state) return;
	app.removeAttribute("aria-busy");
	const focused = document.activeElement && document.activeElement.id;
	const scroll = document.querySelector(".cfg-main") ? document.querySelector(".cfg-main").scrollTop : 0;
	const nav = h("nav.cfg-nav", { "aria-label": "Sections" }, ...SECTIONS.map(([id, title, glyph]) => h("button.cfg-tab" + (id === section ? ".sel" : ""), {
		dataset: { section: id }, "aria-current": id === section ? "page" : null,
		on: { click: () => {
			section = id;
			store(SECTION_KEY, id);
			render();
		} },
	}, icon(glyph), h("span", null, title), count(id))));
	const head = h("header.cfg-head", null,
		h("span.cfg-title", null, "IED Config Server"),
		h("span.muted.mono.cfg-host", null, location.host),
		h("span.grow"),
		h("span.muted.cfg-who", { title: "This browser's token" }, icon("account"), model.me.name),
		h("button.btn.flat", { on: { click: signOut } }, icon("sign-out"), "Sign out"));
	const main = h("main.cfg-main", null, model.error ? h("div.cfg-banner.c-error", { role: "alert" }, model.error) : null, view());
	replace(app, head, h("div.cfg-body", null, nav, main));
	main.scrollTop = scroll;
	// At phone width the sections are a row that scrolls: the open one in view.
	const selected = nav.querySelector(".sel");
	if (selected && nav.scrollWidth > nav.clientWidth) nav.scrollLeft = Math.max(0, selected.offsetLeft - (nav.clientWidth - selected.offsetWidth) / 2);
	if (focused) {
		const el = document.getElementById(focused);
		if (el) el.focus();
	}
}

function count(id) {
	const s = model.state;
	const n = id === "settings" ? Object.keys(s.settings || {}).length : id === "server-plugins" ? (model.serverPlugins.plugins || []).length
		: id === "plugins" ? (s.plugins || []).length : id === "skills" ? (s.skills || []).length : id === "tokens" ? (model.tokens.tokens || []).length : null;
	const trouble = id === "server-plugins" && (model.serverPlugins.plugins || []).some((p) => codeLook(p).accent === "error");
	return n === null ? null : h("span.cfg-count" + (trouble ? ".c-error" : ""), null, String(n));
}

function view() {
	if (section === "settings") return settingsView();
	if (section === "server-plugins") return serverPluginsView();
	if (section === "plugins") return packagesView("plugins", "Plugins", "Client plugins every IED that follows this server loads (in the shared scope, under the app's).", model.state.plugins || [], "id");
	if (section === "skills") return packagesView("skills", "Skills", "Skills every IED that follows this server finds (after the app's).", model.state.skills || [], "name");
	if (section === "tokens") return tokensView();
	return overviewView();
}

function heading(title, note) {
	return [h("h1", null, title), note ? h("p.muted.cfg-note", null, note) : null];
}

function field(label, value, mono) {
	return h("div.cfg-field", null, h("span.cfg-field-label", null, label), h("span" + (mono ? ".mono" : ""), null, value));
}

function overviewView() {
	const now = Date.now();
	const plugins = model.serverPlugins.plugins || [];
	const failing = plugins.filter((p) => codeLook(p).accent === "error");
	const ieds = following(model.tokens.tokens, now);
	const readers = tokenRows(model.tokens.tokens, "", now).filter((t) => t.role === "reader");
	return h("section.cfg-section", null, ...heading("Overview"),
		h("div.cfg-grid", null,
			h("div.cfg-card", null, h("h2", null, "Server"),
				field("Version", model.me.serverVersion || "-", true),
				field("Started", ago(model.me.startedMs, now)),
				field("Config folder", model.me.root, true),
				field("State", short(model.state.version), true)),
			h("div.cfg-card", null, h("h2", null, "Shares"),
				field("Settings", String(Object.keys(model.state.settings || {}).length)),
				field("Plugins", String((model.state.plugins || []).length)),
				field("Skills", String((model.state.skills || []).length)),
				field("Server plugins", failing.length ? h("span.c-error", null, plugins.length + " (" + failing.length + " failed)") : String(plugins.length))),
			h("div.cfg-card.cfg-wide", null, h("h2", null, "IEDs following it"),
				readers.length ? h("div.cfg-list", null, ...readers.map((t) => h("div.cfg-line", null,
					h("span.dot" + (t.following ? ".saved" : ""), { title: t.following ? "Following" : "Not asking now" }),
					h("span.mono", null, t.name),
					h("span.muted", null, t.following ? "following" : t.lastUsedMs ? "last asked " + ago(t.lastUsedMs, now) : "not seen since the server started"))))
					: h("p.muted", null, "No reader token yet: make one under Tokens for each IED."),
				h("p.muted.cfg-small", null, ieds.length + " of " + readers.length + " following now. An IED that follows asks every 25 s."))),
		model.serverPlugins.settingsProblem ? h("div.cfg-banner.c-error", null, "settings.json does not read: " + model.serverPlugins.settingsProblem) : null);
}

function settingsView() {
	const rows = settingRows(model.state.settings, model.settingsText, model.serverPlugins.plugins);
	const editor = h("textarea.input-field.mono.cfg-editor", { id: "cfg-settings", spellcheck: "false", "aria-label": "settings.json", rows: "14",
		on: { input: (event) => {
			settingsDraft = event.target.value === model.settingsText ? null : event.target.value;
			saveButton.disabled = settingsDraft === null;
			revertButton.disabled = settingsDraft === null;
		} } });
	editor.value = settingsDraft !== null ? settingsDraft : model.settingsText || "{}";
	const saveButton = h("button.btn.primary", { disabled: settingsDraft === null, on: { click: async () => {
		try {
			await api("PUT", "settings", editor.value);
			settingsDraft = null;
			settingsMessage = { text: "Saved: every IED that follows this server has it in a moment.", accent: "c-saved" };
			model.settingsText = await api("GET", "settings").then((t) => (typeof t === "string" ? t : JSON.stringify(t, null, "\t")));
		}
		catch (e) {
			settingsMessage = { text: e.message, accent: "c-error" };
		}
		render();
	} } }, "Save");
	const revertButton = h("button.btn", { disabled: settingsDraft === null, on: { click: () => {
		settingsDraft = null;
		settingsMessage = { text: "", accent: "" };
		render();
	} } }, "Revert");
	return h("section.cfg-section", null, ...heading("Settings", "What every IED that follows this server gets in its Shared settings tier, under its own. Server plugins publish over settings.json, key by key."),
		rows.length ? h("div.cfg-table", { role: "table", "aria-label": "Served settings" }, ...rows.map((row) => {
			const id = "setting/" + row.key;
			const open = model.open.has(id);
			return h("div.cfg-row" + (open ? ".open" : ""), { role: "row", dataset: { key: row.key } },
				h("button.cfg-row-head", { "aria-expanded": String(open), on: { click: () => toggle(id) } },
					icon(open ? "chevron-down" : "chevron-right", "sm"),
					h("span.mono.cfg-name", null, row.key),
					row.members ? h("span.cfg-members", null, row.members.length + ": ", row.members.join(", ")) : h("span.mono.muted.cfg-members", null, compact(row.value, 80)),
					h("span.cfg-sources", null, ...row.sources.map((s) => h("span.chip" + (s === "settings.json" ? "" : ".info"), { title: s === "settings.json" ? "Written in settings.json" : "Published by the server plugin " + s }, s)))),
				open ? h("pre.cfg-pre.mono", null, JSON.stringify(row.value, null, 2)) : null);
		})) : h("p.muted", null, "Nothing is served yet."),
		h("h2.cfg-sub", null, "settings.json"),
		h("p.muted.cfg-note", null, "Written by hand: a JSON object, comments allowed. A server plugin's published value for a key comes over it."),
		editor,
		h("div.cfg-actions", null, saveButton, revertButton, settingsMessage.text ? h("span." + (settingsMessage.accent || "muted"), { role: "status" }, settingsMessage.text) : null));
}

function serverPluginsView() {
	const plugins = model.serverPlugins.plugins || [];
	return h("section.cfg-section", null, ...heading("Server plugins", "C4 plugins this server runs. They are never sent to the IEDs; what they publish is. Deploy one with deploy.py server-plugin <folder> (--watch while developing)."),
		plugins.length ? h("div.cfg-table", { role: "table", "aria-label": "Server plugins" }, ...plugins.map((p) => {
			const id = "server-plugins/" + p.id;
			const open = model.open.has(id);
			const look = codeLook(p);
			const published = Object.keys(p.published || {});
			return h("div.cfg-row" + (open ? ".open" : ""), { role: "row", dataset: { id: p.id } },
				h("button.cfg-row-head", { "aria-expanded": String(open), on: { click: () => toggle(id) } },
					icon(open ? "chevron-down" : "chevron-right", "sm"),
					h("span.mono.cfg-name", null, p.id),
					h("span.muted", null, p.version),
					h("span.cfg-state", null, h("span.dot" + (look.accent ? "." + look.accent : "")), look.label + (p.code && p.code.builds ? " \u00b7 build " + p.code.builds : "")),
					h("span.cfg-members", null, published.length ? "publishes " + published.join(", ") : "publishes nothing")),
				open ? h("div.cfg-detail", null,
					(p.problems || []).length ? h("div.c-error", null, ...p.problems.map((x) => h("div", null, x))) : null,
					p.diagnostics ? [h("h3", null, "Diagnostics"), h("pre.cfg-pre.mono.c-error", null, p.diagnostics)] : null,
					h("h3", null, "Published"),
					h("pre.cfg-pre.mono", null, JSON.stringify(p.published || {}, null, 2)),
					files("server-plugins", p.id),
					h("div.cfg-actions", null, removeButton("server-plugins", p.id, "the server plugin " + p.id + " and what it published"))) : null);
		})) : h("p.muted", null, "No server plugin is deployed."));
}

function packagesView(kind, title, note, listed, idField) {
	return h("section.cfg-section", null, ...heading(title, note + " Deploy with deploy.py " + (kind === "plugins" ? "plugin" : "skill") + " <folder>."),
		listed.length ? h("div.cfg-table", { role: "table", "aria-label": title }, ...listed.map((p) => {
			const name = p[idField];
			const id = kind + "/" + name;
			const open = model.open.has(id);
			return h("div.cfg-row" + (open ? ".open" : ""), { role: "row", dataset: { id: name } },
				h("button.cfg-row-head", { "aria-expanded": String(open), on: { click: () => toggle(id, kind, name) } },
					icon(open ? "chevron-down" : "chevron-right", "sm"),
					h("span.mono.cfg-name", null, name),
					p.version ? h("span.muted", null, p.version) : null,
					h("span.mono.muted.cfg-members", { title: p.hash }, short(p.hash, 12))),
				open ? h("div.cfg-detail", null,
					kind === "skills" ? skillText(name) : null,
					files(kind, name),
					h("div.cfg-actions", null, removeButton(kind, name, (kind === "plugins" ? "the plugin " : "the skill ") + name + " from every IED that follows this server"))) : null);
		})) : h("p.muted", null, kind === "plugins" ? "No client plugin is shared." : "No skill is shared."));
}

function files(kind, id) {
	const listed = model.files[kind + "/" + id];
	if (!listed) {
		loadFiles(kind, id);
		return h("p.muted", null, "Reading its files...");
	}
	return [h("h3", null, "Files"), h("div.cfg-files", null, ...listed.map((f) => h("div.cfg-file", null, h("span.mono", null, f.path), h("span.muted", null, size(f.size)))))];
}

async function loadFiles(kind, id) {
	try {
		model.files[kind + "/" + id] = await api("GET", kind + "/" + encodeURIComponent(id) + "/files");
	}
	catch (e) {
		model.files[kind + "/" + id] = [];
		model.error = e.message;
	}
	render();
}

function skillText(name) {
	const text = model.skillText[name];
	if (text === undefined) {
		model.skillText[name] = null;
		api("GET", "skills/" + encodeURIComponent(name) + "/file/SKILL.md").then((t) => {
			model.skillText[name] = typeof t === "string" ? t : JSON.stringify(t);
			render();
		}, (e) => {
			model.skillText[name] = "(" + e.message + ")";
			render();
		});
	}
	return [h("h3", null, "SKILL.md"), h("pre.cfg-pre.cfg-prose", null, text || "Reading it...")];
}

function size(bytes) {
	if (bytes < 1024) return bytes + " B";
	if (bytes < 1048576) return (bytes / 1024).toFixed(1) + " KB";
	return (bytes / 1048576).toFixed(1) + " MB";
}

function removeButton(kind, id, what) {
	return h("button.btn.flat", { on: { click: async () => {
		if (!confirm("Remove " + what + "?")) return;
		try {
			await api("DELETE", kind + "/" + encodeURIComponent(id));
			model.open.delete(kind + "/" + id);
		}
		catch (e) {
			model.error = e.message;
			render();
		}
	} } }, icon("trash"), "Remove");
}

function toggle(id) {
	if (model.open.has(id)) model.open.delete(id);
	else model.open.add(id);
	render();
}

function tokensView() {
	const now = Date.now();
	const rows = tokenRows(model.tokens.tokens, model.tokens.you, now);
	const name = h("input.input-field.mono", { id: "cfg-new-name", placeholder: "laptop", spellcheck: "false", autocomplete: "off", "aria-label": "Token name" });
	const role = h("select.input-field", { id: "cfg-new-role", "aria-label": "Role" }, h("option", { value: "reader" }, "reader: an IED that follows"), h("option", { value: "admin" }, "admin: deploys and this page"));
	const message = h("span.c-error", { role: "alert" });
	const make = async () => {
		const problem = tokenNameProblem(name.value.trim());
		if (problem) {
			message.textContent = problem;
			return;
		}
		const taken = rows.some((t) => t.name === name.value.trim());
		if (taken && !confirm("A token named " + name.value.trim() + " exists: making it again replaces it, and the old one stops working at once. Go on?")) return;
		try {
			model.made = await api("POST", "tokens", { name: name.value.trim(), role: role.value });
			model.link = null;
			await reloadTokens();
		}
		catch (e) {
			message.textContent = e.message;
			return;
		}
		render();
	};
	return h("section.cfg-section", null, ...heading("Tokens", "A reader token lets an IED follow this server (Settings > Environment > Config Server); an admin token deploys and signs in here. Only their hashes are kept: a token is shown once, when it is made."),
		model.made ? shownOnce("Token " + model.made.name + " (" + model.made.role + ")", model.made.token, model.made.role === "reader" ? "Paste it in that IED: Settings > Environment > Config Server > Reader token, then Connect." : "Keep it where your deploy tooling reads it (IED_CONFIG_ADMIN_TOKEN).", () => {
			model.made = null;
			render();
		}) : null,
		model.link ? shownOnce("One-time sign-in link", location.origin + model.link.url, "Open it in the other browser within 10 minutes; it works once.", () => {
			model.link = null;
			render();
		}) : null,
		h("div.cfg-table", { role: "table", "aria-label": "Tokens" },
			h("div.cfg-row.cfg-row-labels", { role: "row" }, h("span", null, "Name"), h("span", null, "Role"), h("span", null, "Made"), h("span", null, "Last used"), h("span")),
			...rows.map((t) => h("div.cfg-row.cfg-token-row", { role: "row", dataset: { name: t.name } },
				h("span.cfg-name", null, t.role === "reader" ? h("span.dot" + (t.following ? ".saved" : ""), { title: t.following ? "Its IED follows now" : "Not asking now" }) : null, h("span.mono", null, t.name), t.you ? h("span.chip", null, "this browser") : null),
				h("span" + (t.role === "admin" ? "" : ".muted"), null, t.role),
				h("span.muted", { dataset: { label: "made" } }, ago(t.createdMs, now)),
				h("span.muted", { dataset: { label: "used" } }, t.lastUsedMs ? ago(t.lastUsedMs, now) : "not yet"),
				h("button.btn.flat", { title: t.you ? "Revoke this browser's token: signs it out" : "Revoke it: it stops working at once", on: { click: () => revoke(t) } }, icon("close"), "Revoke")))),
		h("h2.cfg-sub", null, "New token"),
		h("div.cfg-inline", null, name, role, h("button.btn.primary", { on: { click: make } }, icon("add"), "Make token")),
		h("div.cfg-inline", null, message),
		h("h2.cfg-sub", null, "Another browser"),
		h("p.muted.cfg-note", null, "A one-time link signs another browser in (a phone, the laptop) without pasting a token."),
		h("button.btn", { on: { click: async () => {
			try {
				model.link = await api("POST", "links");
				model.made = null;
			}
			catch (e) {
				model.error = e.message;
			}
			render();
		} } }, icon("link"), "Make a sign-in link"));
}

function shownOnce(title, secret, note, close) {
	const box = h("input.input-field.mono.cfg-secret", { readonly: true, value: secret, "aria-label": title, on: { focus: (event) => event.target.select() } });
	const copied = h("span.c-saved", { role: "status" });
	return h("div.cfg-once", { role: "region", "aria-label": title },
		h("div.cfg-once-head", null, h("strong", null, title), h("span.muted", null, "shown once"), h("span.grow"), h("button.ib", { title: "Done", "aria-label": "Done", on: { click: close } }, icon("close"))),
		h("div.cfg-inline", null, box, h("button.btn", { on: { click: async () => {
			try {
				await navigator.clipboard.writeText(secret);
				copied.textContent = "Copied";
			}
			catch {
				box.focus();
				box.select();
				copied.textContent = "Selected: Ctrl+C copies it";
			}
		} } }, icon("copy"), "Copy"), copied),
		h("p.muted.cfg-small", null, note));
}

async function revoke(t) {
	const what = t.you ? "this browser's token? This signs it out." : t.role === "reader" ? "the reader token " + t.name + "? Its IED stops following at once." : "the admin token " + t.name + "? It stops working at once.";
	if (!confirm("Revoke " + what)) return;
	try {
		await api("DELETE", "tokens/" + encodeURIComponent(t.name));
		if (t.you) {
			clearInterval(tokensTimer);
			model.me = null;
			return signIn();
		}
		await reloadTokens();
	}
	catch (e) {
		model.error = e.message;
	}
	render();
}

start();
