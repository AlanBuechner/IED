// chatpane.test.js - a chat shown (plugins/chat chatpane.js, docs/15 "Chat"): it opens at the end of its transcript,
// is there again whenever it is shown, and keeps the end in view as it grows until the owner scrolls up; its composer
// shows the chat's pinned model and what the next turn starts on.

import { test, assertEqual, assertTrue } from "./harness.js";
import { ChatPane } from "/plugins/ied.chat/ui/chatpane.js";

// What a chat pane asks of the plugin API; `topics` holds the handlers of what it subscribed to.
function fakeIed() {
	const topics = new Map();
	return {
		topics,
		client: {
			subscribe: (topic, handlers) => {
				topics.set(topic, handlers);
				return () => topics.delete(topic);
			},
			request: async () => null,
		},
		toast: () => {},
		contributions: () => [],
		bindings: { has: () => false },
		icons: { file: () => null },
	};
}

function snapshotOf(turnCount) {
	const turns = [];
	for (let i = 0; i < turnCount; i++) turns.push({ turnId: "u" + i, user: { text: "Question " + i }, mode: "agent", segments: [{ segId: "s" + i, type: "text", done: true, text: "Answer " + i }], stop: "finished", recoveries: [] });
	return {
		chatId: "c1", wsId: "w1", title: "Long chat", mode: "agent", permission: "ask", group: "", state: "idle", turnCount, tasksDone: 0, tasksTotal: 0,
		model: "m", groupInUse: "g", tasks: [], queued: [], modified: [], context: null, hasMore: false, pendingApprovals: [], pendingQuestion: null, turns,
	};
}

function frames(count) {
	return new Promise((resolve) => {
		const step = (left) => (left ? requestAnimationFrame(() => step(left - 1)) : resolve());
		step(count);
	});
}

// How far the transcript's end is below what it shows.
function gap(pane) {
	const s = pane.scroller;
	return s.scrollHeight - s.scrollTop - s.clientHeight;
}

// A pane on the page, short enough that the chat scrolls, showing chat c1 of `turnCount` turns.
async function openPane(ied, turnCount) {
	const pane = new ChatPane(ied, "w1", { created: () => {}, openChat: () => {}, shown: () => {} });
	pane.scroller.style.cssText = "height: 120px; overflow: auto;";
	document.body.append(pane.el);
	pane.show("c1");
	ied.topics.get("chat:c1").snap(snapshotOf(turnCount));
	await frames(2);
	return pane;
}

function closePane(pane) {
	pane.dispose();
	pane.el.remove();
}

test("chat pane: a chat opens at its end, and is there again whenever it is shown", async () => {
	const pane = await openPane(fakeIed(), 30);
	try {
		assertTrue(pane.scroller.scrollTop > 0 && gap(pane) <= 1, "opened at its end");
		pane.scroller.scrollTop = 0;
		await frames(2);
		pane.visible(false);
		pane.visible(true);
		await frames(2);
		assertTrue(gap(pane) <= 1, "shown again after the owner had scrolled up: at its end");
	}
	finally {
		closePane(pane);
	}
});

test("chat pane: the end stays in view as the chat grows, until the owner scrolls up", async () => {
	const ied = fakeIed();
	const pane = await openPane(ied, 30);
	try {
		ied.topics.get("chat:c1").evt("turn.start", { turnId: "n1", mode: "agent", user: { text: "One more" } });
		await frames(2);
		assertTrue(gap(pane) <= 1, "a new turn: still at the end");
		pane.scroller.scrollTop = 0;
		await frames(2);
		ied.topics.get("chat:c1").evt("turn.start", { turnId: "n2", mode: "agent", user: { text: "And another" } });
		await frames(2);
		assertEqual(pane.scroller.scrollTop, 0, "scrolled up: the transcript stays where the owner put it");
	}
	finally {
		closePane(pane);
	}
});

// The composer's selector titled `title`: its label.
function picked(pane, title) {
	return [...pane.composer.selectors.querySelectorAll("button.chat-dd")].find((b) => b.title === title).firstChild.textContent;
}

function chipText(pane) {
	const chips = pane.composer.telemetry.querySelectorAll(".chat-mchip");
	return chips.length ? chips[chips.length - 1].textContent : "";
}

test("chat pane: the Model picker shows the chat's pin, and the next turn's model follows a new pin, group or mode", async () => {
	const ied = fakeIed();
	const pane = await openPane(ied, 1);
	try {
		const topic = ied.topics.get("chat:c1");
		const summary = (change) => Object.assign({ chatId: "c1", wsId: "w1", title: "Long chat", mode: "agent", permission: "ask", group: "", effort: "", pinnedModel: "", state: "idle", turnCount: 1 }, change);
		const MODEL = "Model (pinned over the group's)";
		topic.snap(Object.assign(snapshotOf(1), { pinnedModel: "fake", model: "fake", groupInUse: "claude", roleEffort: "" }));
		assertEqual([picked(pane, MODEL), chipText(pane)], ["fake", "fake"], "the snapshot's pin");
		topic.evt("chat.changed", summary({ pinnedModel: "", model: "claude-opus", groupInUse: "claude", roleEffort: "high" }));
		assertEqual([picked(pane, MODEL), chipText(pane), picked(pane, "Effort")], ["Group's", "claude-opus", "High (group)"], "unpinned: the group's model and effort");
		topic.evt("chat.changed", summary({ pinnedModel: "fake", model: "fake", groupInUse: "claude", roleEffort: "high" }));
		assertEqual([picked(pane, MODEL), chipText(pane)], ["fake", "fake"], "pinned from the picker or by the AI");
		topic.evt("turn.start", { turnId: "n1", mode: "agent", model: "fake", group: "claude", user: { text: "Go" } });
		topic.evt("chat.changed", summary({ pinnedModel: "fake", state: "running", model: "fake", groupInUse: "claude", roleEffort: "high" }));
		topic.evt("chat.changed", summary({ pinnedModel: "", state: "running", model: "claude-opus", groupInUse: "claude", roleEffort: "high" }));
		assertEqual([picked(pane, MODEL), chipText(pane)], ["Group's", "claude-opus"], "unpinned mid-turn: the next turn's");
		topic.evt("chat.changed", summary({ mode: "plan", state: "idle", model: "claude-sonnet", groupInUse: "claude", roleEffort: "" }));
		assertEqual([chipText(pane), picked(pane, "Effort")], ["claude-sonnet", "Default"], "a new mode, a new role");
		topic.evt("turn.start", { turnId: "n2", mode: "plan", model: "claude-haiku", group: "claude", user: { text: "Plan" } });
		topic.evt("chat.changed", summary({ mode: "plan", state: "idle", turnCount: 3, model: "claude-sonnet", groupInUse: "claude", roleEffort: "" }));
		assertEqual(chipText(pane), "claude-haiku", "a turn's end keeps the model the turn ran on (its chain's pace may have passed over the first)");
	}
	finally {
		closePane(pane);
	}
});
