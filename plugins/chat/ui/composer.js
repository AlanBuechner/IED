// composer.js - the chat's input bar (docs/15 "Chat", mockup 02): the messages waiting for the running turn's next step
// and those queued for the next turn, the message being edited (Edit on one of the owner's messages: sending it reverts
// the chat to before it), the files the message carries, the text box (Enter sends - to a running turn, at its next
// step - Ctrl+Enter queues for the next turn, Shift+Enter breaks the line, Esc stops a running turn, "@" mentions drawn
// as chips, docs/04; pasted text goes in plain), Attach / Stop / Send, what the chat runs on (a group or a model,
// modelpicker.js), its effort, mode and permission, and the running numbers - tokens in and out, speed, time to first
// token, context use.

import { h, icon, replace } from "/ui-kit/dom.js";
import { MenuHost } from "/ui-kit/menu.js";
import { MODES, PERMISSIONS, compact, contextBar, duration, effortLabel, effortRows, tokensPerSecond } from "./chatmodel.js";
import { keptMentions } from "./mentions.js";
import { choiceLabel, showModelPicker } from "./modelpicker.js";
import { MentionList } from "./mentionlist.js";
import { AttachBar } from "./attachbar.js";
import { InputMarks, insertText, plainText } from "./composertext.js";

const menus = new MenuHost();
const MOST_INPUT_PX = 220;
const PHONE = "(max-width: 719px)";

