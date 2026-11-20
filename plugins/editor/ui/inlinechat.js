// inlinechat.js - the editor's inline chat (F-EDT-100/101, docs/04 "Inline chat"): Ctrl+I opens a box above the
// selected lines (the caret's line when nothing is selected); what is asked there goes to a chat of its own on the server
// (use "inline", in the Chats list's Inline section) with the lines and the file, in Ask mode with permission "plan", so
// the AI reads what it likes and writes nothing. Its reply's code block shows as a diff over the lines as it streams: the
// lines it removes red, those it adds green in the space between them. Accept (Ctrl+Enter) puts the change in the
// document, one undo step; Discard (Esc) drops it; anything typed after the first message asks again in the same chat.
// The session is the chat's: its file, lines and state are the chat's `meta.inline`, so a reload, another device or
// another editor of the file shows it where it was, and accepting or discarding anywhere closes it everywhere. The chat
// plugin's protocol is used as the profiler's and the board's are, never its modules (AGENTS.md rule 7); the box's model
// button opens the chat's own Model menu through its client bindings (chat.modelMenu, chat.modelLabel): the inline
// use's default model and effort (Settings > AI > Defaults), or the session's own, kept on its chat.

import { h, icon, replace } from "/ui-kit/dom.js";
import { renderMarkdown } from "/ui-kit/markdown.js";
import {
	INLINE_USE, INLINE_SECTION, titleOf, firstMessage, followUpMessage, proposalOf, replyText, lineHunks, hunkCounts,
	textHash, locateLines, inlineOf, linesLabel,
} from "./inlineedit.js";

const RENDER_MS = 80;          // a streaming reply redraws the diff at most this often
const WIDGET_MOST_WIDTH = 760;
const NARROW_WIDTH = 440;      // a box this narrow (a phone) has its model button on a row of its own, under the input
const PROPOSAL_KEY = "inlineChatProposal";
// Escape in the lines closes the session only when nothing else of the editor's wants it: a selection to clear, a widget
// or a hover to close.
const ESCAPE_WHEN = "iedInlineChat && !editorHasSelection && !editorHasMultipleSelections && !suggestWidgetVisible && !findWidgetVisible && !parameterHintsVisible && !renameInputVisible && !editorHoverVisible && !inSnippetMode";
const IN_A_TURN = new Set(["running", "awaiting_approval", "awaiting_answer"]);
const STOPS = { user_cancelled: "Stopped.", no_model: "No model to run on: set one in Settings > AI.", interrupted: "Interrupted by a restart.", harness_error: "Stopped by an error in IED." };

// Every inline session of the page: the workspace's inline chats followed from the chats:<wsId> topic, each open one
// shown in every editor of its file.
export class InlineChats {
	constructor(ied, editors) {
		this.ied = ied;
		this.editors = editors;
		this.chats = new Map();       // chatId -> summary, the workspace's inline chats
		this.sessions = new Map();    // editor -> [InlineSession]
		this.here = new Map();        // editor -> its Monaco context key iedInlineChat: the caret is in a session's lines
		this.creating = new Set();    // paths whose session is making its chat: their new chats are not restored meanwhile
		this.stopChats = null;
		this.follow();
		if (ied.workspace.on) ied.workspace.on(() => this.follow());
		editors.onChange(() => this.updateContext());
	}

	follow() {
		if (this.stopChats) this.stopChats();
		this.stopChats = null;
		this.chats.clear();
		const wsId = this.ied.workspace.wsId;
		if (!wsId) return;
		this.stopChats = this.ied.client.subscribe("chats:" + wsId, {
			snap: (data) => {
				this.chats.clear();
				for (const summary of data.chats || []) if (inlineOf(summary)) this.chats.set(summary.chatId, summary);
				this.reconcileAll();
			},
			evt: (kind, data) => {
				if (kind === "chat.deleted" || kind === "chat.archived" || kind === "chat.purged") this.chats.delete(data.chatId);
				else if ((kind === "chat.created" || kind === "chat.changed" || kind === "chat.restored") && inlineOf(data)) this.chats.set(data.chatId, data);
				else return;
				this.reconcileAll();
			},
		});
	}

