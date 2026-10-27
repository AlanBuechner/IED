// chatpane.test.js - a chat shown (plugins/chat chatpane.js, docs/15 "Chat"): it opens at the end of its transcript,
// is there again whenever it is shown, and keeps the end in view as it grows until the owner scrolls up; its composer
// shows the chat's pinned model and what the next turn starts on, and a message waiting for the running turn's next
// step; its details panel previews long lists and keeps the width the owner gives it.

import { test, assertEqual, assertTrue } from "./harness.js";
import { ChatPane } from "/plugins/ied.chat/ui/chatpane.js";
import { TranscriptView, thinkingTail } from "/plugins/ied.chat/ui/transcript.js";

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

// The effort the Model button names (the Effort dropdown went into the Model menu, owner 2026-10-26).
function effortShown(pane) {
	return pane.composer.selectors.querySelector(".chat-model-dd .chat-dd-effort").textContent;
}

async function until(found) {
	for (let i = 0; i < 60 && !found(); i++) await frames(1);
	return found();
}

test("chat pane: the Model menu shows what the chat runs on, and the next turn's model follows a new model, group or mode", async () => {
	const ied = fakeIed();
	const pane = await openPane(ied, 1);
	try {
		const topic = ied.topics.get("chat:c1");
		const summary = (change) => Object.assign({ chatId: "c1", wsId: "w1", title: "Long chat", mode: "agent", permission: "ask", group: "", effort: "", pinnedModel: "", state: "idle", turnCount: 1 }, change);
		const MODEL = "Run this chat on a group or a model, and set its effort";
		topic.snap(Object.assign(snapshotOf(1), { pinnedModel: "fake", model: "fake", groupInUse: "claude", roleEffort: "" }));
		assertEqual([picked(pane, MODEL), chipText(pane)], ["fake", "fake"], "the snapshot's pin");
		topic.evt("chat.changed", summary({ pinnedModel: "", model: "claude-opus", groupInUse: "claude", roleEffort: "high" }));
		assertEqual([picked(pane, MODEL), chipText(pane), effortShown(pane)], ["claude (default)", "claude-opus", "High (group)"], "no model of its own: the default group's model and effort");
		topic.evt("chat.changed", summary({ pinnedModel: "fake", model: "fake", groupInUse: "claude", roleEffort: "high" }));
		assertEqual([picked(pane, MODEL), chipText(pane)], ["fake", "fake"], "pinned from the picker or by the AI");
		topic.evt("turn.start", { turnId: "n1", mode: "agent", model: "fake", group: "claude", user: { text: "Go" } });
		topic.evt("chat.changed", summary({ pinnedModel: "fake", state: "running", model: "fake", groupInUse: "claude", roleEffort: "high" }));
		topic.evt("chat.changed", summary({ pinnedModel: "", state: "running", model: "claude-opus", groupInUse: "claude", roleEffort: "high" }));
		assertEqual([picked(pane, MODEL), chipText(pane)], ["claude (default)", "claude-opus"], "unpinned mid-turn: the next turn's");
		topic.evt("chat.changed", summary({ mode: "plan", state: "idle", model: "claude-sonnet", groupInUse: "claude", roleEffort: "" }));
		assertEqual([chipText(pane), effortShown(pane)], ["claude-sonnet", "Default"], "a new mode, a new role");
		topic.evt("turn.start", { turnId: "n2", mode: "plan", model: "claude-haiku", group: "claude", user: { text: "Plan" } });
		topic.evt("chat.changed", summary({ mode: "plan", state: "idle", turnCount: 3, model: "claude-sonnet", groupInUse: "claude", roleEffort: "" }));
		assertEqual(chipText(pane), "claude-haiku", "a turn's end keeps the model the turn ran on (its chain's pace may have passed over the first)");
	}
	finally {
		closePane(pane);
	}
});

