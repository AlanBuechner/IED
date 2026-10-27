// claudeusage.js - Claude's plan usage in the UI (docs/04 "The Claude Code provider"), from the server's aiusage topic: the
// status bar's "Claude 5h 42% wk 61%" (yellow while a window runs ahead of pace, red when one is used up or the CLI is not
// on a subscription) - resting the pointer on it shows each window's bar, a click (a tap) opens the card with every
// window's bar, pace and reset, the chains as pacing has them now and Open Claude settings - and Settings > AI > Claude:
// the CLI (where it is, its version, whether it is signed in to a Claude subscription; IED never runs Claude on an API
// key) with the one step that sets it up (Install, Sign in: the CLI's own windows, docs/04 "Setting it up"), each window as
// a bar with the even line marked (and where it stands through each weekend when the week's pace counts the work days,
// ai.claude.paceDays), its pace and when it resets, the weekly pace's days, each group chain with a Claude model as pacing
// has it now (docs/04 "Pacing"), Refresh, and Add Claude models.

import { h, icon, replace } from "/ui-kit/dom.js";
import { confirmDialog } from "/ui-kit/dialog.js";
import { hoverCard } from "/ui-kit/hovercard.js";
import { paceLabel } from "./pacing.js";

const SHORT = { five_hour: "5h", seven_day: "wk" };
export const WARN_PACE = 1.25;
// The profiles the server's Add Claude models (ai.claude.setup) adds.
export const CLAUDE_PROFILE_NAMES = ["claude-opus", "claude-sonnet", "claude-haiku", "claude-fable"];
export const INSTALL_SCRIPT = "irm https://claude.ai/install.ps1 | iex";
// ai.claude.workDays' days, Monday first as a week reads, and the schema's default.
export const WEEK_DAYS = [["mon", "Mon"], ["tue", "Tue"], ["wed", "Wed"], ["thu", "Thu"], ["fri", "Fri"], ["sat", "Sat"], ["sun", "Sun"]];
export const WORK_DAYS = ["mon", "tue", "wed", "thu", "fri"];
export const CLAUDE_SETTINGS = { page: "custom:ied.chat.claude" };
const ELLIPSIS = "\u2026";
const NEWLINE = String.fromCharCode(10);

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
	if (window.resetsAtMs) line += ", resets " + resetText(window.resetsAtMs, now) + ", pace " + window.pace.toFixed(2) + "x" + (window.workWeek ? " over its work days" : "");
	if (window.usedUp) line += " - used up";
	return line;
}

// The windows used up, as the tooltip and the cards say it (docs/04 "Usage"): IED leaves no Claude model out of a turn for
// them - Claude says whether it has room (a plan upgraded mid-week, a reading that is off) - so it says what happens
// instead; null when none is.
export function usedUpLine(claude) {
	const spent = (((claude && claude.usage) || {}).windows || []).filter((w) => w.usedUp).map((w) => w.label);
	if (!spent.length) return null;
	return spent.join(", ") + " used up: each turn still asks Claude first; only Claude's refusal moves it to the next model of its chain, and the next turn asks again";
}

// A chain as the tooltip says it: "claude / primary: claude-sonnet now (claude-opus over its pace)".
export function chainLine(chain) {
	const why = (chain.places || []).filter((place) => place.state === "over").map((place) => place.model + " over its pace").join(", ");
	return chain.group + " / " + chain.role + ": " + (chain.now || "nothing") + " now" + (why ? " (" + why + ")" : "");
}

// A day off's start or end as a bar's title says it: "Sat 00:00".
function dayTime(ms) {
	return new Date(ms).toLocaleString([], { weekday: "short", hour: "2-digit", minute: "2-digit", hour12: false });
}

// The days off a bar marks: those inside the window, where the even line stands through each (its ends are the bar's).
export function barDaysOff(window) {
	return (window.daysOff || []).filter((off) => off.at > 0 && off.at < 1);
}

