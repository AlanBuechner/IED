// activity.js - the Chat activity (docs/15 "Chat", mockup 02): a tab per chat of the workspace (chats:<wsId>; chips on
// a phone) above the chat shown (chatpane.js). The server owns every chat; which one is shown and which tabs are closed
// are this device's, kept in localStorage. A chat opens as a tab of the editor area too ("Open in Editor Area").

import { h, icon, replace } from "/ui-kit/dom.js";
import { MenuHost } from "/ui-kit/menu.js";
import { confirmDialog } from "/ui-kit/dialog.js";
import { ChatList, STATE_LABELS, tabGlyphs } from "./chatmodel.js";
import { ChatPane } from "./chatpane.js";
import { stored, store } from "./stored.js";

const menus = new MenuHost();

export class ChatActivityView {
	constructor(ctx) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.list = new ChatList();
		this.closedTabs = new Set(stored("ied.chat.closed:" + ctx.wsId, []));
		this.tabs = h("div.chat-tabs", { role: "tablist", "aria-label": "Chats" });
		this.tabBar = h("div.chat-tabbar", null, this.tabs,
			h("button.ib.chat-new", { title: "New chat (Ctrl+Alt+N)", "aria-label": "New chat", on: { click: () => this.newChat() } }, icon("add")),
			h("span.grow"),
			h("button.ib", { title: "All chats", "aria-label": "All chats", on: { click: (event) => this.allChats(event.currentTarget) } }, icon("history")));
		this.pane = new ChatPane(ctx.ied, ctx.wsId, {
			header: this.tabBar,
			created: (created) => this.created(created),
			openChat: (chatId) => this.select(chatId),
		});
		this.tabBar.append(...this.pane.panelButtons());
		replace(ctx.el, this.pane.el);
		this.stopList = this.ied.client.subscribe("chats:" + ctx.wsId, {
			snap: (data) => this.listed(data),
			evt: (kind, data) => this.listChanged(kind, data),
		});
	}

	get chatId() {
		return this.pane.chatId;
	}

	async request(method, params) {
		return this.pane.request(method, params);
	}

	// --- The list and the tabs ---------------------------------------------------------------------------------------

	listed(data) {
		this.list.snapshot(data);
		const remembered = stored("ied.chat.selected:" + this.ctx.wsId, null);
		const pick = this.chatId && this.list.find(this.chatId) ? this.chatId : remembered && this.list.find(remembered) ? remembered : this.openChats()[0] ? this.openChats()[0].chatId : null;
		if (pick !== this.chatId) this.select(pick);
		this.renderTabs();
	}

	listChanged(kind, data) {
		this.list.event(kind, data);
		if (kind === "chat.deleted" && data.chatId === this.chatId) this.select(this.openChats()[0] ? this.openChats()[0].chatId : null);
		this.renderTabs();
	}

	openChats() {
		return this.list.chats.filter((c) => !this.closedTabs.has(c.chatId) || c.chatId === this.chatId);
	}

	renderTabs() {
		replace(this.tabs, ...this.openChats().map((summary) => {
			const glyphs = tabGlyphs(summary);
			const on = summary.chatId === this.chatId;
			return h("div.chat-tab" + (on ? ".on" : ""), {
				role: "tab", "aria-selected": on ? "true" : "false", tabindex: "0", title: summary.title + " - " + (STATE_LABELS[summary.state] || summary.state), dataset: { chat: summary.chatId, state: summary.state },
				on: {
					click: () => this.select(summary.chatId),
					keydown: (event) => event.key === "Enter" && this.select(summary.chatId),
					auxclick: (event) => event.button === 1 && this.closeTab(summary.chatId),
					contextmenu: (event) => this.tabMenu(event, summary),
				},
			}, glyphs.running ? icon("loading", "spin c-info") : icon("comment-discussion"), h("span.name", null, summary.title),
			glyphs.attention ? h("span.glyph.attn", { title: STATE_LABELS[summary.state] }, "!") : null, glyphs.tasks ? h("span.glyph", null, glyphs.tasks) : null,
			h("button.ib.x", { title: "Close the tab (the chat stays)", "aria-label": "Close " + summary.title, on: { click: (event) => {
				event.stopPropagation();
				this.closeTab(summary.chatId);
			} } }, icon("close")));
		}));
	}

	closeTab(chatId) {
		this.closedTabs.add(chatId);
		store("ied.chat.closed:" + this.ctx.wsId, [...this.closedTabs]);
		if (chatId === this.chatId) {
			const next = this.openChats().find((c) => c.chatId !== chatId);
			this.select(next ? next.chatId : null);
		}
		this.renderTabs();
	}

	tabMenu(event, summary) {
		event.preventDefault();
		menus.show({ at: event, label: summary.title, rows: [
			{ kind: "item", label: "Rename\u2026", id: "rename", enabled: true },
			{ kind: "item", label: "Open in Editor Area", id: "tab", enabled: true },
			{ kind: "item", label: "Close Tab", id: "close", enabled: true },
			{ kind: "separator" },
			{ kind: "item", label: "Delete Chat\u2026", id: "delete", enabled: summary.state === "idle" || summary.state === "paused" },
		], pick: (row) => this.tabAction(row.id, summary) });
	}

	async tabAction(id, summary) {
		if (id === "close") this.closeTab(summary.chatId);
		else if (id === "rename") this.renameInPlace(summary);
		else if (id === "tab") this.ied.tabs.open("chat:" + summary.chatId, { preview: false });
		else if (id === "delete" && await confirmDialog({ title: "Delete chat", message: "Delete \u201c" + summary.title + "\u201d and its history? This cannot be undone.", confirm: "Delete", danger: true })) {
			await this.request("chat.delete", { chatId: summary.chatId });
		}
	}

	// The tab's title becomes a field (VS renames in place): Enter keeps it, Escape or an empty one leaves it as it was.
	renameInPlace(summary) {
		const tab = this.tabs.querySelector(".chat-tab[data-chat='" + summary.chatId + "']");
		if (!tab) return;
		const field = h("input.input-field.chat-rename", { value: summary.title, "aria-label": "Chat name", spellcheck: "false" });
		let done = false;
		const finish = async (keep) => {
			if (done) return;
			done = true;
			const title = field.value.trim();
			if (keep && title && title !== summary.title) await this.request("chat.set", { chatId: summary.chatId, title });
			this.renderTabs();
		};
		field.addEventListener("keydown", (event) => {
			event.stopPropagation();
			if (event.key === "Enter") finish(true);
			else if (event.key === "Escape") finish(false);
		});
		field.addEventListener("blur", () => finish(true));
		for (const kind of ["click", "dblclick", "mousedown"]) field.addEventListener(kind, (event) => event.stopPropagation());
		replace(tab, icon("comment-discussion"), field);
		field.focus();
		field.select();
	}

	allChats(at) {
		const rows = this.list.chats.map((c) => ({ kind: "item", label: c.title, chatId: c.chatId, enabled: true, keys: STATE_LABELS[c.state] || "", icon: c.chatId === this.chatId ? "$(check)" : null }));
		menus.show({ at, label: "All chats", rows: rows.length ? rows : [{ kind: "item", label: "No chats yet", enabled: false }], pick: (row) => {
			this.closedTabs.delete(row.chatId);
			store("ied.chat.closed:" + this.ctx.wsId, [...this.closedTabs]);
			this.select(row.chatId);
		} });
	}

	// A new chat starts as a draft: the box is ready at once and the chat is made by the first message, so keys typed
	// meanwhile are kept and nothing leaves an empty chat behind.
	newChat() {
		this.select(null);
		this.pane.focus();
	}

	created(created) {
		this.list.event("chat.created", created);
		this.select(created.chatId);
	}

	select(chatId) {
		if (chatId) store("ied.chat.selected:" + this.ctx.wsId, chatId);
		this.pane.show(chatId);
		this.renderTabs();
	}

	stop() {
		this.pane.stop();
	}

	dispose() {
		this.pane.dispose();
		this.stopList();
	}
}
