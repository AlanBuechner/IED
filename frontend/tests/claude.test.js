// claude.test.js - Claude's plan usage in the chat plugin's UI (docs/04 "The Claude Code provider"): the status bar's text
// and color from the aiusage snapshot, a window's line, when a window frees, and Add Claude models.

import { test, assertEqual } from "./harness.js";
import { CLAUDE_PROFILES, onSubscription, resetText, usageStatus, windowLine, withClaudeModels } from "/plugins/ied.chat/ui/claudeusage.js";

const NOW = 1761150000000;
const HOUR = 3600000;

function claude(windows, extra) {
	return Object.assign({
		configured: true, nowMs: NOW, blockedUntilMs: 0, paced: [],
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
	const spent = usageStatus(claude([window("five_hour", "Session (5 h)", 100, 2, { usedUp: true })], { blockedUntilMs: NOW + HOUR }));
	assertEqual(spent.level, "error");
	assertEqual(spent.tooltip.includes("Used up; Claude frees in 1 h 0 min"), true);
	const paced = usageStatus(claude([window("seven_day", "Week", 60, 1.2)], { paced: [{ profile: "claude-auto", model: "sonnet", why: "the week is at 1.20x pace" }] }));
	assertEqual(paced.tooltip.includes("claude-auto picks sonnet now (the week is at 1.20x pace)"), true);
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

test("claude: Add Claude models keeps what is there and falls back to the default group's model", () => {
	const local = { endpoint: "http://127.0.0.1:8080/v1" };
	const made = withClaudeModels({ qwen: local, "claude-opus": { provider: "claude-code", model: "opus", contextWindow: 5 } }, { local: { primary: { model: "qwen" } } }, "local");
	assertEqual(Object.keys(made.models).sort(), ["claude-auto", "claude-fable", "claude-haiku", "claude-opus", "claude-sonnet", "qwen"]);
	assertEqual(made.models["claude-opus"].contextWindow, 5, "a profile already there is kept");
	assertEqual(made.models["claude-auto"], CLAUDE_PROFILES["claude-auto"]);
	assertEqual(made.groups.claude, { primary: { model: "claude-auto", fallback: ["qwen"] } });
	assertEqual(made.groups.local, { primary: { model: "qwen" } });
	const bare = withClaudeModels({}, {}, "");
	assertEqual(bare.groups.claude, { primary: { model: "claude-auto" } }, "nothing local to fall back to");
	const already = withClaudeModels({}, { claude: { primary: { model: "claude-opus" } } }, "claude");
	assertEqual(already.groups.claude, { primary: { model: "claude-opus" } }, "a claude group already there is kept");
});