	// An editor shows its text (Editors.attach): its sessions come up, Escape in it can close one, and a context menu
	// entry opens one (a phone has no Ctrl+I).
	attach(editor) {
		this.sessions.set(editor, []);
		const ed = editor.editor;
		const monaco = editor.monaco;
		this.here.set(editor, ed.createContextKey("iedInlineChat", false));
		// Monaco's keybindings are the page's, not the editor's: whichever editor's runs, it acts on the focused one.
		const focused = () => this.editors.all().find((e) => e.editor.hasTextFocus()) || editor;
		const listeners = [
			ed.onDidChangeCursorPosition(() => this.updateContext()),
			ed.onDidFocusEditorText(() => this.updateContext()),
			ed.onDidLayoutChange(() => this.of(editor).forEach((s) => s.place())),
			ed.onDidChangeModelContent(() => this.of(editor).forEach((s) => s.contentChanged())),
			ed.addAction({
				id: "ied.editor.inlineChat",
				label: "Inline Chat",
				keybindings: [monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyI],
				run: () => this.start(focused()),
			}),
			ed.addAction({
				id: "ied.editor.inlineChatEscape",
				label: "Inline Chat: Stop or Discard",
				keybindings: [monaco.KeyCode.Escape],
				precondition: ESCAPE_WHEN,
				run: () => {
					const session = this.at(focused());
					if (session) session.escape();
				},
			}),
		];
		this.reconcile(editor);
		return () => {
			for (const session of this.of(editor)) session.dispose();
			this.sessions.delete(editor);
			this.here.delete(editor);
			for (const listener of listeners) listener.dispose();
			this.updateContext();
		};
	}

	of(editor) {
		return this.sessions.get(editor) || [];
	}

	// The open sessions of each editor's file are shown there, and those whose chat closed (accepted, discarded,
	// deleted - here or on another device) go.
	reconcileAll() {
		for (const editor of this.sessions.keys()) this.reconcile(editor);
	}

	reconcile(editor) {
		const shown = this.of(editor);
		for (const session of shown.slice()) {
			if (!session.chatId) continue;
			const summary = this.chats.get(session.chatId);
			const inline = inlineOf(summary);
			if (!inline || inline.state !== "open") session.close();
			else session.summaryChanged(summary);
		}
		if (this.creating.has(editor.path)) return;
		for (const summary of this.chats.values()) {
			const inline = inlineOf(summary);
			if (inline.state !== "open" || inline.path !== editor.path || this.of(editor).some((s) => s.chatId === summary.chatId)) continue;
			this.add(editor, InlineSession.restore(this, editor, summary));
		}
	}

	add(editor, session) {
		this.sessions.set(editor, this.of(editor).concat(session));
		this.updateContext();
		return session;
	}

	removed(session) {
		this.sessions.set(session.editor, this.of(session.editor).filter((s) => s !== session));
		this.updateContext();
	}

	// The session whose lines hold the caret (or whose box has the focus), or null.
	at(editor) {
		const sessions = this.of(editor);
		const focused = sessions.find((s) => s.hasFocus());
		if (focused) return focused;
		const caret = editor.editor.getPosition();
		return caret ? sessions.find((s) => s.holdsLine(caret.lineNumber)) || null : null;
	}

	// Ctrl+I: the box of the session at the caret, or a new session over the selected lines (the caret's line when
	// nothing is selected). Lines another session holds open that one instead.
	start(editor) {
		if (!editor || !editor.model) return;
		const existing = this.at(editor);
		if (existing) return existing.focus();
		const selection = editor.editor.getSelection();
		let startLine = selection.startLineNumber;
		let endLine = selection.endLineNumber;
		if (endLine > startLine && selection.endColumn === 1) endLine--;
		const overlapping = this.of(editor).find((s) => s.overlaps(startLine, endLine));
		if (overlapping) return overlapping.focus();
		const session = this.add(editor, new InlineSession(this, editor, { startLine, endLine }));
		session.focus();
	}

	// Ctrl+Enter while a proposal is shown: the session at the caret accepts.
	accept() {
		const editor = this.editors.last();
		const session = editor ? this.at(editor) : null;
		if (session) session.accept();
	}

	// Ctrl+Enter accepts while the box with a proposal has the focus, or the caret is in its lines; Escape in an editor
	// acts on the session whose lines hold the caret.
	updateContext() {
		for (const [editor, here] of this.here) here.set(Boolean(this.at(editor)));
		// The box has the focus: the document's keys (Ctrl+D, Ctrl+L, Ctrl+Enter, ...) leave the document alone.
		const typing = [...this.sessions.values()].some((sessions) => sessions.some((s) => s.hasFocus()));
		this.ied.context.set("inlineChatFocus", typing || undefined);
		const editor = this.editors.last();
		const session = editor ? this.at(editor) : null;
		this.ied.context.set(PROPOSAL_KEY, session && session.proposalReady() ? true : undefined);
	}
}

