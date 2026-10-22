// index.js - the chat plugin's UI module (docs/04, docs/15 "Chat"): the Chat activity, a chat as a tab of the editor
// area ("chat:<chatId>"), the status bar's "AI: 1 running, 1 needs you" (from chats:<wsId>) and Claude's plan usage (from
// aiusage), Settings > AI > Models, Model Groups and Claude, and the commands that open the activity, start a chat, stop
// the turn and show Claude's usage.

import { h, icon, replace } from "/ui-kit/dom.js";
import { ChatActivityView } from "./activity.js";
import { ChatTabView } from "./chattab.js";
import { ChatList } from "./chatmodel.js";
import { ModelsPage } from "./modelsettings.js";
import { ModelGroupsPage } from "./groupsettings.js";
import { ClaudePage, ClaudeStatusView } from "./claudeusage.js";

const views = new Set();

function last() {
	return [...views].pop() || null;
}

export function activate(ied) {
	if (!document.querySelector("link[href='" + ied.plugin.base + "ui/chat.css']")) document.head.append(h("link", { rel: "stylesheet", href: ied.plugin.base + "ui/chat.css" }));
	ied.commands.handle("ied.chat.show", () => ied.commands.run("shell.showActivity", { id: "ied.chat" }));
	ied.commands.handle("ied.chat.new", async () => {
		await ied.commands.run("shell.showActivity", { id: "ied.chat" });
		if (last()) last().newChat();
	});
	ied.commands.handle("ied.chat.stop", () => last() && last().stop());
	ied.commands.handle("ied.chat.claudeUsage", () => ied.commands.run("shell.settings", { page: "custom:ied.chat.claude" }));
	ied.tabs.kind("chat", { view: "ChatTab", title: () => "Chat", icon: () => "$(comment-discussion)" });
}

export function ChatActivity(ctx) {
	const view = new ChatActivityView(ctx);
	views.add(view);
	return { dispose: () => {
		views.delete(view);
		view.dispose();
	} };
}

export function ChatTab(ctx) {
	return new ChatTabView(ctx, ctx.ref.slice("chat:".length));
}

// Settings > AI > Models and Model Groups (the settingsPages of the manifest).
export function ModelsSettings(ctx) {
	return new ModelsPage(ctx);
}

export function ModelGroupsSettings(ctx) {
	return new ModelGroupsPage(ctx);
}

export function ClaudeSettings(ctx) {
	return new ClaudePage(ctx);
}

export function ClaudeStatus(ctx) {
	return ClaudeStatusView(ctx);
}

// What the status bar says of the workspace's chats: the running ones and those waiting on the owner.
export function chatStatusText(chats) {
	const running = chats.filter((c) => c.state === "running").length;
	const waiting = chats.filter((c) => c.state === "awaiting_approval" || c.state === "awaiting_answer").length;
	if (!running && !waiting) return null;
	const parts = [];
	if (running) parts.push(running + " running");
	if (waiting) parts.push(waiting + (waiting === 1 ? " needs you" : " need you"));
	return { text: "AI: " + parts.join(", "), attention: waiting > 0 };
}

export function ChatStatus(ctx) {
	const list = new ChatList();
	const render = () => {
		const status = chatStatusText(list.chats);
		ctx.el.hidden = !status;
		if (status) replace(ctx.el, icon("sparkle", "c-ai"), h("span" + (status.attention ? ".c-warn" : ""), null, status.text));
	};
	const stop = ctx.ied.client.subscribe("chats:" + ctx.wsId, {
		snap: (data) => {
			list.snapshot(data);
			render();
		},
		evt: (kind, data) => {
			list.event(kind, data);
			render();
		},
	});
	render();
	return { dispose: stop };
}
