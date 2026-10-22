// claudeusage.js - Claude's plan usage in the UI (docs/04 "The Claude Code provider"), from the server's aiusage topic: the
// status bar's "Claude 5h 42% wk 61%" (yellow while a window runs ahead of pace, red when one is used up or the CLI is not
// on a subscription), and Settings > AI > Claude - the CLI (where it is, its version, whether it is signed in to a Claude
// subscription; IED never runs Claude on an API key), each window as a bar with the even line marked, its pace and when
// it resets, what pacing picks now, Refresh, and Add Claude models.

import { h, icon, replace } from "/ui-kit/dom.js";

const SHORT = { five_hour: "5h", seven_day: "wk" };
export const WARN_PACE = 1.25;
export const CLAUDE_PROFILES = {
	"claude-auto": { provider: "claude-code", model: "auto", pacing: { ladder: ["opus", "sonnet", "haiku"], target: "both" } },
	"claude-fable": { provider: "claude-code", model: "fable" },
	"claude-opus": { provider: "claude-code", model: "opus" },
	"claude-sonnet": { provider: "claude-code", model: "sonnet" },
	"claude-haiku": { provider: "claude-code", model: "haiku" },
};
const TARGETS = [["both", "The session and the week"], ["session", "Each 5-hour session"], ["week", "The week"]];

// When `at` comes, from `now`, as the server says it: "in 2 h 14 min", "in 35 min", "in 3 d 4 h", "now".
export function resetText(at, now) {
	const minutes = Math.ceil((at - now) / 60000);
	if (!at || minutes <= 0) return "now";
	if (minutes >= 24 * 60) return "in " + Math.floor(minutes / (24 * 60)) + " d " + Math.floor(minutes / 60) % 24 + " h";
	if (minutes >= 60) return "in " + Math.floor(minutes / 60) + " h " + minutes % 60 + " min";
	return "in " + minutes + " min";
}

function percent(value) {
	return Math.round(value) + "%";
}

// Whether the CLI the server found is signed in with a Claude subscription (Claude turns are refused otherwise).
export function onSubscription(claude) {
	return !!(claude && claude.cli && claude.cli.subscription);
}

// A window's line: "Week: 61% used, resets in 3 d 0 h, pace 1.07x".
export function windowLine(window, now) {
	let line = window.label + ": " + percent(window.utilization) + " used";
	if (window.resetsAtMs) line += ", resets " + resetText(window.resetsAtMs, now) + ", pace " + window.pace.toFixed(2) + "x";
	if (window.usedUp) line += " - used up";
	return line;
}

// The status bar item for the snapshot's `claude`: {text, level ("", "warn", "error"), tooltip}; null when no claude-code
// model is set up.
export function usageStatus(claude) {
	if (!claude || !claude.configured) return null;
	const usage = claude.usage || {};
	const now = claude.nowMs || Date.now();
	const cli = claude.cli || {};
	if (!cli.path && usage.error) return { text: "Claude: no CLI", level: "error", tooltip: usage.error };
	if (cli.signIn && !onSubscription(claude))
		return { text: "Claude: not on a subscription", level: "error", tooltip: "Claude Code signs in with " + (cli.signIn === "signed_in" ? cli.authMethod : "nothing") + ". IED runs Claude on your subscription only: run claude auth login in a terminal of your own." };
	const windows = usage.windows || [];
	const shown = windows.filter((w) => SHORT[w.id]);
	if (!shown.length) return { text: "Claude", level: "", tooltip: usage.error ? "The usage is not known: " + usage.error : "The plan's usage is not known yet" };
	let level = "";
	if (claude.blockedUntilMs > now || windows.some((w) => w.usedUp)) level = "error";
	else if (usage.status === "allowed_warning" || windows.some((w) => w.pace > WARN_PACE)) level = "warn";
	const lines = windows.map((w) => windowLine(w, now));
	if (claude.blockedUntilMs > now) lines.push("Used up; Claude frees " + resetText(claude.blockedUntilMs, now));
	for (const paced of claude.paced || []) lines.push(paced.profile + " picks " + (paced.model || "nothing") + " now (" + paced.why + ")");
	return { text: "Claude " + shown.map((w) => SHORT[w.id] + " " + percent(w.utilization)).join(" "), level, tooltip: lines.join("\n") };
}

// ai.models and ai.groups with the Claude profiles added (those already there kept as they are) and a `claude` group whose
// primary is claude-auto, falling back to the default group's primary when that is not Claude (a local model).
export function withClaudeModels(models, groups, defaultGroup) {
	const nextModels = Object.assign({}, CLAUDE_PROFILES, models);
	const nextGroups = Object.assign({}, groups);
	if (!nextGroups.claude) {
		const current = groups[defaultGroup] || groups[Object.keys(groups)[0]];
		let fallback = current && current.primary && current.primary.model;
		if (!fallback) fallback = Object.keys(models).find((name) => (models[name].provider || "openai") !== "claude-code");
		if (fallback && nextModels[fallback] && nextModels[fallback].provider === "claude-code") fallback = null;
		nextGroups.claude = { primary: Object.assign({ model: "claude-auto" }, fallback ? { fallback: [fallback] } : {}) };
	}
	return { models: nextModels, groups: nextGroups };
}