// One session in one editor: the box above its lines, the diff of its proposal, and the chat behind it (none until the
// first message is sent).
class InlineSession {
	constructor(chats, editor, { startLine, endLine, chatId, inline, stale }) {
		this.chats = chats;
		this.ied = chats.ied;
		this.editor = editor;
		this.monaco = editor.monaco;
		this.chatId = chatId || null;
		this.inline = inline || null;
		this.stale = Boolean(stale);
		this.turns = [];
		this.state = "idle";
		this.approval = null;
		this.question = null;
		this.sentHash = inline ? inline.hash : null;
		this.busy = false;           // a request of the box's own is on its way
		this.error = "";
		this.closed = false;
		this.stopChat = null;
		this.renderTimer = 0;
		this.colors = { lines: [], html: [], pending: null };
		this.zones = [];
		this.diffKey = "";
		this.sentOnce = false;       // the first message reached the chat (a chat whose send failed gets it again)
		// The model and effort: its own ("" the inline use's default), kept here until the chat is made, then the chat's.
		const summary = this.chatId ? chats.chats.get(this.chatId) : null;
		this.pinned = (summary && summary.pinnedModel) || "";
		this.effort = (summary && summary.effort) || "";
		this.model = "";             // the model its next turn starts on, as its chat's snapshot says
		this.defaultEffort = null;   // the effort a turn gets when it names none, as the snapshot says; null before one
		this.labelKey = "";
		this.closeMenu = null;
		const model = editor.model;
		const lastColumn = model.getLineMaxColumn(endLine);
		this.range = editor.editor.createDecorationsCollection([{
			range: new this.monaco.Range(startLine, 1, endLine, lastColumn),
			options: { stickiness: this.monaco.editor.TrackedRangeStickiness.AlwaysGrowsWhenTypingAtEdges, linesDecorationsClassName: "ic-range-mark", isWholeLine: true },
		}]);
		this.removedLines = editor.editor.createDecorationsCollection([]);
		this.build();
		if (this.chatId) this.followChat();
		this.render();
	}

