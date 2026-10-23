// chatpane.test.js - a chat shown (plugins/chat chatpane.js, docs/15 "Chat"): it opens at the end of its transcript,
// is there again whenever it is shown, and keeps the end in view as it grows until the owner scrolls up; its composer
// shows the chat's pinned model and what the next turn starts on, and a message waiting for the running turn's next
// step; its details panel previews long lists and keeps the width the owner gives it.

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

// The owner's wheel turned up, and the transcript moved as it does then.
function ownerScrollsUp(pane, px) {
	pane.scroller.dispatchEvent(new WheelEvent("wheel", { deltaY: -px, bubbles: true }));
	pane.scroller.scrollTop = Math.max(0, pane.scroller.scrollTop - px);
}

// The transcript `px` taller, as when an image of a tool's result loads.
function grow(pane, px) {
	const block = document.createElement("div");
	block.style.height = px + "px";
	pane.transcript.el.append(block);
}

test("chat pane: a chat opens at its end, and is there again whenever it is shown", async () => {
	const pane = await openPane(fakeIed(), 30);
	try {
		assertTrue(pane.scroller.scrollTop > 0 && gap(pane) <= 1, "opened at its end");
		ownerScrollsUp(pane, pane.scroller.scrollTop);
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
		ownerScrollsUp(pane, pane.scroller.scrollTop);
		await frames(2);
		ied.topics.get("chat:c1").evt("turn.start", { turnId: "n2", mode: "agent", user: { text: "And another" } });
		await frames(2);
		assertEqual(pane.scroller.scrollTop, 0, "scrolled up: the transcript stays where the owner put it");
	}
	finally {
		closePane(pane);
	}
});

