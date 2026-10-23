// chatsview.js - the Chats tool window (docs/15 "Chat activity"): every chat of the workspace (chats:<wsId>) with its
// state, those open as tabs in this window first, then the rest, then the deleted ones, kept for a while and restorable
// (docs/04 "Deleting a chat"); a sub-agent's chat sits under the chat that ran it (docs/04 "A group is a topology"). A
// click opens a chat as a tab; its menu renames, closes or deletes it. Deleting needs no confirmation: the toast offers
// Undo and the Deleted section keeps the chat.

import { h, icon, iconFrom, replace } from "/ui-kit/dom.js";
import { MenuHost } from "/ui-kit/menu.js";
import { confirmDialog } from "/ui-kit/dialog.js";
import { ChatList, STATE_LABELS, ago, chatLook, keptFor } from "./chatmodel.js";
import { chatRequest, deleteChat, restoreChat } from "./chatactions.js";
import { stored, store } from "./stored.js";

const menus = new MenuHost();
const AGO_REFRESH_MS = 60 * 1000;

export class ChatsView {
	constructor(ctx) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.list = new ChatList();
		this.tabs = this.ied.tabs.state();
		this.query = "";
		this.closed = new Set(stored("ied.chat.listClosed", ["deleted"]));
		this.renaming = null;
		this.filter = h("input.input-field.chats-filter", { type: "search", placeholder: "Search chats", "aria-label": "Search chats", spellcheck: "false", on: {
			input: () => {
				this.query = this.filter.value.trim().toLowerCase();
				this.render();
			},
			keydown: (event) => {
				if (event.key === "ArrowDown") this.step(event, 1);
			},
		} });
		this.body = h("div.chats-body", { role: "tree", "aria-label": "Chats", on: { keydown: (event) => this.key(event) } });
		this.el = h("div.chats", null,
			h("div.chats-bar", null, this.filter,
				h("button.ib", { title: "New chat (Ctrl+Alt+N)", "aria-label": "New chat", on: { click: () => this.ied.commands.run("ied.chat.new") } }, icon("add"))),
			this.body);
		ctx.el.append(this.el);
		this.stopList = this.ied.client.subscribe("chats:" + ctx.wsId, {
			snap: (data) => {
				this.list.snapshot(data);
				this.render();
			},
			evt: (kind, data) => {
				this.list.event(kind, data);
				this.render();
			},
		});
		this.stopTabs = this.ied.tabs.onChange((state) => {
			this.tabs = state;
			this.render();
		});
		this.timer = setInterval(() => this.render(), AGO_REFRESH_MS);
		this.render();
	}

	isOpen(chatId) {
		return this.tabs.open.includes("chat:" + chatId);
	}

	matches(title) {
		return !this.query || (title || "").toLowerCase().includes(this.query);
	}

	// --- Drawing -----------------------------------------------------------------------------------------------------

	render() {
		if (this.renaming) return;
		const focused = document.activeElement && this.body.contains(document.activeElement) ? document.activeElement.dataset.chat : null;
		const now = Date.now();
		const chats = this.list.chats.filter((c) => this.matches(c.title));
		const root = rootsOf(chats);
		const open = chats.filter((c) => this.isOpen(root.get(c.chatId)));
		const rest = chats.filter((c) => !this.isOpen(root.get(c.chatId)));
		const deleted = this.list.deleted.filter((d) => this.matches(d.title));
		const sections = [
			open.length ? this.section("open", "Open", nestAgents(open).map(({ chat, depth }) => this.chatRow(chat, now, depth))) : null,
			rest.length ? this.section("recent", open.length ? "Recent" : "Chats", nestAgents(rest).map(({ chat, depth }) => this.chatRow(chat, now, depth))) : null,
			deleted.length ? this.section("deleted", "Deleted", deleted.map((d) => this.deletedRow(d, now))) : null,
		].filter(Boolean);
		if (!sections.length) sections.push(this.emptyEl());
		replace(this.body, ...sections);
		const again = focused ? this.body.querySelector(".chats-row[data-chat='" + focused + "']") : null;
		if (again) again.focus({ preventScroll: true });
	}

	emptyEl() {
		if (this.query) return h("div.chats-empty.muted", null, "No chat matches \u201c" + this.filter.value.trim() + "\u201d.");
		return h("div.chats-empty", null, h("p.muted", null, "No chats yet. A chat runs on the server: close the browser and it keeps going."),
			h("button.btn", { on: { click: () => this.ied.commands.run("ied.chat.new") } }, icon("add"), "New chat"));
	}

	section(id, title, rows) {
		const closed = this.closed.has(id) && !this.query;
		const toggle = () => {
			if (this.closed.has(id)) this.closed.delete(id);
			else this.closed.add(id);
			store("ied.chat.listClosed", [...this.closed]);
			this.render();
		};
		return h("section.chats-sec" + (closed ? ".closed" : ""), { dataset: { section: id } },
			h("button.chats-sec-head", { "aria-expanded": closed ? "false" : "true", on: { click: toggle } }, icon(closed ? "chevron-right" : "chevron-down"), h("span", null, title), h("span.meta", null, String(rows.length))),
			closed ? null : h("div.chats-rows", { role: "group" }, ...rows));
	}

	chatRow(summary, now, depth) {
		const look = chatLook(summary);
		const shown = this.tabs.shown.includes("chat:" + summary.chatId);
		const state = STATE_LABELS[summary.state] || summary.state;
		return h("div.chats-row" + (shown ? ".shown" : "") + (look.attention ? ".attn" : "") + (depth ? ".sub" : ""), {
			role: "treeitem", tabindex: "-1", title: (summary.agent ? "Agent " + summary.agent + ": " : "") + summary.title + " - " + state,
			"aria-level": String((depth || 0) + 1), style: { "--depth": String(depth || 0) }, dataset: { chat: summary.chatId, state: summary.state },
			on: {
				click: () => this.open(summary.chatId),
				auxclick: (event) => event.button === 1 && this.isOpen(summary.chatId) && this.ied.tabs.close("chat:" + summary.chatId),
				contextmenu: (event) => this.chatMenu(event, summary),
			},
		},
		iconFrom(look.icon, null, "chats-state " + look.tone), h("span.name", null, summary.title),
		look.attention ? h("span.chats-flag", null, state) : null,
		h("span.meta", null, look.tasks || ago(summary.updatedMs || now, now)));
	}

	deletedRow(entry, now) {
		return h("div.chats-row.deleted", {
			role: "treeitem", tabindex: "-1", title: entry.title + " - deleted; restorable until " + new Date(entry.purgeMs).toLocaleDateString(), dataset: { chat: entry.chatId, deleted: "1" },
			on: { contextmenu: (event) => this.deletedMenu(event, entry), dblclick: () => this.restore(entry) },
		},
		icon("trash", "chats-state"), h("span.name", null, entry.title), h("span.meta", null, keptFor(entry.purgeMs, now)),
		h("button.ib.chats-restore", { title: "Restore", "aria-label": "Restore " + entry.title, on: { click: (event) => {
			event.stopPropagation();
			this.restore(entry);
		} } }, icon("discard")));
	}

	// --- What the owner does -----------------------------------------------------------------------------------------

	open(chatId) {
		this.ied.tabs.open("chat:" + chatId, { preview: false });
	}

	chatMenu(event, summary) {
		event.preventDefault();
		const open = this.isOpen(summary.chatId);
		menus.show({ at: event, label: summary.title, rows: [
			{ kind: "item", label: "Open", id: "open", enabled: true },
			{ kind: "item", label: "Rename", id: "rename", enabled: true, keys: "F2" },
			{ kind: "item", label: "Close Tab", id: "close", enabled: open },
			{ kind: "separator" },
			{ kind: "item", label: "Delete", id: "delete", enabled: summary.state !== "running", keys: "Del" },
		], pick: (row) => {
			if (row.id === "open") this.open(summary.chatId);
			else if (row.id === "rename") this.rename(summary);
			else if (row.id === "close") this.ied.tabs.close("chat:" + summary.chatId);
			else if (row.id === "delete") this.remove(summary);
		} });
	}

	deletedMenu(event, entry) {
		event.preventDefault();
		menus.show({ at: event, label: entry.title, rows: [
			{ kind: "item", label: "Restore", id: "restore", enabled: true },
			{ kind: "separator" },
			{ kind: "item", label: "Delete Permanently\u2026", id: "purge", enabled: true, keys: "Del" },
		], pick: (row) => (row.id === "restore" ? this.restore(entry) : this.purge(entry)) });
	}

	remove(summary) {
		return deleteChat(this.ied, summary.chatId, summary.title, this.list.keepsDeleted);
	}

	restore(entry) {
		return restoreChat(this.ied, entry.chatId);
	}

	async purge(entry) {
		const sure = await confirmDialog({ title: "Delete permanently", message: "Delete \u201c" + entry.title + "\u201d and its history for good? It cannot be restored.", confirm: "Delete", danger: true });
		if (sure) await chatRequest(this.ied, "chat.purge", { chatId: entry.chatId });
	}

	// The row's title becomes a field (VS renames in place): Enter keeps it, Escape or an empty one leaves it as it was.
	rename(summary) {
		const row = this.body.querySelector(".chats-row[data-chat='" + summary.chatId + "']:not(.deleted)");
		if (!row) return;
		const field = h("input.input-field.chats-rename", { value: summary.title, "aria-label": "Chat name", spellcheck: "false" });
		const finish = async (keep) => {
			if (this.renaming !== field) return;
			this.renaming = null;
			const title = field.value.trim();
			if (keep && title && title !== summary.title) await chatRequest(this.ied, "chat.set", { chatId: summary.chatId, title });
			this.render();
			const again = this.body.querySelector(".chats-row[data-chat='" + summary.chatId + "']");
			if (again) again.focus();
		};
		field.addEventListener("keydown", (event) => {
			event.stopPropagation();
			if (event.key === "Enter") finish(true);
			else if (event.key === "Escape") finish(false);
		});
		field.addEventListener("blur", () => finish(true));
		for (const kind of ["click", "dblclick", "mousedown"]) field.addEventListener(kind, (event) => event.stopPropagation());
		this.renaming = field;
		row.querySelector(".name").replaceWith(field);
		field.focus();
		field.select();
	}

	// --- Keys --------------------------------------------------------------------------------------------------------

	rows() {
		return [...this.body.querySelectorAll(".chats-row")];
	}

	step(event, by) {
		const rows = this.rows();
		if (!rows.length) return;
		event.preventDefault();
		const at = rows.indexOf(document.activeElement);
		rows[Math.max(0, Math.min(rows.length - 1, at < 0 ? 0 : at + by))].focus();
	}

	key(event) {
		const row = event.target.closest(".chats-row");
		if (event.key === "ArrowDown" || event.key === "ArrowUp") return this.step(event, event.key === "ArrowDown" ? 1 : -1);
		if (!row || event.target !== row) return null;
		const chatId = row.dataset.chat;
		const entry = row.dataset.deleted ? this.list.deleted.find((d) => d.chatId === chatId) : this.list.find(chatId);
		if (!entry) return null;
		if (event.key === "Enter") {
			event.preventDefault();
			return row.dataset.deleted ? this.restore(entry) : this.open(chatId);
		}
		if (event.key === "Delete") {
			event.preventDefault();
			return row.dataset.deleted ? this.purge(entry) : entry.state !== "running" && this.remove(entry);
		}
		if (event.key === "F2" && !row.dataset.deleted) {
			event.preventDefault();
			this.rename(entry);
		}
		return null;
	}

	focus() {
		const shown = this.body.querySelector(".chats-row.shown") || this.rows()[0];
		if (shown) shown.focus();
		else this.filter.focus();
	}

	dispose() {
		clearInterval(this.timer);
		this.stopList();
		this.stopTabs();
	}
}

