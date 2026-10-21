// activity.js - the Chat activity (docs/15 "Chat", mockup 02): a tab per chat of the workspace (chats:<wsId>; chips on
// a phone), the selected chat's transcript (chat:<chatId>), the composer and the right panel. The server owns every
// chat; this view only shows one and sends what the owner does. Which chat is shown and which tabs are closed are this
// device's, kept in localStorage.

import { h, icon, replace } from "/ui-kit/dom.js";
import { MenuHost } from "/ui-kit/menu.js";
import { confirmDialog } from "/ui-kit/dialog.js";
import { ChatList, ChatModel, STATE_LABELS, tabGlyphs } from "./chatmodel.js";
import { TranscriptView } from "./transcript.js";
import { Composer } from "./composer.js";
import { SidePanel } from "./sidepanel.js";

const menus = new MenuHost();
const NEAR_BOTTOM_PX = 80;

function stored(key, fallback) {
	try {
		const value = localStorage.getItem(key);
		return value === null ? fallback : JSON.parse(value);
	}
	catch {
		return fallback;
	}
}

function store(key, value) {
	try {
		localStorage.setItem(key, JSON.stringify(value));
	}
	catch {
		// a private window: the choice lasts as long as the page
	}
}

// What the composer shows of a chat not made yet.
function draftSummary(draft) {
	return Object.assign({ title: "New chat", state: "idle" }, draft);
}

