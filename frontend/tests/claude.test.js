// claude.test.js - Claude's plan usage in the chat plugin's UI (docs/04 "The Claude Code provider"): the status bar's text
// and color from the aiusage snapshot, its bars on hover and the card its click opens, a window's line and bar (the work
// week's marks), when a window frees, and the step that sets Claude up.

import { test, assertEqual } from "./harness.js";
import { CLAUDE_PROFILE_NAMES, ClaudeStatusView, chainLine, onSubscription, resetText, setupStep, usageBar, usageBars, usageCard, usageStatus, usedUpLine, windowLine } from "/plugins/ied.chat/ui/claudeusage.js";
import { ChatModel } from "/plugins/ied.chat/ui/chatmodel.js";

const NOW = 1761150000000;
const HOUR = 3600000;

function claude(windows, extra) {
	return Object.assign({
		configured: true, nowMs: NOW, chains: [],
		cli: { path: "C:/claude.exe", version: "2.1.304", signIn: "signed_in", authMethod: "claude.ai", subscription: true },
		usage: { known: true, available: true, plan: "max", status: "allowed", windows },
	}, extra || {});
}

function window(id, label, utilization, pace, extra) {
	return Object.assign({ id, label, utilization, pace, through: 0.5, resetsAtMs: NOW + 2 * HOUR, lengthMs: 5 * HOUR, usedUp: false }, extra || {});
}

test("claude: when a window frees", () => {
	assertEqual(resetText(NOW + 35 * 60000, NOW), "in 35 min");
	assertEqual(resetText(NOW + 134 * 60000, NOW), "in 2 h 14 min");
	assertEqual(resetText(NOW + (3 * 24 + 4) * HOUR, NOW), "in 3 d 4 h");
	assertEqual(resetText(NOW - 1, NOW), "now");
});

test("claude: the status bar says the session and the week", () => {
	assertEqual(usageStatus(null), null);
	assertEqual(usageStatus({ configured: false }), null, "no claude-code model set up: hidden");
	const calm = usageStatus(claude([window("five_hour", "Session (5 h)", 42.4, 0.85), window("seven_day", "Week", 61, 1.07), window("seven_day_opus", "Opus week", 10, 0.2)]));
	assertEqual([calm.text, calm.level], ["Claude 5h 42% wk 61%", ""]);
	assertEqual(calm.tooltip.split("\n")[0], "Session (5 h): 42% used, resets in 2 h 0 min, pace 0.85x");
	const ahead = usageStatus(claude([window("five_hour", "Session (5 h)", 80, 1.6), window("seven_day", "Week", 20, 0.4)]));
	assertEqual(ahead.level, "warn", "a window ahead of pace");
	const warned = usageStatus(claude([window("seven_day", "Week", 70, 1.1)], { usage: { known: true, available: true, status: "allowed_warning", windows: [window("seven_day", "Week", 70, 1.1)] } }));
	assertEqual(warned.level, "warn", "the CLI warns");
	const spent = usageStatus(claude([window("five_hour", "Session (5 h)", 100, 2, { usedUp: true })]));
	assertEqual(spent.level, "error");
	assertEqual(spent.tooltip.includes("Session (5 h) used up: each turn still asks Claude first; only Claude's refusal moves it to the next model of its chain"), true, "a window used up keeps no turn from asking Claude");
	const week = usageStatus(claude([window("seven_day", "Week", 100, 2, { usedUp: true }), window("model:fable", "Fable week", 20, 0.4, { separate: true })]));
	assertEqual(week.level, "error", "the shared week used up");
	assertEqual([week.tooltip.includes("still has room"), week.tooltip.includes("Week used up: each turn still asks Claude first")], [false, true], "nothing says Fable runs on: the shared week limits it too");
	assertEqual(usedUpLine(claude([window("seven_day", "Week", 60, 1)])), null, "none used up: nothing to say");
	const chain = { group: "claude", role: "primary", now: "claude-sonnet", places: [
		{ model: "claude-opus", claude: true, pace: { max: 1, target: "both" }, state: "over", why: "the week is at 1.20x pace, over its 1.00x" },
		{ model: "claude-sonnet", claude: true, pace: { max: 1.25, target: "both" }, state: "use" },
		{ model: "qwen", claude: false, state: "use" }] };
	const paced = usageStatus(claude([window("seven_day", "Week", 60, 1.2)], { chains: [chain] }));
	assertEqual(paced.tooltip.includes("claude / primary: claude-sonnet now (claude-opus over its pace)"), true);
	assertEqual(chainLine(Object.assign({}, chain, { now: "claude-opus", places: [{ model: "claude-opus", state: "use" }, { model: "qwen", state: "use" }] })), "claude / primary: claude-opus now", "a place is in use or over its pace, never left out");
});

