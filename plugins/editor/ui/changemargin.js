// changemargin.js - the track-changes margin (VS): a bar beside each line changed since the file was last saved
// (--accent-unsaved) and beside each line changed and saved since the server opened the document (--accent-saved).
// The baselines are the server's (`doc.baseline`, docs/10), so a reload or another device shows the same marks.

import { changedLines } from "./linediff.js";

const REDRAW_MS = 150;

export class ChangeMargin {
	constructor(editor, client, report) {
		this.editor = editor;
		this.client = client;
		this.report = report;
		this.baseline = null;
		this.asked = 0;
		this.timer = null;
		this.marks = editor.createDecorationsCollection();
	}

	// Asks for the document's baselines again (it was opened, saved, or reloaded from disk), then redraws.
	async refresh(docId) {
		const asked = ++this.asked;
		try {
			const baseline = await this.client.request("doc.baseline", { docId });
			if (asked !== this.asked) return;
			this.baseline = baseline;
			this.redraw();
		}
		catch (e) {
			if (asked === this.asked) this.report("the change margin's baseline: " + e.message, e);
		}
	}

	// The text changed: a redraw once typing pauses.
	changed() {
		clearTimeout(this.timer);
		this.timer = setTimeout(() => this.redraw(), REDRAW_MS);
	}

	redraw() {
		const model = this.editor.getModel();
		if (!this.baseline || !model) return;
		const text = model.getValue();
		const unsaved = changedLines(this.baseline.saved, text);
		const isUnsaved = new Set(unsaved);
		const saved = changedLines(this.baseline.opened, text).filter((line) => !isUnsaved.has(line));
		this.marks.set([...marksFor(unsaved, "tc-unsaved"), ...marksFor(saved, "tc-saved")]);
	}

	dispose() {
		clearTimeout(this.timer);
		this.asked++;
		this.marks.clear();
	}
}

function marksFor(lines, className) {
	return lines.map((line) => ({ range: { startLineNumber: line, startColumn: 1, endLineNumber: line, endColumn: 1 }, options: { linesDecorationsClassName: className } }));
}
