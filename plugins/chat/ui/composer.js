// composer.js - the chat's input bar (docs/15 "Chat", mockup 02): the messages queued for the next turn, the files the
// message carries, the text box (Enter sends, Shift+Enter breaks the line, Esc stops a running turn, "@" mentions,
// docs/04), Attach / Queue / Inject / Stop / Send, what the chat runs on (a group or a model, modelpicker.js), its effort,
// mode and permission, and the running numbers - tokens in and out, speed, time to first token, context use.

import { h, icon, replace } from "/ui-kit/dom.js";
import { MenuHost } from "/ui-kit/menu.js";
import { MODES, PERMISSIONS, compact, contextBar, duration, effortLabel, effortRows, tokensPerSecond } from "./chatmodel.js";
import { keptMentions } from "./mentions.js";
import { choiceLabel, showModelPicker } from "./modelpicker.js";
import { MentionList } from "./mentionlist.js";
import { AttachBar } from "./attachbar.js";

const menus = new MenuHost();
const MOST_INPUT_PX = 220;
const PHONE = "(max-width: 719px)";

// actions: {send(text, as, extras), stop(), unqueue(index) -> Promise<{queued, message} | null>, sendQueued(index),
// set(change), choices() -> Promise<{groups, models}>, mentionRows(query) -> Promise<[row]>, upload(file) -> Promise<{blobId} | null>,
// fileUrl(path), toast(text)}
export class Composer {
	constructor(actions) {
		this.actions = actions;
		this.mentions = [];
		this.queuedEl = h("div.chat-queued-list");
		this.input = h("textarea.chat-input", { rows: 1, "aria-label": "Message", spellcheck: "true" });
		this.list = new MentionList(this.input, (query) => this.actions.mentionRows(query), (row) => this.mentioned(row));
		this.attached = new AttachBar((file) => this.actions.upload(file), (path) => this.actions.fileUrl(path), () => this.syncExtras(), (text) => this.actions.toast(text));
		this.input.addEventListener("keydown", (event) => this.key(event));
		this.input.addEventListener("input", () => {
			this.fit();
			this.list.follow();
			if (this.mentions.length) this.syncExtras();
		});
		this.input.addEventListener("click", () => this.list.follow());
		this.input.addEventListener("paste", (event) => this.pasted(event));
		this.files = h("input", { type: "file", multiple: true, hidden: true, on: { change: () => {
			this.attached.add([...this.files.files]);
			this.files.value = "";
		} } });
		this.attachButton = h("button.ib", { title: "Attach files (or paste or drop them here)", "aria-label": "Attach files", on: { click: () => this.files.click() } }, icon("attach"));
		this.mentionButton = h("button.ib.desk", { title: "Mention a file, folder, #symbol, terminal, output, chat, problems or diff (@)", "aria-label": "Mention", on: { click: () => this.startMention() } }, icon("mention"));
		this.queueButton = h("button.btn.desk", { title: "Send after this turn ends", on: { click: () => this.send("queue") } }, "Queue");
		this.injectButton = h("button.btn", { title: "Give it to the running turn at its next step (Ctrl+Enter)", on: { click: () => this.send("inject") } }, "Inject");
		this.stopButton = h("button.btn.desk", { title: "Stop the turn (Esc)", on: { click: () => this.actions.stop() } }, icon("debug-stop"), "Stop", h("span.kbd", null, "Esc"));
		this.sendButton = h("button.btn.primary", { title: "Send (Enter)", "aria-label": "Send", on: { click: () => this.send("message") } }, icon("send"));
		this.box = h("div.chat-composer", null, this.input,
			h("div.chat-row2", null, this.attachButton, this.mentionButton, h("span.grow.desk"), this.queueButton, this.injectButton, this.stopButton, this.sendButton), this.files);
		this.input.addEventListener("focus", () => this.box.classList.add("focus"));
		this.input.addEventListener("blur", () => {
			this.box.classList.remove("focus");
			this.list.close();
		});
		this.box.addEventListener("dragover", (event) => {
			if (!event.dataTransfer.types.includes("Files")) return;
			event.preventDefault();
			this.box.classList.add("drop");
		});
		this.box.addEventListener("dragleave", () => this.box.classList.remove("drop"));
		this.box.addEventListener("drop", (event) => {
			this.box.classList.remove("drop");
			if (!event.dataTransfer.files.length) return;
			event.preventDefault();
			this.attached.add([...event.dataTransfer.files]);
		});
		this.selectors = h("div.chat-selectors");
		this.telemetry = h("div.chat-telemetry");
		this.el = h("div.chat-composer-wrap", null, h("div.chat-col.chat-compose-col", null, this.queuedEl, this.attached.el, this.list.el, this.box, this.selectors, this.telemetry));
	}