test("claude: the status item's click card draws each window's bar, its pace and the chains, and opens the settings", () => {
	assertEqual(usageCard({ configured: false }), null, "hidden item, no card");
	const chain = { group: "claude", role: "primary", now: "claude-sonnet", places: [
		{ model: "claude-opus", claude: true, pace: { max: 1, target: "both" }, state: "over", why: "the session is at 1.60x pace, over its 1.00x" },
		{ model: "claude-sonnet", claude: true, pace: { max: 1.5, target: "both" }, state: "use" }] };
	let opened = 0;
	const card = usageCard(claude([window("five_hour", "Session (5 h)", 80, 1.6), window("seven_day", "Week", 30, 0.6, { through: 0.25 })], { chains: [chain] }), () => opened++);
	const rows = [...card.querySelectorAll(".claude-hover-window")].map((row) => [row.dataset.window, row.querySelector(".claude-num").textContent,
		row.querySelector(".claude-pace").textContent, row.querySelector(".claude-pace").className, row.querySelector(".claude-bar").className,
		row.querySelector(".claude-bar .fill").style.width, row.querySelector(".claude-bar .even").style.left]);
	assertEqual(rows, [
		["five_hour", "80%", "pace 1.60x", "claude-pace c-warn", "claude-bar warn", "80%", "50%"],
		["seven_day", "30%", "pace 0.60x", "claude-pace", "claude-bar", "30%", "25%"]]);
	assertEqual(card.querySelector(".claude-hover-head .chip").textContent, "max plan");
	assertEqual([...card.querySelectorAll(".claude-place")].map((chip) => chip.textContent + "|" + chip.className), ["claude-opus|chip claude-place warn", "> claude-sonnet|chip claude-place info"], "paces left out");
	assertEqual(card.querySelector(".claude-hover-why").textContent, "claude-opus: the session is at 1.60x pace, over its 1.00x");
	card.querySelector(".claude-open-settings").click();
	assertEqual([card.querySelector(".claude-open-settings").textContent, opened], ["Open Claude settings", 1], "its foot opens Settings > AI > Claude");
	const spent = usageCard(claude([window("five_hour", "Session (5 h)", 100, 2, { usedUp: true })]), () => {});
	assertEqual([spent.querySelector(".claude-pace").textContent, spent.querySelector(".claude-bar").className], ["used up", "claude-bar err"]);
	assertEqual(spent.querySelector(".set-probe.bad").textContent, "Session (5 h) used up: each turn still asks Claude first; only Claude's refusal moves it to the next model of its chain, and the next turn asks again.");
	const week = usageCard(claude([window("seven_day", "Week", 100, 2, { usedUp: true }), window("model:fable", "Fable week", 20, 0.4, { separate: true })]), () => {});
	assertEqual(week.textContent.includes("still has room"), false, "the shared week used up: Fable is not said to run on");
	const keyed = usageCard(claude([], { cli: { path: "C:/claude.exe", signIn: "signed_in", authMethod: "api_key", subscription: false } }), () => {});
	assertEqual([keyed.querySelector(".claude-hover-note").className, keyed.querySelector(".claude-hover-note").textContent.startsWith("Claude Code signs in with api_key."), keyed.querySelector(".claude-hover-window"), !!keyed.querySelector(".claude-open-settings")],
		["claude-hover-note c-error", true, null, true], "no windows: why, and the way to the settings");
});

