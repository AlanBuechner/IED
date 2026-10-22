// chattab.js - a chat as a tab of the editor area ("chat:<chatId>", docs/15 "Chat"): the chat shown on its own, so it
// can stand beside the page it drives or the file it edits. Tabs it opens from its details panel open beside it and are
// tethered to it; its tab is titled by the chat.

import { h, icon } from "/ui-kit/dom.js";
import { ChatPane } from "./chatpane.js";

export class ChatTabView {
	constructor(ctx, chatId) {
		this.ctx = ctx;
		this.header = h("div.chat-tabbar.chat-tabhead", null,
			h("span.chat-tabhead-title", null, icon("comment-discussion"), h("span.name", null, "Chat")),
			h("span.grow"),
			h("button.ib", { title: "Show in the Chat activity", "aria-label": "Show in the Chat activity", on: { click: () => ctx.ied.commands.run("ied.chat.show") } }, icon("comment-discussion")));
		this.pane = new ChatPane(ctx.ied, ctx.wsId, {
			header: this.header,
			tabRef: ctx.ref,
			created: () => {},
			openChat: (id) => ctx.ied.tabs.open("chat:" + id, { preview: false }),
			shown: (summary) => this.titled(summary),
		});
		this.header.append(...this.pane.panelButtons());
		ctx.el.append(this.pane.el);
		this.pane.show(chatId);
	}

	titled(summary) {
		if (!summary || !summary.title) return;
		this.ctx.tab.setTitle(summary.title);
		this.header.querySelector(".chat-tabhead-title .name").textContent = summary.title;
	}

	focus() {
		this.pane.focus();
	}

	dispose() {
		this.pane.dispose();
	}
}
