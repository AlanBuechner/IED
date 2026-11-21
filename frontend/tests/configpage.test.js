// configpage.test.js - the config server page's model (docs/35 "The web page"): where each served setting comes from,
// the tokens as listed and which IEDs follow, a server plugin's code, token names, times.

import { test, assertEqual } from "./harness.js";
import { ago, codeLook, compact, following, readSettingsText, settingRows, tokenNameProblem, tokenRows } from "../config/configmodel.js";

test("config page: where each setting comes from", () => {
	const served = { "ai.models": { "a@lan": {}, "b@lan": {} }, "editor.fontSize": 13 };
	const text = "// by hand\n{ \"editor.fontSize\": 13, \"ai.models\": { \"a@lan\": {} } }";
	const plugins = [{ id: "spark.models", published: { "ai.models": { "b@lan": {} } } }, { id: "other", published: {} }];
	assertEqual(settingRows(served, text, plugins).map((r) => [r.key, r.sources, r.members]), [
		["ai.models", ["settings.json", "spark.models"], ["a@lan", "b@lan"]],
		["editor.fontSize", ["settings.json"], null],
	]);
	assertEqual(readSettingsText("{ \"a\": \"// not a comment\" /* gone */ }"), { a: "// not a comment" }, "comments go, strings stay");
	assertEqual(readSettingsText("[1]"), null, "an object only");
	assertEqual(readSettingsText("{"), null);
	assertEqual(settingRows({ x: 1 }, "{", []).map((r) => r.sources), [[]], "a settings.json that does not read names no source");
});

test("config page: tokens and the IEDs that follow", () => {
	const now = 1800000000000;
	const tokens = [
		{ name: "laptop", role: "reader", createdMs: 1, lastUsedMs: now - 20000 },
		{ name: "web-edge-on-windows-1a2b3c", role: "admin", createdMs: 2, lastUsedMs: now },
		{ name: "desktop", role: "reader", createdMs: 3, lastUsedMs: now - 3600000 },
		{ name: "deploy", role: "admin", createdMs: 4 },
	];
	const rows = tokenRows(tokens, "web-edge-on-windows-1a2b3c", now);
	assertEqual(rows.map((t) => t.name), ["deploy", "web-edge-on-windows-1a2b3c", "desktop", "laptop"], "admins first, then by name");
	assertEqual(rows.map((t) => [t.you, t.browser, t.following]), [[false, false, false], [true, true, false], [false, false, false], [false, false, true]]);
	assertEqual(following(tokens, now).map((t) => t.name), ["laptop"]);
});

test("config page: a server plugin's code", () => {
	assertEqual(codeLook({ state: "loaded", code: { state: "active", builds: 2 } }), { label: "running", accent: "saved" });
	assertEqual(codeLook({ state: "loaded", code: { state: "active", building: true } }).accent, "info");
	assertEqual(codeLook({ state: "loaded", code: { state: "failed", problem: "x" } }), { label: "failed", accent: "error" });
	assertEqual(codeLook({ state: "failed" }).accent, "error");
});

test("config page: names, times, values", () => {
	assertEqual([tokenNameProblem("laptop"), tokenNameProblem("my.pc_2-a")], ["", ""]);
	assertEqual(Boolean(tokenNameProblem("")) && Boolean(tokenNameProblem("a b")) && Boolean(tokenNameProblem("x".repeat(65))), true);
	const now = 1800000000000;
	assertEqual([ago(0, now), ago(now - 2000, now), ago(now - 30000, now), ago(now - 120000, now), ago(now - 7200000, now), ago(now - 2 * 86400000, now)],
		["never", "just now", "30 s ago", "2 min ago", "2 h ago", "2 d ago"]);
	assertEqual(compact({ a: 1 }, 80), "{\"a\":1}");
	assertEqual(compact("x".repeat(200), 10).length, 10, "cut");
});
