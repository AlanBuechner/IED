// index.js - the chat plugin's UI module (docs/04, docs/15 "Chat activity"): chats as tabs ("chat:<chatId>"; "chat:new"
// a draft) in the Chat activity's document well, the Chats tool window (every chat and its state, the deleted ones),
// the status bar's "AI: 1 running, 1 needs you", the Chat activity's badge and the toasts of a turn that ended or waits
// on the owner (from chats:<wsId>), Claude's plan usage
// (from aiusage), Settings > AI > Models, Model Groups and Claude, and the commands that open the activity, start, stop,
// rename and delete chats and show Claude's usage.

import { h, icon, replace } from "/ui-kit/dom.js";
import { ChatTabView, DRAFT } from "./chattab.js";
import { ChatsView } from "./chatsview.js";
import { ChatList } from "./chatmodel.js";
import { deleteChat, renameChat } from "./chatactions.js";
import { ModelsPage } from "./modelsettings.js";
import { ModelGroupsPage } from "./groupsettings.js";
import { ClaudePage, ClaudeStatusView } from "./claudeusage.js";
import { TurnNotices, notifySetting } from "./turnnotices.js";

const tabViews = new Set();   // this window's chat tabs, the one focused last at the end

// The chat tab a command means: the one its menu was opened on ({ref}), else the one focused last.
function tabOf(args) {
	const views = [...tabViews];
	return args && args.ref ? views.find((view) => view.ctx.ref === args.ref) || null : views.pop() || null;
}

export function activate(ied) {
	if (!document.querySelector("link[href='" + ied.plugin.base + "ui/chat.css']")) document.head.append(h("link", { rel: "stylesheet", href: ied.plugin.base + "ui/chat.css" }));
	ied.commands.handle("ied.chat.show", () => ied.commands.run("shell.showActivity", { id: "ied.chat" }));
	ied.commands.handle("ied.chat.new", async () => {
		await ied.commands.run("shell.showActivity", { id: "ied.chat" });
		ied.tabs.open("chat:" + DRAFT, { preview: false });
	});
	ied.commands.handle("ied.chat.stop", (args) => tabOf(args) && tabOf(args).stop());
	ied.commands.handle("ied.chat.rename", (args) => {
		const view = tabOf(args);
		if (view && view.summary) renameChat(ied, view.chatId, view.summary.title);
	});
	ied.commands.handle("ied.chat.delete", (args) => {
		const view = tabOf(args);
		if (view && view.summary) deleteChat(ied, view.chatId, view.summary.title, view.keepsDeleted);
	});
	ied.commands.handle("ied.chat.claudeUsage", () => ied.commands.run("shell.settings", { page: "custom:ied.chat.claude" }));
	ied.tabs.kind("chat", { view: "ChatTab", title: (arg) => (arg === DRAFT ? "New chat" : "Chat"), icon: (arg) => (arg === DRAFT ? "$(add)" : "$(comment-discussion)") });
	badgeActiveChats(ied);
}

// The Chat activity's button shows the workspace's chats in a turn: how many need the owner (warn), else how many run;
// and a chat's turn that ends or waits on the owner is told where they are not looking (turnnotices.js).
function badgeActiveChats(ied) {
	let stop = null;
	const notices = new TurnNotices(ied, (chatId) => showChat(ied, chatId));
	const follow = () => {
		if (stop) stop();
		stop = null;
		ied.activities.badge("ied.chat", null);
		const wsId = ied.workspace.wsId;
		if (!wsId) return;
		const list = new ChatList();
		const render = () => ied.activities.badge("ied.chat", chatBadge(list.chats));
		const stopChats = ied.client.subscribe("chats:" + wsId, {
			snap: (data) => {
				list.snapshot(data);
				render();
			},
			evt: (kind, data) => {
				const before = kind === "chat.changed" ? list.find(data.chatId) : null;
				list.event(kind, data);
				render();
				if (before) notices.changed(Object.assign({}, before), data);
			},
		});
		const stopSettings = ied.client.subscribe("settings:" + wsId, {
			snap: (data) => (notices.setting = notifySetting(data.values)),
			evt: (kind, data) => kind === "settings.changed" && (notices.setting = notifySetting(data.values)),
		});
		stop = () => {
			stopChats();
			stopSettings();
		};
	};
	if (ied.workspace.on) ied.workspace.on(follow);
	follow();
}

// A chat shown in this window: its tab in the Chat activity, opened there or chosen.
async function showChat(ied, chatId) {
	await ied.commands.run("shell.showActivity", { id: "ied.chat" });
	ied.tabs.open("chat:" + chatId, { preview: false });
}

// The badge for the workspace's chats: null while none is in a turn.
export function chatBadge(chats) {
	const status = chatStatusText(chats);
	if (!status) return null;
	const waiting = chats.filter((c) => c.state === "awaiting_approval" || c.state === "awaiting_answer").length;
	const running = chats.filter((c) => c.state === "running").length;
	return { count: waiting || running, tone: waiting ? "warn" : "info", title: status.text.slice("AI: ".length) };
}

export function ChatTab(ctx) {
	const view = new ChatTabView(ctx, ctx.ref.slice("chat:".length));
	tabViews.add(view);
	ctx.el.addEventListener("focusin", () => {
		tabViews.delete(view);
		tabViews.add(view);
	});
	return {
		focus: () => view.focus(),
		visible: (shown) => view.pane.visible(shown),
		dispose: () => {
			tabViews.delete(view);
			view.dispose();
		},
	};
}

export function ChatsList(ctx) {
	return new ChatsView(ctx);
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
