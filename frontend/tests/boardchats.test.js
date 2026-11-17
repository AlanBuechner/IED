// boardchats.test.js - the boards' chats in the Boards activity (docs/32 "Busy board chats"): the activity's badge, a
// board's row marked while its chat runs or waits, and the chats:<wsId> events folded in.

import { test, assertEqual } from "./harness.js";
import { boardChatsBadge, chatMark, foldChat } from "/plugins/ied.board/ui/boardchats.js";
import { BoardListView } from "/plugins/ied.board/ui/boardlist.js";
import { moreBadge } from "../src/shell/badges.js";

const chat = (chatId, state, use) => ({ chatId, state, use: use || "board", title: chatId });

test("board chats: the Boards activity's badge counts the board chats in a turn", () => {
	assertEqual(boardChatsBadge([chat("a", "idle"), chat("b", "running", "chat")]), null, "none of the boards' chats in a turn: no badge (another chat's turn is the Chat activity's)");
	assertEqual(boardChatsBadge([chat("a", "running"), chat("b", "running"), chat("c", "suspended")]), { count: 2, tone: "info", title: "2 chats running" });
	assertEqual(boardChatsBadge([chat("a", "running"), chat("b", "awaiting_approval"), chat("c", "awaiting_answer")]), { count: 2, tone: "warn", title: "1 chat running, 2 need you" },
		"those waiting on the owner first, as the Chat activity's");
});

test("board chats: a phone's More holds the badges of the activities under it", () => {
	assertEqual(moreBadge([]), null);
	assertEqual(moreBadge([{ count: 1, tone: "info" }, { count: 2, tone: "warn" }]), { count: 3, tone: "warn", title: "" }, "counts added, warn when any warns");
});

test("board chats: a row's mark and the topic's events", () => {
	assertEqual([chatMark(null), chatMark(chat("a", "idle")), chatMark(chat("a", "suspended"))], [null, null, null], "a resting chat marks nothing");
	assertEqual([chatMark(chat("a", "running")).label, chatMark(chat("a", "running")).spin, chatMark(chat("a", "awaiting_approval")).attention], ["Running", true, true]);
	let chats = [chat("a", "idle"), chat("b", "idle")];
	chats = foldChat(chats, "chat.changed", chat("a", "running"));
	assertEqual(chats.map((c) => c.chatId + ":" + c.state), ["b:idle", "a:running"], "a change replaces the chat");
	chats = foldChat(chats, "chat.deleted", { chatId: "b" });
	chats = foldChat(chats, "chat.created", chat("c", "idle"));
	assertEqual(chats.map((c) => c.chatId), ["a", "c"], "deleted ones leave, new ones come");
	assertEqual(foldChat(chats, "chat.purged", { chatId: "a" }), chats, "other events change nothing");
});

test("board chats: the Boards window marks a board while its chat runs or waits", () => {
	const topics = {};
	const ied = { client: { subscribe: (topic, handlers) => { topics[topic] = handlers; return () => delete topics[topic]; }, request: async () => null }, toast: () => {} };
	const el = document.createElement("div");
	const view = new BoardListView({ el, ied, wsId: "w1" }, () => {}, () => {});
	topics["ied.board/ws:w1"].snap({ groups: [], boards: [{ path: "a.iedboard", chatId: "c1" }, { path: "b.iedboard", chatId: "c2" }, { path: "c.iedboard" }] });
	const marks = () => [...el.querySelectorAll(".board-list-row")].map((row) => row.dataset.chat + (row.querySelector(".board-chat-flag") ? "!" : "") + (row.querySelector(".spin") ? "~" : ""));
	assertEqual(marks(), ["", "", ""], "before the chats are known: the boards as ever");
	topics["chats:w1"].snap({ chats: [chat("c1", "running"), chat("c2", "idle")] });
	assertEqual(marks(), ["Running~", "", ""], "a running chat turns on its board's row");
	topics["chats:w1"].evt("chat.changed", chat("c2", "awaiting_approval"));
	assertEqual(marks(), ["Running~", "Needs approval!", ""], "one waiting on the owner says on what");
	topics["chats:w1"].evt("chat.changed", chat("c1", "idle"));
	assertEqual(marks(), ["", "Needs approval!", ""], "a turn that ended takes its mark off");
	view.dispose();
	assertEqual(Object.keys(topics), [], "both topics let go");
});
