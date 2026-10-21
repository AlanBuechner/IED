// document.js - a server document as read-only text ("doc:<path>" tabs; docs/10 "Document editing protocol"): opened
// with doc.open, kept current by doc.edit events from every writer (other devices, the AI, a reload from disk), its
// dirty state on the tab. Lines render as they scroll into sight. Editing arrives with Monaco (M2).

import { h, icon, replace } from "/ui-kit/dom.js";
import { applyChanges, lineStartsOf, EditError } from "/ui-kit/textedits.js";

const OVERSCAN = 20;

export class DocumentView {
	constructor(ctx, path) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.path = path;
		this.text = "";
		this.lines = [""];
		this.version = 0;
		this.stop = null;
		this.note = h("span.doc-note.muted", null, "Opening...");
		this.window = h("div.doc-window");
		this.lineBox = h("div.doc-lines", null, this.window);
		this.scroller = h("div.doc-view", { tabindex: "0", role: "document", "aria-label": path, on: { scroll: () => this.draw() } }, this.lineBox);
		ctx.el.append(h("div.ed-tools", null, icon("lock", "sm"), h("span.chip", null, "Read-only"), h("span.ellip.muted", { title: path }, path), h("span.grow"), this.note), h("div.ed-fill", null, this.scroller));
		this.resizeObserver = new ResizeObserver(() => this.draw());
		this.resizeObserver.observe(this.scroller);
		this.open();
	}

	dispose() {
		this.resizeObserver.disconnect();
		if (this.stop) this.stop();
	}

	focus() {
		this.scroller.focus({ preventScroll: true });
	}

	async open() {
		try {
			const opened = await this.ied.client.request("doc.open", { wsId: this.ctx.wsId, path: this.path });
			this.stop = this.ied.client.subscribe("doc:" + opened.docId, {
				snap: (data) => this.snapshot(data),
				evt: (kind, data) => this.event(kind, data),
				gone: () => this.problem("This document is no longer open on the server."),
			});
		}
		catch (e) {
			this.problem(e.code === "not_found" ? "There is no file " + this.path + "." : "The document could not be opened: " + e.message);
		}
	}

	snapshot(data) {
		this.version = data.version;
		this.setText(data.text);
		this.ctx.tab.setDirty(data.dirty);
	}

	event(kind, data) {
		if (kind === "doc.edit") this.edit(data);
		else if (kind === "doc.saved") this.ctx.tab.setDirty(false);
		else if (kind === "doc.diskChanged") replace(this.note, icon("warning", "c-warn"), data.revision === 0 ? "Deleted on disk" : "Changed on disk");
	}

	edit(data) {
		try {
			this.setText(applyChanges(this.text, data.changes));
			this.version = data.version;
			if (data.origin !== "disk") this.ctx.tab.setDirty(true);
		}
		catch (e) {
			if (!(e instanceof EditError)) throw e;
			this.problem("An edit did not apply (" + e.message + "); reopen the tab.");
		}
	}

	setText(text) {
		this.text = text;
		const starts = lineStartsOf(text);
		this.lines = starts.map((start, i) => text.slice(start, i + 1 < starts.length ? starts[i + 1] - 1 : text.length).replace(/\r$/, ""));
		this.note.textContent = this.lines.length + (this.lines.length === 1 ? " line" : " lines");
		this.draw();
	}

	draw() {
		const lineHeight = parseInt(getComputedStyle(this.scroller).getPropertyValue("--line-height-code"), 10) || 18;
		const height = this.scroller.clientHeight || 600;
		const first = Math.max(0, Math.floor(this.scroller.scrollTop / lineHeight) - OVERSCAN);
		const last = Math.min(this.lines.length, Math.ceil((this.scroller.scrollTop + height) / lineHeight) + OVERSCAN);
		this.lineBox.style.height = this.lines.length * lineHeight + "px";
		this.window.style.transform = "translateY(" + first * lineHeight + "px)";
		const rows = [];
		for (let i = first; i < last; i++) rows.push(h("div.doc-line", null, h("span.ln", null, String(i + 1)), h("span.tx", null, this.lines[i])));
		replace(this.window, ...rows);
	}

	problem(message) {
		replace(this.scroller, h("div.ed-problem", null, message));
	}
}
