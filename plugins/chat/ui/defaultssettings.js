// defaultssettings.js - Settings > AI > Defaults (docs/04 "Defaults by use"; owner, 2026-11-11: "we should be able to set
// a default model/effort for things like the chat, planning board, inline chat, etc."): the default chain's first model
// (ai.defaultModel), then each use the plugins contribute (aiUses: Chats, Chats the AI starts, Planning board, Profiler
// questions, ...) with the model and effort a chat of it runs on when it names none. A model's own effort is named, not
// just "Default": Medium, as Claude Code says for Opus 5.5. Each choice is written to the tier being edited (User or
// Solution), with Reset there and the tier that sets the value in effect when it is another.

import { h, icon, replace } from "/ui-kit/dom.js";
import { EFFORTS } from "./chatmodel.js";
import { choicesFrom } from "./modelpicker.js";

const TIER_NAMES = { app: "User", solution: "Solution", project: "Project" };
// The chat plugin's own uses, for a window with no workspace (where no plugin's contributions are read).
const OWN_USES = [
	{ id: "chat", title: "Chats", description: "The chats you start: New chat, the start page, Ctrl+T.", model: "ai.chats.model", effort: "ai.chats.effort" },
	{ id: "agent", title: "Chats the AI starts", description: "chat_create's chats when it names no model; a sub-agent runs on its own (ai.agents).", model: "ai.chats.agentModel", effort: "ai.chats.agentEffort" },
];

function effortName(id) {
	return (EFFORTS.find(([e]) => e === id) || [id, id])[1];
}

export class DefaultsPage {
	constructor(ctx) {
		this.ctx = ctx;
		this.settings = ctx.settings;
		this.usage = null;
		this.list = h("div.set-defaults");
		replace(ctx.el,
			h("div.set-crumbs", null, "AI \u203a Defaults"),
			h("h1", null, "Defaults"),
			h("p.set-lead", null, "What a chat runs on when it names no model or effort of its own, by what it is for. A chat's Model menu still picks its own. Empty choices fall back: a use's model to the default chain, an effort to the model's own."),
			this.list);
		this.stop = this.settings.onChange(() => this.render());
		this.render();
		this.load();
	}

	async load() {
		const client = this.ctx.ied && this.ctx.ied.client;
		if (!client) return;
		try {
			this.usage = await client.request("ai.usage", {});
		}
		catch (e) {
			this.usage = null;
		}
		this.render();
	}

	// The uses the plugins loaded for the workspace contribute, those whose settings this window knows: the chat
	// plugin's own first (the chats you start, then the AI's), the others in the plugins' order.
	uses() {
		const contributed = this.ctx.ied && this.ctx.ied.contributions ? this.ctx.ied.contributions("aiUses").map((c) => c.value) : [];
		const own = OWN_USES.map((use) => use.id);
		const rank = (use) => (own.includes(use.id) ? own.indexOf(use.id) : own.length);
		return (contributed.length ? contributed : OWN_USES).filter((use) => this.settings.schema(use.model) && this.settings.schema(use.effort))
			.sort((a, b) => rank(a) - rank(b));
	}

	choices() {
		const values = {};
		for (const key of ["ai.models", "ai.defaultModel"]) values[key] = this.settings.value(key);
		const claude = (this.usage && this.usage.claude) || {};
		return choicesFrom(values, claude.chains || [], claude.models || [], []);
	}

	render() {
		const choices = this.choices();
		const chain = (choices.chains || []).find((c) => c.role === "primary") || (choices.chains || [])[0];
		// The chain's head now: ai.defaultModel when it names one, else pacing's pick on Claude's ladder.
		const named = choices.defaultModel;
		const chainNow = named || (chain && chain.now) || "";
		const now = chainNow ? (choices.models.find((m) => m.name === chainNow) || { label: chainNow }).label : "";
		const ladder = named ? "Claude, stepping down with the plan's pace" : now ? "Claude, stepping down (" + now + " now)" : "Claude, stepping down";
		const rows = [this.row({ id: "default", title: "Default chain", description: "What a chat runs on when nothing names its model: ai.defaultModel - empty, Claude stepping down to lighter models as the plan runs ahead of its pace (ai.claude.stepDown), then ai.fallbackModel.", model: "ai.defaultModel" }, choices, ladder, null)];
		for (const use of this.uses()) rows.push(this.row(use, choices, "Default chain" + (now ? " (" + now + " now)" : ""), chainNow));
		replace(this.list, ...rows);
	}

