// editorview.js - a server document in Monaco ("doc:<path>" tabs; F-EDT-1, docs/10): DocumentSync keeps the text with
// the server, the lease decides who types (a bar offers "Edit Here" to the others), typing pins a preview tab, the tab
// shows unsaved changes, and Ctrl+S saves. Language features come from the language servers (language.js). A file
// mixing line endings opens read-only: Monaco would make them one kind and the positions it reports would no longer be
// the server's.

import { h, icon, replace } from "/ui-kit/dom.js";
import { DocumentSync } from "/ui-kit/docsync.js";
import { themeName } from "./monaco.js";
import { ChangeMargin } from "./changemargin.js";
import { displayPath, isFileUri } from "./fileuris.js";

const NOTICE_MS = 8000;
// Narrower than this an editor is not laid out yet (a hidden or just-opened tab).
const MIN_REVEAL_WIDTH = 40;

export class CodeEditor {
	// `editors`: the page's open editors (editors.js); `features`: the language servers' (language.js).
	constructor(ctx, path, monaco, editors, features) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.path = path;
		this.monaco = monaco;
		this.editors = editors;
		this.features = features;
		this.detach = null;
		this.attachments = [];    // what Editors.attach added, disposed with the editor
		this.model = null;
		this.applying = false;
		this.mixedEndings = false;
		this.bar = h("div.ed-bar", { hidden: true });
		this.host = h("div.ed-fill.code-host");
		ctx.el.append(this.bar, this.host);
		this.editor = monaco.editor.create(this.host, editorOptions(monaco));
		this.margin = new ChangeMargin(this.editor, this.ied.client, (message, e) => this.ied.report(message, e));
		this.editor.onDidFocusEditorText(() => this.focused());
		this.editor.onDidChangeCursorPosition((event) => {
			this.changed();
			if (event.source !== "modelChange" && this.editor.hasTextFocus()) this.editors.visited(this);
		});
		this.sync = new DocumentSync(this.ied.client, this.target(), { onState: (state) => this.stateChanged(state) });
		editors.add(this);
		this.sync.open(ctx.wsId, path).catch((e) => this.problem(e.code === "not_found" ? "There is no file " + path + "." : "The document could not be opened: " + e.message));
	}

	dispose() {
		this.editors.delete(this);
		if (this.detach) this.detach();
		for (const dispose of this.attachments) if (typeof dispose === "function") dispose();
		this.margin.dispose();
		this.sync.dispose();
		this.editor.dispose();
		if (this.model) this.model.dispose();
	}

	focus() {
		this.editor.focus();
	}

	// ---------------------------------------------------------------------------------------------------------------
	// What DocumentSync drives

	target() {
		return {
			setText: (text) => this.setText(text),
			applyRemote: (changes) => this.applyRemote(changes),
			setReadOnly: (readOnly) => this.setReadOnly(readOnly),
		};
	}

	setText(text) {
		this.mixedEndings = /\r\n/.test(text) && /(^|[^\r])\n/.test(text);
		this.applying = true;
		const first = !this.model;
		if (first) this.model = this.takeModel(text);
		else this.model.setValue(text);
		this.applying = false;
		this.setReadOnly(!this.sync.holdsLease);
		if (!first) return;
		this.detach = this.features.attach(this);
		this.editors.ready(this);
	}

	// The document's model: a new one, or the one a peek made of the file (language.js).
	takeModel(text) {
		const uri = isFileUri(this.path) ? this.monaco.Uri.parse(this.path) : this.monaco.Uri.from({ scheme: "ied", authority: this.ctx.wsId, path: "/" + this.path });
		let model = this.monaco.editor.getModel(uri);
		if (model) {
			model.setValue(text);
			this.monaco.editor.setModelLanguage(model, this.sync.languageId);
		}
		else model = this.monaco.editor.createModel(text, this.sync.languageId, uri);
		model.updateOptions({ bracketColorizationOptions: { enabled: false, independentColorPoolPerBracketType: false } });
		model.onDidChangeContent((event) => this.edited(event));
		this.editor.setModel(model);
		return model;
	}

	// Shows a place: a position or a range (Monaco's), centered when it is out of view. An editor not laid out yet (a
	// tab just opened) would scroll sideways to fit the place into no width, so it shows the place again once it has one.
	reveal(selection) {
		if (!selection) return;
		const range = selection.startLineNumber !== undefined ? selection
			: { startLineNumber: selection.lineNumber, startColumn: selection.column, endLineNumber: selection.lineNumber, endColumn: selection.column };
		this.editor.setSelection({ selectionStartLineNumber: range.startLineNumber, selectionStartColumn: range.startColumn, positionLineNumber: range.startLineNumber, positionColumn: range.startColumn });
		this.editor.layout();
		if (this.editor.getLayoutInfo().contentWidth > MIN_REVEAL_WIDTH) this.editor.revealRangeInCenterIfOutsideViewport(range);
		else this.revealOnLayout(range);
		this.editor.focus();
	}

	revealOnLayout(range) {
		const waiting = this.editor.onDidLayoutChange((layout) => {
			if (layout.contentWidth <= MIN_REVEAL_WIDTH) return;
			waiting.dispose();
			this.editor.setScrollLeft(0);
			this.editor.revealRangeInCenterIfOutsideViewport(range);
		});
	}

	applyRemote(changes) {
		this.applying = true;
		this.model.applyEdits(changes.map((change) => ({ range: toRange(this.monaco, change.range), text: change.text })));
		this.applying = false;
		this.changed();
		this.margin.changed();
	}

	setReadOnly(readOnly) {
		this.editor.updateOptions({ readOnly: readOnly || this.mixedEndings, readOnlyMessage: { value: this.readOnlyReason() } });
		this.showBar();
	}

	// The person typed: Monaco's changes refer to the text before them, as the protocol's do.
	edited(event) {
		if (this.applying) return;
		this.sync.localEdit(event.changes.map((change) => ({ range: fromRange(change.range), text: change.text })));
		this.ctx.tab.pin();
		this.changed();
		this.margin.changed();
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The lease, the bar, saving

	focused() {
		this.ied.context.set("editorLanguage", this.sync.languageId);
		this.editors.touch(this);
		this.editors.visited(this);
		if (this.sync.docId && this.sync.holder === null && !this.sync.readOnly) this.sync.takeLease().catch((e) => this.ied.report("the lease: " + e.message, e));
		this.changed();
	}

	readOnlyReason() {
		if (this.sync.readOnly) return "This file is outside the workspace; IED shows it read-only.";
		if (this.mixedEndings) return "This file mixes line endings (CRLF and LF); it opens read-only until they are one kind.";
		return "Another window is editing this document. Use Edit Here to take over.";
	}

	stateChanged(state) {
		if (state.renamed && state.renamed !== this.path) this.moveTo(state.renamed);
		if (state.baselineMoved) this.margin.refresh(this.sync.docId);
		this.ctx.tab.setDirty(state.dirty);
		if (state.notice) this.notice(state.notice);
		if (state.error) this.problem(state.error);
		this.showBar();
	}

	showBar() {
		if (this.noticeText) return;
		const sync = this.sync;
		if (sync.readOnly) this.setBar("lock", "Read-only: " + displayPath(this.path) + " is outside the workspace.", null);
		else if (this.mixedEndings) this.setBar("warning", "This file mixes line endings (CRLF and LF), so it is read-only here.", null);
		else if (sync.docId && sync.holder && !sync.holdsLease) this.setBar("lock", "Read-only: another window is editing this document.", h("button.btn", { on: { click: () => this.takeOver() } }, "Edit Here"));
		else if (sync.diskChanged) this.setBar("warning", sync.diskChanged === "deleted" ? "The file was deleted on disk; saving writes it again." : "The file changed on disk while you edit it here; saving replaces it.", null);
		else this.bar.hidden = true;
	}

	setBar(glyph, text, action) {
		replace(this.bar, icon(glyph), h("span", null, text), action);
		this.bar.hidden = false;
	}

	notice(text) {
		this.noticeText = text;
		this.setBar("info", text, null);
		setTimeout(() => {
			this.noticeText = null;
			this.showBar();
		}, NOTICE_MS);
	}

	// The file was renamed: the document now lives at `path`, so its tab does - once this editor's edits are on the
	// server, so the new tab's snapshot has them.
	moveTo(path) {
		this.sync.settled().then(() => {
			this.ied.tabs.open("doc:" + path, { preview: false });
			this.ctx.tab.close();
		});
	}

	takeOver() {
		this.sync.takeLease().then(() => this.editor.focus(), (e) => this.ied.report("taking over the document: " + e.message, e));
	}

	save() {
		return this.sync.save().catch((e) => this.ied.report("saving " + this.path + ": " + e.message, e));
	}

	problem(message) {
		replace(this.host, h("div.ed-problem", null, message));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// For the status bar

	position() {
		const at = this.editor.getPosition();
		return at ? { line: at.lineNumber, col: at.column } : null;
	}

	describe() {
		const model = this.model;
		if (!model) return null;
		const options = model.getOptions();
		return {
			position: this.position(),
			eol: model.getEOL() === "\r\n" ? "CRLF" : "LF",
			indent: options.insertSpaces ? "Spaces: " + options.tabSize : "Tabs",
			language: this.sync.languageId,
			readOnly: this.editor.getOption(this.monaco.editor.EditorOption.readOnly),
		};
	}

	changed() {
		this.editors.changed();
	}
}

function toRange(monaco, range) {
	return new monaco.Range(range.start.line + 1, range.start.col + 1, range.end.line + 1, range.end.col + 1);
}

function fromRange(range) {
	return { start: { line: range.startLineNumber - 1, col: range.startColumn - 1 }, end: { line: range.endLineNumber - 1, col: range.endColumn - 1 } };
}

// VS defaults (docs/15, F-EDT-30): tabs, indentation guides, the map scrollbar (not on a phone, F-SHL-44); fonts from
// the theme tokens.
function editorOptions(monaco) {
	const style = getComputedStyle(document.documentElement);
	const phone = window.matchMedia("(max-width: 719px)").matches;
	return {
		theme: themeName(),
		automaticLayout: true,
		fixedOverflowWidgets: true,
		fontFamily: style.getPropertyValue("--font-mono").trim(),
		fontSize: parseInt(style.getPropertyValue("--font-size-code"), 10) || 13,
		lineHeight: parseInt(style.getPropertyValue("--line-height-code"), 10) || 18,
		insertSpaces: false,
		detectIndentation: true,
		tabSize: 4,
		guides: { indentation: true },
		minimap: { enabled: !phone, renderCharacters: false, showSlider: "always" },
		renderWhitespace: "none",
		scrollBeyondLastLine: false,
		smoothScrolling: false,
		readOnly: true,
		bracketPairColorization: { enabled: false },
		"semanticHighlighting.enabled": true,
	};
}
