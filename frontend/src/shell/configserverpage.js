// configserverpage.js - Settings > Environment > Config Server (docs/35 "Settings > Environment > Config Server"): the
// config server this IED follows - its URL, its reader token (kept in the server's secret store, never shown again) and a
// CA certificate, always in the User tier - and the mirror live from the server topic: following or why not, when the
// last change was mirrored, and what the config server shares (each shared setting with its members - the local models'
// profiles -, plugins and skills). Connect stores a token typed in, points configServer.token at it and sets the URL;
// Disconnect clears the URL and keeps the mirror.

import { h, icon, replace } from "/ui-kit/dom.js";
import { SECRET_NAME, STALE_SERVER, ago, keptNote, normalizeUrl, servedSections, shortVersion, stateLook, storeTrouble, tokenSource } from "./configservermodel.js";

const URL_KEY = "configServer.url";
const TOKEN_KEY = "configServer.token";
const CERT_KEY = "configServer.certificate";
const TICK_MS = 10000;

export class ConfigServerPage {
	constructor(shell, ctx) {
		this.shell = shell;
		this.ctx = ctx;
		this.busy = false;
		this.filled = { url: null, cert: null };
		this.status = h("section.cs-status", { "aria-label": "Config server status", "aria-live": "polite" });
		this.urlField = h("input.input-field", { type: "text", id: "cs-url", placeholder: "http://nas.lan:8780/", spellcheck: "false", autocomplete: "off", on: { keydown: (event) => this.enter(event) } });
		this.tokenField = h("input.input-field", { type: "password", id: "cs-token", spellcheck: "false", autocomplete: "new-password", on: { keydown: (event) => this.enter(event) } });
		this.certField = h("input.input-field", { type: "text", id: "cs-cert", placeholder: "Optional: a PEM file of the server's own CA", spellcheck: "false", autocomplete: "off", on: { keydown: (event) => this.enter(event) } });
		this.tokenNote = h("div.cs-note");
		this.tokenActions = h("span.cs-inline");
		this.message = h("div.set-message.c-error", { role: "alert", hidden: true });
		this.connectButton = h("button.btn.primary", { on: { click: () => this.connect() } });
		this.disconnectButton = h("button.btn", { title: "Stop following it; what it shared stays until you connect again", on: { click: () => this.disconnect() } }, icon("debug-disconnect"), "Disconnect");
		ctx.el.append(h("div.cs-page", null,
			h("div.set-crumbs", null, "Environment \u203a Config Server"),
			h("h1", null, "Config Server"),
			h("div.set-desc", null, "A config server keeps what every IED of yours shares in one place: the local AI models' profiles (kept current by its plugins, such as the Spark's model manager), plugins and skills. IED follows it and mirrors what it serves; its settings come under your own, so a value you set wins on this machine."),
			this.status,
			h("h2.cs-head", null, "Connection"),
			h("div.cs-form", null,
				h("label.cs-label", { for: "cs-url" }, "Server URL"), h("div.cs-control", null, this.urlField),
				h("label.cs-label", { for: "cs-token" }, "Reader token"), h("div.cs-control", null, h("div.cs-inline", null, this.tokenField, this.tokenActions), this.tokenNote),
				h("label.cs-label", { for: "cs-cert" }, "CA certificate"), h("div.cs-control", null, this.certField,
					h("div.cs-note", null, "For an https server whose certificate your system does not trust (a NAS's own CA). Empty trusts the system's alone.")),
				h("span"), h("div.cs-control", null, h("div.cs-inline", null, this.connectButton, this.disconnectButton), this.message)),
			h("div.cs-note.cs-howto", null, "A reader token is made on the config server: ", h("code", null, "ied-server --config-root <folder> --config-token reader <this machine>"),
				" (in its container, the image run with ", h("code", null, "--config-token reader <name>"), "). It prints the token once.")));
		this.stopSettings = ctx.settings.onChange(() => this.render());
		this.stopServer = shell.onServerChange(() => this.render());
		this.timer = setInterval(() => this.renderStatus(), TICK_MS);
		this.render();
	}