// The status bar item for the snapshot's `claude`: {text, level ("", "warn", "error"), tooltip, message (why, when there are
// no windows to show)}; null when no claude-code model is set up.
export function usageStatus(claude) {
	if (!claude || !claude.configured) return null;
	const usage = claude.usage || {};
	const now = claude.nowMs || Date.now();
	const cli = claude.cli || {};
	const told = (text, level, message) => ({ text, level, tooltip: message, message });
	if (!cli.path && usage.error) return told("Claude: no CLI", "error", usage.error);
	if (cli.signIn && !onSubscription(claude))
		return told("Claude: not on a subscription", "error", "Claude Code signs in with " + (cli.signIn === "signed_in" ? cli.authMethod : "nothing") + ". IED runs Claude on your subscription only: sign in from Settings > AI > Claude.");
	const windows = usage.windows || [];
	const shown = windows.filter((w) => SHORT[w.id]);
	if (!shown.length) return told("Claude", "", usage.error ? "The usage is not known: " + usage.error : "The plan's usage is not known yet");
	let level = "";
	if (windows.some((w) => w.usedUp)) level = "error";
	else if (usage.status === "allowed_warning" || windows.some((w) => w.pace > WARN_PACE)) level = "warn";
	const lines = windows.map((w) => windowLine(w, now));
	const spent = usedUpLine(claude);
	if (spent) lines.push(spent);
	for (const chain of claude.chains || []) lines.push(chainLine(chain));
	return { text: "Claude " + shown.map((w) => SHORT[w.id] + " " + percent(w.utilization)).join(" "), level, tooltip: lines.join("\n") };
}

// The one step that sets Claude up now (docs/04 "Setting it up"): "not_offered" (a distributed build), "installing",
// "install" (no CLI), "signing_in", "sign_in" (not on a subscription), "asking" (the CLI not asked yet) or "ready".
export function setupStep(claude) {
	const cli = (claude && claude.cli) || {};
	if (claude && claude.offered === false) return "not_offered";
	if (cli.installing) return "installing";
	if (cli.asked === false) return "asking";
	if (!cli.path) return "install";
	if (cli.signingIn) return "signing_in";
	if (!cli.signIn) return "asking";
	return onSubscription(claude) ? "ready" : "sign_in";
}

// The status bar item (statusBarItems ied.chat.claude): the aiusage topic as usageStatus says it, its bars (usageBars) while
// the pointer rests on it, and the card usageCard draws when it is clicked or tapped - the statusbar hands the item's clicks
// to the view's click(); the item's command (the palette's Chat: Claude Usage) opens Settings > AI > Claude.
export function ClaudeStatusView(ctx) {
	let last = null;
	const openSettings = () => {
		card.close();
		ctx.ied.commands.run("shell.settings", CLAUDE_SETTINGS);
	};
	const card = hoverCard(ctx.el, () => usageBars(last && last.claude), { pinned: () => usageCard(last && last.claude, openSettings), label: "Claude usage" });
	ctx.el.setAttribute("aria-haspopup", "dialog");
	const render = (data) => {
		const status = usageStatus(data && data.claude);
		ctx.el.hidden = !status;
		if (status) {
			ctx.el.setAttribute("aria-label", status.text + ". " + status.tooltip.split(NEWLINE).join(". "));
			replace(ctx.el, icon("pulse", status.level ? "c-" + status.level : null), h("span" + (status.level ? ".c-" + status.level : ""), null, status.text));
		}
		card.refresh();
	};
	const stop = ctx.ied.client.subscribe("aiusage", {
		snap: (data) => render(last = data),
		evt: (kind, data) => {
			if (kind === "usage.changed") render(last = data);
		},
	});
	render(last);
	return {
		click: () => card.toggle(),
		dispose: () => {
			stop();
			card.dispose();
		},
	};
}

// What resting the pointer on the status item shows: a bar per window - its name, how much is used and the even line (with
// the weekends' marks) - or why there is none; null while the item is hidden.
export function usageBars(claude) {
	const status = usageStatus(claude);
	if (!status) return null;
	if (status.message) return h("div.claude-hover.claude-bars", null, h("div.claude-hover-note" + (status.level ? ".c-" + status.level : ""), null, status.message));
	const now = claude.nowMs || Date.now();
	return h("div.claude-hover.claude-bars", null,
		...((claude.usage || {}).windows || []).map((w) => h("div.claude-bars-row", { dataset: { window: w.id } },
			h("span.claude-label", null, w.label), usageBar(w, now), h("span.claude-num", null, percent(w.utilization)))),
		h("div.claude-hover-foot", null, "Click for the details"));
}