test("claude: resting on the status item shows a simple bar per window", () => {
	assertEqual(usageBars({ configured: false }), null);
	const bars = usageBars(claude([window("five_hour", "Session (5 h)", 80, 1.6), window("seven_day", "Week", 30, 0.6, { through: 0.25 })], { chains: [{ group: "claude", role: "primary", now: "claude-opus", places: [{ model: "claude-opus", claude: true, state: "use" }] }] }));
	assertEqual([...bars.querySelectorAll(".claude-bars-row")].map((row) => [row.dataset.window, row.querySelector(".claude-label").textContent, row.querySelector(".claude-num").textContent, row.querySelector(".claude-bar .even").style.left]),
		[["five_hour", "Session (5 h)", "80%", "50%"], ["seven_day", "Week", "30%", "25%"]]);
	assertEqual([bars.querySelector(".claude-place"), bars.querySelector(".claude-pace"), bars.querySelector(".claude-hover-foot").textContent], [null, null, "Click for the details"], "no chains or paces: those are the click's");
	const keyed = usageBars(claude([], { cli: { path: "C:/claude.exe", signIn: "signed_in", authMethod: "api_key", subscription: false } }));
	assertEqual(keyed.querySelector(".claude-hover-note").textContent.startsWith("Claude Code signs in with api_key."), true);
});

test("claude: a week paced over its work days marks where the even line stands through each weekend", () => {
	const start = NOW - 3 * 24 * HOUR;
	const week = window("seven_day", "Week", 48, 1, { through: 0.48, lengthMs: 7 * 24 * HOUR, resetsAtMs: start + 7 * 24 * HOUR, workWeek: true,
		daysOff: [{ startMs: start - 10 * HOUR, endMs: start, at: 0 }, { startMs: start + 58 * HOUR, endMs: start + 106 * HOUR, at: 0.4833 }] });
	const bar = usageBar(week, NOW);
	assertEqual([...bar.querySelectorAll(".off")].map((tick) => tick.style.left), ["48.33%"], "a tick inside the window; one at its start is the bar's edge");
	assertEqual(bar.title.includes("48% of the work time gone (the even line); weekend "), true, bar.title);
	assertEqual(windowLine(week, NOW).endsWith("pace 1.00x over its work days"), true);
	assertEqual(usageBar(window("seven_day", "Week", 48, 1), NOW).querySelectorAll(".off").length, 0, "a week paced over every day has no marks");
	const card = usageCard(claude([week], { usage: { known: true, available: true, plan: "max", status: "allowed", paceDays: "workweek", windows: [week] } }), () => {});
	assertEqual(card.querySelector(".claude-hover-foot").textContent.includes("a dashed one is where it stands through a weekend"), true);
});

test("claude: the status item's click opens its card, Escape or a click outside closes it", () => {
	const el = document.body.appendChild(document.createElement("button"));
	const topics = new Map();
	const ran = [];
	const subscribe = (topic, handlers) => {
		topics.set(topic, handlers);
		return () => topics.delete(topic);
	};
	const view = ClaudeStatusView({ el, ied: { client: { subscribe }, commands: { run: (id, args) => ran.push([id, args]) } } });
	try {
		topics.get("aiusage").snap({ claude: claude([window("five_hour", "Session (5 h)", 40, 0.8)]) });
		assertEqual([el.hidden, el.getAttribute("aria-haspopup"), el.getAttribute("aria-expanded")], [false, "dialog", "false"]);
		view.click();
		const card = document.querySelector(".hover-card.pinned");
		assertEqual([!!card, card && card.getAttribute("role"), el.getAttribute("aria-expanded"), document.activeElement && document.activeElement.textContent], [true, "dialog", "true", "Open Claude settings"], "a dialog, focus in it");
		topics.get("aiusage").evt("usage.changed", { claude: claude([window("five_hour", "Session (5 h)", 55, 1.1)]) });
		assertEqual(document.querySelector(".hover-card.pinned .claude-num").textContent, "55%", "it follows the usage");
		document.querySelector(".hover-card.pinned").dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
		assertEqual([document.querySelector(".hover-card.pinned"), document.activeElement === el, document.querySelector(".hover-card")], [null, true, null], "Escape closes it, focus back on the item, no hover card for that");
		view.click();
		document.body.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true }));
		assertEqual(document.querySelector(".hover-card.pinned"), null, "a click outside closes it");
		view.click();
		view.click();
		assertEqual(document.querySelector(".hover-card.pinned"), null, "a second click on the item closes it");
		view.click();
		document.querySelector(".hover-card.pinned .claude-open-settings").click();
		assertEqual([document.querySelector(".hover-card.pinned"), ran], [null, [["shell.settings", { page: "custom:ied.chat.claude" }]]], "Open Claude settings");
	}
	finally {
		view.dispose();
		el.remove();
	}
});

