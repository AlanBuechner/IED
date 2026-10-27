// chatnotices.test.js - a chat's news where the owner is not looking (plugins/chat turnnotices.js, docs/15 "Chat"): a
// turn that ends or waits on the owner is a toast in every window not showing the chat, whose Open shows it; a window in
// the background also gets a system notification, one per browser, whose click brings the window forward.

import { test, assertEqual } from "./harness.js";
import { TurnNotices, notifySetting, turnNotice } from "/plugins/ied.chat/ui/turnnotices.js";

const chat = (state, extra) => Object.assign({ chatId: "c1", title: "Fix the build", state, updatedMs: 1 }, extra);

test("chat notices: a turn that ends or waits on the owner is news; one the owner stopped is not", () => {
	const kind = (before, after) => (turnNotice(before, after) || { kind: null }).kind;
	assertEqual(turnNotice(chat("running"), chat("idle", { lastStop: "finished" })), { kind: "finished", text: "\"Fix the build\" finished", level: "info" });
	assertEqual(kind(chat("running"), chat("awaiting_approval")), "approval", "an approval");
	assertEqual(kind(chat("running"), chat("awaiting_answer")), "question", "a question");
	assertEqual(kind(chat("awaiting_approval"), chat("awaiting_approval")), null, "the same wait again");
	assertEqual(kind(chat("awaiting_approval"), chat("running")), null, "answered: back to work");
	assertEqual(kind(chat("running"), chat("paused", { stateReason: "usage_limit" })), "paused");
	assertEqual(turnNotice(chat("running"), chat("paused", { stateReason: "usage_limit" })).text, "\"Fix the build\" paused: usage limit");
	assertEqual(kind(chat("running"), chat("idle", { lastStop: "user_cancelled" })), null, "the owner stopped it");
	assertEqual(kind(chat("running"), chat("idle", { lastStop: "harness_error" })), "stopped");
	assertEqual(kind(chat("running"), chat("idle", { lastStop: "finished", parentChatId: "c0" })), null, "a sub-agent's chat: its parent hears it");
	assertEqual(kind(chat("running"), chat("awaiting_approval", { parentChatId: "c0" })), "approval", "a sub-agent waiting on the owner still says so");
	assertEqual(kind(chat("idle"), chat("idle", { title: "Renamed" })), null, "a rename");
	assertEqual([notifySetting({}), notifySetting({ "ai.chats.notify": { value: "off" } }), notifySetting({ "ai.chats.notify": { value: "nonsense" } })], ["all", "off", "all"]);
});

// A window: whether it is in front and shows the chat, and what it did. `claims` is shared by the windows of a browser.
function windowOf(claims, front, shown) {
	const did = [];
	const page = {
		shown: () => shown,
		inFront: () => front,
		wait: async () => {},
		claim: (key) => (claims.has(key) ? false : Boolean(claims.add(key))),
		system: (title, body, tag, clicked) => did.push(["system", title, body, tag, clicked]),
		toFront: () => did.push(["front"]),
	};
	const opened = [];
	const notices = new TurnNotices({ toast: (text, options) => did.push(["toast", text, options]) }, (chatId) => opened.push(chatId), page);
	return { notices, did, opened };
}

test("chat notices: a window in front not showing the chat gets a toast whose Open shows it there", async () => {
	const claims = new Set();
	const here = windowOf(claims, true, false);
	await here.notices.changed(chat("running"), chat("idle", { lastStop: "finished" }));
	assertEqual(here.did.map((d) => d[0]), ["toast"], "a toast, no system notification");
	assertEqual([here.did[0][1], here.did[0][2].level, here.did[0][2].action.label], ["\"Fix the build\" finished.", "info", "Open"]);
	here.did[0][2].action.run();
	assertEqual([here.opened, here.did[1]], [["c1"], ["front"]], "Open shows the chat, the window brought forward");
	const behind = windowOf(claims, false, false);
	await behind.notices.changed(chat("running"), chat("idle", { lastStop: "finished" }));
	assertEqual(behind.did, [], "another window of the browser in the background: the news is taken");
});

test("chat notices: the window showing the chat says nothing, and no window of the browser does", async () => {
	const claims = new Set();
	const showing = windowOf(claims, true, true);
	await showing.notices.changed(chat("running"), chat("awaiting_approval"));
	assertEqual(showing.did, [], "the owner sees it");
	const behind = windowOf(claims, false, false);
	await behind.notices.changed(chat("running"), chat("awaiting_approval"));
	assertEqual(behind.did, [], "nor a window in the background");
});

test("chat notices: a window in the background gets a toast and one system notification; the setting turns them off", async () => {
	const claims = new Set();
	const first = windowOf(claims, false, false);
	const second = windowOf(claims, false, false);
	await first.notices.changed(chat("running"), chat("awaiting_answer"));
	await second.notices.changed(chat("running"), chat("awaiting_answer"));
	assertEqual(first.did.map((d) => d[0]), ["toast", "system"]);
	assertEqual(first.did[1].slice(1, 4), ["IED: Fix the build", "\"Fix the build\" asks you a question.", "ied-chat-c1"]);
	first.did[1][4]();
	assertEqual([first.opened, first.did[2]], [["c1"], ["front"]], "its click brings the window forward and shows the chat");
	assertEqual(second.did, [], "one per browser");
	const toasts = windowOf(new Set(), false, false);
	toasts.notices.setting = "toasts";
	await toasts.notices.changed(chat("running"), chat("idle", { lastStop: "finished" }));
	assertEqual(toasts.did.map((d) => d[0]), ["toast"], "\"toasts\": no system notification");
	const off = windowOf(new Set(), true, false);
	off.notices.setting = "off";
	await off.notices.changed(chat("running"), chat("idle", { lastStop: "finished" }));
	assertEqual(off.did, [], "\"off\": nothing");
});
