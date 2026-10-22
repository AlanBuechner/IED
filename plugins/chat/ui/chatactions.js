// chatactions.js - what the Chats list and a chat tab's menu both do to a chat (docs/04 "Deleting a chat"): delete it -
// kept under Deleted, with Undo on the toast - restore it, rename it.

import { confirmDialog, promptDialog } from "/ui-kit/dialog.js";
import { keptFor } from "./chatmodel.js";

// The request's answer, or null with its error as a toast.
export async function chatRequest(ied, method, params) {
	try {
		return await ied.client.request(method, params);
	}
	catch (e) {
		ied.toast(e.message, { level: "error" });
		return null;
	}
}

// `keeps`: the server keeps deleted chats (its chats list has `deleted`); an older one deletes for good, so that is
// confirmed first, as it always was.
export async function deleteChat(ied, chatId, title, keeps) {
	if (!keeps && !(await confirmDialog({ title: "Delete chat", message: "Delete \u201c" + title + "\u201d and its history? This server cannot restore it.", confirm: "Delete", danger: true }))) return;
	const done = await chatRequest(ied, "chat.delete", { chatId });
	if (!done) return;
	if (!done.deleted) {
		ied.toast("Deleted \u201c" + title + "\u201d.", { level: "info" });
		return;
	}
	const kept = keptFor(done.deleted.purgeMs, Date.now()).replace(/ left$/, "");
	ied.toast("Deleted \u201c" + title + "\u201d. It is kept " + kept + " under Deleted.", { level: "info", action: { label: "Undo", run: () => restoreChat(ied, chatId) } });
}

// Brought back, and opened as a tab.
export async function restoreChat(ied, chatId) {
	const restored = await chatRequest(ied, "chat.restore", { chatId });
	if (restored) ied.tabs.open("chat:" + restored.chatId, { preview: false });
}

export async function renameChat(ied, chatId, title) {
	const name = await promptDialog({ title: "Rename chat", value: title, confirm: "Rename" });
	if (name && name !== title) await chatRequest(ied, "chat.set", { chatId, title: name });
}
