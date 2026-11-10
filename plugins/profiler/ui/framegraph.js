// framegraph.js - the frame time graph above the timeline (F-PRF-11): one bar per frame (or the worst of each run of
// frames when there are more than pixels), the budget line at the target frame rate, frames over it in the warning
// color and over twice it in the error color, the timeline's range outlined. It zooms on its own: the wheel or a pinch
// zooms at the pointer, Shift+wheel, a drag with the right or middle button or with one finger pans, and its buttons
// zoom out, in and back to every frame. Zoomed in it asks the server for the frames it shows (prof.frames first..last)
// so each bar is one frame again, scrolls to the timeline's frames when the timeline moves out of its sight, and a
// live capture shown to its newest frame keeps the newest in view. A click zooms the timeline to a frame; a drag to a
// run of frames.

import { h, icon } from "/ui-kit/dom.js";
import { readTheme } from "./timelinedraw.js";
import { duration, count } from "./format.js";
import { FrameSpan, barsServe, barsFetch } from "./model.js";

const CLICK_SLOP_PX = 4;
const WHEEL_ZOOM = 0.0015;
const BUTTON_ZOOM = 2;
const STRIP_HEIGHT = 3;

export class FrameGraph {
	// `fetch(first, last, width)` resolves to a prof.frames answer (-1, -1: every frame); `onPick(from, to)` zooms the
	// timeline to a range of time.
	constructor(host, { fetch, onPick }) {
		this.host = host;
		this.fetch = fetch;
		this.onPick = onPick;
		this.graph = null;
		this.total = 0;
		this.span = null;
		this.view = null;
		this.budgetNs = 16666667;
		this.loading = null;
		this.stale = true;
		this.following = false;
		this.pointers = new Map();
		this.width = 0;
		this.canvas = h("canvas.prof-fg-canvas", { "aria-label": "Frame times" });
		this.tip = h("div.prof-tip", { hidden: true });
		this.range = h("span.prof-fg-range", { hidden: true });
		this.label = h("span.prof-fg-budget");
		this.allButton = h("button.ib", { title: "Show every frame", "aria-label": "Show every frame", on: { click: () => this.showAll() } }, icon("screen-full"));
		this.tools = h("span.prof-fg-tools", null,
			h("button.ib", { title: "Zoom out (wheel down)", "aria-label": "Zoom out", on: { click: () => this.zoomBy(BUTTON_ZOOM) } }, icon("zoom-out")),
			h("button.ib", { title: "Zoom in (wheel up)", "aria-label": "Zoom in", on: { click: () => this.zoomBy(1 / BUTTON_ZOOM) } }, icon("zoom-in")),
			this.allButton, this.label);
		host.append(this.canvas, this.tip, this.range, this.tools);
		this.bind();
		this.resizer = new ResizeObserver(() => this.ensure());
		this.resizer.observe(host);
	}

	dispose() {
		this.disposed = true;
		this.resizer.disconnect();
	}

	setBudget(budgetNs) {
		this.budgetNs = budgetNs;
		this.paint();
	}

	// The capture is loaded, grew or changed: the bars are asked for (again), the old ones drawn meanwhile.
	refresh() {
		this.active = true;
		this.stale = true;
		this.ensure();
	}

	setView(view) {
		this.view = view;
		this.following = true;
		this.follow();
		this.paint();
	}

	// The frames drawn: the zoomed span, else every frame.
	shown() {
		return this.span ? new FrameSpan(this.span.from, this.span.to) : new FrameSpan(0, Math.max(1, this.total));
	}

	get zoomed() {
		return !!this.span;
	}

	// --- Data -------------------------------------------------------------------------------------------------------

	ensure() {
		if (this.disposed) return;
		this.paint();
		if (this.loading || !this.active) return;
		// Hidden (no frames yet, or a phone's tool window over it), it still asks, at a width it may well have.
		const width = this.width > 0 ? this.width : 800;
		const span = this.shown();
		if (!this.stale && barsServe(this.graph, span, width)) return;
		this.stale = false;
		const ask = this.span ? barsFetch(span, this.total, width) : { first: -1, last: -1, width };
		this.loading = this.fetch(ask.first, ask.last, ask.width).then((graph) => {
			this.loading = null;
			if (this.disposed || !graph) return;
			this.answered(graph);
			this.ensure();
		}, () => {
			this.loading = null;
		});
	}

