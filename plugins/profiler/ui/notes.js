// notes.js - the Notes tool window (F-PRF-19): the capture's notes in time order, the owner's and the AI's, each a
// jump to its range on the timeline, with edit and delete; Add note annotates the selection.

import { h, icon, replace } from "/ui-kit/dom.js";
import { duration, instant } from "./format.js";

export function noteRange(note) {
	const span = note.toNs - note.fromNs;
	return span > 0 ? instant(note.fromNs, span) + " to " + instant(note.toNs, span) + " (" + duration(span) + ")" : "at " + instant(note.fromNs, 0);
}

export class NotesPanel {
	constructor({ onAdd, onJump, onEdit, onDelete }) {
		this.onJump = onJump;
		this.onEdit = onEdit;
		this.onDelete = onDelete;
		this.notes = [];
		this.selected = null;
		this.countLabel = h("span.muted");
		this.list = h("div.prof-notes");
		this.el = h("div.prof-panel", null,
			h("div.panel-tools", null, h("span.muted", null, "Notes"), this.countLabel, h("span.grow"),
				h("button.btn.flat", { title: "Add a note on the selection (or the selected zone)", on: { click: () => onAdd() } }, icon("add"), "Add note")),
			h("div.scroll", null, this.list));
		this.render();
	}

	// The notes do not depend on the scope: every note of the capture is listed.
	update() {}

	set(notes) {
		this.notes = notes || [];
		this.render();
	}

	select(id) {
		this.selected = id;
		this.render();
	}

	render() {
		this.countLabel.textContent = this.notes.length + (this.notes.length === 1 ? " note" : " notes");
		if (!this.notes.length) {
			replace(this.list, h("div.muted.prof-empty", null, "No notes yet. Select a range (Shift+drag) or a zone, then Note - or ask the AI to mark what it finds."));
			return;
		}
		replace(this.list, this.notes.map((note) => h("div.row.prof-note" + (note.id === this.selected ? ".on" : ""), {
			dataset: { note: note.id },
			on: { click: () => this.onJump(note) },
		},
		h("span.chip" + (note.by === "ai" ? ".ai" : ".info"), null, note.by === "ai" ? "AI" : "You"),
		h("span.meta.mono", null, noteRange(note)),
		h("span.grow.ellip", { title: note.text }, note.text),
		h("button.btn.flat.note-act", { title: "Edit the note", on: { click: (e) => { e.stopPropagation(); this.onEdit(note); } } }, icon("edit")),
		h("button.btn.flat.note-act", { title: "Delete the note", on: { click: (e) => { e.stopPropagation(); this.onDelete(note); } } }, icon("trash")))));
	}
}
