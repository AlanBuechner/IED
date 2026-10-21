// timeline.js - the timeline view (docs/16, mockup 06): asks the server for what a range shows at the canvas's width
// (prof.timeline merges sub-pixel zones), keeps the last answer drawn while a new one is on its way, and turns input
// into view changes. Wheel zooms at the pointer, a drag pans (time and lanes), Shift+drag selects a range, a click
// selects a zone (or folds a lane from its label, or zooms to a frame from the frame row), a double click zooms to a
// zone; two fingers pinch. Keys: arrows pan and zoom, Home shows everything, F fits the selection, Esc clears it.

import { h } from "/ui-kit/dom.js";
import { TimeView, answerServes, fetchRange, layoutLanes, laneAt, zoneAt } from "./model.js";
import { TimelinePainter, readTheme, HEADER_HEIGHT, RULER_HEIGHT } from "./timelinedraw.js";
import { duration } from "./format.js";

const PHONE = "(max-width: 719px)";
const CLICK_SLOP_PX = 4;
const WHEEL_ZOOM = 0.0015;
const KEY_PAN = 0.15;
const KEY_ZOOM = 1.4;

function rowHeightFor(phone) {
	return phone ? 20 : 18;
}

export class TimelineView {
	// `host` is the element to fill; `fetch(from, to, width)` resolves to a prof.timeline answer.
	constructor(host, { fetch, onSelect, onRange, onView, budgetNs }) {
		this.host = host;
		this.fetch = fetch;
		this.onSelect = onSelect || (() => {});
		this.onRange = onRange || (() => {});
		this.onView = onView || (() => {});
		this.budgetNs = budgetNs || 16666667;
		this.view = new TimeView(0, 1e9);
		this.durationNs = 0;
		this.live = false;
		this.follow = true;
		this.answer = null;
		this.stale = false;
		this.loading = null;
		this.scrollY = 0;
		this.selected = null;
		this.range = null;
		this.collapsed = new Set();
		this.painter = new TimelinePainter();
		this.canvas = h("canvas.prof-canvas", { tabindex: "0", "aria-label": "Timeline" });
		this.tip = h("div.prof-tip", { hidden: true });
		host.append(this.canvas, this.tip);
		this.pointers = new Map();
		this.bind();
		this.resizer = new ResizeObserver(() => this.resize());
		this.resizer.observe(host);
		this.resize();
	}

	dispose() {
		this.resizer.disconnect();
		this.disposed = true;
	}

	get phone() {
		return window.matchMedia(PHONE).matches;
	}

	get labelWidth() {
		return this.phone ? 104 : (this.width < 1100 ? 150 : 176);
	}

	options() {
		const rowHeight = rowHeightFor(this.phone);
		return { rowHeight, pad: 3, messageHeight: 26, plotHeight: 36, collapsed: this.collapsed, slop: this.phone ? 6 : 2 };
	}

	// --- Data -------------------------------------------------------------------------------------------------------

	// The capture's length and whether it is still recording; a live capture followed scrolls with its end.
	setExtent(durationNs, live) {
		this.durationNs = durationNs;
		this.live = live;
		if (live && this.follow) {
			const span = Math.min(this.view.span, Math.max(durationNs, 1));
			this.view.fit(Math.max(0, durationNs - span), Math.max(durationNs, span));
		}
		this.stale = true;
		this.ensure();
	}

	showAll() {
		this.view.fit(0, Math.max(this.durationNs, 1), 0.01);
		this.changed();
	}

	// Data changed on the server (a live capture grew, the lane mode switched): fetch again, drawing the old meanwhile.
	invalidate() {
		this.stale = true;
		this.ensure();
	}

	trackWidth() {
		return Math.max(1, this.width - this.labelWidth);
	}

	ensure() {
		if (this.disposed) return;
		this.schedulePaint();
		if (this.loading || (!this.stale && answerServes(this.answer, this.view, this.trackWidth()))) return;
		const range = fetchRange(this.view, Math.max(this.durationNs, this.view.to));
		const width = Math.round(this.trackWidth() * (range.to - range.from) / Math.max(1, this.view.span));
		this.stale = false;
		this.loading = this.fetch(range.from, range.to, Math.max(1, width)).then((answer) => {
			this.loading = null;
			if (this.disposed || !answer) return;
			this.answer = answer;
			this.durationNs = Math.max(this.durationNs, answer.durationNs);
			this.relayout();
			this.ensure();
		}, () => {
			this.loading = null;
		});
	}

	relayout() {
		this.layout = this.answer ? layoutLanes(this.answer, this.options()) : { lanes: [], height: 0 };
		if (this.selected) {
			const lane = this.layout.lanes.find((l) => l.id === this.selected.laneId);
			const ref = this.selected.zone[4];
			const zone = lane && lane.source.zones ? lane.source.zones.find((z) => z[4] === ref) : null;
			if (zone) this.selected = { laneId: lane.id, zone, lane };
		}
		this.clampScroll();
		this.schedulePaint();
	}

