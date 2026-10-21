// editors.js - the open code editors of the page, the last focused one last, for the commands that act on "the editor"
// (Save) and the status bar items that describe it.

export class Editors {
	constructor() {
		this.open = new Set();
		this.listeners = new Set();
	}

	add(editor) {
		this.open.add(editor);
		this.changed();
	}

	delete(editor) {
		this.open.delete(editor);
		this.changed();
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
