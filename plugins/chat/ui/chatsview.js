// chatsview.js - the Chats tool window (docs/15 "Chat activity"): every chat of the workspace (chats:<wsId>) with its
// state, those open as tabs in this window first, then the rest, then the archived ones, kept until the owner brings
// them back or deletes them (docs/04 "Archiving a chat"), then the deleted ones, kept for a while and restorable
// (docs/04 "Deleting a chat"); a sub-agent's chat sits under the chat that ran it (docs/04 "A group is a topology"). A
// click opens a chat as a tab; its menu renames, closes, archives or deletes it. Neither needs a confirmation: the toast
// offers Undo and the Archived or Deleted section keeps the chat.

import { h, icon, iconFrom, replace } from "/ui-kit/dom.js";
import { MenuHost } from "/ui-kit/menu.js";
import { confirmDialog } from "/ui-kit/dialog.js";
import { ChatList, STATE_LABELS, ago, chatLook, keptFor } from "./chatmodel.js";
import { archiveChat, chatRequest, deleteChat, restoreChat, unarchiveChat } from "./chatactions.js";
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
		// Archived starts folded as Deleted does, also for a list folded before there was an Archived section.
		if (!stored("ied.chat.archivedSeen", false)) {
			this.closed.add("archived");
			store("ied.chat.listClosed", [...this.closed]);
			store("ied.chat.archivedSeen", true);
		}
		this.opened = new Set(stored("ied.chat.listOpened", []));
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
		const kept = keptBySection(chats, root);
		const own = chats.filter((c) => !kept.of.has(c.chatId));
		const open = own.filter((c) => this.isOpen(root.get(c.chatId)));
		const rest = own.filter((c) => !this.isOpen(root.get(c.chatId)));
		const archived = this.list.archived.filter((a) => this.matches(a.title));
		const deleted = this.list.deleted.filter((d) => this.matches(d.title));
		const rows = (list) => nestAgents(list).map(({ chat, depth }) => this.chatRow(chat, now, depth));
		const sections = [
			open.length ? this.section("open", "Open", rows(open)) : null,
			rest.length ? this.section("recent", open.length ? "Recent" : "Chats", rows(rest)) : null,
			...kept.sections.map(([name, list]) => this.section("s:" + name, name, rows(list), true)),
			archived.length ? this.section("archived", "Archived", archived.map((a) => this.archivedRow(a, now))) : null,
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

	// A section of the list, open or closed as the owner left it on this device; a plugin's (`shut`: a board's chats)
	// starts closed, out of the way of the owner's own chats (docs/04 "Chat sections").
	section(id, title, rows, shut) {
		const kept = shut ? this.opened : this.closed;
		const closed = (shut ? !kept.has(id) : kept.has(id)) && !this.query;
		const toggle = () => {
			if (kept.has(id)) kept.delete(id);
			else kept.add(id);
			if (shut) store("ied.chat.listOpened", [...kept]);
			else store("ied.chat.listClosed", [...kept]);
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
			role: "treeitem", tabindex: "-1", title: (summary.agent ? "Agent " + summary.agent + ": " : "") + summary.title + " - " + state + (summary.state === "suspended" && summary.stateReason ? ": " + summary.stateReason : ""),
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

	archivedRow(entry, now) {
		return h("div.chats-row.archived", {
			role: "treeitem", tabindex: "-1", title: entry.title + " - archived " + new Date(entry.archivedMs).toLocaleDateString() + "; double-click to bring it back", dataset: { chat: entry.chatId, archived: "1" },
			on: { contextmenu: (event) => this.archivedMenu(event, entry), dblclick: () => this.unarchive(entry) },
		},
		icon("archive", "chats-state"), h("span.name", null, entry.title), h("span.meta", null, ago(entry.archivedMs || now, now)),
		h("button.ib.chats-restore", { title: "Unarchive", "aria-label": "Unarchive " + entry.title, on: { click: (event) => {
			event.stopPropagation();
			this.unarchive(entry);
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
			this.list.archives ? { kind: "item", label: "Archive", id: "archive", enabled: summary.state !== "running", keys: "Shift+Del" } : null,
			{ kind: "item", label: "Delete", id: "delete", enabled: summary.state !== "running", keys: "Del" },
		].filter(Boolean), pick: (row) => {
			if (row.id === "open") this.open(summary.chatId);
			else if (row.id === "rename") this.rename(summary);
			else if (row.id === "close") this.ied.tabs.close("chat:" + summary.chatId);
			else if (row.id === "archive") this.archive(summary);
			else if (row.id === "delete") this.remove(summary);
		} });
	}

	archivedMenu(event, entry) {
		event.preventDefault();
		menus.show({ at: event, label: entry.title, rows: [
			{ kind: "item", label: "Unarchive", id: "unarchive", enabled: true },
			{ kind: "separator" },
			{ kind: "item", label: "Delete", id: "delete", enabled: true, keys: "Del" },
		], pick: (row) => (row.id === "unarchive" ? this.unarchive(entry) : this.remove(entry)) });
	}

	archive(summary) {
		return archiveChat(this.ied, summary.chatId, summary.title);
	}

	unarchive(entry) {
		return unarchiveChat(this.ied, entry.chatId);
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
		const entry = row.dataset.deleted ? this.list.deleted.find((d) => d.chatId === chatId)
			: row.dataset.archived ? this.list.archived.find((a) => a.chatId === chatId) : this.list.find(chatId);
		if (!entry) return null;
		if (event.key === "Enter") {
			event.preventDefault();
			return row.dataset.deleted ? this.restore(entry) : row.dataset.archived ? this.unarchive(entry) : this.open(chatId);
		}
		if (event.key === "Delete" && event.shiftKey && !row.dataset.deleted && !row.dataset.archived) {
			event.preventDefault();
			return this.list.archives && entry.state !== "running" && this.archive(entry);
		}
		if (event.key === "Delete") {
			event.preventDefault();
			return row.dataset.deleted ? this.purge(entry) : row.dataset.archived ? this.remove(entry) : entry.state !== "running" && this.remove(entry);
		}
		if (event.key === "F2" && !row.dataset.deleted && !row.dataset.archived) {
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

// The chats a plugin keeps in a section of its own (summary.section; docs/04 "Chat sections"), a sub-agent's with its
// orchestrator's: {of: Set of their chatIds, sections: [[name, chats]] by name}.
export function keptBySection(chats, roots) {
	const byId = new Map(chats.map((c) => [c.chatId, c]));
	const named = new Map();
	const of = new Set();
	for (const chat of chats) {
		const top = byId.get(roots.get(chat.chatId)) || chat;
		const name = typeof top.section === "string" ? top.section.trim() : "";
		if (!name) continue;
		if (!named.has(name)) named.set(name, []);
		named.get(name).push(chat);
		of.add(chat.chatId);
	}
	return { of, sections: [...named].sort((a, b) => a[0].localeCompare(b[0])) };
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
