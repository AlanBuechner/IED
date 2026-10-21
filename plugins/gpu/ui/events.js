// events.js - the events grid (F-GPU-10): markers as tree rows, actions as rows under them, the state calls between
// them when "All calls" is on; columns EID, name, GPU ms with an in-cell bar, primitives and pixel shader invocations
// (once timing data is collected). A filter narrows the rows: words match names, `kind:draw` an event kind,
// `>0.5ms` a time. Rows are drawn only where the viewport shows them, so a frame of 100k events scrolls freely.

import { h, icon, replace } from "/ui-kit/dom.js";
import { count, gpuMs } from "./format.js";

const KIND_ICONS = { marker: "symbol-namespace", draw: "triangle-up", dispatch: "symbol-event", copy: "files", clear: "clear-all", resolve: "combine", barrier: "debug-pause", state: "settings-gear", query: "watch", present: "screen-full", fence: "debug-step-over", other: "question" };

export class EventsGrid {
	constructor({ onSelect }) {
		this.onSelect = onSelect;
		this.events = [];          // {eid, parent, kind, hidden, last, queue, name, children}
		this.byEid = new Map();
		this.expanded = new Set();
		this.showAll = false;
		this.filter = "";
		this.timing = null;        // eid -> ms
		this.stats = null;         // eid -> [stat columns]
		this.selected = 0;
		this.rows = [];
		this.rowHeight = 21;
		this.viewport = h("div.viewport", { tabindex: "0", on: { scroll: () => this.paint(), keydown: (e) => this.key(e) } });
		this.content = h("div.rows");
		this.viewport.append(this.content);
		this.allButton = h("button.ib", { title: "Show every API call, not only markers and actions", "aria-label": "All calls", on: { click: () => this.setShowAll(!this.showAll) } }, icon("list-flat"));
		this.countLabel = h("span.muted");
		this.el = h("div.gpu-col.gpu-events.gpu-br", null,
			h("div.gpu-title", null, h("b", null, "Events"), this.countLabel, h("span.grow"), h("span.gpu-events-tools", null, this.allButton)),
			h("div.gpu-grid", null,
				h("div.gpu-head", null, h("span.n", null, "EID"), h("span", null, "Name"), h("span.n", null, "GPU ms"), h("span.n", null, "Prims"), h("span.n", null, "PS inv.")),
				this.viewport));
		new ResizeObserver(() => this.paint()).observe(this.viewport);
	}

	// From the replayer's `events` result.
	load(data) {
		const columns = data.columns;
		const at = (name) => columns.indexOf(name);
		const [cEid, cParent, cKind, cHidden, cLast, cQueue, cName] = ["eid", "parent", "kind", "hidden", "last", "queue", "name"].map(at);
		this.events = data.events.map((e) => ({ eid: e[cEid], parent: e[cParent], kind: e[cKind], hidden: !!e[cHidden], last: e[cLast], queue: e[cQueue], name: e[cName], children: [] }));
		this.byEid = new Map(this.events.map((e) => [e.eid, e]));
		this.roots = [];
		for (const event of this.events) {
			const parent = event.parent ? this.byEid.get(event.parent) : null;
			(parent ? parent.children : this.roots).push(event);
		}
		// Markers start open two levels deep, as PIX opens a frame.
		for (const event of this.events) if (event.kind === "marker" && this.depthOf(event) < 2) this.expanded.add(event.eid);
		const actions = this.events.filter((e) => !e.hidden && e.kind !== "marker").length;
		this.countLabel.textContent = this.events.length.toLocaleString("en-US") + " \u00b7 " + actions.toLocaleString("en-US") + " actions";
		this.rebuild();
	}

	depthOf(event) {
		let depth = 0;
		for (let p = event.parent; p; p = this.byEid.get(p).parent) depth++;
		return depth;
	}

	setTiming(timing) {
		this.timing = new Map();
		const cols = timing.columns;
		for (const row of timing.events) this.timing.set(row[cols.indexOf("eid")], row[cols.indexOf("ms")]);
		for (const row of timing.markers) this.timing.set(row[0], row[1]);
		this.stats = new Map();
		const statCols = timing.statColumns;
		for (const row of timing.stats) this.stats.set(row[0], { prims: row[statCols.indexOf("iaPrimitives")], ps: row[statCols.indexOf("psInvocations")], cs: row[statCols.indexOf("csInvocations")] });
		// Markers add up their actions' statistics.
		for (const event of [...this.events].reverse()) {
			if (event.kind !== "marker") continue;
			let prims = 0, ps = 0;
			for (const child of event.children) {
				const s = this.stats.get(child.eid);
				if (s) prims += s.prims, ps += s.ps;
			}
			if (prims || ps) this.stats.set(event.eid, { prims, ps, cs: 0 });
		}
		this.maxMs = Math.max(0.000001, ...[...this.timing.values()]);
		this.paint(true);
	}

	setShowAll(on) {
		this.showAll = on;
		this.allButton.classList.toggle("on", on);
		this.rebuild();
	}

	setFilter(text) {
		this.filter = text.trim();
		this.rebuild();
	}

	matches(event) {
		if (!this.filter) return true;
		for (const word of this.filter.toLowerCase().split(/\s+/)) {
			if (word.startsWith("kind:")) {
				if (event.kind !== word.slice(5)) return false;
			}
			else if (/^>[\d.]+ms$/.test(word)) {
				const ms = this.timing && this.timing.get(event.eid);
				if (!(ms > parseFloat(word.slice(1)))) return false;
			}
			else if (!event.name.toLowerCase().includes(word)) return false;
		}
		return true;
	}

	visible(event) {
		return this.showAll || !event.hidden;
	}