export class ChatActivityView {
	constructor(ctx) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.list = new ChatList();
		this.model = new ChatModel();
		this.chatId = null;
		this.stopChat = null;
		this.draft = { mode: "agent", permission: "ask", group: "" };
		this.model.summary = draftSummary(this.draft);
		this.closedTabs = new Set(stored("ied.chat.closed:" + ctx.wsId, []));
		const actions = this.actions();
		this.transcript = new TranscriptView(this.model, actions);
		this.composer = new Composer(actions);
		this.panel = new SidePanel(actions);
		this.panel.el.addEventListener("chat-panel-toggle", () => this.panel.update(this.model));
		this.showPanel = stored("ied.chat.panel", true);
		this.tabs = h("div.chat-tabs", { role: "tablist", "aria-label": "Chats" });
		this.panelButton = h("button.ib.desk", { title: "Details panel", "aria-label": "Details panel", on: { click: () => this.togglePanel() } }, icon("layout-sidebar-right"));
		this.tabBar = h("div.chat-tabbar", null, this.tabs,
			h("button.ib.chat-new", { title: "New chat (Ctrl+Alt+N)", "aria-label": "New chat", on: { click: () => this.newChat() } }, icon("add")),
			h("span.grow"),
			h("button.ib", { title: "All chats", "aria-label": "All chats", on: { click: (event) => this.allChats(event.currentTarget) } }, icon("history")),
			this.panelButton);
		this.scroller = h("div.chat-transcript", null, this.transcript.el);
		this.main = h("section.chat-main", null, this.tabBar, this.scroller, this.composer.el);
		this.root = h("div.chat-act" + (this.showPanel ? "" : ".no-panel"), null, this.main, this.panel.el);
		this.root.addEventListener("keydown", (event) => this.key(event));
		replace(ctx.el, this.root);
		this.stopList = this.ied.client.subscribe("chats:" + ctx.wsId, {
			snap: (data) => this.listed(data),
			evt: (kind, data) => this.listChanged(kind, data),
		});
		this.transcript.render();
		this.composer.update(this.model);
		this.panel.update(this.model);
	}

	actions() {
		return {
			approve: (approval, decision, message) => this.approve(approval, decision, message),
			allowAlways: (approval) => this.allowAlways(approval),
			answer: (question, text) => this.request("chat.answer", { chatId: this.chatId, questionId: question.questionId, answer: text }),
			send: (text, as) => this.send(text, as),
			stop: () => this.stop(),
			unqueue: (index) => this.request("chat.unqueue", { chatId: this.chatId, index }),
			set: (change) => this.setChat(change),
			groups: () => this.groups(),
			openFile: (path) => this.openFile(path),
			fileIcon: (path) => this.ied.icons.file(path),
			loadEarlier: () => this.loadEarlier(),
		};
	}

	async request(method, params) {
		try {
			return await this.ied.client.request(method, params);
		}
		catch (e) {
			this.ied.toast(e.message, { level: "error" });
			return null;
		}
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
			{ kind: "item", label: "Close Tab", id: "close", enabled: true },
			{ kind: "separator" },
			{ kind: "item", label: "Delete Chat\u2026", id: "delete", enabled: summary.state === "idle" || summary.state === "paused" },
		], pick: (row) => this.tabAction(row.id, summary) });
	}

	async tabAction(id, summary) {
		if (id === "close") this.closeTab(summary.chatId);
		else if (id === "rename") this.renameInPlace(summary);
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
		this.composer.focus();
	}

	// A draft keeps its group, mode and permission until its first message makes the chat with them.
	setChat(change) {
		if (this.chatId) return this.request("chat.set", Object.assign({ chatId: this.chatId }, change));
		if (change.mode && !change.permission) change.permission = change.mode === "plan" ? "plan" : "ask";
		Object.assign(this.draft, change);
		this.model.summary = draftSummary(this.draft);
		this.composer.update(this.model);
		return null;
	}

	async createChat() {
		const created = await this.request("chat.create", Object.assign({ wsId: this.ctx.wsId }, this.draft, this.draft.group ? {} : { group: undefined }));
		if (!created) return false;
		this.draft = { mode: "agent", permission: "ask", group: "" };
		this.list.event("chat.created", created);
		this.select(created.chatId);
		return true;
	}

	togglePanel() {
		this.showPanel = !this.showPanel;
		store("ied.chat.panel", this.showPanel);
		this.root.classList.toggle("no-panel", !this.showPanel);
	}

	// --- The chat shown ----------------------------------------------------------------------------------------------

	select(chatId) {
		if (this.stopChat) this.stopChat();
		this.stopChat = null;
		this.chatId = chatId;
		this.model = new ChatModel();
		this.transcript.model = this.model;
		if (!chatId) this.model.summary = draftSummary(this.draft);
		if (chatId) {
			store("ied.chat.selected:" + this.ctx.wsId, chatId);
			this.stopChat = this.ied.client.subscribe("chat:" + chatId, {
				snap: (data) => this.shown(data),
				evt: (kind, data) => this.changed(kind, data),
			});
		}
		this.transcript.render();
		this.composer.update(this.model);
		this.panel.update(this.model);
		this.renderTabs();
	}

	shown(data) {
		this.model.snapshot(data);
		this.transcript.render();
		this.composer.update(this.model);
		this.panel.update(this.model);
		this.scrollToEnd(true);
	}

	changed(kind, data) {
		const follow = this.nearBottom();
		const change = this.model.event(kind, data);
		this.transcript.update(change);
		if (change.summary || change.queued || change.turn || change.context || change.approvals || change.question) this.composer.update(this.model);
		if (change.summary || change.tasks || change.modified || change.context) this.panel.update(this.model);
		if (follow) this.scrollToEnd(false);
	}

	nearBottom() {
		const s = this.scroller;
		return s.scrollHeight - s.scrollTop - s.clientHeight < NEAR_BOTTOM_PX;
	}

	scrollToEnd(now) {
		const go = () => (this.scroller.scrollTop = this.scroller.scrollHeight);
		if (now) go();
		requestAnimationFrame(go);
	}

	async loadEarlier() {
		const first = this.model.turns[0];
		const older = await this.request("chat.history", { chatId: this.chatId, before: first ? first.turnId : undefined });
		if (!older) return;
		const height = this.scroller.scrollHeight;
		this.transcript.update(this.model.prepend(older.turns, older.hasMore));
		this.scroller.scrollTop += this.scroller.scrollHeight - height;
	}

	async send(text, as) {
		if (!this.chatId && !(await this.createChat())) return false;
		const sent = await this.request("chat.send", { chatId: this.chatId, text, as });
		if (sent && sent.injected) this.ied.toast("Given to the running turn at its next step.", { level: "info" });
		return sent !== null;
	}

	stop() {
		if (this.chatId) this.request("chat.stop", { chatId: this.chatId });
	}

	approve(approval, decision, message) {
		return this.request("chat.approve", Object.assign({ chatId: this.chatId, approvalId: approval.approvalId, decision }, message ? { message } : {}));
	}

	// "Always allow": the rule joins ai.permissions' allow list in the app tier (docs/11), then this call is allowed.
	async allowAlways(approval) {
		const ok = await confirmDialog({ title: "Always allow", message: "Add the rule " + approval.subject + " to the allow rules of ai.permissions (your settings, every workspace)? Calls it matches will run without asking.", confirm: "Always allow" });
		if (!ok) return;
		const settings = await this.request("settings.get", {});
		const current = settings && settings.values && settings.values["ai.permissions"] ? settings.values["ai.permissions"].value || {} : {};
		const allow = (current.allow || []).includes(approval.subject) ? current.allow : (current.allow || []).concat(approval.subject);
		if (await this.request("settings.set", { tier: "app", key: "ai.permissions", value: Object.assign({}, current, { allow }) })) await this.approve(approval, "allow");
	}

	async groups() {
		const settings = await this.request("settings.get", { wsId: this.ctx.wsId });
		const groups = settings && settings.values && settings.values["ai.groups"] ? settings.values["ai.groups"].value || {} : {};
		return Object.keys(groups);
	}

	openFile(path) {
		const opener = this.ied.contributions("fileOpener").pop();
		if (opener) this.ied.commands.run(opener.value.command, { path, line: 0, col: 0 });
		else this.ied.toast("No plugin opens files, so " + path + " is not shown.", { level: "warn" });
	}

	key(event) {
		if (!event.altKey || event.ctrlKey || event.metaKey) return;
		const approval = this.model.approvals[this.model.approvals.length - 1];
		if (!approval) return;
		if (event.key === "a" || event.key === "A") {
			event.preventDefault();
			this.approve(approval, "allow");
		}
		else if (event.key === "d" || event.key === "D") {
			event.preventDefault();
			const card = this.root.querySelector(".chat-approval[data-approval='" + approval.approvalId + "'] .chat-deny");
			if (card) {
				card.hidden = false;
				card.querySelector("input").focus();
			}
		}
	}

	dispose() {
		if (this.stopChat) this.stopChat();
		this.stopList();
	}
}
