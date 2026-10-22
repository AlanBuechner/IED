// searchpage.js - Settings > Web > Search (docs/29 "Settings"): SearXNG's and Tor's state live from their topics with
// Restart and New circuits, the engines (add from SearXNG's catalog, remove, Test engines), a search to try, then the
// settings window's own rows for the other websearch.* keys. Every setting here is the app tier's.

import { h, icon, replace } from "/ui-kit/dom.js";
import { engineTestLine, lastSearchLine, searxLine, seconds, torLine, withEngine, withoutEngine } from "./model.js";

const ENGINES = "websearch.engines";
const ARROW = " " + String.fromCharCode(0x203a) + " ";
const DOT = " " + String.fromCharCode(0xb7) + " ";

export class SearchPage {
	constructor(ctx) {
		this.ied = ctx.ied;
		this.settings = ctx.settings;
		this.searx = undefined;
		this.tor = undefined;
		this.catalog = [];
		this.tests = null;
		this.busy = { restart: false, test: false };
		this.notes = { status: null, engines: null };
		this.statusEl = h("div.ws-status");
		this.enginesEl = h("div.ws-engines");
		this.optionsEl = h("div.ws-options");
		replace(ctx.el,
			h("div.set-crumbs", null, "Web" + ARROW + "Search" + DOT, h("span.mono", null, "websearch.*")),
			h("h1", null, "Search"),
			h("p.set-lead", null, "The AI's web_search asks several engines at once through a SearXNG that IED runs on this machine. Its requests leave through Tor, so the engines see a Tor exit instead of this machine, and an exit they refuse is swapped for a new circuit. web_fetch (Web" + ARROW + "Fetch) reads the pages it finds."),
			this.statusEl,
			h("h2.ws-head", null, "Engines"),
			this.enginesEl,
			h("h2.ws-head", null, "Try a search"),
			this.tryPanel(),
			h("h2.ws-head", null, "Options"),
			this.optionsEl);
		this.settings.onChange(() => this.render());
		this.follow("websearch:status", (data) => (this.searx = data));
		this.follow("tor:status", (data) => (this.tor = data));
		this.ied.client.request("websearch.catalog", {}).then((answer) => {
			this.catalog = answer.engines || [];
			this.renderEngines();
		}).catch(() => {});
		this.render();
	}

	follow(topic, keep) {
		this.ied.client.subscribe(topic, {
			snap: (data) => {
				keep(data);
				this.render();
			},
			evt: (kind, data) => {
				keep(data);
				this.render();
			},
			gone: () => {
				keep(null);
				this.render();
			},
		});
	}

	value(key) {
		return this.settings.value(key).value;
	}

	render() {
		this.renderStatus();
		this.renderEngines();
		this.renderOptions();
	}

	// --- Status -------------------------------------------------------------------------------------------------------

	renderStatus() {
		const external = Boolean(this.searx && this.searx.state === "external");
		const useTor = this.value("websearch.useTor") !== false;
		const last = this.searx && this.searx.lastSearch;
		replace(this.statusEl,
			stateRow("SearXNG", searxLine(this.searx)),
			stateRow("Tor", torLine(this.tor, useTor, external)),
			last ? h("div.ws-last", null, lastSearchLine(last)) : null,
			h("div.set-bar", null,
				h("button.btn", { disabled: this.busy.restart, on: { click: () => this.restart(false) } }, icon("debug-restart"), "Restart SearXNG"),
				useTor && !external ? h("button.btn", { disabled: this.busy.restart, title: "SearXNG starts again on new Tor circuits: other exits", on: { click: () => this.restart(true) } }, icon("sync"), "New circuits") : null,
				note(this.notes.status)));
	}

	async restart(newCircuit) {
		this.busy.restart = true;
		this.notes.status = { text: newCircuit ? "Starting SearXNG on new circuits..." : "Starting SearXNG...", tone: "busy" };
		this.renderStatus();
		try {
			await this.ied.client.request("websearch.restart", { newCircuit });
			this.notes.status = { text: newCircuit ? "SearXNG runs on new circuits." : "SearXNG started again.", tone: "ok" };
		}
		catch (e) {
			this.notes.status = { text: e.message, tone: "bad" };
		}
		this.busy.restart = false;
		this.renderStatus();
	}

	// --- Engines ------------------------------------------------------------------------------------------------------