	key(event) {
		if (this.list.key(event)) return;
		if (event.key === "Enter" && !event.shiftKey && !event.isComposing) {
			event.preventDefault();
			this.send(event.ctrlKey && this.running ? "inject" : "message");
		}
		else if (event.key === "Escape" && this.running) {
			event.preventDefault();
			this.actions.stop();
		}
	}

	fit() {
		this.input.style.height = "auto";
		this.input.style.height = Math.min(this.input.scrollHeight, MOST_INPUT_PX) + "px";
		this.input.style.overflowY = this.input.scrollHeight > MOST_INPUT_PX ? "auto" : "hidden";
	}

	// A message with mentions or files is queued rather than injected: steering is words only.
	async send(as) {
		const text = this.input.value.trim();
		if (!text) return;
		const mentions = keptMentions(text, this.mentions);
		this.input.value = "";
		this.fit();
		this.list.close();
		const attachments = await this.attached.sent();
		const extras = mentions.length > 0 || attachments.length > 0;
		if (!(await this.actions.send(text, extras && as === "inject" ? "queue" : as, { mentions, attachments }))) {
			this.input.value = text;
			this.fit();
			return;
		}
		this.mentions = [];
		this.attached.clear();
	}

	mentioned(row) {
		this.mentions.push({ kind: row.kind, ref: row.ref, label: row.label });
		this.fit();
		this.syncExtras();
	}

	// "@" typed at the caret, as if by the key.
	startMention() {
		const caret = this.input.selectionStart;
		const text = this.input.value;
		const gap = caret > 0 && !/\s/.test(text[caret - 1]) ? " " : "";
		this.input.value = text.slice(0, caret) + gap + "@" + text.slice(caret);
		const after = caret + gap.length + 1;
		this.input.focus();
		this.input.setSelectionRange(after, after);
		this.list.follow();
	}

	pasted(event) {
		const files = event.clipboardData ? [...event.clipboardData.files] : [];
		if (!files.length) return;
		event.preventDefault();
		this.attached.add(files);
	}

	// Inject takes words only: off while the message names or carries something.
	syncExtras() {
		const extras = this.attached.count > 0 || keptMentions(this.input.value, this.mentions).length > 0;
		this.injectButton.disabled = extras;
		this.injectButton.title = extras ? "A message with mentions or files is queued, not injected" : "Give it to the running turn at its next step (Ctrl+Enter)";
	}

	focus() {
		this.input.focus();
	}

	update(model) {
		const summary = model.summary || {};
		this.running = model.running();
		this.input.placeholder = window.matchMedia(PHONE).matches ? (this.running ? "Message or steer\u2026" : "Message\u2026")
			: "Message " + (summary.title || "the chat") + (this.running ? " - Enter queues it, Ctrl+Enter steers the running turn" : " - Enter sends, Shift+Enter breaks the line");
		this.queueButton.hidden = !this.running;
		this.injectButton.hidden = !this.running;
		this.stopButton.hidden = !this.running;
		replace(this.queuedEl, ...model.queued.map((text, index) => this.queuedRow(text, index)));
		this.renderSelectors(summary, model.groupInUse, model.roleEffort);
		this.renderTelemetry(model);
	}

