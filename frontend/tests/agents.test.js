// agents.test.js - groups as topologies and background tasks in the client (docs/04 "A group is a topology",
// "Background tasks"): the Model menu's families, label and choices; sub-agents edited on the Model Groups page; a
// sub-agent's chat under its parent in the Chats list; a task's time.

import { test, assertEqual } from "./harness.js";
import { choiceLabel, choicesFrom, defaultNow, familyOf, modelFamilies, nextModel } from "/plugins/ied.chat/ui/modelpicker.js";
import { keptBySection, nestAgents, rootsOf } from "/plugins/ied.chat/ui/chatsview.js";
import { taskTime } from "/plugins/ied.chat/ui/sidepanel.js";

test("model menu: models by family, Claude's under one heading", () => {
	const models = [
		{ name: "claude-opus", claude: true }, { name: "qwen", endpoint: "http://spark:8080/v1" }, { name: "claude-haiku", claude: true },
		{ name: "coder", endpoint: "http://spark:8081/v1" }, { name: "odd" },
	];
	assertEqual([familyOf(models[0]), familyOf(models[1]), familyOf(models[4])], ["Claude", "spark", "Other"]);
	assertEqual(modelFamilies(models).map((f) => [f.name, f.models.map((m) => m.name)]), [["Claude", ["claude-opus", "claude-haiku"]], ["spark", ["qwen", "coder"]], ["Other", ["odd"]]]);
});

test("model menu: the label says what the chat runs on, by the model's label", () => {
	const choices = choicesFrom({ "ai.models": { value: { qwen: { endpoint: "http://spark:8080/v1" }, "claude-fable-5-2": { provider: "claude-code", model: "claude-fable-5-2", label: "Fable 5.2" } } } });
	assertEqual(choiceLabel("claude-fable-5-2", choices), "Fable 5.2", "a model, by its label");
	assertEqual(choiceLabel("qwen", choices), "qwen", "a profile without a label, by its name");
	assertEqual(choiceLabel("qwen", null), "qwen", "before the models are known");
	assertEqual([choiceLabel("", null), choiceLabel("", choices)], ["...", "No model"], "never Default: not known yet, or none set up");
});

test("model menu: the model a chat's next turn starts on, never just Default", () => {
	const values = { "ai.models": { value: { qwen: { endpoint: "http://spark:8080/v1" }, "claude-opus": { provider: "claude-code", label: "Opus 5.5" }, "claude-haiku": { provider: "claude-code", label: "Haiku 5.5" } } },
		"ai.chats.model": { value: "claude-opus" }, "board.ai.model": { value: "gone" } };
	const uses = [{ id: "chat", title: "Chats", model: "ai.chats.model", effort: "ai.chats.effort" }, { id: "board", title: "Planning board", model: "board.ai.model", effort: "board.ai.effort" }];
	const choices = choicesFrom(values, [{ role: "primary", now: "claude-haiku" }], [], uses);
	assertEqual(nextModel(choices, { model: "qwen", now: "claude-opus", use: "chat" }), "qwen", "its own");
	assertEqual(nextModel(choices, { model: "", now: "claude-haiku", use: "chat" }), "claude-haiku", "what the server says");
	assertEqual(nextModel(choices, { model: "", now: "", use: "chat" }), "claude-opus", "a chat not sent yet: its use's default model");
	assertEqual(nextModel(choices, { model: "", now: "", use: "board" }), "claude-haiku", "a use's model that is not a model: the default chain's now");
	assertEqual(nextModel(null, { model: "", now: "", use: "chat" }), "", "nothing while the models are not known");
});

test("model menu: the models, the default chain and what a turn on it uses now", () => {
	const values = {
		"ai.models": { value: { qwen: { endpoint: "http://spark:8080/v1", effort: { low: {} } }, "claude-opus": { provider: "claude-code", label: "Opus 5.5" }, "claude-opus-4-6": { provider: "claude-code", efforts: ["low", "medium", "high", "max"] } } },
		"ai.defaultModel": { value: "" },
	};
	const choices = choicesFrom(values, [{ group: "default", role: "primary", now: "claude-sonnet", places: [] }]);
	assertEqual(choices.models.map((m) => [m.name, m.label, m.claude, m.efforts]), [["qwen", "qwen", false, ["low"]], ["claude-opus", "Opus 5.5", true, ["low", "medium", "high", "xhigh", "max"]], ["claude-opus-4-6", "claude-opus-4-6", true, ["low", "medium", "high", "max"]]],
		"labels, Claude's, and the efforts each takes (a Claude model's own list)");
	assertEqual(defaultNow(choices), "claude-sonnet", "pacing says which model the default chain uses now");
	assertEqual(defaultNow(choicesFrom(Object.assign({}, values, { "ai.defaultModel": { value: "qwen" } }), [])), "qwen", "else ai.defaultModel");
	assertEqual(defaultNow(choicesFrom(values, [])), "qwen", "else the first model");
});

test("chats list: a sub-agent's chat under the chat that ran it", () => {
	const chats = [
		{ chatId: "a", title: "Lead" }, { chatId: "b", title: "Other" }, { chatId: "c", title: "coder", parentChatId: "a" },
		{ chatId: "d", title: "reviewer", parentChatId: "c" }, { chatId: "e", title: "orphan", parentChatId: "gone" },
	];
	assertEqual(nestAgents(chats).map(({ chat, depth }) => chat.chatId + depth), ["a0", "c1", "d2", "b0", "e0"]);
	const roots = rootsOf(chats);
	assertEqual([roots.get("d"), roots.get("c"), roots.get("b"), roots.get("e")], ["a", "a", "b", "e"], "a sub-agent goes in its orchestrator's section");
	assertEqual(nestAgents([{ chatId: "x", parentChatId: "y" }, { chatId: "y", parentChatId: "x" }]).length, 2, "a loop still lists both");
});

test("chats list: a plugin's chats in a section of their own, a sub-agent's with its orchestrator", () => {
	const chats = [
		{ chatId: "a", title: "Mine" }, { chatId: "b", title: "Board: Login", section: "Boards" }, { chatId: "c", title: "coder", parentChatId: "b" },
		{ chatId: "d", title: "Board: Sync", section: "Boards" }, { chatId: "e", title: "Notes", section: "  " }, { chatId: "f", title: "Plan", section: "Agents" },
	];
	const kept = keptBySection(chats, rootsOf(chats));
	assertEqual(kept.sections.map(([name, list]) => name + ":" + list.map((c) => c.chatId).join("")), ["Agents:f", "Boards:bcd"], "by name");
	assertEqual([...kept.of].sort(), ["b", "c", "d", "f"], "a blank section is the owner's own");
});

test("background tasks: how long one ran", () => {
	assertEqual([taskTime(42000), taskTime(180000), taskTime(3 * 3600000), taskTime(-5)], ["42 s", "3 min", "3 h", "0 s"]);
});