// The card a click on the status item opens: drawn as Settings > AI > Claude draws the usage - each window's bar with the
// even line, its pace and when it resets, what Claude's usage means for a turn now, and each chain with the model a turn
// uses now and why the ones before it are passed over - with Open Claude settings (`openSettings`) at its foot; null while
// the item is hidden.
export function usageCard(claude, openSettings) {
	const status = usageStatus(claude);
	if (!status) return null;
	const usage = claude.usage || {};
	const now = claude.nowMs || Date.now();
	const head = h("div.claude-hover-head", null, icon("pulse", status.level ? "c-" + status.level : null), h("b", null, "Claude"),
		usage.plan ? h("span.chip", null, usage.plan + " plan") : null, h("span.grow"),
		usage.updatedMs ? h("span.muted", null, "asked " + new Date(usage.updatedMs).toLocaleTimeString([], { hour: "numeric", minute: "2-digit" })) : null);
	const open = h("button.btn.claude-open-settings", { on: { click: () => openSettings && openSettings() } }, icon("settings-gear"), "Open Claude settings");
	if (status.message) return h("div.claude-hover", null, head, h("div.claude-hover-note" + (status.level ? ".c-" + status.level : ""), null, status.message), open);
	return h("div.claude-hover", null, head,
		h("div.claude-hover-windows", null, ...(usage.windows || []).map((w) => hoverWindow(w, now))),
		...usageNotes(claude, usage, now),
		...(claude.chains || []).map((chain) => hoverChain(chain)),
		h("div.claude-hover-foot", null, usage.paceDays === "workweek"
			? "The solid mark on a bar is an even pace to the reset over the work days (1.00x lasts exactly to it); a dashed one is where it stands through a weekend."
			: "The mark on a bar is an even pace to the reset: 1.00x lasts exactly to it."),
		open);
}

function paceClass(window) {
	return window.usedUp ? ".c-error" : window.pace > WARN_PACE ? ".c-warn" : "";
}

function hoverWindow(window, now) {
	return h("div.claude-hover-window", { dataset: { window: window.id } },
		h("div.claude-hover-line", null, h("span.claude-label", null, window.label), h("span.claude-num", null, percent(window.utilization))),
		usageBar(window, now),
		h("div.claude-hover-line", null, h("span.claude-pace" + paceClass(window), null, window.usedUp ? "used up" : "pace " + window.pace.toFixed(2) + "x"),
			window.resetsAtMs ? h("span.muted", null, "resets " + resetText(window.resetsAtMs, now)) : null));
}

// What the usage means for a turn now, or warns of: a window used up, the CLI's warning, an ask that failed.
function usageNotes(claude, usage, now) {
	const notes = [];
	const spent = usedUpLine(claude);
	if (spent) notes.push(h("div.set-probe.bad", null, icon("error"), h("span", null, spent + ".")));
	else if (usage.status === "allowed_warning") notes.push(h("div.set-probe.warn", null, icon("warning"), "Claude Code warns that " + (usage.limitedBy || "a window") + " is near its limit."));
	if (usage.error) notes.push(h("div.set-probe.warn", null, icon("warning"), "The last ask failed: " + usage.error));
	return notes;
}

function hoverChain(chain) {
	const passed = (chain.places || []).filter((place) => place.state === "over" && place.why);
	return h("div.claude-hover-chain", { dataset: { group: chain.group, role: chain.role } },
		h("div.claude-hover-line", null, h("span", null, h("b", null, chain.group), " / " + chain.role), h("span.muted", null, "now ", h("b", null, chain.now || "nothing"))),
		h("span.claude-places", null, ...(chain.places || []).map((place, i) => placeChip(place, i, chain.now, true))),
		...passed.map((place) => h("div.claude-hover-why", null, place.model + ": " + place.why)));
}

// A chain's place: the model a turn uses now in blue, one over its pace yellow; `brief` leaves its pace out.
function placeChip(place, index, now, brief) {
	const state = place.state === "over" ? ".warn" : place.model === now ? ".info" : "";
	const pace = place.claude && !brief ? paceLabel(place.pace) : "";
	const why = place.why ? (place.model === now ? place.why + "; a turn still uses it: nothing earlier fits" : place.why) : place.model === now ? "A turn uses it now" : "";
	return h("span.chip.claude-place" + state, { dataset: { place: place.model, state: place.state }, title: why },
		(index > 0 ? "> " : "") + place.model, pace ? h("span.muted", null, " " + pace) : null);
}

