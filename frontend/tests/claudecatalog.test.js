// claudecatalog.test.js - the Claude models the CLI offers in the client (docs/04 "The Claude models"): a family's profile
// labelled by the model it runs now, every version in the Model menu under Claude > Versions, and Settings > AI > Models
// saying how they stay current.

import { test, assertEqual } from "./harness.js";
import { catalogEfforts, follows, runsOf, versionsOf } from "/plugins/ied.chat/ui/claudecatalog.js";
import { choicesFrom, showModelPicker } from "/plugins/ied.chat/ui/modelpicker.js";
import { ModelsPage } from "/plugins/ied.chat/ui/modelsettings.js";

const ALL = ["low", "medium", "high", "xhigh", "max"];
// aiusage claude.models as the server writes it (ClaudeModels.cf WriteClaudeModels), from the CLI 2.1.317's catalog.
export const CATALOG = [
	{ value: "opus", resolved: "claude-opus-5-5", label: "Opus 5.5", family: "opus", alias: true, runs: "opus", newest: true, efforts: ALL },
	{ value: "claude-fable-5-2", resolved: "claude-fable-5-2", label: "Fable 5.2", family: "fable", alias: false, newest: true, efforts: ALL },
	{ value: "fable", resolved: "claude-fable-5-1", label: "Fable 5.1", family: "fable", alias: true, runs: "claude-fable-5-2", newest: false, efforts: ALL },
	{ value: "claude-haiku-4-5-20251001", resolved: "claude-haiku-4-5-20251001", label: "Haiku 4.5", family: "haiku", alias: false, newest: false },
	{ value: "claude-opus-4-6", resolved: "claude-opus-4-6", label: "Opus 4.6", family: "opus", alias: false, newest: false, efforts: ["low", "medium", "high", "max"] },
];

test("claude catalog: what a profile runs, and the versions", () => {
	assertEqual([runsOf("opus", CATALOG).label, runsOf("fable", CATALOG).label, runsOf("claude-opus-4-6", CATALOG).label, runsOf("opus", [])], ["Opus 5.5", "Fable 5.2", "Opus 4.6", null],
		"an alias its family's newest; a full id itself; nothing before the CLI was asked");
	assertEqual(runsOf("mythos", [{ value: "claude-mythos-6", family: "mythos", newest: true, label: "Mythos 6" }]).label, "Mythos 6", "a family with no alias: its newest");
	assertEqual([follows("fable"), follows("claude-fable-5-2"), follows("default")], [true, false, false]);
	assertEqual(versionsOf(CATALOG).map((m) => m.value), ["claude-fable-5-2", "claude-haiku-4-5-20251001", "claude-opus-4-6"]);
	assertEqual([catalogEfforts(CATALOG[4]), catalogEfforts(CATALOG[3])], [["low", "medium", "high", "max"], []], "Haiku 4.5 takes no effort level");
});

test("model menu: a Claude family runs its newest, and every version the CLI offers is under Versions", () => {
	const values = { "ai.models": { value: {
		"claude-opus": { provider: "claude-code", model: "opus" }, "claude-fable": { provider: "claude-code", model: "fable" },
		"claude-opus-4-6": { provider: "claude-code", model: "claude-opus-4-6", label: "Pinned Opus" }, qwen: { endpoint: "http://spark:8080/v1" },
	} } };
	const choices = choicesFrom(values, [], CATALOG);
	assertEqual(choices.models.map((m) => [m.name, m.label, !!m.follows, !!m.version, m.efforts.length]), [
		["claude-opus", "Opus 5.5", true, false, 5], ["claude-fable", "Fable 5.2", true, false, 5], ["claude-opus-4-6", "Pinned Opus", false, false, 4], ["qwen", "qwen", false, false, 5],
		["claude-fable-5-2", "Fable 5.2", false, true, 5], ["claude-haiku-4-5-20251001", "Haiku 4.5", false, true, 0],
	], "labelled by what each runs now; the versions no profile is named after");
	const at = document.createElement("button");
	document.body.append(at);
	const close = showModelPicker(at, choices, { model: "claude-haiku-4-5-20251001", effort: "" }, () => {}, () => {});
	const popup = document.querySelector(".chat-picker");
	const head = [...popup.querySelectorAll(".chat-picker-row.fam")].map((b) => [b.querySelector(".name").textContent, b.getAttribute("aria-expanded")]);
	assertEqual(head, [["Claude", "true"], ["Versions", "true"]], "folded open: they hold the chat's version");
	const versions = popup.querySelector(".chat-picker-family.sub");
	assertEqual([...versions.querySelectorAll(".chat-picker-row .name")].map((n) => n.textContent), ["Fable 5.2", "Haiku 4.5"]);
	assertEqual(popup.querySelector(".chat-picker-effort").textContent.includes("takes no effort level"), true, "Haiku 4.5: no effort slider");
	close();
	at.remove();
});

test("settings: the Models page says how Claude's models stay current", async () => {
	const models = {
		"claude-opus": { provider: "claude-code", model: "opus" },
		"claude-fable": { provider: "claude-code", model: "fable" },
		desk: { endpoint: "http://10.0.0.2:8080/v1", privacy: "lan" },
	};
	const el = document.createElement("div");
	el.className = "set-page";
	const client = { request: async (method) => method === "ai.usage" ? { claude: { models: CATALOG } } : null };
	const page = new ModelsPage({ el, settings: { value: () => ({ value: models }), onChange: () => () => {} }, ied: { client } });
	await page.loadCatalog();
	const card = (name) => el.querySelector(".set-model[data-model='" + name + "']");
	const facts = (name) => [...card(name).querySelectorAll(".set-model-facts span")].map((s) => s.textContent);
	assertEqual([card("claude-fable").querySelector(".set-model-head b").textContent, card("claude-fable").querySelector(".chip").textContent], ["Fable 5.2", "Claude Code"]);
	assertEqual(facts("claude-fable"), ["runs the newest Fable: Fable 5.2 (claude-fable-5-2)", "Claude Code's fable alias is still Fable 5.1", "on your subscription"]);
	assertEqual(facts("claude-opus"), ["runs the newest Opus: Opus 5.5 (claude-opus-5-5)", "on your subscription"], "every effort: none said");
	assertEqual([...card("claude-opus").querySelectorAll("button")].map((b) => b.getAttribute("aria-label")), ["Remove claude-opus"], "no endpoint to test or edit");
	assertEqual(el.querySelector(".set-claude-models .chip").textContent, "Claude Code", "the Claude models in a section of their own");
	assertEqual(!!el.querySelector(".set-claude-models .set-model[data-model='desk']"), false, "a model server is not among them");
	assertEqual(card("desk").querySelector(".chip").textContent, "lan", "a model server's card as before");
	const versions = [...el.querySelectorAll(".set-claude-version")].map((row) => [row.dataset.model, row.querySelector(".chip") ? row.querySelector(".chip").textContent : "", row.lastChild.textContent]);
	assertEqual(versions, [["claude-fable-5-2", "newest Fable", "effort low, medium, high, xhigh, max"], ["claude-haiku-4-5-20251001", "", "no effort levels"], ["claude-opus-4-6", "", "effort low, medium, high, max"]]);
	page.dispose();
});