	dispose() {
		this.stopSettings();
		this.stopServer();
		clearInterval(this.timer);
	}

	value(key) {
		const entry = this.ctx.settings.value(key);
		return entry ? entry.value : null;
	}

	followed() {
		return this.shell.server.configServer || null;
	}

	secrets() {
		return this.shell.server.secrets || null;
	}

	enter(event) {
		event.stopPropagation();
		if (event.key === "Enter") {
			event.preventDefault();
			this.connect();
		}
	}

	render() {
		this.fill(this.urlField, "url", this.value(URL_KEY) || "");
		this.fill(this.certField, "cert", this.value(CERT_KEY) || "");
		this.renderToken();
		this.renderButtons();
		this.renderStatus();
	}

	// A field shows the setting unless the owner is editing it: it follows the setting while its text is what was put
	// there last.
	fill(field, which, value) {
		if (this.filled[which] === null || field.value === this.filled[which]) field.value = value;
		this.filled[which] = value;
	}

	renderToken() {
		const secrets = this.secrets();
		const source = tokenSource(this.value(TOKEN_KEY), secrets);
		const storeProblem = storeTrouble(secrets);
		const remove = source.kind === "secret" && source.stored
			? h("button.btn.flat", { title: "Delete the stored token from this machine", on: { click: () => this.removeToken(source.name) } }, icon("trash"), "Remove")
			: null;
		replace(this.tokenActions, remove);
		this.tokenField.disabled = Boolean(storeProblem) || this.busy;
		if (storeProblem) {
			this.tokenField.placeholder = "";
			replace(this.tokenNote, source.kind === "env" ? "Read from the environment variable " : null, source.kind === "env" ? h("code", null, source.variable) : null, source.kind === "env" ? " of the IED server. " : null, h("span.c-error", null, storeProblem));
		}
		else if (source.kind === "secret" && source.stored) {
			this.tokenField.placeholder = "Stored on this machine: type a new one to replace it";
			replace(this.tokenNote, keptNote(secrets));
		}
		else if (source.kind === "secret") {
			this.tokenField.placeholder = "Paste the reader token";
			replace(this.tokenNote, h("span.c-error", null, "The secret '" + source.name + "' is not stored on this machine."), " Paste the token and Connect.");
		}
		else if (source.kind === "env") {
			this.tokenField.placeholder = "Paste a token to store it here instead";
			replace(this.tokenNote, "Read from the environment variable ", h("code", null, source.variable), " of the IED server (as it was when the server started).");
		}
		else {
			this.tokenField.placeholder = "Paste the reader token";
			replace(this.tokenNote, keptNote(secrets));
		}
	}

	renderButtons() {
		const url = this.value(URL_KEY) || "";
		replace(this.connectButton, icon(url ? "check" : "plug"), url ? "Apply" : "Connect");
		this.connectButton.disabled = this.busy;
		this.disconnectButton.hidden = !url;
		this.disconnectButton.disabled = this.busy;
	}

	renderStatus() {
		const status = this.followed();
		if (!status) {
			replace(this.status, h("p.muted", null, "This IED server does not follow config servers."));
			return;
		}
		const look = stateLook(status);
		const head = h("div.cs-state", null,
			h("span.cs-dot", { dataset: { accent: look.accent } }),
			h("span.cs-state-label", null, look.label),
			status.url ? h("span.mono.cs-url", { title: status.url }, status.url) : h("span.muted", null, "No config server set"),
			h("span.grow"),
			look.trouble || status.state === "connecting" ? h("button.btn.flat", { title: "Try the config server again now", on: { click: () => this.retry() } }, icon("refresh"), "Retry now") : null);
		const lines = [];
		if (status.error) lines.push(h("div.cs-line.c-error", null, status.error));
		if (status.syncedMs) lines.push(h("div.cs-line.muted", null, "Last change mirrored " + ago(status.syncedMs, Date.now()) + (status.version ? " \u00b7 version " + shortVersion(status.version) : "")));
		if (look.trouble || status.state === "off") {
			const has = servedSections(status).some((section) => section.items.length);
			if (has) lines.push(h("div.cs-line.muted", null, status.state === "off" ? "IED keeps what it mirrored last:" : "IED goes on with what it mirrored last:"));
		}
		replace(this.status, head, ...lines, this.served(status));
	}