	changed() {
		this.onView(this.view);
		this.ensure();
	}

	// --- Painting ---------------------------------------------------------------------------------------------------

	resize() {
		const rect = this.host.getBoundingClientRect();
		this.width = Math.max(1, Math.floor(rect.width));
		this.height = Math.max(1, Math.floor(rect.height));
		const ratio = window.devicePixelRatio || 1;
		this.canvas.width = Math.floor(this.width * ratio);
		this.canvas.height = Math.floor(this.height * ratio);
		this.canvas.style.width = this.width + "px";
		this.canvas.style.height = this.height + "px";
		this.theme = readTheme();
		if (this.answer) this.relayout();
		this.ensure();
	}

	schedulePaint() {
		if (this.paintQueued) return;
		this.paintQueued = true;
		requestAnimationFrame(() => {
			this.paintQueued = false;
			this.paint();
		});
	}

	paint() {
		if (this.disposed || !this.theme) return;
		const ratio = window.devicePixelRatio || 1;
		const ctx = this.canvas.getContext("2d");
		ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
		this.painter.paint({
			ctx, width: this.width, height: this.height, labelWidth: this.labelWidth, view: this.view, answer: this.answer,
			layout: this.layout || { lanes: [], height: 0 }, scrollY: this.scrollY, theme: this.theme, options: this.options(),
			selected: this.selected, range: this.range, budgetNs: this.budgetNs, phone: this.phone,
		});
	}

	clampScroll() {
		const most = Math.max(0, (this.layout ? this.layout.height : 0) - (this.height - HEADER_HEIGHT));
		this.scrollY = Math.min(Math.max(0, this.scrollY), most);
	}

	// --- Geometry ---------------------------------------------------------------------------------------------------

	point(event) {
		const rect = this.canvas.getBoundingClientRect();
		return { x: event.clientX - rect.left, y: event.clientY - rect.top };
	}

	timeAt(x) {
		return this.view.ns(x - this.labelWidth, this.trackWidth());
	}

	hit(x, y) {
		if (!this.layout || y < HEADER_HEIGHT) return null;
		const ly = y - HEADER_HEIGHT + this.scrollY;
		const lane = laneAt(this.layout, ly);
		if (!lane) return null;
		if (x < this.labelWidth) return { lane, label: true };
		const zone = zoneAt(lane, this.view, this.trackWidth(), x - this.labelWidth, ly, this.options());
		return { lane, zone };
	}

	frameAt(x) {
		const frames = this.answer && this.answer.frames ? this.answer.frames.items || [] : [];
		const t = this.timeAt(x);
		return frames.find(([, start, end]) => t >= start && t < end) || null;
	}

	// --- Input ------------------------------------------------------------------------------------------------------

	bind() {
		const c = this.canvas;
		c.addEventListener("wheel", (e) => this.wheel(e), { passive: false });
		c.addEventListener("pointerdown", (e) => this.down(e));
		c.addEventListener("pointermove", (e) => this.move(e));
		c.addEventListener("pointerup", (e) => this.up(e));
		c.addEventListener("pointercancel", (e) => this.pointers.delete(e.pointerId));
		c.addEventListener("pointerleave", () => this.hideTip());
		c.addEventListener("dblclick", (e) => this.doubleClick(e));
		c.addEventListener("keydown", (e) => this.key(e));
		c.style.touchAction = "none";
	}

	interacted() {
		this.follow = false;
	}

	wheel(e) {
		e.preventDefault();
		const p = this.point(e);
		if (p.x < this.labelWidth || e.shiftKey) {
			this.scrollY += e.deltaY;
			this.clampScroll();
			this.schedulePaint();
			return;
		}
		this.interacted();
		if (Math.abs(e.deltaX) > Math.abs(e.deltaY)) this.view.panBy(e.deltaX * this.view.span / this.trackWidth());
		else this.view.zoomAt(this.timeAt(p.x), Math.exp(e.deltaY * WHEEL_ZOOM), Math.max(this.durationNs * 1.2, 1));
		this.changed();
	}

	down(e) {
		this.canvas.focus({ preventScroll: true });
		const p = this.point(e);
		this.pointers.set(e.pointerId, p);
		this.canvas.setPointerCapture(e.pointerId);
		if (this.pointers.size === 2) {
			const [a, b] = [...this.pointers.values()];
			this.pinch = { distance: Math.hypot(a.x - b.x, a.y - b.y), view: new TimeView(this.view.from, this.view.to), mid: (a.x + b.x) / 2 };
			this.drag = null;
			return;
		}
		const selecting = e.shiftKey && p.x >= this.labelWidth;
		this.drag = { start: p, from: this.view.from, scrollY: this.scrollY, moved: false, selecting };
		if (selecting) this.range = { from: this.timeAt(p.x), to: this.timeAt(p.x) };
	}