// The status bar item (statusBarItems ied.chat.claude): the aiusage topic as usageStatus says it.
export function ClaudeStatusView(ctx) {
	const render = (data) => {
		const status = usageStatus(data && data.claude);
		ctx.el.hidden = !status;
		if (!status) return;
		ctx.el.title = status.tooltip;
		replace(ctx.el, icon("pulse", status.level ? "c-" + status.level : null), h("span" + (status.level ? ".c-" + status.level : ""), null, status.text));
	};
	let last = null;
	const stop = ctx.ied.client.subscribe("aiusage", {
		snap: (data) => render(last = data),
		evt: (kind, data) => {
			if (kind === "usage.changed") render(last = data);
		},
	});
	render(last);
	return { dispose: stop };
}

function usageBar(window, now) {
	const used = Math.max(0, Math.min(100, window.utilization));
	const through = Math.max(0, Math.min(100, window.through * 100));
	const level = window.usedUp ? "err" : window.pace > WARN_PACE ? "warn" : "";
	return h("div.claude-bar" + (level ? "." + level : ""), { title: percent(window.utilization) + " used, " + percent(through) + " of the window gone (the even line)" },
		h("i.fill", { style: { width: used + "%" } }), window.resetsAtMs ? h("i.even", { style: { left: through + "%" } }) : null);
}

export class ClaudePage {
	constructor(ctx) {
		this.ctx = ctx;
		this.settings = ctx.settings;
		this.claude = null;
		this.refreshing = false;
		this.body = h("div.claude-page");
		replace(ctx.el,
			h("div.set-crumbs", null, "AI \u203a Claude \u00b7 ", h("span.mono", null, "ai.claude.command")),
			h("h1", null, "Claude"),
			h("p.set-lead", null, "Claude runs through your own Claude Code CLI, signed in to your Claude subscription (Pro or Max): its 5-hour session and its weekly limits are what IED shows and paces. IED never reads or stores Claude credentials and never runs Claude on an API key: sign in yourself, in a terminal of your own, with ", h("span.mono", null, "claude auth login"), "."),
			this.body);
		this.stop = ctx.ied.client.subscribe("aiusage", {
			snap: (data) => this.show(data),
			evt: (kind, data) => kind === "usage.changed" && this.show(data),
		});
		this.stopSettings = this.settings.onChange(() => this.render());
		this.render();
	}

	show(data) {
		this.claude = data && data.claude;
		this.render();
	}

	render() {
		const claude = this.claude;
		if (!claude) {
			replace(this.body, h("p.muted", null, icon("loading", "spin"), " Asking the server\u2026"));
			return;
		}
		const now = claude.nowMs || Date.now();
		replace(this.body, this.cliCard(claude), this.usageCard(claude, now), this.pacingCard(claude), this.setupBar(claude));
	}

	cliCard(claude) {
		const cli = claude.cli || {};
		const state = !cli.path ? ["err", "not found"] : !cli.signIn ? ["", "not asked yet"] : onSubscription(claude) ? ["ok", "on your subscription"] : cli.signIn === "signed_in" ? ["err", "signed in with " + cli.authMethod + ", not a subscription"] : ["err", "not signed in"];
		const usage = claude.usage || {};
		return h("div.set-model.claude-cli", null,
			h("div.set-model-head", null, h("b", null, "Claude Code"), h("span.chip" + (state[0] ? "." + state[0] : ""), null, state[1]),
				usage.plan ? h("span.chip", null, usage.plan + " plan") : null, h("span.grow"),
				h("button.btn", { disabled: this.refreshing || claude.refreshing, on: { click: () => this.refresh() } }, icon(this.refreshing || claude.refreshing ? "loading" : "refresh", this.refreshing || claude.refreshing ? "spin" : null), "Refresh")),
			h("div.set-model-facts", null,
				cli.path ? h("span.mono", { title: cli.path }, cli.path) : h("span", null, "no claude.exe on PATH, in %USERPROFILE%\\.local\\bin or the Claude desktop app"),
				cli.version ? h("span", null, "version " + cli.version) : null,
				cli.authMethod && cli.authMethod !== "none" ? h("span", null, "signs in with " + cli.authMethod) : null),
			cli.path && cli.signIn && !onSubscription(claude) ? h("div.set-probe.bad", null, icon("error"),
				h("span", null, "Claude turns are refused until it is signed in to your Claude subscription. In a terminal of your own run ", h("span.mono", null, "\"" + cli.path + "\" auth login"), ".")) : null);
	}

