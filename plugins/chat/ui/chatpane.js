// chatpane.js - one chat shown (docs/15 "Chat"): its transcript (chat:<chatId>), composer and details panel - as wide
// as the owner drags it, on this device - and what the owner does in it - send, stop, approve, answer. The Chat
// activity shows one at a time under its tabs; a chat opened as a tab of the editor area (chat:<chatId>) is one on its
// own, and what it opens from its panel opens beside it, tethered (docs/15 "Tethered tabs").

import { h, icon } from "/ui-kit/dom.js";
import { confirmDialog } from "/ui-kit/dialog.js";
import { ChatModel } from "./chatmodel.js";
import { TranscriptView } from "./transcript.js";
import { CodeColors } from "./codecolors.js";
import { Composer } from "./composer.js";
import { PREVIEW_ROWS, SidePanel } from "./sidepanel.js";
import { MentionSource } from "./mentions.js";
import { workspaceFileUrl } from "./userchips.js";
import { stored, store } from "./stored.js";
import { choicesFrom } from "./modelpicker.js";
import { askToNotify, notifySetting } from "./turnnotices.js";

const NEAR_BOTTOM_PX = 80;
const OWNER_SCROLL_MS = 800;   // a scroll this soon after the owner's wheel, touch or key is theirs
const CLAMPED_PX = 2;          // a transcript that shrank under its end is scrolled up by the browser, this close to it
const PANEL_MIN_PX = 200;
const TRANSCRIPT_MIN_PX = 360;   // what a wide panel leaves the transcript
const PANEL_STEP_PX = 16;        // an arrow key on the panel's edge

function newDraft() {
	return { mode: "agent", permission: "auto", group: "", effort: "" };
}

// `text` on the system clipboard: the Clipboard API where the page may use it, else the copy command over a text area
// of its own (a page on the network's address, as a phone has it, gets no Clipboard API). Whether it was copied.
export async function copyText(text) {
	if (navigator.clipboard && window.isSecureContext) {
		try {
			await navigator.clipboard.writeText(text);
			return true;
		}
		catch {
			// refused (the page has no focus): the copy command below
		}
	}
	const area = h("textarea", { readonly: true, "aria-hidden": "true", style: { position: "fixed", left: "-10000px", top: "0" } });
	area.value = text;
	document.body.append(area);
	area.select();
	let copied = false;
	try {
		copied = document.execCommand("copy");
	}
	finally {
		area.remove();
	}
	return copied;
}

