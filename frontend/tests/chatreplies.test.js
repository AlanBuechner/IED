// chatreplies.test.js - chats waiting on each other in the chat plugin's views (docs/04 "Chats waiting on each other"):
// a suspended chat's look and toast, the turn a wait is in, the composer telling that a message stops it, the running
// wait card's hint, a reply's chip, and the details panel's "Replies".

import { test, assertEqual, assertTrue } from "./harness.js";
import { ChatModel, STATE_LABELS, WAIT_TOOLS, chatLook } from "/plugins/ied.chat/ui/chatmodel.js";
import { turnNotice } from "/plugins/ied.chat/ui/turnnotices.js";
import { Composer } from "/plugins/ied.chat/ui/composer.js";
import { SidePanel } from "/plugins/ied.chat/ui/sidepanel.js";

function snapshot(extra) {
	return Object.assign({
		chatId: "c1", wsId: "w1", title: "Lead", mode: "agent", permission: "auto", group: "", state: "running", turnCount: 1, tasksDone: 0, tasksTotal: 0,
		model: "m", groupInUse: "g", tasks: [], queued: [], modified: [], context: null, hasMore: false, pendingApprovals: [], pendingQuestion: null,
		turns: [{ turnId: "u1", user: { text: "go" }, mode: "agent", segments: [], stop: null, recoveries: [] }],
	}, extra);
}

test("chat replies: a suspended chat waits quietly, and its owner hears it suspended", () => {
	assertEqual(STATE_LABELS.suspended, "Suspended");
	assertEqual(chatLook({ state: "suspended", tasksDone: 1, tasksTotal: 3 }), { tasks: "1/3", icon: "$(clock)", tone: "", attention: false });
	const model = new ChatModel();
	model.snapshot(snapshot({ state: "suspended", stateReason: "Waiting for the helper", turns: [] }));
	assertTrue(!model.running(), "a suspended chat runs no turn");
	assertEqual(turnNotice({ state: "running" }, { state: "suspended", title: "Lead", stateReason: "Waiting for the helper" }),
		{ kind: "suspended", text: "\"Lead\" suspended: Waiting for the helper", level: "info" });
	assertEqual(turnNotice({ state: "running" }, { state: "suspended", title: "Agent", parentChatId: "c0" }), null, "a sub-agent's chat tells its parent, not the owner");
});

test("chat replies: the turn's running wait is known, and the composer says a message stops it", () => {
	assertTrue(WAIT_TOOLS.has("chat_wait") && WAIT_TOOLS.has("background_wait"));
	const model = new ChatModel();
	model.snapshot(snapshot());
	assertEqual(model.runningWait(), null, "no call yet");
	model.event("segment.start", { turnId: "u1", segId: "s1", type: "tool", done: false, tool: "background_wait", callId: "k1", argsText: "{}", state: "streaming" });
	model.event("tool.state", { callId: "k1", state: "running" });
	assertEqual(model.runningWait().callId, "k1");
	const composer = new Composer({ send: async () => true, stop: () => {}, unqueue: async () => null, sendQueued: () => {}, set: () => {}, choices: async () => null,
		mentionRows: async () => [], upload: async () => null, fileUrl: (path) => path, toast: () => {} });
	composer.update(model);
	assertTrue(/stops the wait/.test(composer.input.placeholder), composer.input.placeholder);
	assertTrue(/Stop the wait/.test(composer.sendButton.title), composer.sendButton.title);
	model.event("tool.state", { callId: "k1", state: "ok", ms: 900, result: { preview: "Stopped waiting: a message came", isError: false } });
	composer.update(model);
	assertEqual(model.runningWait(), null);
	assertTrue(!/stops the wait/.test(composer.input.placeholder), "once the wait ended the box says the usual");
});

test("chat replies: the details panel lists the replies awaited and owed, each opening its chat", () => {
	const opened = [];
	const panel = new SidePanel({ openFile: () => {}, fileIcon: () => null, openTab: () => {}, openChat: (chatId) => opened.push(chatId), stopTask: () => {}, closeDetails: () => {}, runCommand: () => {} });
	const model = new ChatModel();
	model.snapshot(snapshot({ state: "suspended", turns: [] }));
	panel.update(model);
	assertEqual(panel.body.querySelector("[data-section=replies]"), null, "no section while nothing is owed");
	const now = Date.now();
	model.snapshot(snapshot({ state: "suspended", turns: [], awaits: [{ chatId: "c2", title: "Helper", sinceMs: now - 90000 }], owes: [{ chatId: "c0", title: "Owner's lead", sinceMs: now - 1000 }] }));
	panel.update(model);
	const section = panel.body.querySelector("[data-section=replies]");
	assertTrue(Boolean(section), "the section shows");
	assertEqual(section.querySelector(".chat-sec-head .meta").textContent, "1 awaited, 1 owed");
	const rows = [...section.querySelectorAll(".chat-sec-row.reply")];
	assertEqual(rows.map((r) => r.querySelector(".name").textContent), ["Helper", "Owner's lead"]);
	assertEqual(rows.map((r) => r.querySelector(".dir").textContent), ["owes a reply", "owed a reply"], "the other chat is the subject: it owes this one, or is owed");
	rows[0].click();
	assertEqual(opened, ["c2"]);
});
