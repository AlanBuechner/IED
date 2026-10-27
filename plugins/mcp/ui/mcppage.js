// mcppage.js - Settings > AI > MCP Servers (docs/04 "MCP" In the UI): the workspace's servers live from the topic
// mcp:<wsId> - state, command line or URL, permission, privacy, tools, the last error and output - with Test, Start or
// Restart, Stop, Edit, Remove and on/off; Add server takes a command or a URL and tries it (mcp.test) before Save.
// Servers are written to the User (app) tier; those a solution or project file sets, or a plugin, are shown with where
// they come from and changed there.

import { h, icon, replace } from "/ui-kit/dom.js";
import { confirmDialog } from "/ui-kit/dialog.js";
import { PERMISSIONS, fieldsOf, serverFrom, sourceOf, stateLine, testLine } from "./model.js";

const KEY = "mcp.servers";
const ARROW = " " + String.fromCharCode(0x203a) + " ";
const DOT = " " + String.fromCharCode(0xb7) + " ";
const PRIVACY = [["", "As its place says (local for a command)"], ["local", "local"], ["lan", "lan"], ["cloud", "cloud"]];

export class McpPage {
	constructor(ctx) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.settings = ctx.settings;
		this.snapshot = undefined;
		this.appServers = {};
		this.editing = null;      // a server's name, "" for a new one
		this.draft = null;        // the form's fields while it is open
		this.tests = new Map();   // name -> mcp.test's answer, or {busy}
		this.busy = new Set();
		this.statusEl = h("div.mcp-status");
		this.listEl = h("div.mcp-servers");
		this.optionsEl = h("div.mcp-options");
		replace(ctx.el,
			h("div.set-crumbs", null, "AI" + ARROW + "MCP Servers" + DOT, h("span.mono", null, KEY)),
			h("h1", null, "MCP Servers"),
			h("p.mcp-lead", null, "Programs and services that give the AI more tools: Blender, a game engine, anything that speaks the Model Context Protocol. IED starts a command when a chat first needs it and speaks to it on its standard input and output, or reaches a running server at its http:// URL. Their tools are the chat's tools mcp_<server>_<tool>, loaded on demand, and run with the same permission checks and approval cards as IED's own."),
			this.statusEl,
			h("div.mcp-bar", null, h("button.btn", { dataset: { act: "add" }, on: { click: () => this.edit("") } }, icon("add"), "Add server")),
			this.listEl,
			h("h2.mcp-head", null, "Options"),
			this.optionsEl);
		this.stopChange = this.settings.onChange(() => this.reloadApp());
		this.follow();
		this.reloadApp();
	}

	follow() {
		const topic = "mcp:" + this.ctx.wsId;
		this.ied.client.request("mcp.status", { wsId: this.ctx.wsId }).then((snapshot) => {
			this.snapshot = snapshot;
			this.render();
			this.ied.client.subscribe(topic, {
				snap: (data) => this.take(data),
				evt: (kind, data) => this.take(data),
				gone: () => {},
			});
		}).catch((e) => {
			this.snapshot = null;
			this.failure = e.message;
			this.render();
		});
	}

	take(data) {
		if (!data || !data.servers) return;
		this.snapshot = data;
		this.render();
	}

	async reloadApp() {
		try {
			const app = await this.ied.client.request("settings.get", {});
			const value = app.values && app.values[KEY];
			this.appServers = Object.assign({}, (value && value.value) || {});
		}
		catch (e) {
			this.appServers = {};
		}
		this.render();
	}

	effective() {
		return Object.assign({}, this.settings.value(KEY).value || {});
	}

	render() {
		this.renderStatus();
		this.renderList();
		this.renderOptions();
	}

	renderStatus() {
		if (this.snapshot === undefined) return replace(this.statusEl, h("span.muted", null, "Asking the server..."));
		if (this.snapshot === null) return replace(this.statusEl, h("div.mcp-note.mcp-bad", null, "MCP is not available here: " + (this.failure || "the MCP plugin does not answer")));
		const notes = [];
		if (!this.snapshot.trusted) notes.push(h("div.mcp-note.mcp-warn", null, icon("shield"), "This workspace is not trusted: its MCP servers do not start and the AI gets none of their tools. Trust it to use them."));
		for (const problem of this.snapshot.problems || []) notes.push(h("div.mcp-note.mcp-bad", null, icon("error"), problem));
		replace(this.statusEl, notes);
	}

	renderList() {
		const servers = this.snapshot ? this.snapshot.servers : [];
		const cards = servers.map((server) => this.editing === server.name ? this.form(server.name, this.appServers[server.name] || {}) : this.card(server));
		if (this.editing === "") cards.unshift(this.form("", {}));
		const empty = !servers.length && this.editing !== "" && this.snapshot ? h("p.muted", null, "No MCP server yet. Add one with its command (for example python and the path of its script, or uvx and its package) or its URL.") : null;
		replace(this.listEl, cards, empty);
	}

	renderOptions() {
		const keys = this.settings.keys.filter((k) => k !== KEY);
		if (typeof this.settings.row !== "function") return replace(this.optionsEl, h("p.muted", null, keys.join(", ")));
		replace(this.optionsEl, keys.map((key) => this.settings.row(key)));
	}

	// --- A server ------------------------------------------------------------------------------------------------------

	card(server) {
		const trusted = this.snapshot && this.snapshot.trusted;
		const line = stateLine(server, trusted);
		const source = sourceOf(server, this.appServers, this.effective());
		const mine = source === "app";
		const running = server.state === "running" || server.state === "starting";
		const busy = this.busy.has(server.name);
		const test = this.tests.get(server.name);
		const tools = server.tools || [];
		const preloaded = tools.filter((t) => t.preloaded).length;
		return h("div.mcp-server", { dataset: { server: server.name, state: server.state } },
			h("div.mcp-server-head", null,
				h("span.mcp-dot.mcp-" + line.tone),
				h("b", null, server.name),
				h("span.chip", null, server.transport === "http" ? "http" : "stdio"),
				h("span.chip" + (server.permission === "ask" ? "" : server.permission === "read" ? ".ok" : ".warn"), { title: (PERMISSIONS.find((p) => p[0] === server.permission) || [])[1] }, server.permission === "ask" ? "asks" : server.permission === "read" ? "read-only" : "follows the mode"),
				h("span.chip" + (server.privacy === "cloud" ? ".warn" : ""), null, server.privacy),
				mine ? null : h("span.chip", { title: "Change it where it is set" }, source === "workspace" ? "solution or project file" : source),
				h("span.grow"),
				h("div.mcp-actions", null,
					h("button.btn", { dataset: { act: "test" }, disabled: busy || !trusted || !server.enabled, title: "Start it (again) and list its tools", on: { click: () => this.test(server.name) } }, icon("beaker"), "Test"),
					running ? h("button.btn", { dataset: { act: "stop" }, disabled: busy, on: { click: () => this.act("mcp.stop", server.name) } }, icon("debug-stop"), "Stop")
						: h("button.btn", { dataset: { act: "start" }, disabled: busy || !trusted || !server.enabled, on: { click: () => this.act("mcp.restart", server.name) } }, icon("play"), "Start"),
					running ? h("button.btn", { dataset: { act: "restart" }, disabled: busy, on: { click: () => this.act("mcp.restart", server.name) } }, icon("debug-restart"), "Restart") : null,
					mine ? h("button.btn", { dataset: { act: "toggle" }, on: { click: () => this.setEnabled(server.name, !server.enabled) } }, icon(server.enabled ? "circle-slash" : "check"), server.enabled ? "Turn off" : "Turn on") : null,
					mine ? h("button.btn", { dataset: { act: "edit" }, on: { click: () => this.edit(server.name) } }, icon("edit"), "Edit") : null,
					mine ? h("button.btn.flat", { dataset: { act: "remove" }, title: "Remove " + server.name, "aria-label": "Remove " + server.name, on: { click: () => this.remove(server.name) } }, icon("trash")) : null)),
			h("div.mcp-target.mono", { title: server.target }, server.target),
			h("div.mcp-line.mcp-" + line.tone, null, line.text),
			test ? this.testView(test) : null,
			tools.length ? h("details.mcp-tools", null,
				h("summary", null, tools.length + (tools.length === 1 ? " tool" : " tools") + (preloaded ? DOT + preloaded + " sent whole with every request" : DOT + "loaded on demand")),
				h("div.mcp-tool-list", null, tools.map((tool) => h("div.mcp-tool", { dataset: { tool: tool.name } },
					h("span.mcp-tool-name.mono", null, tool.aiName),
					h("span.mcp-tool-about", null, tool.description || tool.title || ""),
					tool.preloaded ? h("span.chip", null, "preloaded") : null,
					tool.readOnlyHint ? h("span.chip", { title: "The server says it only reads; the permission is yours to set" }, "says read-only") : null)))) : null,
			(server.log || []).length ? h("details.mcp-log", null, h("summary", null, "Its output"), h("pre.mcp-log-text", null, server.log.join("\n"))) : null);
	}

	testView(test) {
		if (test.busy) return h("div.mcp-line.mcp-busy", null, icon("loading", "spin"), "Starting it and asking for its tools...");
		const line = testLine(test);
		return h("div.mcp-test", null,
			h("div.mcp-line.mcp-" + line.tone, null, icon(test.ok ? "pass" : "error"), line.text),
			!test.ok && test.log ? h("pre.mcp-log-text", null, test.log) : null);
	}

	async test(name, entry) {
		this.tests.set(name || "", { busy: true });
		this.render();
		let answer;
		try {
			answer = await this.ied.client.request("mcp.test", Object.assign({ wsId: this.ctx.wsId, name }, entry ? { server: entry } : {}));
		}
		catch (e) {
			answer = { ok: false, error: e.message, tools: [] };
		}
		this.tests.set(name || "", answer);
		this.render();
		return answer;
	}

	async act(method, name) {
		this.busy.add(name);
		this.tests.delete(name);
		this.render();
		try {
			await this.ied.client.request(method, { wsId: this.ctx.wsId, name });
		}
		catch (e) {
			this.ied.toast(e.message, { level: "error" });
		}
		this.busy.delete(name);
		this.render();
	}

	async write(servers) {
		const error = await this.settings.set(KEY, servers, "app");
		if (!error) this.appServers = servers;
		return error;
	}

	async setEnabled(name, enabled) {
		const servers = Object.assign({}, this.appServers);
		servers[name] = Object.assign({}, servers[name]);
		if (enabled) delete servers[name].enabled;
		else servers[name].enabled = false;
		const error = await this.write(servers);
		if (error) this.ied.toast(error, { level: "error" });
		this.render();
	}

	async remove(name) {
		if (!(await confirmDialog({ title: "Remove MCP server", message: "Remove " + name + " from mcp.servers? Its tools leave the chats' next turns.", confirm: "Remove", danger: true }))) return;
		const servers = Object.assign({}, this.appServers);
		delete servers[name];
		const error = await this.write(servers);
		if (error) this.ied.toast(error, { level: "error" });
		this.render();
	}

	edit(name) {
		this.editing = name;
		this.draft = fieldsOf(name, name ? this.appServers[name] || {} : {});
		this.tests.delete("");
		this.render();
		const first = this.listEl.querySelector(".mcp-server.editing input");
		if (first) first.focus();
	}

	// --- The form ------------------------------------------------------------------------------------------------------

	form(name) {
		const draft = this.draft || fieldsOf(name, {});
		const inputs = {};
		const field = (label, key, extra) => {
			const input = h("input.input-field", Object.assign({ "aria-label": label, spellcheck: "false", value: String(draft[key] === undefined ? "" : draft[key]) }, extra || {}));
			inputs[key] = input;
			return h("label.set-field.mcp-field", { dataset: { field: key } }, h("span", null, label), input);
		};
		const area = (label, key, placeholder) => {
			const input = h("textarea.input-field.mcp-area", { "aria-label": label, spellcheck: "false", rows: "2", placeholder });
			input.value = draft[key] || "";
			inputs[key] = input;
			return h("label.set-field.mcp-field", { dataset: { field: key } }, h("span", null, label), input);
		};
		const select = (label, key, options) => {
			const input = h("select.input-field", { "aria-label": label }, options.map(([value, text]) => h("option", Object.assign({ value }, String(draft[key]) === value ? { selected: true } : {}), text)));
			inputs[key] = input;
			return h("label.set-field.mcp-field", { dataset: { field: key } }, h("span", null, label), input);
		};
		const kind = select("Kind", "kind", [["command", "A command IED starts (stdio)"], ["url", "A running server's URL (streamable HTTP)"]]);
		const commandFields = [
			field("Command", "command", { placeholder: "python, uvx, npx or a path" }),
			field("Arguments", "args", { placeholder: "C:/tools/server.py --port 9876" }),
			field("Working folder", "cwd", { placeholder: "the workspace's root" }),
			area("Environment", "env", "NAME=value, one a line; NAME=env:VAR reads IED's VAR"),
		];
		const urlFields = [
			field("URL", "url", { placeholder: "http://127.0.0.1:8000/mcp" }),
			area("Headers", "headers", "Name: value, one a line; Authorization: env:TOKEN reads IED's TOKEN"),
		];
		const show = () => {
			for (const el of commandFields) el.hidden = inputs.kind.value !== "command";
			for (const el of urlFields) el.hidden = inputs.kind.value !== "url";
		};
		inputs.kind.addEventListener("change", show);
		const preload = h("input.set-check", Object.assign({ type: "checkbox", "aria-label": "Send its tools whole with every request" }, draft.preload ? { checked: true } : {}));
		const message = h("div.set-message", { hidden: true });
		const read = () => {
			const fields = {};
			for (const [key, input] of Object.entries(inputs)) fields[key] = input.value;
			fields.preload = preload.checked;
			fields.enabled = draft.enabled;
			this.draft = Object.assign({}, fields);
			return serverFrom(fields);
		};
		const fail = (e) => {
			message.textContent = e.message;
			message.className = "set-message c-error";
			message.hidden = false;
		};
		const save = async () => {
			let made;
			try {
				made = read();
			}
			catch (e) {
				return fail(e);
			}
			if (made.name !== name && (this.appServers[made.name] || (this.snapshot && this.snapshot.servers.some((s) => s.name === made.name)))) return fail(new Error("there is a server " + made.name + " already"));
			const servers = Object.assign({}, this.appServers);
			if (name && name !== made.name) delete servers[name];
			servers[made.name] = made.entry;
			const error = await this.write(servers);
			if (error) return fail(new Error(error));
			if (this.tests.has("")) this.tests.set(made.name, this.tests.get(""));
			this.tests.delete("");
			this.editing = null;
			this.draft = null;
			this.render();
			return null;
		};
		const tryIt = async () => {
			let made;
			try {
				made = read();
			}
			catch (e) {
				return fail(e);
			}
			message.hidden = true;
			await this.test("", made.entry);
			return null;
		};
		const test = this.tests.get("");
		const el = h("div.mcp-server.editing", { dataset: { server: name || "new" } },
			h("div.set-form.mcp-form", null,
				field("Name", "name", { placeholder: "blender" }),
				kind,
				commandFields,
				urlFields,
				select("Permission", "permission", PERMISSIONS),
				select("Privacy", "privacy", PRIVACY),
				field("Call timeout (s)", "timeoutSeconds", { placeholder: "120", inputmode: "numeric" }),
				field("Start timeout (s)", "startSeconds", { placeholder: "30", inputmode: "numeric" }),
				h("label.mcp-check", null, preload, h("span", null, "Send its tools whole with every request (preload)"))),
			message,
			test ? this.testView(test) : null,
			h("div.mcp-bar", null,
				h("button.btn.primary", { dataset: { act: "save" }, on: { click: save } }, "Save"),
				h("button.btn", { dataset: { act: "try" }, disabled: test && test.busy, on: { click: tryIt } }, icon("beaker"), "Test"),
				h("button.btn.flat", { dataset: { act: "cancel" }, on: { click: () => {
					this.editing = null;
					this.draft = null;
					this.tests.delete("");
					this.render();
				} } }, "Cancel")));
		show();
		return el;
	}

	dispose() {
		if (this.stopChange) this.stopChange();
	}
}