// A turn's attachments as chat.send takes them again: files the chat's media folder keeps, by their path.
function keptAttachments(attachments) {
	return (attachments || []).map((attachment) => ({ path: attachment.path, name: attachment.name, mime: attachment.mime }));
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
		this.lastTop = 0;
		this.ownerMs = 0;    // the owner's last wheel, touch or key on the transcript
		this.held = false;   // the owner holds a pointer down on it (its scrollbar)
		const touched = (event) => this.touched(event);
		this.scroller = h("div.chat-transcript", { on: { scroll: () => this.scrolled(), wheel: touched, touchstart: touched, touchmove: touched, keydown: touched, pointerdown: touched } }, this.transcript.el);
		this.main = h("section.chat-main", null, host.header || null, this.scroller, this.composer.el);
		this.sash = h("div.chat-panel-sash", { role: "separator", tabindex: "0", "aria-orientation": "vertical", "aria-label": "Details panel width", title: "Drag to resize; double-click for the default width",
			on: { pointerdown: (event) => this.dragPanel(event), dblclick: () => this.keepPanel(0), keydown: (event) => this.panelKey(event) } });
		this.el = h("div.chat-act" + (this.showPanel ? "" : ".no-panel"), null, this.main, this.sash, this.panel.el);
		this.sizePanel(stored("ied.chat.panelWidth", 0));
		this.el.addEventListener("keydown", (event) => this.key(event));
		this.resizer = new ResizeObserver(() => this.atEnd && this.toEnd());
		this.resizer.observe(this.scroller);
		this.resizer.observe(this.transcript.el);
		this.transcript.render();
		this.composer.update(this.model);
		this.panel.update(this.model);
		this.stopSettings = ied.client.subscribe("settings:" + wsId, {
			snap: (data) => this.settingsChanged(data.values),
			evt: (kind, data) => kind === "settings.changed" && this.settingsChanged(data.values),
		});
	}

	// ai.chats.panelRows: how many rows a long list of the details panel shows until Show all.
	settingsChanged(values) {
		this.notify = notifySetting(values);
		const rows = values && values["ai.chats.panelRows"] ? values["ai.chats.panelRows"].value : PREVIEW_ROWS;
		const count = Number.isInteger(rows) && rows > 0 ? rows : PREVIEW_ROWS;
		if (count === this.panel.previewRows) return;
		this.panel.previewRows = count;
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
			choices: () => this.choices(),
			openFile: (path, line) => this.openFile(path, line),
			openTab: (ref) => this.openTab(ref),
			closeDetails: () => this.showDetails(false),
			openChat: (chatId) => this.host.openChat(chatId),
			stopTask: (taskId) => this.request("chat.stopTask", { chatId: this.chatId, taskId }),
			fileIcon: (path) => this.ied.icons.file(path),
			loadEarlier: () => this.loadEarlier(),
			codeBlock: (code, fence, closed) => this.codeColors.element(code, fence, closed),
			colorDiff: (path, spans) => this.codeColors.diff(path, spans),
			runCommand: (id, args) => this.ied.commands.run(id, args),
			copy: (text, what) => this.copy(text, what),
			editMessage: (turn) => this.editMessage(turn),
			resend: (turn) => this.resend(turn),
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
		if (this.composer.editing && chatId !== this.chatId) this.composer.stopEditing(true);
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
		if (!chatId) this.nameDraftGroup();
	}

	// A draft's Model menu names the default group a chat made now would run on.
	async nameDraftGroup() {
		const choices = await this.choices();
		const group = choices && choices.groups.find((g) => g.isDefault);
		if (this.chatId || !group) return;
		this.model.groupInUse = group.name;
		this.composer.update(this.model);
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
		if (change.all) return this.reverted();
		this.transcript.update(change);
		if (change.summary || change.queued || change.turn || change.context || change.approvals || change.question) this.composer.update(this.model);
		if (change.summary || change.tasks || change.modified || change.context || change.resources || change.clips || change.background) this.panel.update(this.model);
		if (change.summary && this.host.shown) this.host.shown(this.model.summary);
	}

	// The chat was reverted to before a turn (here or on another device): it is drawn again as it is now, and a message
	// being edited whose turn is gone is no longer one to replace (its text stays in the box).
	reverted() {
		this.transcript.render();
		const editing = this.composer.editing;
		if (editing && !this.model.turn(editing.turnId)) this.composer.stopEditing(false);
		this.composer.update(this.model);
		this.panel.update(this.model);
		this.scrollToEnd();
		if (this.host.shown) this.host.shown(this.model.summary);
	}

	gap() {
		const s = this.scroller;
		return s.scrollHeight - s.scrollTop - s.clientHeight;
	}

	// The transcript scrolled. Only the owner moving it up stops it following its end: a scroll event comes a frame after
	// its scroll, and what grew meanwhile (an image of a tool's result loading, a reply streaming) leaves the end farther
	// away than it was, which says nothing of the owner. Back near the end, it follows again.
	scrolled() {
		const top = this.scroller.scrollTop;
		const up = top < this.lastTop;
		if (up && this.ownerScrolls() && this.gap() > CLAMPED_PX) this.atEnd = false;
		else if (!up && this.gap() < NEAR_BOTTOM_PX) this.atEnd = true;
		this.lastTop = top;
	}

	ownerScrolls() {
		return this.held || Date.now() - this.ownerMs < OWNER_SCROLL_MS;
	}

	// The owner's hand on the transcript; a pointer held down (on its scrollbar) counts until it is let go.
	touched(event) {
		this.ownerMs = Date.now();
		if (event.type !== "pointerdown") return;
		this.held = true;
		const release = () => {
			this.held = false;
			this.ownerMs = Date.now();
			window.removeEventListener("pointerup", release, true);
			window.removeEventListener("pointercancel", release, true);
		};
		window.addEventListener("pointerup", release, true);
		window.addEventListener("pointercancel", release, true);
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
		this.lastTop = this.scroller.scrollTop;
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
		if (change.mode && !change.permission) change.permission = change.mode === "plan" ? "plan" : "auto";
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

	// extras: {mentions, attachments}, sent when there are any; `revertTo` (an edited message): the chat is reverted to
	// before that turn first, once the owner says so.
	async send(text, as, extras) {
		askToNotify(this.notify);
		if (extras && extras.revertTo && !(await this.revertTo(extras.revertTo))) return false;
		if (!this.chatId && !(await this.createChat())) return false;
		const params = { chatId: this.chatId, text, as };
		if (extras && extras.mentions.length) params.mentions = extras.mentions;
		if (extras && extras.attachments.length) params.attachments = extras.attachments;
		return (await this.request("chat.send", params)) !== null;
	}

	stop() {
		if (this.chatId) this.request("chat.stop", { chatId: this.chatId });
	}

	// --- A message's buttons (docs/15 "Message buttons") -------------------------------------------------------------

	async copy(text, what) {
		if (await copyText(text)) this.ied.toast(what === "reply" ? "Copied the reply." : "Copied the message.", { level: "info" });
		else this.ied.toast("The clipboard is not available here.", { level: "warn" });
	}

	// The owner's message back in the composer, with what it named and carried; sending it reverts the chat to before it.
	editMessage(turn) {
		const user = turn.user || {};
		this.composer.edit(turn.turnId, turn.startedMs, { text: user.text || "", mentions: user.mentions || [], attachments: user.attachments || [] });
	}

	// The chat reverted to before the owner's message, which is sent again as it was.
	async resend(turn) {
		const user = turn.user || {};
		if (!(await this.revertTo(turn.turnId, "Resend"))) return;
		await this.send(user.text || "", "message", { mentions: user.mentions || [], attachments: keptAttachments(user.attachments) });
	}

	// chat.revert to before `turnId`, once the owner confirms: what goes, that it is kept, that files stay as they are, and
	// that a running turn stops first. Whether it was reverted.
	async revertTo(turnId, title = "Edit and send") {
		const at = this.model.turns.findIndex((turn) => turn.turnId === turnId);
		const after = at < 0 ? 0 : this.model.turns.length - at - 1;
		const going = after ? "This message, its reply and the " + (after === 1 ? "turn" : after + " turns") + " after it leave the chat" : "This message and its reply leave the chat";
		const message = going + "; they are kept in the chat's folder (reverted/). Files they changed are not changed back."
			+ (this.model.running() ? " The turn running now stops first." : "");
		if (!(await confirmDialog({ title: title + ": revert the chat to before this message?", message, confirm: title === "Resend" ? "Revert and resend" : "Revert and send" }))) return false;
		return (await this.request("chat.revert", { chatId: this.chatId, turnId })) !== null;
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

	// What the Model menu lists: the groups and the models of the settings, and the chains pacing has now - the model a turn
	// of each group's Claude chain uses (aiusage; none when the server cannot say) (modelpicker.js).
	async choices() {
		const [settings, usage] = await Promise.all([this.request("settings.get", { wsId: this.wsId }), this.ied.client.request("ai.usage", {}).catch(() => null)]);
		const chains = usage && usage.claude && usage.claude.chains ? usage.claude.chains : [];
		return settings && settings.values ? choicesFrom(settings.values, chains) : null;
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

	// The details panel `px` wide, kept for this device's chats; 0 is the stylesheet's width.
	sizePanel(px) {
		this.panelWidth = px > 0 ? Math.max(PANEL_MIN_PX, Math.round(px)) : 0;
		if (this.panelWidth) this.el.style.setProperty("--chat-panel-w", this.panelWidth + "px");
		else this.el.style.removeProperty("--chat-panel-w");
		this.sash.setAttribute("aria-valuenow", String(Math.round(this.panel.el.offsetWidth || this.panelWidth)));
	}

	widest() {
		return Math.max(PANEL_MIN_PX, this.el.clientWidth - TRANSCRIPT_MIN_PX);
	}

	dragPanel(event) {
		if (event.button !== 0) return;
		event.preventDefault();
		const startX = event.clientX;
		const startWidth = this.panel.el.offsetWidth;
		const widest = this.widest();
		this.sash.setPointerCapture(event.pointerId);
		this.sash.classList.add("active");
		const move = (e) => this.sizePanel(Math.min(widest, startWidth + startX - e.clientX));
		const up = () => {
			this.sash.removeEventListener("pointermove", move);
			this.sash.removeEventListener("pointerup", up);
			this.sash.removeEventListener("pointercancel", up);
			this.sash.classList.remove("active");
			this.keepPanel(this.panelWidth);
		};
		this.sash.addEventListener("pointermove", move);
		this.sash.addEventListener("pointerup", up);
		this.sash.addEventListener("pointercancel", up);
	}

	panelKey(event) {
		const step = event.key === "ArrowLeft" ? PANEL_STEP_PX : event.key === "ArrowRight" ? -PANEL_STEP_PX : 0;
		if (!step) return;
		event.preventDefault();
		this.keepPanel(Math.min(this.widest(), this.panel.el.offsetWidth + step));
	}

	// The panel `px` wide (0: the default) for this device's chats from now on.
	keepPanel(px) {
		this.sizePanel(px);
		store("ied.chat.panelWidth", this.panelWidth);
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
		this.stopSettings();
		if (this.stopChat) this.stopChat();
	}
}
