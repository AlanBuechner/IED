// modelsettings.js - Settings > AI > Models (docs/04 "Model profiles"): each ai.models profile with its endpoint, model,
// window and privacy, a form to add or change one, and Test connection (ai.probe), which asks the server what it serves:
// its models, its context per slot and its slots. Profiles live in the app tier (the User settings file), never in a
// repository; a key is only ever named by its environment variable.

import { h, icon, replace } from "/ui-kit/dom.js";
import { confirmDialog } from "/ui-kit/dialog.js";
import { compact } from "./chatmodel.js";

const PRIVACY = ["local", "lan", "cloud"];

// A probe's one-line summary.
export function probeLine(probe) {
	if (!probe.ok) return "Not reachable: " + (probe.error || "no answer");
	const parts = [probe.server];
	if (probe.slots) parts.push(probe.slots + (probe.slots === 1 ? " slot" : " slots"));
	if (probe.contextWindow) parts.push(compact(probe.contextWindow) + " context");
	parts.push(probe.models.length ? "serves " + probe.models.join(", ") : "serves no model");
	parts.push(probe.ms + " ms");
	return parts.join(" \u00b7 ");
}

// The profile a form's fields describe; throws when one is not usable.
export function profileFrom(fields) {
	const name = fields.name.trim();
	if (!name) throw new Error("a model needs a name");
	if (!/^http:\/\//.test(fields.endpoint.trim())) throw new Error("the endpoint must be an http:// URL, like http://127.0.0.1:8080/v1");
	const profile = { endpoint: fields.endpoint.trim() };
	if (fields.model.trim()) profile.model = fields.model.trim();
	for (const key of ["contextWindow", "maxOutput"]) {
		if (!String(fields[key]).trim()) continue;
		const value = Number(fields[key]);
		if (!Number.isInteger(value) || value < 0) throw new Error(key + " must be a whole number");
		profile[key] = value;
	}
	profile.privacy = fields.privacy;
	if (fields.apiKeyEnv.trim()) profile.apiKey = { env: fields.apiKeyEnv.trim() };
	return { name, profile };
}

export class ModelsPage {
	constructor(ctx) {
		this.ctx = ctx;
		this.settings = ctx.settings;
		this.probes = new Map();
		this.editing = null;
		this.list = h("div.set-models");
		replace(ctx.el,
			h("div.set-crumbs", null, "AI \u203a Models \u00b7 ", h("span.mono", null, "ai.models")),
			h("h1", null, "Models"),
			h("p.set-lead", null, "Where each model is served and how to talk to it: an OpenAI-compatible endpoint (llama.cpp, vLLM, LM Studio, Ollama's /v1). Groups give models to roles. Models are kept in your User settings, never in a repository."),
			h("div.set-bar", null, h("button.btn", { on: { click: () => this.edit("") } }, icon("add"), "Add model")),
			this.list);
		this.stop = this.settings.onChange(() => this.render());
		this.render();
	}

	models() {
		return Object.assign({}, this.settings.value("ai.models").value || {});
	}

	render() {
		const models = this.models();
		const names = Object.keys(models);
		const cards = names.map((name) => this.editing === name ? this.form(name, models[name]) : this.card(name, models[name]));
		if (this.editing === "") cards.unshift(this.form("", { endpoint: "http://", privacy: "local" }));
		replace(this.list, ...cards, names.length || this.editing === "" ? null : h("p.muted", null, "No model yet. Add the endpoint of a model server you run."));
	}

	card(name, profile) {
		const probe = this.probes.get(name);
		return h("div.set-model", { dataset: { model: name } },
			h("div.set-model-head", null, h("b", null, name), h("span.chip" + (profile.privacy === "cloud" ? ".warn" : ""), null, profile.privacy || "local"),
				h("span.grow"),
				h("button.btn", { on: { click: () => this.test(name) } }, icon("plug"), "Test connection"),
				h("button.btn", { on: { click: () => this.edit(name) } }, icon("edit"), "Edit"),
				h("button.btn.flat", { title: "Remove " + name, "aria-label": "Remove " + name, on: { click: () => this.remove(name) } }, icon("trash"))),
			h("div.set-model-facts", null,
				h("span.mono", null, profile.endpoint), h("span", null, "model " + (profile.model || name)),
				h("span", null, profile.contextWindow ? compact(profile.contextWindow) + " context" : "context: asked from the server"),
				profile.apiKey ? h("span", null, "key from %" + profile.apiKey.env + "%") : null),
			probe ? h("div.set-probe" + (probe.ok ? (probe.modelServed || !probe.models.length ? ".ok" : ".warn") : ".bad"), null,
				icon(probe.ok ? "pass" : "error"), probeLine(probe),
				probe.ok && probe.models.length && !probe.modelServed ? " - it does not serve " + (profile.model || name) : "") : null);
	}

	form(name, profile) {
		const field = (label, key, value, extra) => {
			const input = h("input.input-field", Object.assign({ "aria-label": label, spellcheck: "false", value: value === undefined ? "" : String(value) }, extra || {}));
			fields[key] = input;
			return h("label.set-field", null, h("span", null, label), input);
		};
		const fields = {};
		const privacy = h("select.input-field", { "aria-label": "Privacy" }, ...PRIVACY.map((p) => h("option", Object.assign({ value: p }, (profile.privacy || "local") === p ? { selected: true } : {}), p)));
		const message = h("div.set-message", { hidden: true });
		const result = h("div.set-probe", { hidden: true });
		const read = () => profileFrom({
			name: fields.name.value, endpoint: fields.endpoint.value, model: fields.model.value, contextWindow: fields.contextWindow.value,
			maxOutput: fields.maxOutput.value, privacy: privacy.value, apiKeyEnv: fields.apiKeyEnv.value,
		});
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
			const models = this.models();
			if (made.name !== name && models[made.name]) return fail(new Error("there is a model " + made.name + " already"));
			const kept = Object.assign({}, models[name] || {}, made.profile);
			if (!made.profile.apiKey) delete kept.apiKey;
			for (const key of ["model", "contextWindow", "maxOutput"]) if (made.profile[key] === undefined) delete kept[key];
			if (name && name !== made.name) delete models[name];
			models[made.name] = kept;
			const error = await this.settings.set("ai.models", models, "app");
			if (error) return fail(new Error(error));
			this.editing = null;
			this.render();
			return null;
		};
		const test = async () => {
			let made;
			try {
				made = read();
			}
			catch (e) {
				return fail(e);
			}
			result.hidden = false;
			result.className = "set-probe";
			replace(result, icon("loading", "spin"), "Asking " + made.profile.endpoint + "\u2026");
			const probe = await this.probe({ profile: made.profile });
			result.className = "set-probe " + (probe.ok ? "ok" : "bad");
			replace(result, icon(probe.ok ? "pass" : "error"), probeLine(probe));
			if (probe.ok && probe.contextWindow && !fields.contextWindow.value) fields.contextWindow.placeholder = "server says " + probe.contextWindow;
			return null;
		};
		return h("div.set-model.editing", { dataset: { model: name || "new" } },
			h("div.set-form", null,
				field("Name", "name", name, { placeholder: "qwen-coder@desk" }),
				field("Endpoint", "endpoint", profile.endpoint, { placeholder: "http://127.0.0.1:8080/v1" }),
				field("Model id", "model", profile.model, { placeholder: "the name above" }),
				field("Context window", "contextWindow", profile.contextWindow, { placeholder: "asked from the server", inputmode: "numeric" }),
				field("Max output", "maxOutput", profile.maxOutput, { placeholder: "the server's default", inputmode: "numeric" }),
				h("label.set-field", null, h("span", null, "Privacy"), privacy),
				field("API key variable", "apiKeyEnv", profile.apiKey ? profile.apiKey.env : "", { placeholder: "none (an environment variable's name)" })),
			message, result,
			h("div.set-bar", null,
				h("button.btn.primary", { on: { click: save } }, "Save"),
				h("button.btn", { on: { click: test } }, icon("plug"), "Test connection"),
				h("button.btn.flat", { on: { click: () => {
					this.editing = null;
					this.render();
				} } }, "Cancel")));
	}

	edit(name) {
		this.editing = name;
		this.render();
		const first = this.list.querySelector(".set-model.editing input");
		if (first) first.focus();
	}

	async probe(params) {
		try {
			return await this.ctx.ied.client.request("ai.probe", Object.assign({ wsId: this.ctx.wsId }, params));
		}
		catch (e) {
			return { ok: false, error: e.message, models: [] };
		}
	}

	async test(name) {
		this.probes.set(name, { ok: false, error: "asking\u2026", models: [] });
		this.render();
		this.probes.set(name, await this.probe({ model: name }));
		this.render();
	}

	async remove(name) {
		if (!(await confirmDialog({ title: "Remove model", message: "Remove " + name + " from ai.models? Groups that name it fall back to their next model.", confirm: "Remove", danger: true }))) return;
		const models = this.models();
		delete models[name];
		const error = await this.settings.set("ai.models", models, "app");
		if (error) this.ctx.ied.toast(error, { level: "error" });
	}

	dispose() {
		this.stop();
	}
}
