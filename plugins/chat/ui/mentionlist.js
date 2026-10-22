// mentionlist.js - the composer's @ list (docs/04 "Attachments and @-mentions"): above the text box while an "@word" is
// typed, narrowing as it grows; Up/Down choose, Enter or Tab inserts, Esc closes, a click inserts.

import { h, icon, replace } from "/ui-kit/dom.js";
import { KIND_ICONS, mentionAt } from "./mentions.js";

// input: the composer's textarea; rows(query) -> Promise<[row]>; inserted(row): a row was put in the text.
export class MentionList {
	constructor(input, rows, inserted) {
		this.input = input;
		this.rows = rows;
		this.inserted = inserted;
		this.shown = [];
		this.active = 0;
		this.at = null;
		this.asked = 0;
		this.el = h("div.chat-mention-list", { role: "listbox", "aria-label": "Mentions", hidden: true });
		this.el.addEventListener("mousedown", (event) => event.preventDefault());
	}

	get open() {
		return !this.el.hidden;
	}

	// The text or caret changed: the list for the "@word" at the caret, or none.
	async follow() {
		const at = mentionAt(this.input.value, this.input.selectionStart);
		if (!at) return this.close();
		this.at = at;
		const asked = ++this.asked;
		const rows = await this.rows(at.query);
		if (asked !== this.asked) return;
		this.shown = rows;
		this.active = 0;
		this.render();
	}

	close() {
		this.asked++;
		this.at = null;
		this.el.hidden = true;
	}

	// A key while the list is open: true when the list took it.
	key(event) {
		if (!this.open) return false;
		if (event.key === "ArrowDown" || event.key === "ArrowUp") {
			const step = event.key === "ArrowDown" ? 1 : -1;
			this.active = (this.active + step + this.shown.length) % this.shown.length;
			this.render();
		}
		else if ((event.key === "Enter" || event.key === "Tab") && !event.shiftKey) this.insert(this.shown[this.active]);
		else if (event.key === "Escape") this.close();
		else return false;
		event.preventDefault();
		return true;
	}

	insert(row) {
		if (!row || !this.at) return;
		const text = this.input.value;
		const caret = this.input.selectionStart;
		const written = "@" + row.label + " ";
		this.input.value = text.slice(0, this.at.start) + written + text.slice(caret);
		const after = this.at.start + written.length;
		this.input.setSelectionRange(after, after);
		this.close();
		this.inserted(row);
	}

	render() {
		this.el.hidden = !this.shown.length;
		replace(this.el, ...this.shown.map((row, index) => h("div.chat-mention-row" + (index === this.active ? ".on" : ""), {
			role: "option", "aria-selected": String(index === this.active), title: row.detail ? row.label + " - " + row.detail : row.label,
			on: { click: () => this.insert(row) },
		}, icon(KIND_ICONS[row.kind] || "mention"), h("span.ellip", null, "@" + row.label), row.detail ? h("span.muted.ellip", null, row.detail) : null)));
		const on = this.el.querySelector(".on");
		if (on) on.scrollIntoView({ block: "nearest" });
	}
}