	// One use: its title and what it is for, its Model and Effort choices, and where each value comes from.
	row(use, choices, emptyModel, chainNow) {
		const model = this.settings.value(use.model);
		const effort = use.effort ? this.settings.value(use.effort) : null;
		const chosen = typeof model.value === "string" ? model.value : "";
		const runs = chosen || chainNow || "";
		const profile = choices.models.find((m) => m.name === runs);
		const modelSelect = h("select.input-field", { "aria-label": use.title + " model", dataset: { key: use.model }, on: { change: (event) => this.write(use.model, event.target.value) } },
			h("option", { value: "" }, emptyModel),
			...this.modelOptions(choices, chosen));
		modelSelect.value = chosen;
		const parts = [h("label.set-default-field", null, h("span", null, "Model"), modelSelect, this.source(use.model, model))];
		if (effort) {
			const own = profile && profile.defaultEffort ? effortName(profile.defaultEffort) : "";
			const takes = profile ? profile.efforts : EFFORTS.map(([id]) => id);
			const value = typeof effort.value === "string" ? effort.value : "";
			const levels = EFFORTS.filter(([id]) => takes.includes(id) || id === value);
			const effortSelect = h("select.input-field", { "aria-label": use.title + " effort", dataset: { key: use.effort }, on: { change: (event) => this.write(use.effort, event.target.value) } },
				h("option", { value: "" }, own ? "The model's own (" + own + ")" : "The model's own"),
				...levels.map(([id, name]) => h("option", { value: id }, takes.includes(id) ? name : name + " (" + (profile ? profile.label : "the model") + " does not take it)")));
			effortSelect.value = value;
			parts.push(h("label.set-default-field", null, h("span", null, "Effort"), effortSelect, this.source(use.effort, effort)));
		}
		return h("div.set-model.set-default", { dataset: { use: use.id } },
			h("div.set-model-head", null, h("b", null, use.title), h("span.grow"), h("span.mono.muted", null, use.model + (use.effort ? ", " + use.effort : ""))),
			use.description ? h("div.muted", null, use.description) : null,
			h("div.set-default-fields", null, ...parts));
	}

	// The models a use can name: the Claude models by what each runs now, the versions Claude Code offers, the others.
	modelOptions(choices, chosen) {
		const option = (m) => h("option", { value: m.name }, m.label && m.label !== m.name ? m.label + " \u00b7 " + m.name : m.name);
		const main = choices.models.filter((m) => !m.version);
		const versions = choices.models.filter((m) => m.version);
		const out = [h("optgroup", { label: "Models" }, ...main.map(option))];
		if (versions.length) out.push(h("optgroup", { label: "Claude versions" }, ...versions.map(option)));
		if (chosen && !choices.models.some((m) => m.name === chosen)) out.push(h("option", { value: chosen }, chosen + " (not a model here)"));
		return out;
	}

	// Where the value in effect comes from, and Reset when the tier being edited sets it.
	source(key, value) {
		const tier = this.settings.tier();
		if (value.source === tier) return h("button.btn.flat.set-default-reset", { title: "Reset " + key + " in the " + TIER_NAMES[tier] + " tier", on: { click: () => this.reset(key) } }, icon("discard"), "Reset");
		if (value.source && value.source !== "default") return h("span.muted.set-default-source", null, "set in " + (TIER_NAMES[value.source] || value.source));
		return null;
	}

	async write(key, value) {
		const error = await this.settings.set(key, value);
		if (error && this.ctx.ied && this.ctx.ied.toast) this.ctx.ied.toast(error, { level: "error" });
	}

	async reset(key) {
		const error = await this.settings.reset(key);
		if (error && this.ctx.ied && this.ctx.ied.toast) this.ctx.ied.toast(error, { level: "error" });
	}

	dispose() {
		this.stop();
	}
}
