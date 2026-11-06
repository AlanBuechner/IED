// hex.js - the hex viewer ("hex:<path>" tabs, F-EDT-96), read-only: any file of the workspace as offsets, bytes and
// their characters, 16 a row (8 or 4 on a narrow view), only the rows in sight fetched (64 KiB ranges, kept while they
// are recent), so a file of gigabytes opens at once. A click or the arrow keys select a byte, shown in both columns and
// read by the data inspector (little- or big-endian); Go To takes an offset in hex or decimal (Ctrl+G). Reloaded when
// the file changes on disk.

import { h, replace } from "/ui-kit/dom.js";
import { fileUrl, fetchRange, watchFile } from "./fileurl.js";
import { downloadButton, formatSize, notice } from "./chrome.js";
import { byteText, bytesPerRow, charText, inspect, offsetDigits, offsetText, parseOffset, printable } from "./hexrows.js";

const CHUNK = 65536;
const KEPT_CHUNKS = 96;
const OVERSCAN = 8;
const MOST_SCROLL_PX = 8000000;   // taller spacers misbehave; past this the scroll position is scaled to the rows

export class HexViewer {
	constructor(ctx, path) {
		this.ctx = ctx;
		this.path = path;
		this.url = fileUrl(ctx.wsId, path);
		this.size = -1;
		this.chunks = new Map();
		this.pending = new Map();
		this.selected = 0;
		this.littleEndian = true;
		this.perRow = 16;
		this.frame = 0;
		this.generation = 0;
		this.info = h("span.ft-info");
		this.goTo = h("input.ft-goto.mono", { type: "text", placeholder: "Go to offset", title: "Go to an offset: 0x1F or 1Fh in hex, 31 in decimal (Ctrl+G)", "aria-label": "Go to offset", on: { keydown: (e) => this.goToKey(e) } });
		this.endianButton = h("button.btn.flat.ft-text", { title: "Read the inspector's values little- or big-endian", on: { click: () => this.toggleEndian() } }, "LE");
		this.window = h("div.ft-hex-rows.mono");
		this.spacer = h("div.ft-hex-spacer");
		this.scroller = h("div.ft-hex-scroll", { tabindex: "0", role: "grid", "aria-label": path, on: { scroll: () => this.queueDraw(), keydown: (e) => this.key(e), click: (e) => this.clicked(e) } }, this.spacer, this.window);
		this.inspector = h("div.ft-hex-inspector.mono");
		this.status = h("div.ft-status.mono");
		this.stage = h("div.ft-stage.ft-hex-stage", null, this.scroller, this.inspector);
		ctx.el.classList.add("ft-view");
		ctx.el.append(h("div.ft-tools", null, this.info, h("span.grow"), this.goTo, this.endianButton, downloadButton(ctx, path)), this.stage, this.status);
		this.resizer = new ResizeObserver(() => this.queueDraw());
		this.resizer.observe(this.scroller);
		this.stopWatch = watchFile(ctx.ied, ctx.wsId, path, (change) => (change === "deleted" ? this.problem("The file was deleted.") : this.reload()));
		this.reload();
	}

	async reload() {
		const generation = ++this.generation;
		this.chunks.clear();
		this.pending.clear();
		if (!this.scroller.isConnected) replace(this.stage, this.scroller, this.inspector);
		try {
			const first = await fetchRange(this.url, 0, CHUNK - 1);
			if (generation !== this.generation) return;
			this.size = first.size;
			if (first.bytes.length) this.chunks.set(0, first.bytes);
			this.selected = Math.min(this.selected, Math.max(0, this.size - 1));
			replace(this.info, h("span.ft-chip", null, formatSize(this.size)), h("span.ft-chip.ft-wide", null, this.size.toLocaleString("en-US") + " bytes"));
			this.digits = offsetDigits(this.size);
			this.queueDraw();
		}
		catch (e) {
			if (generation === this.generation) this.problem(e.message);
		}
	}