// A window's bar: the use, the even line where an even pace to the reset is now (over the work days when the week's pace
// counts them) and, then, a dashed mark where the even line stands through each weekend - the work week's end.
export function usageBar(window, now) {
	const used = Math.max(0, Math.min(100, window.utilization));
	const through = Math.max(0, Math.min(100, window.through * 100));
	const level = window.usedUp ? "err" : window.pace > WARN_PACE ? "warn" : "";
	const offs = barDaysOff(window);
	const gone = percent(through) + (window.workWeek ? " of the work time gone (the even line)" : " of the window gone (the even line)");
	const weekends = offs.map((off) => "; weekend " + dayTime(off.startMs) + " - " + dayTime(off.endMs) + " at " + percent(off.at * 100)).join("");
	return h("div.claude-bar" + (level ? "." + level : ""), { title: percent(window.utilization) + " used, " + gone + weekends },
		h("i.fill", { style: { width: used + "%" } }),
		...offs.map((off) => h("i.off", { dataset: { at: off.at.toFixed(3) }, style: { left: off.at * 100 + "%" } })),
		window.resetsAtMs ? h("i.even", { style: { left: through + "%" } }) : null);
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
			h("p.set-lead", null, "Claude runs through your own Claude Code CLI on your Claude subscription (Pro or Max): its 5-hour session and its weekly limits are what IED shows and paces. Sign in once below - Claude Code's own login opens in a window of its own and in your browser; IED never sees or stores your credentials and never runs Claude on an API key - and IED sets the Claude models up by itself."),
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
		// a server that just started asks the CLI on its own after a while; the page asks now
		if (this.claude && this.claude.cli && this.claude.cli.asked === false && !this.askedOnce) {
			this.askedOnce = true;
			this.refresh();
		}
	}

	render() {
		const claude = this.claude;
		if (!claude) {
			replace(this.body, h("p.muted", null, icon("loading", "spin"), " Asking the server\u2026"));
			return;
		}
		const now = claude.nowMs || Date.now();
		if (claude.offered === false) {
			replace(this.body, h("div.set-probe.warn", null, icon("warning"), h("span", null, "Claude through a Pro or Max login is not in distributed builds of IED: Anthropic's terms do not let a third-party product offer claude.ai login without its approval. Your own builds of IED (Development) have it.")));
			return;
		}
		replace(this.body, this.cliCard(claude), this.usageCard(claude, now), this.paceDaysCard(), this.pacingCard(claude), this.setupBar(claude), this.settingRows());
	}

	// ai.claude.paceDays and ai.claude.workDays: what a weekly window's pace counts - every day, or the work days.
	paceDaysCard() {
		const days = this.settings.value("ai.claude.paceDays").value === "workweek" ? "workweek" : "week";
		const work = this.settings.value("ai.claude.workDays").value || WORK_DAYS;
		const choice = (id, label) => h("button" + (days === id ? ".on" : ""), { type: "button", role: "radio", "aria-checked": days === id ? "true" : "false", dataset: { days: id },
			on: { click: () => days !== id && this.write("ai.claude.paceDays", id) } }, label);
		const day = ([id, label]) => {
			const on = work.includes(id);
			return h("button.chip.claude-day" + (on ? ".on" : ""), { type: "button", "aria-pressed": on ? "true" : "false", dataset: { day: id }, title: on ? "A work day: its time counts" : "A day off: its time does not count",
				on: { click: () => this.write("ai.claude.workDays", WEEK_DAYS.map(([d]) => d).filter((d) => (d === id ? !on : work.includes(d)))) } }, label);
		};
		return h("div.set-model.claude-pace-days", null,
			h("div.set-model-head", null, h("b", null, "Weekly pace"), h("span.grow"), h("span.mono.muted", null, "ai.claude.paceDays")),
			h("span.set-seg.claude-segs", { role: "radiogroup", "aria-label": "What a week's pace counts" }, choice("week", "Every day"), choice("workweek", "Work days only")),
			days === "workweek" ? h("div.claude-days", { role: "group", "aria-label": "Work days" }, ...WEEK_DAYS.map(day)) : null,
			h("p.muted", null, days === "workweek"
				? "A weekly window's pace counts only the work days, midnight to midnight in the time zone of the computer IED runs on: it stands still over a weekend, wherever the week resets. The bars still show the whole week, with a dashed mark where the even line stands through each weekend. The 5-hour session counts every hour."
				: "A weekly window's pace counts all seven days. Work days only paces the week over the days you work, so a weekend off does not make the week look behind."));
	}

	// The page's other settings as the settings window draws them.
	settingRows() {
		if (typeof this.settings.row !== "function") return null;
		return h("div.claude-settings", null, h("div.set-sub", null, "Settings"), this.settings.row("ai.claude.command"), this.settings.row("ai.claude.usageRefreshMinutes"));
	}

	async write(key, value) {
		const error = await this.settings.set(key, value, "app");
		if (error) {
			this.ctx.ied.toast(error, { level: "error" });
			return;
		}
		try {
			this.show(await this.ctx.ied.client.request("ai.usage", {}));
		}
		catch (e) {
			this.ctx.ied.toast(e.message, { level: "error" });
		}
	}

	cliCard(claude) {
		const cli = claude.cli || {};
		const state = cli.asked === false ? ["", "looking for it"] : !cli.path ? ["err", "not found"] : !cli.signIn ? ["", "not asked yet"] : onSubscription(claude) ? ["ok", "on your subscription"] : cli.signIn === "signed_in" ? ["err", "signed in with " + cli.authMethod + ", not a subscription"] : ["err", "not signed in"];
		const usage = claude.usage || {};
		return h("div.set-model.claude-cli", null,
			h("div.set-model-head", null, h("b", null, "Claude Code"), h("span.chip" + (state[0] ? "." + state[0] : ""), null, state[1]),
				usage.plan ? h("span.chip", null, usage.plan + " plan") : null, h("span.grow"),
				h("button.btn", { disabled: this.refreshing || claude.refreshing, on: { click: () => this.refresh() } }, icon(this.refreshing || claude.refreshing ? "loading" : "refresh", this.refreshing || claude.refreshing ? "spin" : null), "Refresh")),
			h("div.set-model-facts", null,
				cli.path ? h("span.mono", { title: cli.path }, cli.path) : cli.asked === false ? null : h("span", null, "no claude.exe on PATH, in %USERPROFILE%\\.local\\bin, WinGet's links or the Claude desktop app"),
				cli.version ? h("span", null, "version " + cli.version) : null,
				cli.authMethod && cli.authMethod !== "none" ? h("span", null, "signs in with " + cli.authMethod) : null),
			this.stepRow(claude));
	}

	// What it takes to get Claude running from here: one button, or what IED is waiting for.
	stepRow(claude) {
		const step = setupStep(claude);
		const cli = claude.cli || {};
		if (step === "install") return h("div.claude-step", null,
			h("span", null, "Claude Code is not on this computer. Install it - or the Claude desktop app, which brings it."),
			h("button.btn.primary", { on: { click: () => this.install() } }, icon("cloud-download"), "Install Claude Code"));
		if (step === "installing") return h("div.claude-step", null, icon("loading", "spin"), h("span", null, "Installing in the PowerShell window that opened on the computer IED runs on" + ELLIPSIS));
		if (step === "sign_in") return h("div.claude-step", null,
			h("span", null, cli.signIn === "signed_in" ? "It signs in with " + cli.authMethod + ", not your subscription: sign in with your Claude account instead." : "Sign in with your Claude account (Pro or Max) to use Claude here."),
			h("button.btn.primary", { on: { click: () => this.signIn() } }, icon("account"), "Sign in with your Claude subscription"));
		if (step === "signing_in") return h("div.claude-step", null, icon("loading", "spin"), h("span", null, "Finish signing in in the Claude Code window and the browser page that opened on the computer IED runs on" + ELLIPSIS));
		if (step === "asking") return h("div.claude-step", null, icon("loading", "spin"), h("span", null, "Looking for Claude Code and asking it how it signs in" + ELLIPSIS));
		return null;
	}

	usageCard(claude, now) {
		const usage = claude.usage || {};
		const windows = usage.windows || [];
		const rows = windows.map((w) => h("div.claude-window", { dataset: { window: w.id } },
			h("span.claude-label", null, w.label), usageBar(w, now),
			h("span.claude-num", null, percent(w.utilization)),
			h("span.claude-pace" + paceClass(w), { title: (w.workWeek ? "Over its work days: " : "") + "1.00x lasts exactly to the reset" }, w.usedUp ? "used up" : "pace " + w.pace.toFixed(2) + "x"),
			h("span.muted", null, w.resetsAtMs ? "resets " + resetText(w.resetsAtMs, now) : "")));
		const notes = usageNotes(claude, usage, now);
		if (usage.extraEnabled) notes.push(h("div.muted", null, "Extra usage is on" + (usage.extraUtilization >= 0 ? " (" + percent(usage.extraUtilization) + " of its monthly limit used)" : "") + "."));
		return h("div.set-model.claude-usage", null,
			h("div.set-model-head", null, h("b", null, "Usage"), h("span.grow"), usage.updatedMs ? h("span.muted", null, "asked " + new Date(usage.updatedMs).toLocaleTimeString()) : null),
			rows.length ? h("div.claude-windows", null, ...rows) : h("p.muted", null, usage.known && !usage.available ? "This sign-in has no plan limits to show." : onSubscription(claude) ? "The plan's usage is not known yet." : "Sign in with your subscription to see its usage."),
			...notes);
	}

	// Each group role whose chain has a Claude model: its places in order - the one a turn uses now marked, those over their
	// pace with why - and the way to Model Groups, where the chains are edited.
	pacingCard(claude) {
		const chains = claude.chains || [];
		if (!chains.length) return null;
		return h("div.set-model.claude-pacing", null,
			h("div.set-model-head", null, h("b", null, "Pacing"), h("span.grow"),
				h("button.btn", { on: { click: () => this.ctx.ied.commands.run("shell.settings", { page: "custom:ied.chat.groups" }) } }, icon("edit"), "Edit in Model Groups")),
			h("p.muted", null, "A turn tries its role's chain in order. A Claude model with a pace is passed over while your plan runs ahead of it, so the session or the week lasts; one without is used whatever the pace. A window used up keeps no model out: Claude itself refuses a request it has no room for, and the turn goes on down the chain. A chat's mode picks the role: Ask primary, Plan planning, Agent implementation."),
			...chains.map((chain) => h("div.claude-chain", { dataset: { group: chain.group, role: chain.role } },
				h("span.claude-chain-name", null, h("b", null, chain.group), " / " + chain.role),
				h("span.claude-places", null, ...(chain.places || []).map((place, i) => placeChip(place, i, chain.now))),
				h("span.claude-chain-now", null, "now ", h("b", null, chain.now || "nothing")))));
	}

	setupBar(claude) {
		if (setupStep(claude) !== "ready") return null;
		const models = this.settings.value("ai.models").value || {};
		const missing = CLAUDE_PROFILE_NAMES.filter((name) => !models[name]);
		return h("div.set-bar", null,
			missing.length ? h("button.btn" + (claude.configured ? "" : ".primary"), { on: { click: () => this.addModels() } }, icon("add"), claude.configured ? "Add the other Claude models" : "Add Claude models") : null,
			h("span.muted", null, claude.configured ? "Claude models are in Settings > AI > Models; their chains and paces in Model Groups." : "Adds claude-opus, claude-sonnet, claude-haiku and claude-fable to ai.models and a claude group whose chain steps down from Opus as your plan runs ahead of pace, then to Fable."));
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

	async ask(method) {
		try {
			this.show(await this.ctx.ied.client.request(method, {}));
		}
		catch (e) {
			this.ctx.ied.toast(e.message, { level: "error" });
		}
	}

	addModels() {
		return this.ask("ai.claude.setup");
	}

	signIn() {
		return this.ask("ai.claude.signIn");
	}

	async install() {
		const ok = await confirmDialog({ title: "Install Claude Code", message: "IED opens a PowerShell window on the computer it runs on and runs Anthropic's installer there: " + INSTALL_SCRIPT, confirm: "Install" });
		if (ok) await this.ask("ai.claude.install");
	}

	dispose() {
		this.stop();
		this.stopSettings();
	}
}
