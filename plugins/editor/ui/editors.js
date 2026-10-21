// editors.js - the open code editors of the page, the last focused one last, for the commands that act on "the editor"
// (Save), the status bar items that describe it, whatever waits for a document's editor to show its text, the features
// attached to every editor once it shows its text (the breakpoint margin), and the navigation history their cursors
// make.

import { Navigation } from "./navigation.js";

export class Editors {
	constructor() {
		this.open = new Set();
		this.listeners = new Set();
		this.waiting = new Map();   // ref -> resolvers waiting for its editor's text
		this.attachers = [];        // fn(editor) -> dispose, for every editor that shows its text
		this.navigation = new Navigation();
	}

	// `fn(editor)` for each editor once it shows its text; what it returns is called when the editor goes.
	attach(fn) {
		this.attachers.push(fn);
		for (const editor of this.open) if (editor.model) editor.attachments.push(fn(editor));
	}

	// `editor`'s cursor moved where the person put it.
	visited(editor) {
		const at = editor.position();
		if (at) this.navigation.visit({ path: editor.path, line: at.line - 1, col: at.col - 1 });
	}

	add(editor) {
		this.open.add(editor);
		this.changed();
	}

	delete(editor) {
		this.open.delete(editor);
		this.changed();
	}

	// `editor` shows its document's text.
	ready(editor) {
		for (const fn of this.attachers) editor.attachments.push(fn(editor));
		const waiting = this.waiting.get(editor.ctx.ref) || [];
		this.waiting.delete(editor.ctx.ref);
		for (const resolve of waiting) resolve(editor);
	}

	// Resolves with the editor of tab `ref` once it shows its text.
	whenReady(ref) {
		const editor = this.byRef(ref);
		if (editor && editor.model) return Promise.resolve(editor);
		return new Promise((resolve) => this.waiting.set(ref, (this.waiting.get(ref) || []).concat(resolve)));
	}

	// `editor` took the focus: it is now the last.
	touch(editor) {
		this.open.delete(editor);
		this.open.add(editor);
		this.changed();
	}

	last() {
		return [...this.open].pop() || null;
	}

	byRef(ref) {
		return [...this.open].find((editor) => editor.ctx.ref === ref) || null;
	}

	all() {
		return [...this.open];
	}

	onChange(fn) {
		this.listeners.add(fn);
		return () => this.listeners.delete(fn);
	}

	changed() {
		for (const fn of [...this.listeners]) fn(this.last());
	}
}
