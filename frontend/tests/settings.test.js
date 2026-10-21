// settings.test.js - the settings window's model (docs/07): pages from the settings plugins declare and the pages they
// draw, titles, controls and search; a refused write's message; and the Models and Model Groups pages' helpers.

import { test, assertEqual, assertThrows } from "./harness.js";
import { controlKind, findPage, inputText, matches, parseInput, problemsFor, settingTitle, settingsPages, sourceLabel } from "../src/shell/settingsmodel.js";
import { refusal } from "../src/shell/settingsview.js";
import { probeLine, profileFrom } from "/plugins/ied.chat/ui/modelsettings.js";
import { freshName, roleDefault, withRole } from "/plugins/ied.chat/ui/groupsettings.js";

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
	assertThrows(() => profileFrom({ name: "x", endpoint: "https://a/v1", model: "", contextWindow: "", maxOutput: "", privacy: "local", apiKeyEnv: "" }), Error, "only http");
	assertThrows(() => profileFrom({ name: "", endpoint: "http://a/v1", model: "", contextWindow: "", maxOutput: "", privacy: "local", apiKeyEnv: "" }), Error, "a name");
	assertThrows(() => profileFrom({ name: "x", endpoint: "http://a/v1", model: "", contextWindow: "1.5", maxOutput: "", privacy: "local", apiKeyEnv: "" }), Error, "a whole window");
	assertEqual(probeLine({ ok: true, server: "llama.cpp", slots: 4, contextWindow: 262144, models: ["qwen"], ms: 12 }), "llama.cpp \u00b7 4 slots \u00b7 262k context \u00b7 serves qwen \u00b7 12 ms");
	assertEqual(probeLine({ ok: true, server: "llama.cpp", slots: 1, contextWindow: 0, vision: true, models: ["gemma"], ms: 5 }), "llama.cpp \u00b7 1 slot \u00b7 takes images \u00b7 serves gemma \u00b7 5 ms");
	assertEqual(probeLine({ ok: false, error: "connection refused", models: [] }), "Not reachable: connection refused");
});

test("settings: group roles", () => {
	const group = { primary: { model: "a", temperature: 0.3 } };
	assertEqual(withRole(group, "primary", { temperature: undefined }), { primary: { model: "a" } }, "an emptied field goes");
	assertEqual(withRole(group, "primary", { model: "" }), {}, "a role without a model goes");
	assertEqual(withRole(group, "worker", { model: "b", fallback: ["a"] }), { primary: { model: "a", temperature: 0.3 }, worker: { model: "b", fallback: ["a"] } });
	assertEqual(withRole(group, "primary", { fallback: [] }), group, "an empty fallback list goes");
	assertEqual([freshName({}, "group"), freshName({ group: {}, "group 2": {} }, "group")], ["group", "group 3"]);
	assertEqual([roleDefault(group, ["b", "a"]), roleDefault({}, ["b", "a"]), roleDefault(group, ["b"]), roleDefault({}, [])], ["a", "b", "b", ""], "a role turned on gets the primary's model, else the first");
});