	served(status) {
		const sections = servedSections(status);
		// Nothing mirrored: the rows say "none" only while following (the server shares nothing), not while it is not.
		if (status.state !== "following" && !sections.some((section) => section.items.length)) return null;
		return h("div.cs-served", { role: "list", "aria-label": "Shared by the config server" }, ...sections.map((section) =>
			h("div.cs-served-row", { role: "listitem", dataset: { kind: section.id } },
				h("span.cs-served-title", null, section.title),
				h("div.cs-served-items", null, section.items.length ? section.items.map((item) => h("div.cs-item", null,
					h("span.mono", null, item.name),
					item.members ? h("span.cs-members", null, item.members.length + ": ", ...item.members.map((member, i) => [i ? ", " : null, h("span.mono", null, member)])) : null))
					: h("span.muted", null, section.empty)))));
	}

	say(text) {
		this.message.textContent = text || "";
		this.message.hidden = !text;
	}

	setBusy(busy) {
		this.busy = busy;
		this.renderButtons();
		this.renderToken();
	}

	async connect() {
		if (this.busy) return;
		this.say("");
		let url;
		try {
			url = normalizeUrl(this.urlField.value);
		}
		catch (e) {
			this.say(e.message);
			return;
		}
		if (!url) {
			this.say("Type the config server's URL.");
			return;
		}
		const token = this.tokenField.value.trim();
		const source = tokenSource(this.value(TOKEN_KEY), this.secrets());
		if (!token && !(source.kind === "secret" && source.stored) && source.kind !== "env") {
			this.say("Paste a reader token: the config server refuses IED without one.");
			return;
		}
		const cert = this.certField.value.trim();
		this.setBusy(true);
		try {
			if (token) {
				await this.shell.client.request("secrets.set", { name: SECRET_NAME, value: token });
				this.tokenField.value = "";
				if (source.kind !== "secret" || source.name !== SECRET_NAME) await this.write(TOKEN_KEY, { secret: SECRET_NAME });
			}
			if (cert !== (this.value(CERT_KEY) || "")) await (cert ? this.write(CERT_KEY, cert) : this.reset(CERT_KEY));
			this.urlField.value = url;
			if (url !== (this.value(URL_KEY) || "")) await this.write(URL_KEY, url);
			else await this.shell.client.request("configServer.retry", {});
		}
		catch (e) {
			this.say(/unknown method/.test(e.message) ? STALE_SERVER : e.message);
		}
		finally {
			this.setBusy(false);
		}
	}

	async disconnect() {
		if (this.busy) return;
		this.say("");
		this.setBusy(true);
		try {
			await this.reset(URL_KEY);
			this.urlField.value = "";
			this.filled.url = "";
		}
		catch (e) {
			this.say(e.message);
		}
		finally {
			this.setBusy(false);
		}
	}

	async removeToken(name) {
		if (this.busy) return;
		this.say("");
		this.setBusy(true);
		try {
			await this.shell.client.request("secrets.remove", { name });
			const value = this.value(TOKEN_KEY);
			if (value && value.secret === name) await this.reset(TOKEN_KEY);
		}
		catch (e) {
			this.say(e.message);
		}
		finally {
			this.setBusy(false);
		}
	}

	async retry() {
		try {
			await this.shell.client.request("configServer.retry", {});
		}
		catch (e) {
			this.say(e.message);
		}
	}

	// The settings window's write and reset, in the User tier whatever tier it edits; a refusal throws.
	async write(key, value) {
		const error = await this.ctx.settings.set(key, value, "app");
		if (error) throw new Error(error);
	}

	async reset(key) {
		const error = await this.ctx.settings.reset(key, "app");
		if (error) throw new Error(error);
	}
}