	rebuild() {
		this.rows = [];
		if (this.filter) {
			// A filter lists the matching events flat, in order.
			for (const event of this.events) if (this.visible(event) && this.matches(event)) this.rows.push({ event, depth: 0 });
		}
		else {
			const walk = (list, depth) => {
				for (const event of list) {
					if (!this.visible(event)) continue;
					this.rows.push({ event, depth });
					if (event.children.length && this.expanded.has(event.eid)) walk(event.children, depth + 1);
				}
			};
			walk(this.roots || [], 0);
		}
		this.paint(true);
	}

	paint(force) {
		const rowHeight = window.matchMedia("(max-width: 719px)").matches ? 36 : 21;
		this.rowHeight = rowHeight;
		this.content.style.height = this.rows.length * rowHeight + "px";
		const top = this.viewport.scrollTop;
		const first = Math.max(0, Math.floor(top / rowHeight) - 5);
		const last = Math.min(this.rows.length, Math.ceil((top + this.viewport.clientHeight) / rowHeight) + 5);
		if (!force && this.painted && this.painted[0] === first && this.painted[1] === last) return;
		this.painted = [first, last];
		const nodes = [];
		for (let i = first; i < last; i++) nodes.push(this.row(this.rows[i], i * rowHeight));
		replace(this.content, nodes);
	}

	row({ event, depth }, y) {
		const open = this.expanded.has(event.eid);
		const ms = this.timing ? this.timing.get(event.eid) : undefined;
		const stats = this.stats ? this.stats.get(event.eid) : undefined;
		const twisty = event.children.length && !this.filter
			? h("span.tw", { on: { click: (e) => { e.stopPropagation(); this.toggle(event); } } }, icon(open ? "chevron-down" : "chevron-right"))
			: h("span.tw");
		const isCall = event.kind !== "marker" && /\(/.test(event.name);
		const fn = isCall ? event.name.slice(0, event.name.indexOf("(")) : event.name;
		const args = isCall ? event.name.slice(event.name.indexOf("(")) : "";
		const name = event.kind === "marker"
			? [h("span.ellip", null, event.name)]
			: [h("span", null, fn), args ? h("span.api", null, args) : null];
		const classes = "div.gpu-r" + (event.kind === "marker" ? ".mk" : "") + (event.hidden ? ".call" : "") + (event.eid === this.selected ? ".sel" : "");
		return h(classes, {
			style: { top: y + "px", "--d": depth },
			dataset: { eid: event.eid },
			on: { click: () => this.select(event.eid), dblclick: () => this.toggle(event) },
		},
			h("span.n.d", null, String(event.eid)),
			h("span.nm", null, twisty, icon(KIND_ICONS[event.kind] || "circle-small"), name),
			h("span.n", null, ms === undefined ? "" : h("span.gpu-dur", null, h("i", { style: { width: Math.min(100, 100 * ms / this.maxMs) + "%" } }), h("b", null, gpuMs(ms)))),
			h("span.n", null, stats && stats.prims ? count(stats.prims) : ""),
			h("span.n", null, stats && stats.ps ? count(stats.ps) : ""));
	}

	toggle(event) {
		if (!event.children.length) return;
		if (this.expanded.has(event.eid)) this.expanded.delete(event.eid);
		else this.expanded.add(event.eid);
		this.rebuild();
	}

	// Selects `eid`, opening the markers it is inside, and scrolls it into view.
	select(eid, quiet) {
		const event = this.byEid.get(eid);
		if (!event) return;
		for (let p = event.parent; p; p = this.byEid.get(p).parent) this.expanded.add(p);
		if (event.hidden && !this.showAll) this.setShowAll(true);
		this.selected = eid;
		this.rebuild();
		const index = this.rows.findIndex((r) => r.event.eid === eid);
		if (index >= 0) {
			const y = index * this.rowHeight;
			if (y < this.viewport.scrollTop || y + this.rowHeight > this.viewport.scrollTop + this.viewport.clientHeight)
				this.viewport.scrollTop = Math.max(0, y - this.viewport.clientHeight / 3);
		}
		this.paint(true);
		if (!quiet) this.onSelect(eid);
	}

	key(e) {
		const index = this.rows.findIndex((r) => r.event.eid === this.selected);
		const go = (i) => {
			if (i >= 0 && i < this.rows.length) this.select(this.rows[i].event.eid);
			e.preventDefault();
		};
		if (e.key === "ArrowDown") go(index + 1);
		else if (e.key === "ArrowUp") go(Math.max(0, index - 1));
		else if (e.key === "PageDown") go(Math.min(this.rows.length - 1, index + Math.floor(this.viewport.clientHeight / this.rowHeight)));
		else if (e.key === "PageUp") go(Math.max(0, index - Math.floor(this.viewport.clientHeight / this.rowHeight)));
		else if (e.key === "Home") go(0);
		else if (e.key === "End") go(this.rows.length - 1);
		else if (e.key === "ArrowRight" || e.key === "ArrowLeft") {
			const event = this.byEid.get(this.selected);
			if (!event) return;
			const open = this.expanded.has(event.eid);
			if (e.key === "ArrowRight" && event.children.length && !open) this.toggle(event);
			else if (e.key === "ArrowLeft" && open) this.toggle(event);
			else if (e.key === "ArrowLeft" && event.parent) this.select(event.parent);
			e.preventDefault();
		}
	}

	// The action an event stands for: itself, or a marker's last action - what its range produced.
	actionAt(eid) {
		const start = this.byEid.get(eid);
		if (!start) return null;
		if (start.kind !== "marker") return start;
		for (let e = start.last || eid; e > eid; e--) {
			const event = this.byEid.get(e);
			if (event && !event.hidden && event.kind !== "marker") return event;
		}
		return null;
	}

	focus() {
		this.viewport.focus({ preventScroll: true });
	}
}
