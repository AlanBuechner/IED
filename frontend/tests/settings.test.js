// settings.test.js - the settings window's model (docs/07): pages from the settings plugins declare and the pages they
// draw, titles, controls and search; a refused write's message; and the Models page and its helpers.

import { test, assertEqual, assertThrows } from "./harness.js";
import { controlKind, findPage, groupOf, inputText, isModified, matches, pageGroups, pageOwning, parseInput, problemsFor, searchByPage, settingTitle, settingsPages, sourceLabel } from "../src/shell/settingsmodel.js";
import { refusal } from "../src/shell/settingsview.js";
import { ModelsPage, probeLine, profileFrom } from "/plugins/ied.chat/ui/modelsettings.js";
import { paceLabel } from "/plugins/ied.chat/ui/pacing.js";

const SETTINGS = {
	"ai.maxSteps": { page: "AI", schema: { type: "integer", description: "Model requests per turn" } },
	"ai.models": { page: "AI", schema: { type: "object" } },
	"cpp.clangd.path": { page: "Text Editor/C/C++", schema: { type: "string" } },
	"terminal.shell": { page: "Terminal", schema: { type: ["string", "array"] } },
	"plain.flag": { schema: { type: "boolean" } },
};

test("settings: pages, nesting and contributed pages", () => {
	const contributed = [{ plugin: { id: "ied.chat" }, value: { id: "models", page: "AI", title: "Models", view: "ModelsSettings", keys: ["ai.models"] } }];
	const pages = settingsPages(SETTINGS, contributed);
	assertEqual(pages.map((p) => p.title), ["AI", "Plain", "Terminal", "Text Editor"]);
	const ai = pages[0];
	assertEqual([ai.keys, ai.children.map((c) => c.title)], [["ai.maxSteps"], ["Models"]], "a contributed page takes its keys off the generic page");
	assertEqual(pages[3].children.map((c) => [c.title, c.keys]), [["C/C++", ["cpp.clangd.path"]]], "the first slash nests");
	assertEqual(findPage(pages, "custom:models").view, "ModelsSettings");
	assertEqual(findPage(pages, "nope"), null);
	assertEqual(pageOwning(pages, "ai.models").id, "custom:models", "search sends a key its plugin's page edits to that page");
	assertEqual(pageOwning(pages, "ai.maxSteps"), null);
});

test("settings: titles, controls and values", () => {
	assertEqual([settingTitle("cpp.clangd.path"), settingTitle("ai.maxSteps"), settingTitle("flag")], ["Clangd Path", "Max Steps", "Flag"]);
	assertEqual(["ai.maxSteps", "terminal.shell", "plain.flag"].map((k) => controlKind(SETTINGS[k].schema)), ["integer", "json", "boolean"]);
	assertEqual(controlKind({ enum: ["a", "b"] }), "enum");
	assertEqual([parseInput("integer", " 12 "), parseInput("json", "[1]"), parseInput("string", " x ")], [12, [1], " x "]);
	assertThrows(() => parseInput("integer", "1.5"), Error, "not a whole number");
	assertThrows(() => parseInput("number", ""), Error, "an empty number");
	assertThrows(() => parseInput("json", "{"), Error, "not JSON");
	assertEqual([inputText("json", { a: 1 }), inputText("string", null)], ["{\n  \"a\": 1\n}", ""]);
	assertEqual([sourceLabel("app"), sourceLabel("solution"), sourceLabel("default")], ["User", "Solution", "Default"]);
});

test("settings: search and problems", () => {
	assertEqual(Object.keys(SETTINGS).filter((k) => matches(k, SETTINGS[k], "model requests")), ["ai.maxSteps"], "words from the description");
	assertEqual(Object.keys(SETTINGS).filter((k) => matches(k, SETTINGS[k], "clangd")), ["cpp.clangd.path"]);
	const tiers = { app: { problems: [{ key: "ai.maxSteps", line: 3, message: "below the minimum" }] }, solution: { problems: [{ key: "other", line: 1, message: "x" }] } };
	assertEqual(problemsFor(tiers, "ai.maxSteps"), [{ tier: "app", key: "ai.maxSteps", line: 3, message: "below the minimum" }]);
	assertEqual(refusal({ message: "the value does not fit ai.maxSteps", data: { problems: ["/: -1 is less than minimum 0"] } }), "the value does not fit ai.maxSteps: /: -1 is less than minimum 0");
	assertEqual(refusal({ message: "unavailable" }), "unavailable");
});

