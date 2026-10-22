// bufview.js - the buffer view (docs/22 "Buffer view"): a buffer's bytes after the selected event, read as rows of a
// layout - the one its binding suggests (a vertex buffer's attributes, an index buffer's format), which the user can
// edit - a page of elements at a time from an offset.

import { h, icon, replace } from "/ui-kit/dom.js";
import { DOT, bytes } from "./format.js";
import { parseLayout, readRows } from "./layout.js";

const PAGE_ROWS = 256;

export class BufferPanel {
	// read(id, eid, offset, size) resolves to the bytes (an ArrayBuffer).
	constructor({ onClose, read }) {
		this.read = read;
		this.edited = new Map();         // the user's layout for a buffer as one binding suggested it: "id|suggested" -> text
		this.generation = 0;
		this.title = h("b");
		this.layoutInput = h("input.gpu-buf-layout", {
			type: "text", spellcheck: "false", "aria-label": "Layout",
			title: "One element's fields in order: float3 position; float2 uv; uint16 index; unorm8x4 color; float4x4 world; pad 4; or hex",
			on: { change: () => this.setLayout(this.layoutInput.value) },
		});
		this.offsetInput = h("input", { type: "number", min: "0", "aria-label": "Offset in bytes", title: "Offset in bytes", on: { change: () => this.go(Number(this.offsetInput.value) || 0) } });
		this.status = h("span.range");
		this.problem = h("div.gpu-state-msg.error", { hidden: true });
		this.table = h("div.gpu-mesh-table");
		this.el = h("div.gpu-shader.gpu-buffer", null,
			h("div.gpu-rv-tools", null, icon("symbol-array"), this.title, h("span.grow"),
				h("button.ib", { title: "Back to the event's output", "aria-label": "Close", on: { click: onClose } }, icon("close"))),
			h("div.gpu-rv-tools.gpu-buf-tools", null,
				this.layoutInput,
				h("span.gpu-buf-nav", null,
					h("span.range", null, "at"), this.offsetInput,
					h("button.ib", { title: "Previous elements", "aria-label": "Previous elements", on: { click: () => this.page(-1) } }, icon("chevron-left")),
					h("button.ib", { title: "Next elements", "aria-label": "Next elements", on: { click: () => this.page(1) } }, icon("chevron-right")),
					this.status)),
			this.problem, this.table);
	}

	// Shows buffer `target` ({id, name, size, offset?, layout?}) as it is after event `eid`, from the binding's offset.
	open(target, eid) {
		this.target = target;
		this.key = target.id + "|" + (target.layout || "");
		this.layoutText = this.edited.get(this.key) || target.layout || "float4";
		this.layoutInput.value = this.layoutText;
		this.offset = target.offset || 0;
		this.eid = eid;
		this.load();
	}

	// The same rows after another event.
	at(eid) {
		this.eid = eid;
		this.load();
	}

	setLayout(text) {
		this.layoutText = text;
		this.edited.set(this.key, text);
		this.load();
	}

	go(offset) {
		this.offset = Math.max(0, Math.min(offset, Math.max(0, (this.target.size || 0) - 1)));
		this.load();
	}

	page(direction) {
		if (this.layout) this.go(this.offset + direction * PAGE_ROWS * this.layout.stride);
	}

	async load() {
		const generation = ++this.generation;
		const target = this.target;
		this.title.textContent = "Buffer " + DOT + " " + (target.name || "#" + target.id) + " " + DOT + " " + bytes(target.size) + " " + DOT + " after EID " + this.eid;
		this.offsetInput.value = String(this.offset);
		try {
			this.layout = parseLayout(this.layoutText);
			this.problem.hidden = true;
		}
		catch (e) {
			this.problem.textContent = e.message;
			this.problem.hidden = false;
			this.status.textContent = "";
			replace(this.table);
			return;
		}
		const size = Math.min(PAGE_ROWS * this.layout.stride, Math.max(0, (target.size || 0) - this.offset));
		if (size < this.layout.stride && !this.layout.hex) {
			this.status.textContent = "";
			replace(this.table, h("div.gpu-state-msg", null, "No whole element at offset " + this.offset + " (" + this.layout.stride + " B each)."));
			return;
		}
		try {
			const data = await this.read(target.id, this.eid, this.offset, size);
			if (generation === this.generation) this.render(data);
		}
		catch (e) {
			if (generation === this.generation) replace(this.table, h("div.gpu-state-msg.error", null, e.message || String(e)));
		}
	}

	render(data) {
		const layout = this.layout;
		const rows = readRows(layout, data);
		if (layout.hex) {
			this.status.textContent = bytes(data.byteLength) + " from " + this.offset;
			const head = h("tr", null, h("th", null, "Offset"), Array.from({ length: 16 }, (_, i) => h("th", null, i.toString(16).padStart(2, "0"))), h("th.t", null, "Text"));
			replace(this.table, h("table", null, h("thead", null, head), h("tbody", null, rows.map((row, i) =>
				h("tr", null, h("td.d", null, String(this.offset + i * 16)), row.cells.map((cell) => h("td", null, cell)), h("td.t", null, row.text))))));
			return;
		}
		// Elements count from the binding's start, so a row's number is its index there.
		const first = Math.floor((this.offset - (this.target.offset || 0)) / layout.stride);
		this.status.textContent = rows.length + " elements " + DOT + " " + layout.stride + " B each";
		const head = h("tr", null, h("th", null, "#"), h("th", null, "Offset"),
			layout.fields.map((f) => h("th", { colspan: String(f.components), title: f.type + " +" + f.offset }, f.name)));
		replace(this.table, h("table", null, h("thead", null, head), h("tbody", null, rows.map((row, i) =>
			h("tr", null, h("td.d", null, String(first + i)), h("td.d", null, String(this.offset + i * layout.stride)), row.cells.map((cell) => h("td", null, cell)))))));
	}
}