	// The byte at `offset`, or undefined while its chunk is on its way (asked for here).
	byteAt(offset) {
		const index = Math.floor(offset / CHUNK);
		const chunk = this.chunks.get(index);
		if (chunk) {
			this.chunks.delete(index);
			this.chunks.set(index, chunk);
			return chunk[offset - index * CHUNK];
		}
		this.fetchChunk(index);
		return undefined;
	}

	fetchChunk(index) {
		if (this.pending.has(index)) return;
		const generation = this.generation;
		const done = fetchRange(this.url, index * CHUNK, index * CHUNK + CHUNK - 1).then(({ bytes }) => {
			if (generation !== this.generation) return;
			this.chunks.set(index, bytes);
			while (this.chunks.size > KEPT_CHUNKS) this.chunks.delete(this.chunks.keys().next().value);
			this.queueDraw();
		}, (e) => {
			if (generation === this.generation) this.status.textContent = "Could not read the file at " + offsetText(index * CHUNK, this.digits) + ": " + e.message;
		}).finally(() => this.pending.delete(index));
		this.pending.set(index, done);
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Layout

	queueDraw() {
		if (this.frame) return;
		this.frame = requestAnimationFrame(() => {
			this.frame = 0;
			this.draw();
		});
	}

	metrics() {
		const style = getComputedStyle(this.scroller);
		const rowHeight = parseInt(style.getPropertyValue("--line-height-code"), 10) || 18;
		if (!this.charWidth) {
			const probe = h("span.mono", { style: { visibility: "hidden", position: "absolute" } }, "0".repeat(32));
			this.window.append(probe);
			this.charWidth = probe.getBoundingClientRect().width / 32 || 7;
			probe.remove();
		}
		return { rowHeight, columns: Math.floor((this.scroller.clientWidth - 16) / this.charWidth) };
	}

	draw() {
		if (this.size < 0) return;
		const { rowHeight, columns } = this.metrics();
		this.perRow = bytesPerRow(columns, this.digits);
		const rows = Math.max(1, Math.ceil(this.size / this.perRow));
		const height = this.scroller.clientHeight;
		const visible = Math.ceil(height / rowHeight) + 1;
		const total = rows * rowHeight;
		const scaled = total > MOST_SCROLL_PX;
		this.spacer.style.height = (scaled ? MOST_SCROLL_PX : total) + "px";
		const top = this.scroller.scrollTop;
		let first;
		let y;
		if (scaled) {
			const fraction = top / Math.max(1, MOST_SCROLL_PX - height);
			first = Math.min(Math.max(0, rows - visible + 1), Math.round(fraction * Math.max(0, rows - visible + 1)));
			y = top;
		}
		else {
			first = Math.max(0, Math.floor(top / rowHeight) - OVERSCAN);
			y = first * rowHeight;
		}
		const last = Math.min(rows, first + visible + (scaled ? 0 : 2 * OVERSCAN));
		this.layout = { rowHeight, rows, visible, scaled, first, height };
		this.window.style.transform = "translateY(" + y + "px)";
		const lines = [];
		for (let r = first; r < last; r++) lines.push(this.row(r));
		replace(this.window, lines);
		this.renderInspector();
	}

	row(r) {
		const start = r * this.perRow;
		const hexCells = [];
		const charCells = [];
		for (let i = 0; i < this.perRow; i++) {
			const offset = start + i;
			if (offset >= this.size) break;
			const b = this.byteAt(offset);
			const selected = offset === this.selected ? ".sel" : "";
			const known = b !== undefined;
			hexCells.push(h("span.ft-hb" + selected + (known && b === 0 ? ".ft-zero" : ""), { dataset: { offset: String(offset) } }, known ? byteText(b) : "??"));
			if (i === 7 && this.perRow > 8) hexCells.push(h("span.ft-gap"));
			charCells.push(h("span.ft-hc" + selected + (known && !printable(b) ? ".ft-np" : ""), { dataset: { offset: String(offset) } }, known ? charText(b) : " "));
		}
		return h("div.ft-hex-row", null, h("span.ft-ho", null, offsetText(start, this.digits)), h("span.ft-hx", null, hexCells), h("span.ft-ha", null, charCells));
	}

	renderInspector() {
		const bytes = [];
		for (let i = 0; i < 8 && this.selected + i < this.size; i++) {
			const b = this.byteAt(this.selected + i);
			if (b === undefined) break;
			bytes.push(b);
		}
		const values = inspect(Uint8Array.from(bytes), this.littleEndian);
		replace(this.inspector, h("div.ft-hex-title", null, "Offset " + offsetText(this.selected, this.digits) + " (" + this.selected + ")"),
			values.map(([label, value]) => h("div.ft-hex-value", null, h("span.muted", null, label), h("span", null, value))));
		this.status.textContent = this.size === 0 ? "The file is empty." : "Offset 0x" + offsetText(this.selected, this.digits) + "  (" + this.selected + ")   " + values[2][0] + " " + values[2][1] + "   " + values[1][0] + " " + values[1][1];
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Selection

	select(offset) {
		if (this.size <= 0) return;
		this.selected = Math.max(0, Math.min(this.size - 1, offset));
		this.reveal();
		this.queueDraw();
	}

	reveal() {
		if (!this.layout) return;
		const { rowHeight, rows, visible, scaled, first, height } = this.layout;
		const row = Math.floor(this.selected / this.perRow);
		if (scaled) {
			if (row >= first && row < first + visible - 1) return;
			const target = Math.min(rows - visible + 1, Math.max(0, row - Math.floor(visible / 2)));
			this.scroller.scrollTop = (target / Math.max(1, rows - visible + 1)) * (MOST_SCROLL_PX - height);
			return;
		}
		const top = row * rowHeight;
		if (top < this.scroller.scrollTop) this.scroller.scrollTop = top;
		else if (top + rowHeight > this.scroller.scrollTop + height) this.scroller.scrollTop = top + rowHeight - height;
	}

	clicked(event) {
		const cell = event.target.closest("[data-offset]");
		if (cell) this.select(Number(cell.dataset.offset));
		this.scroller.focus({ preventScroll: true });
	}

	key(event) {
		const page = Math.max(1, (this.layout ? this.layout.visible - 1 : 16)) * this.perRow;
		const rowStart = this.selected - (this.selected % this.perRow);
		const moves = {
			ArrowLeft: this.selected - 1, ArrowRight: this.selected + 1, ArrowUp: this.selected - this.perRow, ArrowDown: this.selected + this.perRow,
			PageUp: this.selected - page, PageDown: this.selected + page,
			Home: event.ctrlKey ? 0 : rowStart, End: event.ctrlKey ? this.size - 1 : rowStart + this.perRow - 1,
		};
		if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "g") {
			event.preventDefault();
			this.goTo.focus();
			this.goTo.select();
			return;
		}
		if (!(event.key in moves) || event.altKey) return;
		event.preventDefault();
		this.select(moves[event.key]);
	}

	goToKey(event) {
		if (event.key === "Escape") return this.scroller.focus();
		if (event.key !== "Enter") return undefined;
		const offset = parseOffset(this.goTo.value, this.size);
		this.goTo.classList.toggle("bad", offset === null);
		if (offset === null) return undefined;
		this.select(offset);
		return this.scroller.focus({ preventScroll: true });
	}

	toggleEndian() {
		this.littleEndian = !this.littleEndian;
		this.endianButton.textContent = this.littleEndian ? "LE" : "BE";
		this.renderInspector();
	}

	problem(message) {
		replace(this.stage, notice(message));
	}

	focus() {
		this.scroller.focus({ preventScroll: true });
	}

	visible(shown) {
		if (shown) this.queueDraw();
	}

	dispose() {
		this.generation++;
		cancelAnimationFrame(this.frame);
		this.resizer.disconnect();
		this.stopWatch();
	}
}