	renderEngines() {
		const engines = this.value(ENGINES) || [];
		const unknown = (this.searx && this.searx.unknownEngines) || [];
		const rows = engines.map((name) => {
			const entry = this.catalog.find((e) => e.name === name);
			const result = this.tests && this.tests.find((r) => r.name === name);
			const line = unknown.includes(name) ? { text: "SearXNG has no engine by this name: left out", tone: "bad" } : engineTestLine(result);
			return h("div.ws-engine", { dataset: { engine: name } },
				h("span.ws-engine-name", null, name),
				h("span.ws-engine-cats", null, entry ? entry.categories.join(", ") : ""),
				h("span.ws-engine-test.ws-" + line.tone, null, line.text),
				h("button.btn.flat", { title: "Remove " + name, "aria-label": "Remove " + name, disabled: engines.length < 2, on: { click: () => this.setEngines(withoutEngine(engines, name)) } }, icon("close")));
		});
		const input = h("input.input-field.ws-add", { list: "ws-catalog", placeholder: "Add an engine by SearXNG's name", "aria-label": "Engine to add", spellcheck: "false" });
		const add = () => this.setEngines(withEngine(engines, input.value));
		input.addEventListener("keydown", (e) => {
			if (e.key === "Enter") add();
		});
		const offered = this.catalog.filter((e) => !engines.includes(e.name));
		replace(this.enginesEl,
			h("div.ws-engine-list", null, rows),
			h("div.set-bar", null,
				input,
				h("datalist", { id: "ws-catalog" }, offered.map((e) => h("option", { value: e.name }, e.categories.join(", ")))),
				h("button.btn", { on: { click: add } }, icon("add"), "Add"),
				h("button.btn", { disabled: this.busy.test, on: { click: () => this.testEngines() } }, icon("beaker"), this.busy.test ? "Testing..." : "Test engines"),
				note(this.notes.engines)));
	}

	async setEngines(list) {
		const error = await this.settings.set(ENGINES, list, "app");
		this.notes.engines = error ? { text: error, tone: "bad" } : null;
		this.renderEngines();
	}

	async testEngines() {
		this.busy.test = true;
		this.notes.engines = { text: "Searching with every engine (the first search also starts Tor and SearXNG: up to a minute)...", tone: "busy" };
		this.renderEngines();
		try {
			const answer = await this.ied.client.request("websearch.testEngines", {});
			this.tests = answer.engines;
			const working = answer.engines.filter((e) => e.results > 0).length;
			this.notes.engines = { text: working + " of " + answer.engines.length + " answered in " + seconds(answer.ms) + (answer.viaTor ? ", through Tor" : ""), tone: working ? "ok" : "bad" };
		}
		catch (e) {
			this.notes.engines = { text: e.message, tone: "bad" };
		}
		this.busy.test = false;
		this.renderEngines();
	}

	// --- Try a search -------------------------------------------------------------------------------------------------

	tryPanel() {
		const input = h("input.input-field.ws-query", { placeholder: "Search the web as the AI would", "aria-label": "Search the web", spellcheck: "false" });
		const out = h("div.ws-results");
		const go = async () => {
			const query = input.value.trim();
			if (!query) return;
			replace(out, note({ text: "Searching...", tone: "busy" }));
			const started = Date.now();
			try {
				const answer = await this.ied.client.request("websearch.search", { query, count: 8 });
				replace(out, resultsView(answer, Date.now() - started));
			}
			catch (e) {
				replace(out, note({ text: e.message, tone: "bad" }));
			}
		};
		input.addEventListener("keydown", (e) => {
			if (e.key === "Enter") go();
		});
		return h("div.ws-try", null, h("div.set-bar", null, input, h("button.btn", { on: { click: go } }, icon("search"), "Search")), out);
	}

	// --- Options ------------------------------------------------------------------------------------------------------

	renderOptions() {
		const keys = this.settings.keys.filter((k) => k !== ENGINES);
		if (typeof this.settings.row !== "function") {
			replace(this.optionsEl, h("p.muted", null, "This window cannot show the other settings here: " + keys.join(", ")));
			return;
		}
		replace(this.optionsEl, keys.map((key) => this.settings.row(key)));
	}
}

function stateRow(label, line) {
	return h("div.ws-state.ws-" + line.tone, null, h("span.ws-dot"), h("span.ws-label", null, label), h("span.ws-text", null, line.text));
}

function note(message) {
	return message ? h("span.ws-note.ws-" + message.tone, null, message.text) : null;
}

function resultsView(answer, ms) {
	const hits = answer.results.map((r) => h("div.ws-hit", null,
		h("a.ws-hit-title", { href: r.url, target: "_blank", rel: "noopener noreferrer" }, r.title || r.url),
		h("div.ws-hit-url.mono", null, r.url),
		r.content ? h("div.ws-hit-snippet", null, r.content) : null,
		h("div.ws-hit-by", null, r.engines.join(", ") + (r.published ? DOT + r.published : ""))));
	const failed = answer.failed.length ? "; failed: " + answer.failed.map(([engine, reason]) => engine + " (" + reason + ")").join(", ") : "";
	return [
		h("div.ws-last", null, answer.results.length + " of " + answer.total + " results in " + seconds(ms) + (answer.viaTor ? ", through Tor" : "") + failed),
		hits.length ? hits : h("p.muted", null, "No results."),
	];
}