	// A queued message: edit it (back into the box with what it names and carries), send it now when no turn runs, or
	// drop it.
	queuedRow(text, index) {
		return h("div.chat-queued", null, icon("list-ordered"), h("span.muted", null, "Queued for the next turn:"), h("span.ellip", { title: text }, text),
			this.running ? null : h("button.ib", { title: "Send it now", "aria-label": "Send the queued message now", on: { click: () => this.actions.sendQueued(index) } }, icon("send")),
			h("button.ib", { title: "Edit", "aria-label": "Edit the queued message", on: { click: () => this.editQueued(index) } }, icon("edit")),
			h("button.ib", { title: "Remove", "aria-label": "Remove the queued message", on: { click: () => this.actions.unqueue(index) } }, icon("close")));
	}

	async editQueued(index) {
		const taken = await this.actions.unqueue(index);
		if (!taken) return;
		const message = taken.message || { text: "" };
		this.input.value = message.text;
		this.mentions = (message.mentions || []).slice();
		this.attached.keep(message.attachments || []);
		this.fit();
		this.focus();
	}

	renderSelectors(summary, groupInUse, roleEffort) {
		const mode = MODES.find(([id]) => id === summary.mode) || MODES[2];
		const permission = PERMISSIONS.find(([id]) => id === summary.permission) || PERMISSIONS.find(([id]) => id === "auto");
		replace(this.selectors,
			h("span.muted", null, "Model"), this.modelButton(summary, groupInUse),
			h("span.muted", null, "Effort"), this.dropdown(effortLabel(summary.effort, roleEffort), "Effort", () => effortRows(summary.effort, roleEffort), (row) => this.actions.set({ effort: row.value })),
			h("span.muted", null, "Mode"), this.dropdown(mode[1], "Mode", () => rowsOf(MODES, summary.mode), (row) => this.actions.set({ mode: row.value })),
			h("span.muted", null, "Permissions"), this.dropdown(permission[1], "Permissions", () => rowsOf(PERMISSIONS, summary.permission), (row) => this.actions.set({ permission: row.value })));
	}

	dropdown(label, title, rows, pick) {
		return h("button.chat-dd", { title, "aria-haspopup": "menu", on: { click: async (event) => {
			const at = event.currentTarget;
			menus.show({ rows: await rows(), at, pick, label: title });
		} } }, h("span", null, label), icon("chevron-down"));
	}

	// The Model menu: a group (an orchestrator and its agents) or one model (docs/04 "One choice per chat").
	modelButton(summary, groupInUse) {
		const label = choiceLabel(summary, groupInUse);
		return h("button.chat-dd.chat-model-dd", { title: "Run this chat on a group or a model", "aria-haspopup": "dialog", on: { click: async (event) => {
			const at = event.currentTarget;
			const choices = await this.actions.choices();
			if (choices) showModelPicker(at, choices, { group: summary.group || "", model: summary.pinnedModel || "" }, (change) => this.actions.set(change));
		} } }, h("span", null, label), icon("chevron-down"));
	}

	renderTelemetry(model) {
		const turn = model.lastTurn();
		const bar = contextBar(model.context);
		const speed = tokensPerSecond(turn);
		replace(this.telemetry,
			turn ? h("span", null, h("b", null, compact(turn.tokensIn)), " in / ", h("b", null, compact(turn.tokensOut)), " out") : null,
			speed ? h("span", null, h("b", null, speed), " tok/s") : null,
			turn && turn.ttftMs ? h("span", null, "TTFT ", h("b", null, duration(turn.ttftMs))) : null,
			bar.window ? h("span.chat-mchip", { title: "Context used of " + (bar.model || "the model") + "'s window" }, "ctx ", h("span.chat-bar", null, h("i", { style: { width: Math.min(bar.percent, 100) + "%" } })), h("b", null, bar.percent + "%"), " of " + compact(bar.window))
				: bar.used ? h("span.chat-mchip", { title: "Context used; the model's window is not known yet" }, "ctx ", h("b", null, compact(bar.used))) : null,
			model.model ? h("span.chat-mchip", null, h("span.dot.info"), model.model) : model.summary && model.summary.chatId ? h("span.chat-mchip.c-warn", null, "no model set up (ai.models)") : null);
	}
}

function rowsOf(options, current) {
	return options.map(([id, label]) => ({ kind: "item", label, value: id, enabled: true, icon: id === current ? "$(check)" : null }));
}