	move(e) {
		const p = this.point(e);
		if (this.pointers.has(e.pointerId)) this.pointers.set(e.pointerId, p);
		if (this.pinch && this.pointers.size === 2) {
			const [a, b] = [...this.pointers.values()];
			const distance = Math.max(1, Math.hypot(a.x - b.x, a.y - b.y));
			const anchor = this.pinch.view.ns(this.pinch.mid - this.labelWidth, this.trackWidth());
			this.view = new TimeView(this.pinch.view.from, this.pinch.view.to).zoomAt(anchor, this.pinch.distance / distance, Math.max(this.durationNs * 1.2, 1));
			this.interacted();
			this.changed();
			return;
		}
		if (!this.drag) {
			this.hover(p);
			return;
		}
		const dx = p.x - this.drag.start.x;
		const dy = p.y - this.drag.start.y;
		if (!this.drag.moved && Math.hypot(dx, dy) < CLICK_SLOP_PX) return;
		this.drag.moved = true;
		this.hideTip();
		if (this.drag.selecting) {
			this.range.to = this.timeAt(p.x);
			this.schedulePaint();
			return;
		}
		this.interacted();
		const span = this.view.span;
		this.view.from = this.drag.from - dx * span / this.trackWidth();
		this.view.to = this.view.from + span;
		this.scrollY = this.drag.scrollY - dy;
		this.clampScroll();
		this.changed();
	}

	up(e) {
		this.pointers.delete(e.pointerId);
		if (this.pinch) {
			if (this.pointers.size < 2) this.pinch = null;
			this.drag = null;
			return;
		}
		const drag = this.drag;
		this.drag = null;
		if (!drag) return;
		if (drag.selecting) {
			if (drag.moved && Math.abs(this.range.to - this.range.from) > 0) {
				this.onRange(this.normalizedRange());
			}
			else this.range = null;
			this.schedulePaint();
			return;
		}
		if (!drag.moved) this.click(this.point(e));
	}

	normalizedRange() {
		return this.range ? { from: Math.min(this.range.from, this.range.to), to: Math.max(this.range.from, this.range.to) } : null;
	}

	click(p) {
		if (p.y >= RULER_HEIGHT && p.y < HEADER_HEIGHT && p.x >= this.labelWidth) {
			const frame = this.frameAt(p.x);
			if (frame) this.zoomTo(frame[1], frame[2]);
			return;
		}
		const hit = this.hit(p.x, p.y);
		if (hit && hit.label) {
			if (this.collapsed.has(hit.lane.id)) this.collapsed.delete(hit.lane.id);
			else this.collapsed.add(hit.lane.id);
			this.relayout();
			return;
		}
		this.select(hit && hit.zone ? { laneId: hit.lane.id, zone: hit.zone, lane: hit.lane } : null);
	}

	select(selected) {
		this.selected = selected;
		this.schedulePaint();
		this.onSelect(selected);
	}

	doubleClick(e) {
		const p = this.point(e);
		const hit = this.hit(p.x, p.y);
		if (hit && hit.zone) this.zoomTo(hit.zone[1], hit.zone[2]);
	}

	zoomTo(from, to) {
		this.interacted();
		this.view.fit(from, to, 0.05);
		this.changed();
	}

	key(e) {
		const span = this.view.span;
		const center = (this.view.from + this.view.to) / 2;
		const handled = {
			ArrowLeft: () => this.view.panBy(-span * KEY_PAN),
			ArrowRight: () => this.view.panBy(span * KEY_PAN),
			ArrowUp: () => this.view.zoomAt(center, 1 / KEY_ZOOM, Math.max(this.durationNs * 1.2, 1)),
			ArrowDown: () => this.view.zoomAt(center, KEY_ZOOM, Math.max(this.durationNs * 1.2, 1)),
			Home: () => this.view.fit(0, Math.max(this.durationNs, 1), 0.01),
			f: () => this.fitSelection(),
			Escape: () => {
				this.range = null;
				this.select(null);
			},
		}[e.key];
		if (!handled) return;
		e.preventDefault();
		this.interacted();
		handled();
		this.changed();
	}

	fitSelection() {
		const range = this.normalizedRange();
		if (range) this.view.fit(range.from, range.to, 0.02);
		else if (this.selected) this.view.fit(this.selected.zone[1], this.selected.zone[2], 0.05);
	}

	hover(p) {
		const hit = this.hit(p.x, p.y);
		if (!hit || !hit.zone) {
			this.hideTip();
			return;
		}
		const zone = hit.zone;
		const merged = zone[3] < 0;
		const name = merged ? zone[4] + " zones" : (this.answer.names || [])[zone[3]] || "?";
		this.tip.textContent = name + " \u00b7 " + duration(zone[2] - zone[1]) + (hit.lane.kind === "task" ? "" : " \u00b7 " + hit.lane.label);
		this.tip.hidden = false;
		const x = Math.min(p.x + 14, this.width - this.tip.offsetWidth - 4);
		this.tip.style.left = Math.max(0, x) + "px";
		this.tip.style.top = (p.y + 16) + "px";
	}

	hideTip() {
		this.tip.hidden = true;
	}
}