test("chat pane: only the owner scrolling up stops following the end - never the chat growing between a scroll and its event", async () => {
	const pane = await openPane(fakeIed(), 30);
	try {
		grow(pane, 400);
		pane.scroller.dispatchEvent(new Event("scroll"));
		await frames(2);
		assertTrue(gap(pane) <= 1, "an image loaded before the last scroll's event: still at the end");
		ownerScrollsUp(pane, 300);
		await frames(2);
		const top = pane.scroller.scrollTop;
		grow(pane, 400);
		await frames(2);
		assertEqual(pane.scroller.scrollTop, top, "scrolled up by the owner: the transcript stays where they put it");
		pane.scroller.scrollTop = pane.scroller.scrollHeight;
		await frames(2);
		grow(pane, 400);
		await frames(2);
		assertTrue(gap(pane) <= 1, "back at the end: it follows again");
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

test("chat pane: the Model menu shows what the chat runs on, and the next turn's model follows a new model, group or mode", async () => {
	const ied = fakeIed();
	const pane = await openPane(ied, 1);
	try {
		const topic = ied.topics.get("chat:c1");
		const summary = (change) => Object.assign({ chatId: "c1", wsId: "w1", title: "Long chat", mode: "agent", permission: "ask", group: "", effort: "", pinnedModel: "", state: "idle", turnCount: 1 }, change);
		const MODEL = "Run this chat on a group or a model";
		topic.snap(Object.assign(snapshotOf(1), { pinnedModel: "fake", model: "fake", groupInUse: "claude", roleEffort: "" }));
		assertEqual([picked(pane, MODEL), chipText(pane)], ["fake", "fake"], "the snapshot's pin");
		topic.evt("chat.changed", summary({ pinnedModel: "", model: "claude-opus", groupInUse: "claude", roleEffort: "high" }));
		assertEqual([picked(pane, MODEL), chipText(pane), picked(pane, "Effort")], ["claude (default)", "claude-opus", "High (group)"], "no model of its own: the default group's model and effort");
		topic.evt("chat.changed", summary({ pinnedModel: "fake", model: "fake", groupInUse: "claude", roleEffort: "high" }));
		assertEqual([picked(pane, MODEL), chipText(pane)], ["fake", "fake"], "pinned from the picker or by the AI");
		topic.evt("turn.start", { turnId: "n1", mode: "agent", model: "fake", group: "claude", user: { text: "Go" } });
		topic.evt("chat.changed", summary({ pinnedModel: "fake", state: "running", model: "fake", groupInUse: "claude", roleEffort: "high" }));
		topic.evt("chat.changed", summary({ pinnedModel: "", state: "running", model: "claude-opus", groupInUse: "claude", roleEffort: "high" }));
		assertEqual([picked(pane, MODEL), chipText(pane)], ["claude (default)", "claude-opus"], "unpinned mid-turn: the next turn's");
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

// The details panel's section `id`: the names of its rows and its Show all button's text.
function panelList(pane, id) {
	const section = pane.panel.el.querySelector(".chat-sec[data-section='" + id + "']");
	const more = section.querySelector(".chat-sec-more:not(.above)");
	const above = section.querySelector(".chat-sec-more.above");
	return { names: [...section.querySelectorAll(".chat-sec-row .name")].map((n) => n.textContent), more: more ? more.textContent : null, button: more, above: above ? above.textContent : null, aboveButton: above };
}

test("chat pane: a long list of the details panel shows its first rows until Show all, as many as ai.chats.panelRows", async () => {
	const ied = fakeIed();
	const pane = await openPane(ied, 1);
	try {
		const modified = [];
		for (let i = 1; i <= 12; i++) modified.push({ path: "src/f" + i + ".c", plus: i, minus: 0 });
		ied.topics.get("chat:c1").snap(Object.assign(snapshotOf(1), { modified }));
		let files = panelList(pane, "files");
		assertEqual([files.names, files.more], [["f1.c", "f2.c", "f3.c", "f4.c", "f5.c"], "Show all 12"], "five, then Show all");
		files.button.click();
		files = panelList(pane, "files");
		assertEqual([files.names.length, files.more], [12, "Show fewer"], "every file once pressed");
		files.button.click();
		ied.topics.get("settings:w1").snap({ values: { "ai.chats.panelRows": { value: 3 } } });
		files = panelList(pane, "files");
		assertEqual([files.names, files.more], [["f1.c", "f2.c", "f3.c"], "Show all 12"], "the setting's count");
		ied.topics.get("chat:c1").evt("files.changed", { modified: modified.slice(0, 3) });
		assertEqual(panelList(pane, "files").more, null, "no button for a list that fits");
	}
	finally {
		closePane(pane);
	}
});

test("chat pane: the task list's preview follows what the AI does, under a row that counts the tasks above", async () => {
	const ied = fakeIed();
	const pane = await openPane(ied, 1);
	try {
		const topic = ied.topics.get("chat:c1");
		const tasks = (...statuses) => statuses.map((status, i) => ({ id: "t" + (i + 1), title: "Task " + (i + 1), status }));
		const named = (...numbers) => numbers.map((n) => "Task " + n);
		const [D, S, N, P] = ["done", "skipped", "in_progress", "pending"];
		topic.snap(Object.assign(snapshotOf(1), { tasks: tasks(P, P, P, P, P, P, P, P, P, P) }));
		let list = panelList(pane, "tasks");
		assertEqual([list.names, list.above, list.more], [named(1, 2, 3, 4, 5), null, "Show all 10"], "none started: the first");
		topic.evt("tasks.changed", { items: tasks(D, D, D, D, D, D, D, N, P, P) });
		list = panelList(pane, "tasks");
		assertEqual([list.names, list.above], [named(6, 7, 8, 9, 10), "5 done above"], "the one in progress, after the one done before it");
		topic.evt("tasks.changed", { items: tasks(D, D, D, S, D, P, P, P, P, P) });
		assertEqual([panelList(pane, "tasks").names, panelList(pane, "tasks").above], [named(5, 6, 7, 8, 9), "4 done above"], "between two: the next one up");
		topic.evt("tasks.changed", { items: tasks(D, P, D, D, D, D, N, P, P, P) });
		assertEqual(panelList(pane, "tasks").above, "5 above", "one above is not done");
		topic.evt("tasks.changed", { items: tasks(D, D, D, D, D, D, D, D, D, D) });
		list = panelList(pane, "tasks");
		assertEqual([list.names, list.above], [named(6, 7, 8, 9, 10), "5 done above"], "all done: the last");
		list.aboveButton.click();
		list = panelList(pane, "tasks");
		assertEqual([list.names.length, list.above, list.more], [10, null, "Show fewer"], "the row above shows them all");
	}
	finally {
		closePane(pane);
	}
});

test("chat pane: a message sent to the running turn waits above the composer, then shows where the turn took it", async () => {
	const ied = fakeIed();
	const pane = await openPane(ied, 1);
	try {
		const topic = ied.topics.get("chat:c1");
		topic.evt("turn.start", { turnId: "n1", mode: "agent", user: { text: "Go" } });
		topic.evt("queue.changed", { queued: [], pending: ["use tabs"] });
		const waiting = pane.composer.queuedEl.querySelectorAll(".chat-queued.pending");
		assertEqual([waiting.length, waiting.length && waiting[0].textContent], [1, "At the next step:use tabs"], "waiting for the next step");
		topic.evt("segment.start", { turnId: "n1", segId: "m1", type: "message", done: true, text: "use tabs", atMs: 1 });
		topic.evt("queue.changed", { queued: [], pending: [] });
		const shown = pane.transcript.el.querySelector(".chat-turn[data-turn='n1'] .chat-seg[data-type='message']");
		assertEqual([shown.querySelector(".chat-head b").textContent, shown.querySelector(".chat-user").textContent], ["You", "use tabs"], "the owner's message, in the turn");
		assertEqual(pane.composer.queuedEl.children.length, 0, "no longer waiting");
	}
	finally {
		closePane(pane);
	}
});

test("chat pane: the details panel's edge sizes it, and the width is kept for the next pane", async () => {
	const link = document.querySelector("link[data-test='chat-css']") || document.head.appendChild(Object.assign(document.createElement("link"), { rel: "stylesheet", href: "/plugins/ied.chat/ui/chat.css" }));
	link.dataset.test = "chat-css";
	if (!link.sheet) await new Promise((resolve) => link.addEventListener("load", resolve, { once: true }));
	const kept = localStorage.getItem("ied.chat.panelWidth");
	localStorage.removeItem("ied.chat.panelWidth");
	const pane = await openPane(fakeIed(), 1);
	pane.el.style.cssText = "width: 1200px; height: 400px;";
	try {
		const before = pane.panel.el.offsetWidth;
		pane.sash.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowLeft", bubbles: true }));
		assertEqual([pane.panel.el.offsetWidth, Number(localStorage.getItem("ied.chat.panelWidth"))], [before + 16, before + 16], "an arrow key widens it, kept");
		const next = new ChatPane(fakeIed(), "w1", { created: () => {}, openChat: () => {}, shown: () => {} });
		assertEqual(next.el.style.getPropertyValue("--chat-panel-w"), (before + 16) + "px", "a new pane opens as wide");
		next.dispose();
		pane.sash.dispatchEvent(new MouseEvent("dblclick", { bubbles: true }));
		assertEqual([pane.panel.el.offsetWidth, Number(localStorage.getItem("ied.chat.panelWidth"))], [before, 0], "a double-click: the default width, kept");
	}
	finally {
		closePane(pane);
		if (kept === null) localStorage.removeItem("ied.chat.panelWidth");
		else localStorage.setItem("ied.chat.panelWidth", kept);
	}
});