	usageCard(claude, now) {
		const usage = claude.usage || {};
		const windows = usage.windows || [];
		const rows = windows.map((w) => h("div.claude-window", { dataset: { window: w.id } },
			h("span.claude-label", null, w.label), usageBar(w, now),
			h("span.claude-num", null, percent(w.utilization)),
			h("span.claude-pace" + (w.usedUp ? ".c-error" : w.pace > WARN_PACE ? ".c-warn" : ""), { title: "1.00x lasts exactly to the reset" }, w.usedUp ? "used up" : "pace " + w.pace.toFixed(2) + "x"),
			h("span.muted", null, w.resetsAtMs ? "resets " + resetText(w.resetsAtMs, now) : "")));
		const notes = [];
		if (claude.blockedUntilMs > now) notes.push(h("div.set-probe.bad", null, icon("error"), "Claude is used up; it frees " + resetText(claude.blockedUntilMs, now) + ". Turns go to their group's fallback meanwhile."));
		else if (usage.status === "allowed_warning") notes.push(h("div.set-probe.warn", null, icon("warning"), "Claude Code warns that " + (usage.limitedBy || "a window") + " is near its limit."));
		if (usage.error) notes.push(h("div.set-probe.warn", null, icon("warning"), "The last ask failed: " + usage.error));
		if (usage.extraEnabled) notes.push(h("div.muted", null, "Extra usage is on" + (usage.extraUtilization >= 0 ? " (" + percent(usage.extraUtilization) + " of its monthly limit used)" : "") + "."));
		return h("div.set-model.claude-usage", null,
			h("div.set-model-head", null, h("b", null, "Usage"), h("span.grow"), usage.updatedMs ? h("span.muted", null, "asked " + new Date(usage.updatedMs).toLocaleTimeString()) : null),
			rows.length ? h("div.claude-windows", null, ...rows) : h("p.muted", null, usage.known && !usage.available ? "This sign-in has no plan limits to show." : "The plan's usage is not known yet."),
			...notes);
	}

	pacingCard(claude) {
		const models = this.settings.value("ai.models").value || {};
		const paced = claude.paced || [];
		if (!paced.length) return null;
		return h("div.set-model.claude-pacing", null,
			h("div.set-model-head", null, h("b", null, "Pacing")),
			h("p.muted", null, "A model with \"model\": \"auto\" picks, each turn, the strongest model of its ladder whose pace is within its limit (1.00x for the first, 1.25x for the second, 1.50x for the third), so the plan's usage lasts to its reset."),
			...paced.map((p) => h("div.claude-paced", { dataset: { profile: p.profile } },
				h("b", null, p.profile), h("span.mono", null, (p.ladder || []).join(" > ")),
				h("select.input-field", { "aria-label": "Pace " + p.profile + " against", on: { change: (event) => this.setTarget(p.profile, event.target.value, models) } },
					...TARGETS.map(([value, label]) => h("option", Object.assign({ value }, value === p.target ? { selected: true } : {}), label))),
				h("span", null, "now ", h("b", null, p.model || "none"), " - " + p.why))));
	}

	setupBar(claude) {
		const models = this.settings.value("ai.models").value || {};
		const missing = Object.keys(CLAUDE_PROFILES).filter((name) => !models[name]);
		return h("div.set-bar", null,
			missing.length ? h("button.btn" + (claude.configured ? "" : ".primary"), { on: { click: () => this.addModels() } }, icon("add"), claude.configured ? "Add the other Claude models" : "Add Claude models") : null,
			h("span.muted", null, claude.configured ? "Claude models are in Settings > AI > Models; the claude group uses claude-auto." : "Adds claude-auto (paced), claude-fable, claude-opus, claude-sonnet and claude-haiku to ai.models and a claude group."));
	}

	async refresh() {
		this.refreshing = true;
		this.render();
		try {
			const data = await this.ctx.ied.client.request("ai.usage", { refresh: true });
			this.claude = data.claude;
		}
		catch (e) {
			this.ctx.ied.toast(e.message, { level: "error" });
		}
		this.refreshing = false;
		this.render();
	}

	async setTarget(profile, target, models) {
		const entry = Object.assign({}, models[profile]);
		entry.pacing = Object.assign({}, entry.pacing || {}, { target });
		const error = await this.settings.set("ai.models", Object.assign({}, models, { [profile]: entry }), "app");
		if (error) this.ctx.ied.toast(error, { level: "error" });
		else this.refresh();
	}

	async addModels() {
		const made = withClaudeModels(this.settings.value("ai.models").value || {}, this.settings.value("ai.groups").value || {}, this.settings.value("ai.defaultGroup").value || "");
		const error = (await this.settings.set("ai.models", made.models, "app")) || (await this.settings.set("ai.groups", made.groups, "app"));
		if (error) this.ctx.ied.toast(error, { level: "error" });
		else this.refresh();
	}

	dispose() {
		this.stop();
		this.stopSettings();
	}
}