	answered(graph) {
		const before = this.total;
		const atEnd = this.span && this.span.to >= before - 0.5;
		this.graph = graph;
		this.total = graph.total || 0;
		if (this.span) {
			if (atEnd && this.total > before) this.span.panBy(this.total - before, this.total);
			else this.span.clamp(this.total);
			if (this.span.whole(this.total)) this.span = null;
		}
		if (this.following) this.follow();
	}

	// The frames the timeline shows, from the bars: {first, last}, or null when no bar fetched holds its range.
	outline() {
		if (!this.view || !this.graph) return null;
		let first = -1;
		let last = -1;
		for (const bar of this.graph.bars) {
			if (bar[3] <= this.view.from || bar[2] >= this.view.to) continue;
			if (first < 0) first = bar[4];
			last = bar[5];
		}
		return first < 0 ? null : { first, last };
	}

	// Zoomed in, the graph scrolls to the timeline's frames when they are out of its sight; until the owner moves the
	// graph themselves, each answer may scroll it closer (a frame the bars do not reach is estimated first).
	follow() {
		if (!this.span || !this.view || !this.graph || this.drag || this.pinch) return;
		const shown = this.outline();
		let target;
		if (shown) {
			if (shown.last + 1 > this.span.from && shown.first < this.span.to) {
				this.following = false;
				return;
			}
			target = (shown.first + shown.last + 1) / 2;
		}
		else target = this.estimate((this.view.from + this.view.to) / 2);
		if (target === null) return;
		this.span.panBy(target - (this.span.from + this.span.to) / 2, this.total);
		this.ensure();
	}

	// The frame at a time the fetched bars do not reach, from the nearest of them and the mean frame time.
	estimate(ns) {
		const bars = this.graph.bars;
		const mean = this.graph.meanNs;
		if (!bars.length || !mean) return null;
		const head = bars[0];
		const tail = bars[bars.length - 1];
		if (ns < head[2]) return Math.max(0, head[4] - (head[2] - ns) / mean);
		if (ns >= tail[3]) return Math.min(this.total, tail[5] + 1 + (ns - tail[3]) / mean);
		return null;
	}

	// The bar drawn at x: the one whose frames hold the frame there, else the nearest.
	barAt(x) {
		const bars = this.graph ? this.graph.bars : [];
		if (!bars.length) return null;
		const frame = Math.floor(this.shown().frame(x, this.width));
		let low = 0;
		let high = bars.length - 1;
		while (low < high) {
			const mid = (low + high) >> 1;
			if (bars[mid][5] < frame) low = mid + 1;
			else high = mid;
		}
		return bars[low];
	}

	// --- Zooming ----------------------------------------------------------------------------------------------------

	setSpan(span) {
		this.following = false;
		this.span = span.whole(this.total) ? null : span;
		this.hideTip();
		this.ensure();
	}

	// The buttons zoom about the timeline's frames when they are in sight, else about the middle.
	zoomBy(factor) {
		if (!this.total) return;
		const span = this.shown();
		const shown = this.outline();
		const inSight = shown && shown.last + 1 > span.from && shown.first < span.to;
		const anchor = inSight ? (Math.max(shown.first, span.from) + Math.min(shown.last + 1, span.to)) / 2 : (span.from + span.to) / 2;
		this.setSpan(span.zoomAt(anchor, factor, this.total));
	}

	showAll() {
		this.following = false;
		this.span = null;
		this.ensure();
	}

	// --- Input ------------------------------------------------------------------------------------------------------

	bind() {
		const c = this.canvas;
		c.style.touchAction = "none";
		c.addEventListener("wheel", (e) => this.wheel(e), { passive: false });
		c.addEventListener("pointerdown", (e) => this.down(e));
		c.addEventListener("pointermove", (e) => this.move(e));
		c.addEventListener("pointerup", (e) => this.up(e));
		c.addEventListener("pointercancel", (e) => this.cancel(e));
		c.addEventListener("pointerleave", () => this.hideTip());
		c.addEventListener("contextmenu", (e) => e.preventDefault());
	}