test("settings: a model profile from its form", () => {
	const made = profileFrom({ name: " desk ", endpoint: "http://10.1.2.3:8080/v1", model: "", contextWindow: "32768", maxOutput: "", privacy: "lan", apiKeyEnv: "KEY" });
	assertEqual(made, { name: "desk", profile: { endpoint: "http://10.1.2.3:8080/v1", contextWindow: 32768, privacy: "lan", apiKey: { env: "KEY" } } });
	const seeing = { name: "v", endpoint: "http://a/v1", model: "", contextWindow: "", maxOutput: "", privacy: "local", apiKeyEnv: "" };
	assertEqual(profileFrom(Object.assign({ vision: "" }, seeing)).profile.vision, undefined, "left out: the server says");
	assertEqual(profileFrom(Object.assign({ vision: "true" }, seeing)).profile.vision, true, "sent images");
	assertEqual(profileFrom(Object.assign({ vision: "false" }, seeing)).profile.vision, false, "never sent images");
	assertThrows(() => profileFrom({ name: "x", endpoint: "https://a/v1", model: "", contextWindow: "", maxOutput: "", privacy: "local", apiKeyEnv: "" }), Error, "only http");
	assertThrows(() => profileFrom({ name: "", endpoint: "http://a/v1", model: "", contextWindow: "", maxOutput: "", privacy: "local", apiKeyEnv: "" }), Error, "a name");
	assertThrows(() => profileFrom({ name: "x", endpoint: "http://a/v1", model: "", contextWindow: "1.5", maxOutput: "", privacy: "local", apiKeyEnv: "" }), Error, "a whole window");
	assertEqual(probeLine({ ok: true, server: "llama.cpp", slots: 4, contextWindow: 262144, models: ["qwen"], ms: 12 }), "llama.cpp \u00b7 4 slots \u00b7 262k context \u00b7 serves qwen \u00b7 12 ms");
	assertEqual(probeLine({ ok: true, server: "llama.cpp", slots: 1, contextWindow: 0, vision: true, models: ["gemma"], ms: 5 }), "llama.cpp \u00b7 1 slot \u00b7 takes images \u00b7 serves gemma \u00b7 5 ms");
	assertEqual(probeLine({ ok: false, error: "connection refused", models: [] }), "Not reachable: connection refused");
});

test("settings: a Claude model's pace", () => {
	assertEqual([paceLabel(undefined), paceLabel({ max: 1.25 }), paceLabel({ max: 1, target: "week" })], ["always", "up to 1.25x, session and week", "up to 1.00x, week"]);
});

test("settings: the Models page shows a Claude model as Claude Code's, with nothing to test or edit", () => {
	const models = {
		"claude-fable-5-2": { provider: "claude-code", model: "claude-fable-5-2", label: "Fable 5.2", efforts: ["low", "medium", "high"] },
		"claude-opus": { provider: "claude-code", model: "opus" },
		desk: { endpoint: "http://10.0.0.2:8080/v1", privacy: "lan" },
	};
	const el = document.createElement("div");
	const page = new ModelsPage({ el, settings: { value: () => ({ value: models }), onChange: () => () => {} }, ied: {} });
	const card = (name) => el.querySelector(".set-model[data-model='" + name + "']");
	const fable = card("claude-fable-5-2");
	assertEqual([fable.classList.contains("claude-model"), fable.querySelector(".set-model-head b").textContent, fable.querySelector(".chip").textContent],
		[true, "Fable 5.2", "Claude Code"], "the CLI's name, then the id");
	assertEqual([...fable.querySelectorAll(".set-model-facts span")].map((s) => s.textContent), ["model claude-fable-5-2", "effort low, medium, high", "on your subscription"]);
	assertEqual([...fable.querySelectorAll("button")].map((b) => b.getAttribute("aria-label")), ["Remove claude-fable-5-2"], "no Test connection or Edit: Claude Code has no endpoint");
	assertEqual([...card("claude-opus").querySelectorAll(".set-model-facts span")].map((s) => s.textContent), ["model opus", "on your subscription"], "every effort: none listed");
	assertEqual([card("desk").classList.contains("claude-model"), card("desk").querySelector(".chip").textContent, card("desk").querySelectorAll("button").length], [false, "lan", 3], "an endpoint's card as before");
	assertEqual(el.querySelector(".set-lead").textContent.includes("Groups"), false);
	page.dispose();
});

test("settings: a key kept only for old files is not shown", () => {
	const pages = settingsPages({ "ai.groups": { page: "AI", schema: { type: "object", "x-ied-hidden": true } }, "ai.maxSteps": { page: "AI", schema: { type: "integer" } } }, []);
	assertEqual(pages[0].keys, ["ai.maxSteps"]);
});

test("settings: a page's settings in groups that fold", () => {
	assertEqual([groupOf("ai.maxSteps", {}), groupOf("ai.chats.keepDeletedDays", {}), groupOf("cpp.clangd.path", {}), groupOf("ai.edits.formatOnWrite", { schema: { "x-ied-group": "Writing" } })],
		["", "Chats", "Clangd", "Writing"], "the key's middle part, or the schema's own group");
	const settings = { "ai.maxSteps": {}, "ai.chats.notify": {}, "ai.chats.keepDeletedDays": {}, "ai.claude.command": {} };
	assertEqual(pageGroups(Object.keys(settings), settings).map((g) => [g.id, g.title, g.keys]), [
		["general", "General", ["ai.maxSteps"]],
		["chats", "Chats", ["ai.chats.notify", "ai.chats.keepDeletedDays"]],
		["claude", "Claude", ["ai.claude.command"]],
	], "General first, then the others by title");
	assertEqual([isModified({ a: { source: "app" } }, "a"), isModified({ a: { source: "default" } }, "a"), isModified({}, "a")], [true, false, false]);
});

test("settings: search finds by page, a page's title too", () => {
	const pages = settingsPages(SETTINGS, []);
	const hits = searchByPage(pages, SETTINGS, "steps");
	assertEqual(hits.map((hit) => [hit.page.title, hit.keys]), [["AI", ["ai.maxSteps"]]]);
	assertEqual(searchByPage(pages, SETTINGS, "zzz"), [], "nothing");
});
