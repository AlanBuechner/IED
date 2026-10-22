// chattab.js - a chat as a tab ("chat:<chatId>", docs/15 "Chat activity"): the Chat activity's document well holds them,
// docked like any tab - split, dragged, moved to the Editor's well. "chat:new" is a draft: its first message makes the
// chat, and the tab becomes the chat's in its place. The tab is titled by the chat, its icon says the chat's state, and
// it closes when the chat is deleted. Tabs it opens from its details panel open beside it and are tethered to it.

import { h, icon } from "/ui-kit/dom.js";
import { ChatPane } from "./chatpane.js";
import { STATE_LABELS, chatLook } from "./chatmodel.js";

export const DRAFT = "new";

export class ChatTabView {
	constructor(ctx, chatId) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.chatId = chatId === DRAFT ? null : chatId;
		this.summary = null;
		this.keepsDeleted = false;   // whether the server keeps deleted chats (chatactions.js deleteChat)
		this.state = h("span.chat-tabhead-state");
		this.header = h("div.chat-tabbar.chat-tabhead", null, this.state, h("span.grow"),
			h("button.ib.chat-phone-only", { title: "All chats", "aria-label": "All chats", on: { click: () => this.ied.commands.run("shell.showView", { id: "ied.chat.list" }) } }, icon("list-flat")));
		this.pane = new ChatPane(ctx.ied, ctx.wsId, {
			header: this.header,
			tabRef: ctx.ref,
			created: (created) => this.created(created),
			openChat: (id) => this.ied.tabs.open("chat:" + id, { preview: false }),
			shown: (summary) => this.shown(summary),
		});
		this.header.append(...this.pane.panelButtons());
		ctx.el.append(this.pane.el);
		this.stopList = this.chatId ? this.ied.client.subscribe("chats:" + ctx.wsId, { snap: (data) => this.listed(data), evt: (kind, data) => this.listChanged(kind, data) }) : () => {};
		this.pane.show(this.chatId);
	}

	// The draft's first message made the chat: it shows here while the turn starts, and the chat's own tab takes the
	// draft's place (opened after it, then the draft closed).
	created(created) {
		this.pane.show(created.chatId);
		this.ied.tabs.open("chat:" + created.chatId, { preview: false });
		this.ctx.tab.close();
	}

	shown(summary) {
		if (!summary || !this.chatId) return;
		this.summary = summary;
		const look = chatLook(summary);
		this.ctx.tab.setTitle(summary.title);
		this.ctx.tab.setIcon(look.icon);
		const label = look.attention || summary.state === "running" ? STATE_LABELS[summary.state] || summary.state : "";
		this.state.className = "chat-tabhead-state" + (look.tone ? " " + look.tone : "");
		this.state.textContent = label;
	}

	// A tab the layout kept for a chat deleted while this window was away closes too - when the list says it is deleted,
	// never merely for missing it (a plugin still starting lists nothing).
	listed(data) {
		this.keepsDeleted = Array.isArray(data.deleted);
		if ((data.deleted || []).some((d) => d.chatId === this.chatId)) this.ctx.tab.close();
	}

	// The chat was deleted (here or on another device): its tab closes.
	listChanged(kind, data) {
		if ((kind === "chat.deleted" || kind === "chat.purged") && data.chatId === this.chatId) this.ctx.tab.close();
	}

	focus() {
		this.pane.focus();
	}

	stop() {
		this.pane.stop();
	}

	dispose() {
		this.stopList();
		this.pane.dispose();
	}
}