	point(e) {
		const rect = this.canvas.getBoundingClientRect();
		return { x: e.clientX - rect.left, y: e.clientY - rect.top };
	}

	wheel(e) {
		e.preventDefault();
		if (!this.total) return;
		const span = this.shown();
		if (e.shiftKey || Math.abs(e.deltaX) > Math.abs(e.deltaY)) {
			const delta = Math.abs(e.deltaX) > Math.abs(e.deltaY) ? e.deltaX : e.deltaY;
			this.setSpan(span.panBy(delta * span.count / this.width, this.total));
			return;
		}
		this.setSpan(span.zoomAt(span.frame(this.point(e).x, this.width), Math.exp(e.deltaY * WHEEL_ZOOM), this.total));
	}

	down(e) {
		const p = this.point(e);
		this.canvas.setPointerCapture(e.pointerId);
		this.pointers.set(e.pointerId, p);
		this.hideTip();
		if (this.pointers.size === 2) {
			const [a, b] = [...this.pointers.values()];
			this.pinch = { distance: Math.max(1, Math.hypot(a.x - b.x, a.y - b.y)), span: this.shown(), mid: (a.x + b.x) / 2 };
			this.drag = null;
			this.paint();
			return;
		}
		const pan = e.pointerType === "touch" || e.button === 1 || e.button === 2;
		this.drag = { start: p, at: p, pan, span: this.shown(), moved: false };
	}

	move(e) {
		const p = this.point(e);
		if (this.pointers.has(e.pointerId)) this.pointers.set(e.pointerId, p);
		if (this.pinch && this.pointers.size === 2) {
			const [a, b] = [...this.pointers.values()];
			const distance = Math.max(1, Math.hypot(a.x - b.x, a.y - b.y));
			const start = this.pinch.span;
			const span = new FrameSpan(start.from, start.to);
			this.setSpan(span.zoomAt(start.frame(this.pinch.mid, this.width), this.pinch.distance / distance, this.total));
			return;
		}
		if (!this.drag) {
			this.hover(p);
			return;
		}
		const dx = p.x - this.drag.start.x;
		if (!this.drag.moved && Math.abs(dx) < CLICK_SLOP_PX) return;
		this.drag.moved = true;
		this.drag.at = p;
		if (this.drag.pan) {
			const start = this.drag.span;
			this.setSpan(new FrameSpan(start.from, start.to).panBy(-dx * start.count / this.width, this.total));
			return;
		}
		this.paint();
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
		this.paint();
		if (drag.pan && (drag.moved || e.pointerType !== "touch")) return;
		const end = drag.moved ? this.point(e).x : drag.start.x;
		const a = this.barAt(Math.min(drag.start.x, end));
		const b = this.barAt(Math.max(drag.start.x, end));
		if (a && b) this.onPick(a[2], b[3]);
	}

	cancel(e) {
		this.pointers.delete(e.pointerId);
		this.pinch = null;
		this.drag = null;
		this.paint();
	}

	// The tip names the frame (or run of frames) under the pointer and its time.
	hover(p) {
		const bar = this.barAt(p.x);
		if (!bar) {
			this.hideTip();
			return;
		}
		const [worst, ns, , , first, last] = bar;
		this.tip.textContent = first === last
			? "Frame " + count(first) + " \u00b7 " + duration(ns)
			: "Frames " + count(first) + "\u2013" + count(last) + " \u00b7 worst " + duration(ns) + " (frame " + count(worst) + ")";
		this.tip.hidden = false;
		const x = Math.min(p.x + 14, this.width - this.tip.offsetWidth - 4);
		this.tip.style.left = Math.max(0, x) + "px";
		this.tip.style.top = (p.y + 16) + "px";
	}

	hideTip() {
		this.tip.hidden = true;
	}

	// --- Painting ---------------------------------------------------------------------------------------------------