// Each chat's topmost ancestor in `chats` (itself for a chat no listed chat ran): a sub-agent's chat goes in its
// orchestrator's section.
export function rootsOf(chats) {
	const byId = new Map(chats.map((c) => [c.chatId, c]));
	const roots = new Map();
	for (const chat of chats) {
		let at = chat;
		const seen = new Set();
		while (at.parentChatId && byId.has(at.parentChatId) && !seen.has(at.chatId)) {
			seen.add(at.chatId);
			at = byId.get(at.parentChatId);
		}
		roots.set(chat.chatId, at.chatId);
	}
	return roots;
}

// The chats in order with each sub-agent's chat right under the chat that ran it ([{chat, depth}]); one whose parent is
// not in the list stays where it is.
export function nestAgents(chats) {
	const ids = new Set(chats.map((c) => c.chatId));
	const children = new Map();
	for (const chat of chats) {
		if (!chat.parentChatId || !ids.has(chat.parentChatId)) continue;
		if (!children.has(chat.parentChatId)) children.set(chat.parentChatId, []);
		children.get(chat.parentChatId).push(chat);
	}
	const out = [];
	const placed = new Set();
	const place = (chat, depth) => {
		if (placed.has(chat.chatId)) return;
		placed.add(chat.chatId);
		out.push({ chat, depth });
		for (const child of children.get(chat.chatId) || []) place(child, depth + 1);
	};
	for (const chat of chats)
		if (!chat.parentChatId || !ids.has(chat.parentChatId)) place(chat, 0);
	for (const chat of chats) place(chat, 0);
	return out;
}
