// agents.test.js - groups as topologies and background tasks in the client (docs/04 "A group is a topology",
// "Background tasks"): the Model menu's families, label and choices; sub-agents edited on the Model Groups page; a
// sub-agent's chat under its parent in the Chats list; a task's time.

import { test, assertEqual } from "./harness.js";
import { choiceLabel, choicesFrom, familyOf, groupTurn, modelFamilies, roleOfMode } from "/plugins/ied.chat/ui/modelpicker.js";
import { withAgent, withAgentRenamed, withoutAgent } from "/plugins/ied.chat/ui/groupsettings.js";
import { nestAgents, rootsOf } from "/plugins/ied.chat/ui/chatsview.js";
import { taskTime } from "/plugins/ied.chat/ui/sidepanel.js";

test("model menu: models by family, Claude's under one heading", () => {
	const models = [
		{ name: "claude-opus", claude: true }, { name: "qwen", endpoint: "http://spark:8080/v1" }, { name: "claude-haiku", claude: true },
		{ name: "coder", endpoint: "http://spark:8081/v1" }, { name: "odd" },
	];
	assertEqual([familyOf(models[0]), familyOf(models[1]), familyOf(models[4])], ["Claude", "spark", "Other"]);
	assertEqual(modelFamilies(models).map((f) => [f.name, f.models.map((m) => m.name)]), [["Claude", ["claude-opus", "claude-haiku"]], ["spark", ["qwen", "coder"]], ["Other", ["odd"]]]);
});

test("model menu: the label says what the chat runs on", () => {
	assertEqual(choiceLabel({ group: "", pinnedModel: "qwen" }, "team"), "qwen", "a model of its own");
	assertEqual(choiceLabel({ group: "team", pinnedModel: "" }, "team"), "team", "a group");
	assertEqual(choiceLabel({ group: "", pinnedModel: "" }, "team"), "team (default)", "the default group");
	assertEqual(choiceLabel({ group: "", pinnedModel: "" }, ""), "Default", "a draft before its group is known");
	assertEqual(choiceLabel({ group: "team", pinnedModel: "qwen" }, "team"), "qwen in team", "the AI pinned a model over the group");
});

test("model menu: groups and models from the settings", () => {
	const choices = choicesFrom({
		"ai.groups": { value: { solo: { primary: { model: "qwen" } }, team: { description: "Opus leads", primary: { model: "claude-opus" }, agents: { coder: { model: "qwen" }, reviewer: { model: "claude-haiku" } } } } },
		"ai.models": { value: { qwen: { endpoint: "http://spark:8080/v1" }, "claude-opus": { provider: "claude-code" } } },
		"ai.defaultGroup": { value: "team" },
	});
	assertEqual(choices.groups.map((g) => [g.name, g.isDefault, g.orchestrator, g.agents]), [["solo", false, "qwen", 0], ["team", true, "claude-opus", 2]]);
	assertEqual(choices.models.map((m) => [m.name, m.claude]), [["qwen", false], ["claude-opus", true]]);
	assertEqual(choicesFrom({ "ai.groups": { value: { a: {}, b: {} } }, "ai.defaultGroup": { value: "gone" } }).groups.map((g) => g.isDefault), [true, false], "the first group when the default names none");
});

test("model menu: what a group gives a turn of the chat's mode now - its chain's model and its role's effort", () => {
	const choices = choicesFrom({
		"ai.groups": { value: {
			claude: { primary: { model: "claude-opus", effort: "high", pace: { max: 1 }, fallback: [{ model: "claude-sonnet", pace: { max: 1.25 } }, "qwen"] } },
			split: { primary: { model: "gone", fallback: ["qwen"] }, planning: { model: "claude-opus", effort: "max" } },
		} },
		"ai.models": { value: { qwen: { endpoint: "http://spark:8080/v1", effort: { low: {} } }, "claude-opus": { provider: "claude-code" }, "claude-sonnet": { provider: "claude-code" } } },
	}, [{ group: "claude", role: "primary", now: "claude-sonnet", places: [] }, { group: "split", role: "planning", now: "claude-opus", places: [] }]);
	const [claude, split] = choices.groups;
	assertEqual(groupTurn(choices, claude, "agent"), { role: "primary", model: "claude-sonnet", effort: "high" }, "a role left off is primary; pacing's chain says the model now");
	assertEqual(groupTurn(choices, split, "plan"), { role: "planning", model: "claude-opus", effort: "max" }, "Plan mode: planning");
	assertEqual(groupTurn(choices, split, "ask"), { role: "primary", model: "qwen", effort: "" }, "a chain without Claude: its first model ai.models has");
	assertEqual([roleOfMode("ask"), roleOfMode("plan"), roleOfMode("agent")], ["primary", "planning", "implementation"]);
	assertEqual(choices.models.map((m) => [m.name, m.efforts.length]), [["qwen", 1], ["claude-opus", 5], ["claude-sonnet", 5]], "the efforts each model takes");
});

test("model groups: sub-agents added, changed, renamed and removed", () => {
	let group = { primary: { model: "big" } };
	group = withAgent(group, "coder", { model: "small", description: "Edits code", mode: undefined });
	group = withAgent(group, "reviewer", { model: "big", mode: "ask", calls: ["coder"] });
	assertEqual(group.agents, { coder: { model: "small", description: "Edits code" }, reviewer: { model: "big", mode: "ask", calls: ["coder"] } });
	group = withAgent(group, "coder", { description: "" });
	assertEqual(group.agents.coder, { model: "small" }, "an emptied field goes");
	const renamed = withAgentRenamed(group, "coder", "writer");
	assertEqual([Object.keys(renamed.agents), renamed.agents.reviewer.calls], [["writer", "reviewer"], ["writer"]], "in its place and in its callers");
	assertEqual(withAgentRenamed(group, "coder", "reviewer"), group, "a taken name changes nothing");
	const removed = withoutAgent(group, "coder");
	assertEqual(removed.agents, { reviewer: { model: "big", mode: "ask" } }, "nobody calls it any more");
	assertEqual("agents" in withoutAgent(removed, "reviewer"), false, "no agents left, no agents key");
	assertEqual(group.primary, { model: "big" }, "the orchestrator is untouched");
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

test("background tasks: how long one ran", () => {
	assertEqual([taskTime(42000), taskTime(180000), taskTime(3 * 3600000), taskTime(-5)], ["42 s", "3 min", "3 h", "0 s"]);
});
