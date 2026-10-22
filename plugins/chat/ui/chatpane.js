// chatpane.js - one chat shown (docs/15 "Chat"): its transcript (chat:<chatId>), composer and details panel, and what
// the owner does in it - send, stop, approve, answer. The Chat activity shows one at a time under its tabs; a chat opened
// as a tab of the editor area (chat:<chatId>) is one on its own, and what it opens from its panel opens beside it,
// tethered (docs/15 "Tethered tabs").

import { h, icon } from "/ui-kit/dom.js";
import { confirmDialog } from "/ui-kit/dialog.js";
import { ChatModel } from "./chatmodel.js";
import { TranscriptView } from "./transcript.js";
import { Composer } from "./composer.js";
import { SidePanel } from "./sidepanel.js";
import { stored, store } from "./stored.js";

const NEAR_BOTTOM_PX = 80;

function newDraft() {
	return { mode: "agent", permission: "ask", group: "" };
}

// What the composer shows of a chat not made yet.
function draftSummary(draft) {
	return Object.assign({ title: "New chat", state: "idle" }, draft);
}

export class ChatPane {
	// host: {header (an element above the transcript), created(summary) (a draft's first message made its chat),
	// openChat(chatId), shown(summary) (the chat's summary changed), tabRef (the pane's own tab, when it is one)}
	constructor(ied, wsId, host) {
		this.ied = ied;
		this.wsId = wsId;
		this.host = host;
		this.model = new ChatModel();
		this.chatId = null;
		this.stopChat = null;
		this.draft = newDraft();
		this.model.summary = draftSummary(this.draft);
		const actions = this.actions();
		this.transcript = new TranscriptView(this.model, actions);
		this.composer = new Composer(actions);
		this.panel = new SidePanel(actions);
		this.panel.el.addEventListener("chat-panel-toggle", () => this.panel.update(this.model));
		this.showPanel = stored("ied.chat.panel", true);
		this.scroller = h("div.chat-transcript", null, this.transcript.el);
		this.main = h("section.chat-main", null, host.header || null, this.scroller, this.composer.el);
		this.el = h("div.chat-act" + (this.showPanel ? "" : ".no-panel"), null, this.main, this.panel.el);
		this.el.addEventListener("keydown", (event) => this.key(event));
		this.transcript.render();
		this.composer.update(this.model);
		this.panel.update(this.model);
	}

	// The header's buttons that show the details panel: beside the transcript on a desktop, a sheet on a phone.
	panelButtons() {
		return [
			h("button.ib.desk", { title: "Details panel", "aria-label": "Details panel", on: { click: () => this.togglePanel() } }, icon("layout-sidebar-right")),
			h("button.ib.chat-phone-only", { title: "Details", "aria-label": "Chat details", on: { click: () => this.showDetails(true) } }, icon("layout-sidebar-right")),
		];
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
			openTab: (ref) => this.openTab(ref),
			closeDetails: () => this.showDetails(false),
			openChat: (chatId) => this.host.openChat(chatId),
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

	focus() {
		this.composer.focus();
	}

	// --- The chat shown ----------------------------------------------------------------------------------------------

	// Shows `chatId`, or a draft for null.
	show(chatId) {
		if (this.stopChat) this.stopChat();
		this.stopChat = null;
		this.chatId = chatId;
		this.model = new ChatModel();
		this.transcript.model = this.model;
		if (!chatId) this.model.summary = draftSummary(this.draft);
		if (chatId) {
			this.stopChat = this.ied.client.subscribe("chat:" + chatId, {
				snap: (data) => this.shown(data),
				evt: (kind, data) => this.changed(kind, data),
			});
		}
		this.transcript.render();
		this.composer.update(this.model);
		this.panel.update(this.model);
	}

	shown(data) {
		this.model.snapshot(data);
		this.transcript.render();
		this.composer.update(this.model);
		this.panel.update(this.model);
		this.scrollToEnd(true);
		if (this.host.shown) this.host.shown(this.model.summary);
	}

	changed(kind, data) {
		const follow = this.nearBottom();
		const change = this.model.event(kind, data);
		this.transcript.update(change);
		if (change.summary || change.queued || change.turn || change.context || change.approvals || change.question) this.composer.update(this.model);
		if (change.summary || change.tasks || change.modified || change.context || change.resources) this.panel.update(this.model);
		if (change.summary && this.host.shown) this.host.shown(this.model.summary);
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

	// --- What the owner does -----------------------------------------------------------------------------------------

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
		const created = await this.request("chat.create", Object.assign({ wsId: this.wsId }, this.draft, this.draft.group ? {} : { group: undefined }));
		if (!created) return false;
		this.draft = newDraft();
		this.host.created(created);
		return true;
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
		const settings = await this.request("settings.get", { wsId: this.wsId });
		const groups = settings && settings.values && settings.values["ai.groups"] ? settings.values["ai.groups"].value || {} : {};
		return Object.keys(groups);
	}

	openFile(path) {
		const opener = this.ied.contributions("fileOpener").pop();
		if (opener) this.ied.commands.run(opener.value.command, { path, line: 0, col: 0 });
		else this.ied.toast("No plugin opens files, so " + path + " is not shown.", { level: "warn" });
	}

	// A tab the chat uses (a page it drives): in a chat tab, beside it and tethered to it; from the activity, as a tab.
	openTab(ref) {
		this.showDetails(false);
		if (this.host.tabRef) this.ied.tabs.openBeside(this.host.tabRef, ref, { tether: true });
		else this.ied.tabs.open(ref, { preview: false });
	}

	// On a phone the details panel is a sheet over the transcript, opened from the header.
	showDetails(on) {
		this.el.classList.toggle("phone-panel", on);
	}

	togglePanel() {
		this.showPanel = !this.showPanel;
		store("ied.chat.panel", this.showPanel);
		this.el.classList.toggle("no-panel", !this.showPanel);
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
			const card = this.el.querySelector(".chat-approval[data-approval='" + approval.approvalId + "'] .chat-deny");
			if (card) {
				card.hidden = false;
				card.querySelector("input").focus();
			}
		}
	}

	dispose() {
		if (this.stopChat) this.stopChat();
	}
}
