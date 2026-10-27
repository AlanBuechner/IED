// turnnotices.js - a chat's news where the owner is not looking (owner, 2026-10-26: "toasts when the AI has finished or
// needs user input"; docs/15 "Chat"): when a chat's turn ends, or starts waiting on the owner - an approval, a question -
// every window not showing that chat gets a toast naming the chat and what happened, whose Open shows the chat there.
// A window in the background (hidden, minimized, another program in front) also gets a system notification - one per
// browser, whichever of its windows takes it first - whose click brings the window forward (the desktop app's through
// its bridge, docs/27) and shows the chat. ai.chats.notify turns either off.

const IN_A_TURN = new Set(["running", "awaiting_approval", "awaiting_answer"]);
const STOPS = { harness_error: "was stopped by an error in IED", no_model: "has no model to run on", interrupted: "was interrupted by a restart" };
const CLAIM_WAIT_MS = 300;   // a window in the background waits this long for a window in front to take the news
const CLAIM_KEEP_MS = 60 * 1000;

// What a chat's summary going from `before` to `after` tells its owner: {kind, text, level}, or null for nothing. A turn
// the owner stopped tells nothing, nor does a sub-agent's chat ending (its parent hears it).
export function turnNotice(before, after) {
	const was = before ? before.state : "idle";
	const name = "\"" + (after.title || "Chat") + "\"";
	if (after.state === "awaiting_approval" && was !== "awaiting_approval") return { kind: "approval", text: name + " needs your approval", level: "attention" };
	if (after.state === "awaiting_answer" && was !== "awaiting_answer") return { kind: "question", text: name + " asks you a question", level: "attention" };
	if (!IN_A_TURN.has(was) || IN_A_TURN.has(after.state)) return null;
	if (after.state === "paused") return { kind: "paused", text: name + " paused: " + String(after.stateReason || "it stopped").replace(/_/g, " "), level: "warning" };
	const stop = after.lastStop || "finished";
	if (stop === "user_cancelled" || after.parentChatId) return null;
	if (stop === "finished") return { kind: "finished", text: name + " finished", level: "info" };
	return { kind: "stopped", text: name + " " + (STOPS[stop] || "stopped: " + stop.replace(/_/g, " ")), level: "warning" };
}

// ai.chats.notify as the settings topic has it: "all", "toasts" or "off".
export function notifySetting(values) {
	const entry = values && values["ai.chats.notify"];
	return entry && ["all", "toasts", "off"].includes(entry.value) ? entry.value : "all";
}

// On the owner's own act (a message sent), the browser is asked once whether IED may show system notifications.
export function askToNotify(setting) {
	if (setting !== "all" || typeof Notification === "undefined" || Notification.permission !== "default") return;
	Notification.requestPermission().catch(() => {});
}

// The window's ways, as a test replaces them.
const PAGE = {
	shown: (chatId) => [...document.querySelectorAll(".view[data-ref='chat:" + chatId + "']")].some((view) => view.getClientRects().length > 0),
	inFront: () => document.visibilityState === "visible" && document.hasFocus(),
	wait: (ms) => new Promise((resolve) => setTimeout(resolve, ms)),
	// Whether this window takes `key` (no other window of the browser took it), taking it.
	claim: (key) => {
		try {
			const now = Date.now();
			for (let i = localStorage.length - 1; i >= 0; i--) {
				const name = localStorage.key(i);
				if (name && name.startsWith("ied.chat.notice:") && now - Number(localStorage.getItem(name)) > CLAIM_KEEP_MS) localStorage.removeItem(name);
			}
			if (localStorage.getItem(key)) return false;
			localStorage.setItem(key, String(now));
			return true;
		}
		catch {
			return true;
		}
	},
	system: (title, body, tag, clicked) => {
		if (typeof Notification === "undefined" || Notification.permission !== "granted") return;
		const note = new Notification(title, { body, tag });
		note.onclick = () => {
			note.close();
			clicked();
		};
	},
	toFront: () => {
		window.focus();
		if (window.iedDesktop && typeof window.iedDesktop.focus === "function") window.iedDesktop.focus();
	},
};

export class TurnNotices {
	// open(chatId): shows the chat in this window; page: PAGE's ways, for a test.
	constructor(ied, open, page = PAGE) {
		this.ied = ied;
		this.open = open;
		this.page = page;
		this.setting = "all";
	}

	// A chat's summary changed from `before` (null for a chat new to the list) to `after`. The window in front takes the
	// news first; one showing the chat takes it and says nothing; a window in the background waits, then takes what no
	// window took - a toast, and a system notification.
	async changed(before, after) {
		const notice = this.setting === "off" ? null : turnNotice(before, after);
		if (!notice) return;
		const key = "ied.chat.notice:" + after.chatId + ":" + notice.kind + ":" + (after.updatedMs || 0);
		const shown = this.page.shown(after.chatId);
		if (shown && this.page.inFront()) {
			this.page.claim(key);
			return;
		}
		const front = this.page.inFront();
		if (!front) await this.page.wait(CLAIM_WAIT_MS);
		if (!this.page.claim(key) && !front) return;
		const show = () => {
			this.page.toFront();
			this.open(after.chatId);
		};
		if (!shown) this.ied.toast(notice.text + ".", { level: notice.level, action: { label: "Open", run: show } });
		if (!front && this.setting === "all") this.page.system("IED: " + (after.title || "Chat"), notice.text + ".", "ied-chat-" + after.chatId, show);
	}
}