// actions: {send(text, as, extras) (extras: {mentions, attachments, revertTo?: the turn an edited message replaces}),
// stop(), unqueue(index) -> Promise<{queued, message} | null>, sendQueued(index), set(change), choices() -> Promise<{groups, models}>,
// mentionRows(query) -> Promise<[row]>, upload(file) -> Promise<{blobId} | null>, fileUrl(path), toast(text)}
export class Composer {
	constructor(actions) {
		this.actions = actions;
		this.mentions = [];
		this.editing = null;   // the owner's message being edited: {turnId, atMs}
		this.queuedEl = h("div.chat-queued-list");
		this.editingEl = h("div.chat-editing", { hidden: true });
		this.input = h("textarea.chat-input", { rows: 1, "aria-label": "Message", spellcheck: "true" });
		this.marks = new InputMarks(this.input);
		this.list = new MentionList(this.input, (query) => this.actions.mentionRows(query), (row) => this.mentioned(row));
		this.attached = new AttachBar((file) => this.actions.upload(file), (path) => this.actions.fileUrl(path), () => {}, (text) => this.actions.toast(text));
		this.input.addEventListener("keydown", (event) => this.key(event));
		this.input.addEventListener("input", () => {
			this.textChanged();
			this.list.follow();
		});
		this.input.addEventListener("click", () => this.list.follow());
		this.input.addEventListener("paste", (event) => this.pasted(event));
		this.files = h("input", { type: "file", multiple: true, hidden: true, on: { change: () => {
			this.attached.add([...this.files.files]);
			this.files.value = "";
		} } });
		this.attachButton = h("button.ib", { title: "Attach files (or paste or drop them here)", "aria-label": "Attach files", on: { click: () => this.files.click() } }, icon("attach"));
		this.mentionButton = h("button.ib.desk", { title: "Mention a file, folder, #symbol, terminal, output, chat, problems or diff (@)", "aria-label": "Mention", on: { click: () => this.startMention() } }, icon("mention"));
		this.stopButton = h("button.btn.desk", { title: "Stop the turn (Esc)", on: { click: () => this.actions.stop() } }, icon("debug-stop"), "Stop", h("span.kbd", null, "Esc"));
		this.sendButton = h("button.btn.primary", { title: "Send (Enter)", "aria-label": "Send", on: { click: () => this.send("message") } }, icon("send"));
		this.box = h("div.chat-composer", null, h("div.chat-input-box", null, this.marks.el, this.input),
			h("div.chat-row2", null, this.attachButton, this.mentionButton, h("span.grow.desk"), this.stopButton, this.sendButton), this.files);
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
			if (event.dataTransfer.files.length) {
				event.preventDefault();
				this.attached.add([...event.dataTransfer.files]);
				return;
			}
			const text = event.dataTransfer.getData("text/plain");
			if (!text) return;
			event.preventDefault();
			insertText(this.input, plainText(text));
		});
		this.selectors = h("div.chat-selectors");
		this.telemetry = h("div.chat-telemetry");
		this.el = h("div.chat-composer-wrap", null, h("div.chat-col.chat-compose-col", null, this.queuedEl, this.editingEl, this.attached.el, this.list.el, this.box, this.selectors, this.telemetry));
	}

	// The box's text, mentions and the chips drawn for them, after the text changed: typed, pasted, set.
	textChanged() {
		this.fit();
		this.marks.update(this.mentions);
	}

	setText(text) {
		this.input.value = text;
		this.textChanged();
	}

	key(event) {
		if (this.list.key(event)) return;
		if (event.key === "Enter" && !event.shiftKey && !event.isComposing) {
			event.preventDefault();
			this.send(event.ctrlKey && this.running ? "queue" : "message");
		}
		else if (event.key === "Escape" && this.running) {
			event.preventDefault();
			this.actions.stop();
		}
		else if (event.key === "Escape" && this.editing) {
			event.preventDefault();
			this.stopEditing(true);
		}
	}

	fit() {
		this.input.style.height = "auto";
		this.input.style.height = Math.min(this.input.scrollHeight, MOST_INPUT_PX) + "px";
		this.input.style.overflowY = this.input.scrollHeight > MOST_INPUT_PX ? "auto" : "hidden";
		this.marks.sync();
	}

	// `as` "message" (to a running turn: at its next step) or "queue" (the next turn). An edited message reverts the chat
	// to before the one it replaces, then goes as a new turn.
	async send(as) {
		const text = this.input.value.trim();
		if (!text) return;
		const mentions = keptMentions(text, this.mentions);
		const editing = this.editing;
		this.setText("");
		this.list.close();
		const attachments = await this.attached.sent();
		const extras = editing ? { mentions, attachments, revertTo: editing.turnId } : { mentions, attachments };
		if (!(await this.actions.send(text, as, extras))) {
			this.setText(text);
			return;
		}
		this.mentions = [];
		this.marks.update(this.mentions);
		this.attached.clear();
		if (editing === this.editing) this.stopEditing(false);
	}

	mentioned(row) {
		this.mentions.push({ kind: row.kind, ref: row.ref, label: row.label });
		this.textChanged();
	}

	// "@" typed at the caret, as if by the key.
	startMention() {
		const caret = this.input.selectionStart;
		const text = this.input.value;
		const gap = caret > 0 && !/\s/.test(text[caret - 1]) ? " " : "";
		this.setText(text.slice(0, caret) + gap + "@" + text.slice(caret));
		const after = caret + gap.length + 1;
		this.input.focus();
		this.input.setSelectionRange(after, after);
		this.list.follow();
	}

	// Files pasted are attached; text goes in plain (plainText), never as the page it came from had it.
	pasted(event) {
		const data = event.clipboardData;
		const files = data ? [...data.files] : [];
		if (files.length) {
			event.preventDefault();
			this.attached.add(files);
			return;
		}
		if (!data) return;
		event.preventDefault();
		const text = plainText(data.getData("text/plain"));
		if (text) insertText(this.input, text);
	}

	// One of the owner's messages back in the box to edit (`message`: {text, mentions, attachments}): sending it reverts the
	// chat to before its turn and sends it as a new one. What the box held is replaced.
	edit(turnId, atMs, message) {
		this.editing = { turnId, atMs };
		this.mentions = (message.mentions || []).slice();
		this.attached.clear();
		this.attached.keep(message.attachments || []);
		this.setText(message.text || "");
		this.renderEditing();
		this.focus();
		this.input.setSelectionRange(this.input.value.length, this.input.value.length);
	}

	// `clear`: the box emptied too (Cancel); after a send it is empty already.
	stopEditing(clear) {
		this.editing = null;
		this.renderEditing();
		if (!clear) return;
		this.mentions = [];
		this.attached.clear();
		this.setText("");
	}

	renderEditing() {
		this.editingEl.hidden = !this.editing;
		if (!this.editing) return replace(this.editingEl);
		const at = this.editing.atMs ? new Date(this.editing.atMs) : null;
		const when = at ? " from " + String(at.getHours()).padStart(2, "0") + ":" + String(at.getMinutes()).padStart(2, "0") : "";
		return replace(this.editingEl, icon("edit"), h("span.ellip", { title: "Sending it reverts the chat to before that message: it and every turn after it leave the chat (kept in its folder). Files they changed stay as they are." },
			"Editing your message" + when + ": sending it reverts the chat to before it"),
		h("button.btn.flat", { title: "Stop editing and empty the box", on: { click: () => this.stopEditing(true) } }, "Cancel"));
	}

	focus() {
		this.input.focus();
	}

	update(model) {
		const summary = model.summary || {};
		this.running = model.running();
		this.input.placeholder = window.matchMedia(PHONE).matches ? "Message\u2026"
			: "Message " + (summary.title || "the chat") + (this.running ? " - Enter sends it into the running turn, Ctrl+Enter queues it for after" : " - Enter sends, Shift+Enter breaks the line");
		this.sendButton.title = this.running ? "Send it into the running turn: the AI gets it after its current step (Enter)" : "Send (Enter)";
		this.stopButton.hidden = !this.running;
		replace(this.queuedEl, ...model.pending.map((text) => this.pendingRow(text)), ...model.queued.map((text, index) => this.queuedRow(text, index)));
		this.renderSelectors(summary, model.groupInUse, model.roleEffort);
		this.renderTelemetry(model);
	}

	// A message sent to the running turn, until the turn takes it at its next step.
	pendingRow(text) {
		return h("div.chat-queued.pending", { title: "The AI gets it after its current step: once the tools it runs now return" }, icon("arrow-right"), h("span.muted", null, "At the next step:"), h("span.ellip", null, text));
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
		this.mentions = (message.mentions || []).slice();
		this.attached.keep(message.attachments || []);
		this.setText(message.text);
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
			bar.window ? h("span.chat-mchip", { title: "Context used of the model's window" }, "ctx ", h("span.chat-bar", null, h("i", { style: { width: Math.min(bar.percent, 100) + "%" } })), h("b", null, bar.percent + "%"), " of " + compact(bar.window))
				: bar.used ? h("span.chat-mchip", { title: "Context used; the model's window is not known yet" }, "ctx ", h("b", null, compact(bar.used))) : null,
			model.model ? h("span.chat-mchip", null, h("span.dot.info"), model.model) : model.summary && model.summary.chatId ? h("span.chat-mchip.c-warn", null, "no model set up (ai.models)") : null);
	}
}

function rowsOf(options, current) {
	return options.map(([id, label]) => ({ kind: "item", label, value: id, enabled: true, icon: id === current ? "$(check)" : null }));
}