	paint() {
		const rect = this.host.getBoundingClientRect();
		this.width = Math.max(0, Math.floor(rect.width));
		const width = Math.max(1, this.width);
		const height = Math.max(1, Math.floor(rect.height));
		const ratio = window.devicePixelRatio || 1;
		if (this.canvas.width !== width * ratio || this.canvas.height !== height * ratio) {
			this.canvas.width = width * ratio;
			this.canvas.height = height * ratio;
			this.canvas.style.width = width + "px";
			this.canvas.style.height = height + "px";
		}
		const ctx = this.canvas.getContext("2d");
		ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
		const theme = readTheme();
		ctx.fillStyle = theme.bg1;
		ctx.fillRect(0, 0, width, height);
		this.label.textContent = duration(this.budgetNs) + " \u00b7 " + Math.round(1e9 / this.budgetNs) + " FPS";
		const span = this.shown();
		this.range.hidden = !this.span;
		this.allButton.classList.toggle("on", !!this.span);
		if (this.span) this.range.textContent = "Frames " + count(Math.floor(span.from)) + "\u2013" + count(Math.max(Math.floor(span.from), Math.ceil(span.to) - 1)) + " of " + count(this.total);
		const bars = this.graph ? this.graph.bars : [];
		if (!bars.length) return;
		const floor = height - STRIP_HEIGHT - 1;
		const top = Math.max(this.budgetNs * 1.6, Math.min(this.graph.maxNs, this.budgetNs * 4));
		for (const bar of bars) {
			const ns = bar[1];
			const x0 = Math.round(span.x(bar[4], width));
			const x1 = Math.round(span.x(bar[5] + 1, width));
			if (x1 < 0 || x0 > width) continue;
			const gap = x1 - x0 > 3 ? 1 : 0;
			const barHeight = Math.max(1, Math.min(1, ns / top) * (floor - 4));
			ctx.fillStyle = ns > this.budgetNs * 2 ? theme.frameSpike : ns > this.budgetNs ? theme.frameOver : theme.bg4;
			ctx.fillRect(x0, floor - barHeight, Math.max(1, x1 - x0 - gap), barHeight);
		}
		const budgetY = Math.round(floor - this.budgetNs / top * (floor - 4));
		ctx.strokeStyle = theme.text2;
		ctx.setLineDash([3, 3]);
		ctx.beginPath();
		ctx.moveTo(0, budgetY + 0.5);
		ctx.lineTo(width, budgetY + 0.5);
		ctx.stroke();
		ctx.setLineDash([]);
		this.paintOutline(ctx, theme, span, width, floor);
		this.paintPick(ctx, theme, floor);
		if (this.span) this.paintStrip(ctx, theme, width, height);
	}

	// The timeline's frames outlined.
	paintOutline(ctx, theme, span, width, floor) {
		const shown = this.outline();
		if (!shown) return;
		const x0 = Math.round(span.x(shown.first, width));
		const x1 = Math.round(span.x(shown.last + 1, width));
		if (x1 < 0 || x0 > width) return;
		ctx.strokeStyle = theme.focus;
		ctx.lineWidth = 1;
		ctx.strokeRect(Math.max(0, x0) + 0.5, 1.5, Math.max(2, Math.min(width, x1) - Math.max(0, x0) - 1), floor - 2);
	}

	// The run of frames a drag is picking.
	paintPick(ctx, theme, floor) {
		const drag = this.drag;
		if (!drag || drag.pan || !drag.moved) return;
		const x0 = Math.min(drag.start.x, drag.at.x);
		const x1 = Math.max(drag.start.x, drag.at.x);
		ctx.fillStyle = theme.focus;
		ctx.globalAlpha = 0.18;
		ctx.fillRect(x0, 0, x1 - x0, floor);
		ctx.globalAlpha = 1;
	}

	// Zoomed in, where the frames shown are among all of them: a thin strip along the bottom.
	paintStrip(ctx, theme, width, height) {
		const total = Math.max(1, this.total);
		const y = height - STRIP_HEIGHT;
		ctx.fillStyle = theme.bg3;
		ctx.fillRect(0, y, width, STRIP_HEIGHT);
		const x0 = this.span.from / total * width;
		const x1 = this.span.to / total * width;
		ctx.fillStyle = theme.text3;
		ctx.fillRect(Math.floor(x0), y, Math.max(3, Math.ceil(x1 - x0)), STRIP_HEIGHT);
	}
}