test("chat pane: the Model menu says what each group runs on now and its effort, and its slider sets the chat's effort", async () => {
	const ied = fakeIed();
	const asked = [];
	ied.client.request = async (method, params) => {
		asked.push([method, params]);
		if (method === "settings.get") return { values: {
			"ai.models": { value: { "claude-opus": { provider: "claude-code", model: "opus" }, "claude-sonnet": { provider: "claude-code", model: "sonnet" }, qwen: { endpoint: "http://spark:8080/v1", effort: { low: {}, high: {} } } } },
			"ai.groups": { value: { claude: { primary: { model: "claude-opus", effort: "high", fallback: ["claude-sonnet", "qwen"] } }, local: { primary: { model: "qwen" }, implementation: { model: "qwen", effort: "low" } } } },
			"ai.defaultGroup": { value: "claude" } } };
		if (method === "ai.usage") return { claude: { chains: [{ group: "claude", role: "primary", now: "claude-sonnet", places: [] }] } };
		return null;
	};
	const pane = await openPane(ied, 1);
	const picker = () => document.querySelector(".chat-picker");
	const open = async () => {
		pane.composer.selectors.querySelector(".chat-model-dd").click();
		return until(picker);
	};
	try {
		const topic = ied.topics.get("chat:c1");
		topic.snap(Object.assign(snapshotOf(1), { mode: "ask", effort: "", model: "claude-sonnet", groupInUse: "claude", roleEffort: "high" }));
		assertEqual(effortShown(pane), "High (group)");
		const menu = await open();
		assertTrue(!!menu, "the Model menu opened");
		assertEqual([...menu.querySelectorAll(".chat-picker-row.grp")].map((row) => [row.dataset.group, row.dataset.now, row.querySelector(".sub").textContent]),
			[["claude", "claude-sonnet", "now claude-sonnet - effort High - no agents"], ["local", "qwen", "now qwen - effort the model's own - no agents"]],
			"each group: the model a turn of the chat's mode uses now (pacing's chain, or its first model) and its default effort");
		assertEqual([...menu.querySelectorAll(".chat-effort-tick")].map((tick) => tick.textContent), ["Group", "Low", "Med", "High", "XHigh", "Max"], "the group's default, then Sonnet's levels");
		const slider = menu.querySelector(".chat-effort-slider");
		assertEqual([slider.value, menu.querySelector(".chat-effort-value").textContent, menu.querySelector(".chat-picker-effort").dataset.model], ["0", "Group default (High)", "claude-sonnet"]);
		menu.querySelector(".chat-effort-tick[data-effort='max']").click();
		assertEqual(asked.filter(([method]) => method === "chat.set").map(([, params]) => params), [{ chatId: "c1", effort: "max" }], "the chat's effort");
		assertEqual([!!picker(), slider.value, menu.querySelector(".chat-effort-value").textContent], [true, "5", "Max"], "the menu stays open");
		slider.value = "2";
		slider.dispatchEvent(new Event("change"));
		assertEqual(asked.filter(([method]) => method === "chat.set").pop()[1], { chatId: "c1", effort: "medium" }, "dragged or moved with the keys");
		const rows = [...menu.querySelectorAll(".chat-picker-col")[1].querySelectorAll(".chat-picker-row")].filter((row) => !row.closest("[hidden]"));
		rows[rows.length - 1].focus();
		rows[rows.length - 1].dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowDown", bubbles: true }));
		assertEqual(document.activeElement, slider, "Down past a column's last row reaches the slider");
		menu.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
		assertEqual(picker(), null, "Escape closes it");
		topic.evt("chat.changed", { chatId: "c1", wsId: "w1", title: "Long chat", mode: "ask", permission: "ask", group: "", effort: "", pinnedModel: "qwen", state: "idle", turnCount: 1, model: "qwen", groupInUse: "claude", roleEffort: "" });
		assertEqual(effortShown(pane), "Default", "a single model: its own default");
		const alone = await open();
		assertEqual([...alone.querySelectorAll(".chat-effort-tick")].map((tick) => tick.textContent), ["Default", "Low", "High"], "a single model has no group default, and its profile's levels");
		alone.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
	}
	finally {
		if (picker()) picker().remove();
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

// The pane's requests, answered by `answers[method]` (a value or a function of the params); every call kept in `calls`.
function answering(ied, answers) {
	ied.calls = [];
	ied.client.request = async (method, params) => {
		ied.calls.push([method, params]);
		const answer = answers[method];
		return typeof answer === "function" ? answer(params) : answer === undefined ? null : answer;
	};
	return ied;
}

async function confirmShown(press) {
	await frames(1);
	const button = document.querySelector(".popup.dialog .btn" + (press ? ".primary" : ":not(.primary)"));
	assertTrue(button, "the dialog asks first");
	const said = document.querySelector(".popup.dialog").textContent;
	button.click();
	return said;
}

test("chat pane: the owner's message has Copy, Edit and Resend, the reply Copy; Resend reverts to before it and sends it again", async () => {
	const ied = answering(fakeIed(), { "chat.revert": { dropped: 1 }, "chat.send": { turnId: "u9" } });
	const pane = await openPane(ied, 2);
	try {
		const copied = [];
		pane.copy = async (text, what) => copied.push([what, text]);
		const turn = pane.transcript.el.querySelector(".chat-turn[data-turn='u1']");
		const buttons = [...turn.querySelectorAll(".chat-head .chat-msg-tools button")].map((b) => b.getAttribute("aria-label"));
		assertEqual(buttons, ["Copy the message", "Edit", "Resend"], "the owner's message");
		turn.querySelector(".chat-head .chat-msg-tools button").click();
		turn.querySelector(".chat-foot .chat-msg-tools button").click();
		assertEqual(copied, [["message", "Question 1"], ["reply", "Answer 1"]], "Copy gives the message as written and the reply's Markdown");
		turn.querySelector(".chat-head .chat-msg-tools button[aria-label='Resend']").click();
		const said = await confirmShown(true);
		assertTrue(said.includes("leave the chat") && said.includes("kept") && said.includes("not changed back"), "it says what goes, that it is kept, and that files stay: " + said);
		await frames(2);
		assertEqual(ied.calls.filter(([m]) => m.startsWith("chat.")).map(([m, p]) => [m, p.turnId || p.text]), [["chat.revert", "u1"], ["chat.send", "Question 1"]], "reverted to before it, then sent again");
	}
	finally {
		closePane(pane);
	}
});

test("chat pane: Edit puts the message in the composer; sending it asks, reverts, then sends; cancelled, nothing changes", async () => {
	const ied = answering(fakeIed(), { "chat.revert": { dropped: 2 }, "chat.send": { turnId: "u9" } });
	const pane = await openPane(ied, 3);
	try {
		const head = pane.transcript.el.querySelector(".chat-turn[data-turn='u1'] .chat-head");
		head.querySelector("button[aria-label='Edit']").click();
		assertEqual([pane.composer.input.value, pane.composer.editingEl.hidden], ["Question 1", false], "in the box, as an edit");
		pane.composer.setText("Question 1, better");
		pane.composer.send("message");
		const said = await confirmShown(false);
		assertTrue(said.includes("the turn after it"), "one turn after it goes too: " + said);
		await frames(2);
		assertEqual([ied.calls.filter(([m]) => m.startsWith("chat.")).length, pane.composer.input.value], [0, "Question 1, better"], "cancelled: no request, the text back in the box");
		pane.composer.send("message");
		await confirmShown(true);
		await frames(2);
		assertEqual(ied.calls.filter(([m]) => m.startsWith("chat.")).map(([m, p]) => [m, p.turnId || p.text]), [["chat.revert", "u1"], ["chat.send", "Question 1, better"]], "reverted, then the edited text sent");
	}
	finally {
		closePane(pane);
	}
});

test("chat pane: a chat reverted on any device is drawn as the server has it now", async () => {
	const ied = fakeIed();
	const pane = await openPane(ied, 3);
	try {
		pane.composer.edit("u2", 0, { text: "Question 2", mentions: [], attachments: [] });
		ied.topics.get("chat:c1").evt("chat.reverted", { turnId: "u1", dropped: 2, file: "reverted/1-u1.jsonl", snapshot: snapshotOf(1) });
		assertEqual([...pane.transcript.el.querySelectorAll(".chat-turn")].map((t) => t.dataset.turn), ["u0"], "the turns kept");
		assertEqual([pane.composer.editing, pane.composer.input.value], [null, "Question 2"], "the message being edited is gone: no longer an edit, its text kept");
	}
	finally {
		closePane(pane);
	}
});

test("chat pane: what the chat has open has Open and Force close, from a right click or a finger held on it", async () => {
	const ied = fakeIed();
	const ran = [];
	ied.commands = { run: async (id, args) => ran.push([id, args]) };
	const pane = await openPane(ied, 1);
	try {
		pane.openTab = (ref) => ran.push(["open", ref]);
		ied.topics.get("chat:c1").evt("resources.changed", { resources: [{ ref: "browser:pg-1", title: "App", kind: "page", close: "ied.browser.forceClose" }, { ref: "x:1", title: "Other", kind: "process" }] });
		const row = (ref) => pane.panel.el.querySelector(".chat-sec[data-section='open'] .chat-sec-row[data-ref='" + ref + "']");
		row("browser:pg-1").dispatchEvent(new MouseEvent("contextmenu", { bubbles: true, cancelable: true, clientX: 10, clientY: 10 }));
		const items = () => [...document.querySelectorAll(".popup.menu .menu-item")].map((i) => [i.textContent, i.classList.contains("dis")]);
		assertEqual(items(), [["Open", false], ["Force close", false]], "its menu");
		[...document.querySelectorAll(".popup.menu .menu-item")][1].click();
		assertEqual(ran, [["ied.browser.forceClose", { ref: "browser:pg-1" }]], "Force close runs the command its plugin named");
		const other = row("x:1");
		other.dispatchEvent(new PointerEvent("pointerdown", { pointerType: "touch", clientX: 5, clientY: 5, bubbles: true }));
		await new Promise((resolve) => setTimeout(resolve, 650));
		assertEqual(items(), [["Open", false], ["Force close", true]], "a finger held opens it; nothing to force-close without a command");
		other.dispatchEvent(new PointerEvent("pointerup", { pointerType: "touch", bubbles: true }));
		other.click();
		assertEqual(ran.length, 1, "the click that ends the hold opens nothing");
		document.querySelector(".popup.menu .menu-item").click();
		assertEqual(ran[1], ["open", "x:1"], "Open opens its tab");
	}
	finally {
		document.querySelectorAll(".popup.menu").forEach((menu) => menu.remove());
		closePane(pane);
	}
});

test("chat: a link in a reply opens the file it names, an address in its own window", () => {
	const opened = [];
	const view = new TranscriptView({ turns: [], segment: () => null, segmentId: () => 0 }, { openFile: (path, line) => opened.push([path, line]), loadEarlier: () => {}, codeBlock: () => null });
	const turn = { turnId: "t1", user: { text: "q" }, segments: [{ segId: "s1", type: "text", done: true, text: "see [the guide](docs/guide.md) and https://example.com/x" }], stop: "finished", recoveries: [] };
	view.el.append(...[view.turnEl(turn)]);
	const file = view.el.querySelector('a[href="docs/guide.md"]');
	const web = view.el.querySelector('a[href="https://example.com/x"]');
	assertTrue(web.hasAttribute("target") && web.getAttribute("rel") === "noopener noreferrer", "an address is marked to open beside the page");
	const click = (el) => { const e = new MouseEvent("click", { bubbles: true, cancelable: true, view: window }); el.dispatchEvent(e); return e.defaultPrevented; };
	assertEqual(click(web), false, "nothing is taken back: the browser opens it in a window");
	assertEqual(click(file), true, "a file of the workspace opens as a tab, and the page itself is not navigated");
	assertEqual(opened, [["docs/guide.md", undefined]], "the file the path names");
});

test("chat: a server that says it is still reading the prompt is believed, not called stalled", () => {
	const turns = new Map();
	const view = new TranscriptView({ turns: [], segment: () => null, segmentId: () => 0, turn: (id) => turns.get(id) }, { openFile: () => {}, loadEarlier: () => {}, codeBlock: () => null });
	const reading = (prefill, stop) => {
		const turn = { turnId: "t" + turns.size, user: { text: "q" }, segments: [], prefill, stop, recoveries: [] };
		turns.set(turn.turnId, turn);
		return turn;
	};
	const shown = (turn) => {
		const el = view.turnEl(turn);
		view.turnEls.set(turn.turnId, { el, prefill: el.querySelector(".chat-prefill") });
		return el.querySelector(".chat-prefill");
	};
	const working = shown(reading({ working: true, total: 245000, waitedMs: 95000 }, null));
	assertTrue(!working.hidden && working.textContent.includes("Still reading the prompt"), "the line the server's answer earns");
	assertTrue(working.textContent.includes("the server says it is still working"), "and says where it comes from");
	assertTrue(working.textContent.includes("1m 35s"), "how long it has waited");
	const estimated = shown(reading({ estimated: true, total: 12000, estimatedMs: 45000, startedMs: Date.now() - 1000 }, null));
	assertTrue(estimated.textContent.includes("Reading the prompt") && !estimated.textContent.includes("Still"), "an estimate before the deadline reads as it did");
	assertTrue(shown(reading({ working: true, total: 1, waitedMs: 1 }, "finished")).hidden, "gone when the turn ends");
	// The estimate's line starts a timer that redraws it every second; nothing here needs it after the checks.
	if (view.prefillTimer) clearInterval(view.prefillTimer);
	view.prefillTimer = 0;
});

test("chat: the streaming thinking's preview holds the last whole lines, never cut inside one", () => {
	assertEqual(thinkingTail(""), "", "nothing yet");
	assertEqual(thinkingTail("one line"), "one line", "a single line: all of it");
	assertEqual(thinkingTail("a\nb\nc\nd"), "a\nb\nc\nd", "four lines: all of them");
	assertEqual(thinkingTail("a\nb\nc\nd\ne\nf"), "c\nd\ne\nf", "more: the last four, the line being written last");
	const long = "word ".repeat(400);
	assertEqual(thinkingTail("first\n" + long), "first\n" + long, "a long line is whole: its start never slides away as it grows");
	const lines = Array.from({ length: 10 }, (_, i) => "line " + i + " " + "x".repeat(i * 30));
	assertEqual(thinkingTail(lines.join("\n")).split("\n"), lines.slice(-4), "every line kept is whole");
});

// A chat pane on the page (with chat.css) whose turn is thinking: [pane, topic, the thinking segment's element].
async function thinkingPane() {
	const link = document.querySelector("link[data-test='chat-css']") || document.head.appendChild(Object.assign(document.createElement("link"), { rel: "stylesheet", href: "/plugins/ied.chat/ui/chat.css" }));
	link.dataset.test = "chat-css";
	if (!link.sheet) await new Promise((resolve) => link.addEventListener("load", resolve, { once: true }));
	const ied = fakeIed();
	const pane = await openPane(ied, 1);
	pane.el.style.cssText = "width: 900px; height: 600px;";
	const topic = ied.topics.get("chat:c1");
	topic.evt("turn.start", { turnId: "n1", mode: "agent", user: { text: "Think" } });
	topic.evt("segment.start", { turnId: "n1", segId: "k1", type: "thinking", done: false, text: "" });
	await frames(1);
	return [pane, topic, pane.transcript.el.querySelector(".chat-seg[data-seg='k1']")];
}

test("chat: a thinking block opens from its header while it streams, and goes on streaming open", async () => {
	const [pane, topic, seg] = await thinkingPane();
	try {
		const head = seg.querySelector("button.chat-think");
		for (let i = 0; i < 5; i++) {
			topic.evt("segment.delta", { segId: "k1", text: "step " + i + "\n" });
			await frames(2);
		}
		assertTrue(head.isConnected && seg.querySelector("button.chat-think") === head, "the header is the same element through the deltas: a press on it is not lost");
		assertEqual(head.getAttribute("aria-expanded"), "false", "closed while streaming");
		head.click();
		const body = seg.querySelector(".chat-think-body");
		assertEqual([head.getAttribute("aria-expanded"), body.hidden, seg.querySelector(".chat-think-tail").hidden], ["true", false, true], "open mid-stream: the whole text, no preview");
		assertTrue(body.textContent.startsWith("step 0\n") && body.textContent.includes("step 4"), "the whole text so far");
		topic.evt("segment.delta", { segId: "k1", text: "step 5" });
		await frames(2);
		assertTrue(body.isConnected && body.textContent.endsWith("step 5"), "it goes on streaming into the open body");
		head.click();
		assertEqual([body.hidden, seg.querySelector(".chat-think-tail").hidden], [true, false], "closed again: the preview");
		topic.evt("segment.end", { segId: "k1", tokens: 40, ms: 1200 });
		await frames(2);
		assertTrue(seg.querySelector(".chat-think-tail").hidden && head.textContent.includes("1.2 s"), "done: the header says how long it took");
	}
	finally {
		closePane(pane);
	}
});

test("chat: the thinking preview keeps its size, and its text only moves up - never sideways", async () => {
	const [pane, topic, seg] = await thinkingPane();
	try {
		const tail = seg.querySelector(".chat-think-tail");
		const lines = seg.querySelector(".chat-think-lines");
		const sizes = new Set();
		const lefts = new Set();
		// The left edge of the n-th word of the text shown, as drawn.
		const wordLeft = (n) => {
			const node = lines.firstChild;
			const at = node.textContent.indexOf("w" + n + " ");
			const range = document.createRange();
			range.setStart(node, at);
			range.setEnd(node, at + 1);
			return Math.round(range.getBoundingClientRect().left);
		};
		for (let i = 0; i < 120; i++) {
			topic.evt("segment.delta", { segId: "k1", text: "w" + i + " " });
			await frames(1);
			const box = tail.getBoundingClientRect();
			sizes.add(Math.round(box.width) + "x" + Math.round(box.height));
			if (i >= 70) lefts.add(wordLeft(70));
		}
		assertEqual(lefts.size, 1, "a word of a long line stays where it was drawn as the line grows: " + [...lefts].join(", "));
		const before = wordLeft(110);
		for (let i = 0; i < 6; i++) {
			topic.evt("segment.delta", { segId: "k1", text: "\nshort line " + i });
			await frames(1);
			const box = tail.getBoundingClientRect();
			sizes.add(Math.round(box.width) + "x" + Math.round(box.height));
		}
		assertEqual(sizes.size, 1, "one size from a single long line to many short ones: " + [...sizes].join(", "));
		const height = parseFloat(getComputedStyle(tail).fontSize) * 1.5 * 4;
		assertTrue(Math.abs(tail.getBoundingClientRect().height - height) < 1, "four lines tall");
		assertTrue(lines.scrollWidth <= tail.clientWidth + 1, "long lines wrap: nothing reaches past the box's right edge");
		assertTrue(!lines.textContent.includes("w110 ") || wordLeft(110) === before, "a line kept stays where it was");
	}
	finally {
		closePane(pane);
	}
});