	// A session from its chat's summary: its lines found again where they are now (a reload, another device).
	static restore(chats, editor, summary) {
		const inline = inlineOf(summary);
		const lines = editor.model.getLinesContent();
		const count = Math.max(1, inline.lines || inline.endLine - inline.startLine + 1);
		let startLine = locateLines(lines, inline.startLine, count, inline.hash);
		const stale = !startLine;
		if (stale) startLine = Math.min(inline.startLine, lines.length);
		const endLine = Math.min(lines.length, startLine + count - 1);
		return new InlineSession(chats, editor, { startLine, endLine, chatId: summary.chatId, inline, stale });
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The box: an overlay widget over a view zone that keeps its room above the lines.

	build() {
		this.input = h("textarea.ic-input", {
			rows: "1",
			spellcheck: "false",
			"aria-label": "Ask the AI about the selected lines",
			placeholder: "Ask to change these lines, or ask about them",
			on: { keydown: (e) => this.key(e), input: () => this.grow() },
		});
		this.modelName = h("span.ic-pick-model", null, "...");
		this.effortName = h("span.ic-pick-effort");
		this.modelButton = h("button.ic-pick", { title: "Pick the model and effort this chat runs on", "aria-haspopup": "dialog", hidden: !this.ied.bindings || !this.ied.bindings.has("chat.modelMenu"),
			on: { click: () => this.pickModel() } }, this.modelName, this.effortName, icon("chevron-down"));
		this.sendButton = h("button.ic-btn", { title: "Send (Enter)", "aria-label": "Send", on: { click: () => this.send() } }, icon("send"));
		this.stopButton = h("button.ic-btn", { title: "Stop", "aria-label": "Stop", hidden: true, on: { click: () => this.stop() } }, icon("debug-stop"));
		const closeButton = h("button.ic-btn", { title: "Discard (Esc)", "aria-label": "Close", on: { click: () => this.discard() } }, icon("close"));
		this.status = h("div.ic-status", { role: "status", hidden: true });
		this.answer = h("div.ic-answer", { hidden: true });
		this.counts = h("span.ic-counts");
		this.acceptButton = h("button.btn.primary.ic-accept", { title: "Put the change in the document (Ctrl+Enter)", on: { click: () => this.accept() } }, "Accept", h("span.ic-key", null, "Ctrl+Enter"));
		this.discardButton = h("button.btn", { title: "Drop the change (Esc)", on: { click: () => this.discard() } }, "Discard");
		this.chatLink = h("button.ic-link", { title: "Show this inline chat in the Chat activity", on: { click: () => this.openChat() } }, "Open in Chat");
		this.actions = h("div.ic-actions", { hidden: true }, this.acceptButton, this.discardButton, this.counts, this.chatLink);
		this.node = h("div.ic-widget", { role: "group", "aria-label": "Inline chat", on: { focusin: () => this.focusMoved(), focusout: () => setTimeout(() => this.focusMoved()) } },
			h("div.ic-head", null, icon("sparkle", "ic-glyph"), this.input, this.modelButton, this.sendButton, this.stopButton, closeButton),
			this.status, this.answer, this.actions);
		this.overlay = {
			getId: () => "ied.inlineChat." + Math.random().toString(36).slice(2),
			getDomNode: () => this.node,
			getPosition: () => ({ preference: { top: this.top || 0, left: this.left || 0 } }),
		};
		const id = this.overlay.getId();
		this.overlay.getId = () => id;
		this.editor.editor.addOverlayWidget(this.overlay);
		this.zone = { afterLineNumber: this.startLine() - 1, heightInPx: 40, domNode: h("div.ic-room"), suppressMouseDown: true, onDomNodeTop: (top) => this.moved(top) };
		this.editor.editor.changeViewZones((zones) => (this.zoneId = zones.addZone(this.zone)));
		this.resize = new ResizeObserver(() => this.fit());
		this.resize.observe(this.node);
		this.place();
	}

	moved(top) {
		if (this.top === top) return;
		this.top = top;
		this.editor.editor.layoutOverlayWidget(this.overlay);
	}

	// The box across the code's width, from where the text starts.
	place() {
		const layout = this.editor.editor.getLayoutInfo();
		const room = layout.width - layout.contentLeft - layout.verticalScrollbarWidth - (layout.minimap ? layout.minimap.minimapWidth : 0) - 12;
		this.left = layout.contentLeft + 2;
		const width = Math.max(160, Math.min(WIDGET_MOST_WIDTH, room));
		this.node.style.width = width + "px";
		this.node.classList.toggle("narrow", width < NARROW_WIDTH);
		this.editor.editor.layoutOverlayWidget(this.overlay);
	}

	// The zone above the lines as tall as the box, and there still when lines above them come or go.
	fit() {
		if (this.closed) return;
		const height = Math.ceil(this.node.offsetHeight) + 6;
		const after = this.startLine() - 1;
		if (height === this.zone.heightInPx && after === this.zone.afterLineNumber) return;
		this.zone.heightInPx = height;
		this.zone.afterLineNumber = after;
		this.editor.editor.changeViewZones((zones) => zones.layoutZone(this.zoneId));
	}

	grow() {
		this.input.style.height = "auto";
		this.input.style.height = Math.min(this.input.scrollHeight, 160) + "px";
	}

	focus() {
		this.editor.editor.revealLinesInCenterIfOutsideViewport(Math.max(1, this.startLine() - 1), this.endLine());
		requestAnimationFrame(() => this.input.focus());
	}

	hasFocus() {
		return this.node.contains(document.activeElement);
	}

	focusMoved() {
		if (this.closed) return;
		this.chats.updateContext();
	}

	key(event) {
		if (event.isComposing) return;
		if (event.key === "Enter" && !event.shiftKey && !event.ctrlKey && !event.altKey) {
			event.preventDefault();
			this.send();
		}
		else if (event.key === "Escape") {
			event.preventDefault();
			this.escape();
		}
		else if (event.key === "Tab" && !event.shiftKey && !this.input.value && this.proposalReady()) {
			event.preventDefault();
			this.accept();
		}
	}

	// Escape: a reply on its way stops; otherwise the session is dropped.
	escape() {
		if (this.running()) this.stop();
		else this.discard();
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The lines

	lineRange() {
		const range = this.range.getRange(0);
		return range || new this.monaco.Range(1, 1, 1, 1);
	}

	startLine() {
		return this.lineRange().startLineNumber;
	}

	endLine() {
		const range = this.lineRange();
		return Math.max(range.startLineNumber, range.endLineNumber);
	}

	holdsLine(line) {
		return line >= this.startLine() - 1 && line <= this.endLine();
	}

	overlaps(startLine, endLine) {
		return startLine <= this.endLine() && endLine >= this.startLine();
	}

	currentLines() {
		const model = this.editor.model;
		const lines = [];
		for (let line = this.startLine(); line <= this.endLine(); line++) lines.push(model.getLineContent(line));
		return lines;
	}

	contentChanged() {
		if (this.closed) return;
		this.fit();
		this.scheduleRender();
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The chat

	running() {
		return IN_A_TURN.has(this.state) || this.busy;
	}

	followChat() {
		if (this.stopChat) return;
		this.node.dataset.showsChat = this.chatId;
		this.stopChat = this.ied.client.subscribe("chat:" + this.chatId, {
			snap: (data) => {
				this.turns = (data.turns || []).map((t) => ({ turnId: t.turnId, model: t.model, stop: t.stop, segments: (t.segments || []).map((s) => Object.assign({}, s)) }));
				this.state = data.state || "idle";
				this.stateReason = data.stateReason || "";
				this.choiceFrom(data);
				this.approval = (data.pendingApprovals || []).find((a) => !a.decision) || null;
				this.question = data.pendingQuestion || null;
				this.scheduleRender();
			},
			evt: (kind, data) => this.event(kind, data),
		});
	}

	event(kind, data) {
		const turn = this.turns[this.turns.length - 1];
		const segment = (id) => (turn ? turn.segments.find((s) => s.segId === id) : null);
		if (kind === "turn.start") {
			this.turns.push({ turnId: data.turnId, model: data.model, stop: null, segments: [] });
			this.state = "running";
			this.error = "";
		}
		else if (kind === "segment.start" && turn) turn.segments.push(Object.assign({ text: "" }, data));
		else if (kind === "segment.delta") {
			const seg = segment(data.segId);
			if (seg && data.text) seg.text = (seg.text || "") + data.text;
		}
		else if (kind === "segment.end") {
			const seg = segment(data.segId);
			if (seg) seg.done = true;
		}
		else if (kind === "tool.state" && turn) {
			const seg = turn.segments.find((s) => s.callId === data.callId);
			if (seg) Object.assign(seg, { state: data.state, result: data.result || seg.result });
		}
		else if (kind === "turn.end" && turn) turn.stop = data.stop;
		else if (kind === "chat.changed") {
			this.state = data.state || this.state;
			this.stateReason = data.stateReason || "";
			this.choiceFrom(data);
		}
		else if (kind === "approval.request") this.approval = data;
		else if (kind === "approval.resolved") this.approval = null;
		else if (kind === "question.request") this.question = data;
		else if (kind === "question.resolved") this.question = null;
		else if (kind === "chat.reverted" && data.snapshot) {
			this.turns = (data.snapshot.turns || []).map((t) => ({ turnId: t.turnId, model: t.model, stop: t.stop, segments: (t.segments || []).slice() }));
			this.state = data.snapshot.state || "idle";
		}
		else return;
		this.scheduleRender();
	}

	summaryChanged(summary) {
		this.inline = inlineOf(summary);
		if (summary.state && summary.state !== this.state && !this.stopChat) this.state = summary.state;
		this.choiceFrom(summary);
		this.scheduleRender();
	}

	// The chat's model and effort from a summary, a snapshot or its chat.changed: its own, and - a snapshot's - what a turn
	// would use now.
	choiceFrom(data) {
		if ("pinnedModel" in data) this.pinned = data.pinnedModel || "";
		if ("effort" in data) this.effort = data.effort || "";
		if (data.model) this.model = data.model;
		if ("defaultEffort" in data) this.defaultEffort = data.defaultEffort || "";
	}

	// The chat's Model menu at the box's button (chat.modelMenu): a choice before the first message is kept for the chat
	// it makes; after, it goes to the chat (chat.set), a model from its next turn, an effort from its next step.
	async pickModel() {
		if (this.closeMenu) this.closeMenu();
		this.closeMenu = null;
		try {
			const menu = await this.ied.bindings.call("chat.modelMenu", Object.assign(this.choiceArgs(), { at: this.modelButton, onChange: (change) => this.setChoice(change) }));
			if (this.closed && menu) menu.close();
			else this.closeMenu = menu ? menu.close : null;
		}
		catch (e) {
			this.failed("Could not show the models: " + e.message);
		}
	}

	setChoice(change) {
		if (this.closed) return;
		if ("model" in change) this.pinned = change.model || "";
		if ("effort" in change) this.effort = change.effort || "";
		this.render();
		if (this.chatId) this.ied.client.request("chat.set", Object.assign({ chatId: this.chatId }, change)).catch((e) => this.failed("Could not change the model: " + e.message));
	}

	choiceArgs() {
		return { wsId: this.editor.ctx.wsId, use: INLINE_USE, model: this.pinned, effort: this.effort, now: this.pinned ? "" : this.model, defaultEffort: this.defaultEffort };
	}

	// The button names the model the next turn starts on and its effort, as a chat's Model button does (chat.modelLabel).
	refreshModel() {
		if (this.modelButton.hidden) return;
		const args = this.choiceArgs();
		const key = JSON.stringify(args);
		if (key === this.labelKey) return;
		this.labelKey = key;
		this.ied.bindings.call("chat.modelLabel", args).then((label) => {
			if (this.closed || key !== this.labelKey || !label) return;
			this.modelName.textContent = label.model;
			this.effortName.textContent = label.effort;
			this.modelButton.title = label.title;
		}).catch((e) => {
			if (key === this.labelKey) this.modelName.textContent = "Model";
			this.ied.report("inline chat: " + e.message, e);
		});
	}

	async send() {
		const prompt = this.input.value.trim();
		if (!prompt || this.running() || this.closed) return;
		const editor = this.editor;
		const lines = this.currentLines();
		const where = { path: editor.path, startLine: this.startLine(), endLine: this.endLine(), lines, fence: this.fence() };
		const hash = textHash(lines.join("\n"));
		this.busy = true;
		this.error = "";
		this.input.value = "";
		this.grow();
		this.render();
		try {
			const first = !this.sentOnce && !this.turns.length;
			if (!this.chatId) {
				const inline = { path: editor.path, startLine: where.startLine, endLine: where.endLine, lines: lines.length, hash, state: "open" };
				this.chats.creating.add(editor.path);
				let made;
				try {
					const create = {
						wsId: editor.ctx.wsId, title: titleOf(editor.path, where.startLine, where.endLine, prompt), section: INLINE_SECTION,
						use: INLINE_USE, mode: "ask", permission: "plan", meta: { inline },
					};
					// The model and effort picked in the box before it asked; none: the inline use's defaults.
					if (this.pinned) create.model = this.pinned;
					if (this.effort) create.effort = this.effort;
					made = await this.ied.client.request("chat.create", create);
				}
				finally {
					this.chats.creating.delete(editor.path);
				}
				this.chatId = made.chatId;
				this.inline = inline;
				this.chats.chats.set(made.chatId, made);
				this.followChat();
			}
			else {
				const moved = this.inline && (this.inline.startLine !== where.startLine || this.inline.endLine !== where.endLine || this.inline.hash !== hash);
				if (moved) await this.setInline({ startLine: where.startLine, endLine: where.endLine, lines: lines.length, hash });
			}
			if (first) await this.ied.client.request("chat.send", { chatId: this.chatId, text: firstMessage(Object.assign({ prompt }, where)), mentions: [{ kind: "file", ref: editor.path, label: editor.path }] });
			else await this.ied.client.request("chat.send", { chatId: this.chatId, text: followUpMessage(Object.assign({ prompt }, where, { lines: hash === this.sentHash ? null : lines })) });
			this.sentOnce = true;
			this.sentHash = hash;
			this.stale = false;
		}
		catch (e) {
			this.error = "Could not ask: " + e.message;
			if (!this.input.value) this.input.value = prompt;
		}
		finally {
			this.busy = false;
			this.chats.reconcile(editor);
			this.render();
		}
	}

	// The fence's language: the document's ("c", "cpp"), so the model's reply is colored as code in the chat too.
	fence() {
		const language = this.editor.model.getLanguageId();
		return language && language !== "plaintext" ? language : "";
	}

	stop() {
		if (this.chatId) this.ied.client.request("chat.stop", { chatId: this.chatId }).catch((e) => this.failed("Could not stop: " + e.message));
	}

	setInline(changes) {
		this.inline = Object.assign({}, this.inline, changes);
		return this.ied.client.request("chat.set", { chatId: this.chatId, meta: { inline: this.inline } });
	}

	// The proposal goes into the document as one undo step, and the session closes everywhere.
	async accept() {
		const proposal = this.proposal();
		if (!proposal || this.closed) return;
		const editor = this.editor;
		try {
			await takeLease(editor);
		}
		catch (e) {
			return this.failed(e.message);
		}
		const model = editor.model;
		const start = this.startLine();
		const end = this.endLine();
		const range = new this.monaco.Range(start, 1, end, model.getLineMaxColumn(end));
		const text = proposal.lines.join(model.getEOL());
		editor.editor.pushUndoStop();
		editor.editor.executeEdits("ied.inlineChat", [{ range, text, forceMoveMarkers: true }]);
		editor.editor.pushUndoStop();
		const after = start + Math.max(1, proposal.lines.length) - 1;
		editor.editor.setSelection(new this.monaco.Selection(after, model.getLineMaxColumn(after), after, model.getLineMaxColumn(after)));
		this.finish("accepted");
		editor.editor.focus();
	}

	async discard() {
		if (this.closed) return;
		if (this.running()) this.stop();
		this.finish("discarded");
		this.editor.editor.focus();
	}

	// Closed here at once, and the chat told, so the other places showing it close too.
	finish(state) {
		const chatId = this.chatId;
		const inline = this.inline;
		this.close();
		if (chatId && inline) this.ied.client.request("chat.set", { chatId, meta: { inline: Object.assign({}, inline, { state }) } }).catch((e) => this.ied.report("inline chat: " + e.message, e));
	}

	async openChat() {
		if (!this.chatId) return;
		await this.ied.commands.run("shell.showActivity", { id: "ied.chat" });
		this.ied.tabs.open("chat:" + this.chatId, { preview: false });
	}

	failed(message) {
		this.error = message;
		this.render();
	}

	close() {
		if (this.closed) return;
		this.dispose();
		this.chats.removed(this);
	}

	dispose() {
		if (this.closed) return;
		this.closed = true;
		clearTimeout(this.renderTimer);
		if (this.closeMenu) this.closeMenu();
		this.closeMenu = null;
		if (this.stopChat) this.stopChat();
		this.resize.disconnect();
		const ed = this.editor.editor;
		ed.changeViewZones((zones) => {
			zones.removeZone(this.zoneId);
			for (const id of this.zones) zones.removeZone(id);
		});
		this.zones = [];
		ed.removeOverlayWidget(this.overlay);
		this.range.clear();
		this.removedLines.clear();
	}

	// ---------------------------------------------------------------------------------------------------------------
	// What it shows

	// The latest turn's code block, else the one before it (a follow-up that asked a question keeps the change shown).
	proposal() {
		const original = this.currentLines();
		for (let i = this.turns.length - 1; i >= 0; i--) {
			const turn = this.turns[i];
			const found = proposalOf(replyText(turn), original, !turn.stop);
			if (found && (found.lines.length || found.closed)) return Object.assign(found, { turn, latest: i === this.turns.length - 1 });
		}
		return null;
	}

	proposalReady() {
		if (this.closed || this.running()) return false;
		const proposal = this.proposal();
		return Boolean(proposal && proposal.closed);
	}

	scheduleRender() {
		if (this.renderTimer || this.closed) return;
		this.renderTimer = setTimeout(() => {
			this.renderTimer = 0;
			this.render();
		}, RENDER_MS);
	}

	render() {
		if (this.closed) return;
		const proposal = this.proposal();
		const running = this.running();
		const latest = this.turns[this.turns.length - 1];
		const hunks = proposal ? lineHunks(this.currentLines(), proposal.lines) : [];
		this.sendButton.hidden = running;
		this.stopButton.hidden = !running || !this.chatId;
		this.input.placeholder = proposal ? "Ask for changes to this proposal" : this.chatId ? "Ask again, or for a change" : "Ask to change these lines, or ask about them";
		replace(this.status, ...this.statusParts(running, latest, proposal, hunks));
		this.status.hidden = !this.status.childNodes.length;
		const prose = latest ? (proposal && proposal.latest ? proposal.prose : replyText(latest).trim()) : "";
		if (prose !== this.shownProse) {
			this.shownProse = prose;
			replace(this.answer, prose ? renderMarkdown(prose) : null);
		}
		this.answer.hidden = !prose;
		this.showDiff(hunks, proposal ? proposal.lines : []);
		const counts = hunkCounts(hunks);
		replace(this.counts, counts.plus ? h("span.ic-plus", null, "+" + counts.plus) : null, counts.minus ? h("span.ic-minus", null, "-" + counts.minus) : null);
		const ready = Boolean(proposal && proposal.closed && !running);
		this.actions.hidden = !this.chatId;
		this.acceptButton.hidden = !ready;
		this.acceptButton.disabled = !hunks.length;
		this.discardButton.textContent = ready ? "Discard" : "Close";
		this.counts.hidden = !proposal;
		this.node.classList.toggle("running", running);
		this.refreshModel();
		this.chats.updateContext();
		this.fit();
	}

	statusParts(running, latest, proposal, hunks) {
		const parts = [];
		if (this.error) return [icon("error", "c-error"), h("span", null, this.error)];
		if (this.stale && !this.turns.length) parts.push(icon("warning", "c-warn"), h("span", null, "These lines changed since the question; a change would replace them as they are now."));
		if (this.approval) return [icon("shield", "c-warn"), h("span", null, "The AI asks to " + (this.approval.summary || this.approval.tool) + "."), h("button.ic-link", { on: { click: () => this.openChat() } }, "Decide in the chat")];
		if (this.question) return [icon("question", "c-warn"), h("span", null, this.question.question || "The AI asks a question."), h("button.ic-link", { on: { click: () => this.openChat() } }, "Answer in the chat")];
		if (running) {
			const tool = latest ? [...latest.segments].reverse().find((s) => s.type === "tool" && s.state === "running") : null;
			const doing = this.busy && !this.stopChat ? "Starting the chat" : tool ? (tool.result && tool.result.preview) || "Running " + tool.tool : proposal && proposal.latest ? "Writing the change" : latest && latest.segments.length ? "Working" : "Thinking";
			return [icon("loading", "spin c-ai"), h("span", null, doing), h("span.ic-model", null, (latest && latest.model) || this.model || "")];
		}
		if (latest && latest.stop && latest.stop !== "finished") parts.push(icon("warning", "c-warn"), h("span", null, STOPS[latest.stop] || "Stopped: " + String(latest.stop).replace(/_/g, " ") + (this.stateReason ? " (" + this.stateReason + ")" : "") + "."));
		else if (latest && latest.stop && !proposal && !replyText(latest).trim()) parts.push(h("span", null, "The reply had no change."));
		else if (proposal && proposal.closed && !hunks.length) parts.push(h("span", null, "The proposal leaves " + (this.endLine() > this.startLine() ? "lines " : "line ") + linesLabel(this.startLine(), this.endLine()) + " as they are."));
		return parts;
	}

	// The removed lines marked in the document, the added ones in zones under them, colored as the editor colors the
	// document's language.
	showDiff(hunks, proposed) {
		const start = this.startLine();
		const html = this.colored(proposed);
		const key = JSON.stringify([start, hunks, html.map(Boolean)]);
		if (key === this.diffKey) return;
		this.diffKey = key;
		const removed = [];
		for (const hunk of hunks) {
			for (let i = 0; i < hunk.removed; i++) {
				const line = start + hunk.at + i;
				removed.push({ range: new this.monaco.Range(line, 1, line, 1), options: { isWholeLine: true, className: "ic-removed", linesDecorationsClassName: "ic-removed-mark" } });
			}
		}
		this.removedLines.set(removed);
		const font = this.editor.editor.getOption(this.monaco.editor.EditorOption.fontInfo);
		const lineHeight = this.editor.editor.getOption(this.monaco.editor.EditorOption.lineHeight);
		const tabSize = this.editor.model.getOptions().tabSize;
		const offsets = indexesOf(hunks);
		const markLeft = this.editor.editor.getLayoutInfo().decorationsLeft + 4;
		this.editor.editor.changeViewZones((zones) => {
			for (const id of this.zones) zones.removeZone(id);
			this.zones = [];
			for (const hunk of hunks) {
				if (!hunk.added.length) continue;
				const first = offsets.get(hunk);
				const node = h("div.ic-added", { style: { "font-family": font.fontFamily, "font-size": font.fontSize + "px", "line-height": lineHeight + "px", "tab-size": String(tabSize) } });
				hunk.added.forEach((text, i) => {
					const line = h("div.ic-line");
					if (html[first + i]) line.innerHTML = html[first + i];
					else line.textContent = text || " ";
					node.append(line);
				});
				const after = hunk.removed ? start + hunk.at + hunk.removed - 1 : start + hunk.at - 1;
				this.zones.push(zones.addZone({ afterLineNumber: after, heightInLines: hunk.added.length, domNode: node, marginDomNode: h("div", null, h("div.ic-added-mark", { style: { left: markLeft + "px" } })), suppressMouseDown: true }));
			}
		});
	}

	// Each proposed line's HTML as Monaco colors it, from the last time the lines were colored ("" for a line that has
	// changed since: it shows plain meanwhile, so a streaming reply keeps the lines it already had colored); the lines
	// are colored again when they changed.
	colored(lines) {
		const text = lines.join("\n");
		const known = this.colors;
		if (known.pending !== text && known.lines.join("\n") !== text) {
			known.pending = text;
			const language = this.editor.model.getLanguageId();
			this.monaco.editor.colorize(text, language, { tabSize: this.editor.model.getOptions().tabSize }).then((html) => {
				if (known.pending !== text || this.closed) return;
				this.colors = { lines: lines.slice(), html: html.split("<br/>"), pending: null };
				this.scheduleRender();
			}, () => {});
		}
		return lines.map((line, i) => (this.colors.lines[i] === line && this.colors.html[i] ? this.colors.html[i] : ""));
	}
}

// Each hunk's first added line's index among all the added lines (the proposal's, which `colored` colors as one text).
function indexesOf(hunks) {
	const offsets = new Map();
	let unchanged = 0;
	let at = 0;
	for (const hunk of hunks) {
		at += hunk.at - unchanged;
		offsets.set(hunk, at);
		at += hunk.added.length;
		unchanged = hunk.at + hunk.removed;
	}
	return offsets;
}

// Accepting writes the document: this window takes its lease when nobody holds it.
async function takeLease(editor) {
	const sync = editor.sync;
	if (sync.readOnly) throw new Error("This document is read-only" + (sync.readOnlyReason ? ": " + sync.readOnlyReason : "") + ".");
	if (editor.mixedEndings) throw new Error("This file mixes line endings, so it is read-only here.");
	if (sync.holdsLease) return;
	if (sync.holder) throw new Error("Another window is editing this document: use Edit Here, then Accept.");
	await sync.takeLease();
}