test("claude: only a subscription sign-in runs Claude", () => {
	assertEqual(onSubscription(claude([])), true);
	const keyed = claude([], { cli: { path: "C:/claude.exe", signIn: "signed_in", authMethod: "api_key", subscription: false } });
	assertEqual(onSubscription(keyed), false);
	const status = usageStatus(keyed);
	assertEqual([status.text, status.level], ["Claude: not on a subscription", "error"]);
	assertEqual(status.tooltip.includes("api_key"), true);
	assertEqual(usageStatus(claude([], { cli: { path: "", signIn: "", authMethod: "" }, usage: { error: "no Claude Code CLI was found" } })).text, "Claude: no CLI");
	assertEqual(usageStatus(claude([])).text, "Claude", "usage not known yet");
});

test("claude: a window's line", () => {
	assertEqual(windowLine(window("seven_day", "Week", 61.4, 1.074), NOW), "Week: 61% used, resets in 2 h 0 min, pace 1.07x");
	assertEqual(windowLine(window("five_hour", "Session (5 h)", 100, 2, { usedUp: true }), NOW), "Session (5 h): 100% used, resets in 2 h 0 min, pace 2.00x - used up");
	assertEqual(windowLine(window("x", "Fable week", 5, 0, { resetsAtMs: 0 }), NOW), "Fable week: 5% used");
});

test("claude: a chat set up after its snapshot shows the model its turns use", () => {
	const model = new ChatModel();
	model.snapshot({ chatId: "c1", title: "Hello", mode: "agent", state: "idle", model: "", groupInUse: "", tasks: [], queued: [], modified: [], context: null, turns: [], pendingApprovals: [], pendingQuestion: null });
	assertEqual(model.model, "", "no model when the snapshot was taken");
	model.event("turn.start", { turnId: "u2", mode: "agent", model: "claude-sonnet", group: "claude", user: { text: "hi" } });
	assertEqual([model.model, model.groupInUse], ["claude-sonnet", "claude"]);
});

test("claude: the one step that sets Claude up", () => {
	const cli = (fields) => Object.assign({ path: "C:/claude.exe", signIn: "signed_in", authMethod: "claude.ai", subscription: true, signingIn: false, installing: false }, fields);
	assertEqual(setupStep(claude([], { offered: false })), "not_offered", "a distributed build");
	assertEqual(setupStep(claude([], { cli: cli({ path: "" }) })), "install");
	assertEqual(setupStep(claude([], { cli: cli({ path: "", signIn: "", asked: false }) })), "asking", "a server that has not looked yet: not \"install\"");
	assertEqual(setupStep(claude([], { cli: cli({ path: "", installing: true }) })), "installing");
	assertEqual(setupStep(claude([], { cli: cli({ signIn: "" }) })), "asking", "the CLI not asked yet");
	assertEqual(setupStep(claude([], { cli: cli({ signIn: "signed_out", authMethod: "none", subscription: false }) })), "sign_in");
	assertEqual(setupStep(claude([], { cli: cli({ authMethod: "api_key", subscription: false }) })), "sign_in", "an API key is no sign-in");
	assertEqual(setupStep(claude([], { cli: cli({ signingIn: true, subscription: false }) })), "signing_in");
	assertEqual(setupStep(claude([], { cli: cli({}) })), "ready");
	assertEqual(CLAUDE_PROFILE_NAMES, ["claude-opus", "claude-sonnet", "claude-haiku", "claude-fable"]);
});
