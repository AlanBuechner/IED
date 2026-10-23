// chatpane.js - one chat shown (docs/15 "Chat"): its transcript (chat:<chatId>), composer and details panel, and what
// the owner does in it - send, stop, approve, answer. The Chat activity shows one at a time under its tabs; a chat opened
// as a tab of the editor area (chat:<chatId>) is one on its own, and what it opens from its panel opens beside it,
// tethered (docs/15 "Tethered tabs").

import { h, icon } from "/ui-kit/dom.js";
import { confirmDialog } from "/ui-kit/dialog.js";
import { ChatModel } from "./chatmodel.js";
import { TranscriptView } from "./transcript.js";
import { CodeColors } from "./codecolors.js";
import { Composer } from "./composer.js";
import { SidePanel } from "./sidepanel.js";
import { MentionSource } from "./mentions.js";
import { workspaceFileUrl } from "./userchips.js";
import { stored, store } from "./stored.js";

const NEAR_BOTTOM_PX = 80;

function newDraft() {
	return { mode: "agent", permission: "ask", group: "", effort: "" };
}

// What the composer shows of a chat not made yet.
function draftSummary(draft) {
	return Object.assign({ title: "New chat", state: "idle", pinnedModel: draft.model || "" }, draft);
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
		this.codeColors = new CodeColors(ied);
		const actions = this.actions();
		this.transcript = new TranscriptView(this.model, actions);
		this.composer = new Composer(actions);
		this.panel = new SidePanel(actions);
		this.panel.el.addEventListener("chat-panel-toggle", () => this.panel.update(this.model));
		this.showPanel = stored("ied.chat.panel", true);
		this.atEnd = true;   // the transcript shows its end, and keeps it as it grows, until the owner scrolls up
		this.scroller = h("div.chat-transcript", { on: { scroll: () => (this.atEnd = this.nearBottom()) } }, this.transcript.el);
		this.main = h("section.chat-main", null, host.header || null, this.scroller, this.composer.el);
		this.el = h("div.chat-act" + (this.showPanel ? "" : ".no-panel"), null, this.main, this.panel.el);
		this.el.addEventListener("keydown", (event) => this.key(event));
		this.resizer = new ResizeObserver(() => this.atEnd && this.toEnd());
		this.resizer.observe(this.scroller);
		this.resizer.observe(this.transcript.el);
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
			send: (text, as, extras) => this.send(text, as, extras),
			mentionRows: (query) => this.mentionRows(query),
			upload: (file) => this.upload(file),
			toast: (text) => this.ied.toast(text, { level: "warn" }),
			stop: () => this.stop(),
			unqueue: (index) => this.request("chat.unqueue", { chatId: this.chatId, index }),
			sendQueued: (index) => this.request("chat.unqueue", { chatId: this.chatId, index, start: true }),
			fileUrl: (path) => workspaceFileUrl(this.wsId, path),
			set: (change) => this.setChat(change),
			groups: () => this.groups(),
			models: () => this.models(),
			openFile: (path, line) => this.openFile(path, line),
			openTab: (ref) => this.openTab(ref),
			closeDetails: () => this.showDetails(false),
			openChat: (chatId) => this.host.openChat(chatId),
			fileIcon: (path) => this.ied.icons.file(path),
			loadEarlier: () => this.loadEarlier(),
			codeBlock: (code, fence, closed) => this.codeColors.element(code, fence, closed),
			colorDiff: (path, spans) => this.codeColors.diff(path, spans),
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

	// Whenever the chat is shown (its tab opened or chosen, its activity back), it opens at its end.
	visible(shown) {
		if (shown) this.scrollToEnd();
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
		this.scrollToEnd();
		if (this.host.shown) this.host.shown(this.model.summary);
	}

	changed(kind, data) {
		const change = this.model.event(kind, data);
		this.transcript.update(change);
		if (change.summary || change.queued || change.turn || change.context || change.approvals || change.question) this.composer.update(this.model);
		if (change.summary || change.tasks || change.modified || change.context || change.resources || change.clips) this.panel.update(this.model);
		if (change.summary && this.host.shown) this.host.shown(this.model.summary);
	}

	nearBottom() {
		const s = this.scroller;
		return s.scrollHeight - s.scrollTop - s.clientHeight < NEAR_BOTTOM_PX;
	}

	// The transcript at its end, kept there as it grows (a reply streaming, an image loading, the composer growing) until
	// the owner scrolls up; again on the next frame, for a pane being placed on the page now.
	scrollToEnd() {
		this.atEnd = true;
		this.toEnd();
		requestAnimationFrame(() => this.toEnd());
	}

	toEnd() {
		this.scroller.scrollTop = this.scroller.scrollHeight;
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

	// extras: {mentions, attachments}, sent when there are any.
	async send(text, as, extras) {
		if (!this.chatId && !(await this.createChat())) return false;
		const params = { chatId: this.chatId, text, as };
		if (extras && extras.mentions.length) params.mentions = extras.mentions;
		if (extras && extras.attachments.length) params.attachments = extras.attachments;
		const sent = await this.request("chat.send", params);
		if (sent && sent.injected) this.ied.toast("Given to the running turn at its next step.", { level: "info" });
		return sent !== null;
	}

	stop() {
		if (this.chatId) this.request("chat.stop", { chatId: this.chatId });
	}

	// The @ list's rows: its lists other than files and symbols asked once per "@" (mentions.js).
	mentionRows(query) {
		if (!this.mentionSource || this.mentionSource.chatId !== this.chatId) this.mentionSource = new MentionSource((method, params) => this.request(method, params), this.wsId, this.chatId);
		if (!query) this.mentionSource.reset();
		return this.mentionSource.rows(query);
	}

	// A file for the next message, uploaded now (PUT /api/upload, docs/10): its blob id, or null with a toast.
	async upload(file) {
		try {
			const reply = await fetch("/api/upload", { method: "PUT", body: file, headers: { "Content-Type": file.type || "application/octet-stream" }, credentials: "same-origin" });
			const text = await reply.text();
			if (reply.ok) return JSON.parse(text);
			this.ied.toast(file.name + " was not uploaded: " + (text.trim() || reply.status), { level: "error" });
		}
		catch (e) {
			this.ied.toast(file.name + " was not uploaded: " + e.message, { level: "error" });
		}
		return null;
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

	// The models of ai.models a chat can be pinned to: [{name, claude}].
	async models() {
		const settings = await this.request("settings.get", { wsId: this.wsId });
		const models = settings && settings.values && settings.values["ai.models"] ? settings.values["ai.models"].value || {} : {};
		return Object.keys(models).map((name) => ({ name, claude: models[name].provider === "claude-code" }));
	}

	openFile(path, line) {
		const opener = this.ied.contributions("fileOpener").pop();
		if (opener) this.ied.commands.run(opener.value.command, { path, line: line || 0, col: 0 });
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
		this.resizer.disconnect();
		if (this.stopChat) this.stopChat();
	}
}
