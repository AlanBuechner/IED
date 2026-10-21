// timeline.js - the timing timeline across the top of a capture (F-GPU-12): a ruler, the markers as nested bars, and
// one row per queue with its actions. Before timing data exists the axis is the events' order; after Collect Timing
// Data it is GPU time. A click selects the event under the pointer.

import { h } from "/ui-kit/dom.js";

const LABEL = 96;
const RULER = 15;
const MARKER_ROW = 16;
const QUEUE_ROW = 13;

function token(name) {
	return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

export class Timeline {
	constructor({ onSelect }) {
		this.onSelect = onSelect;
		this.canvas = h("canvas", { "aria-label": "Timing timeline" });
		this.el = h("div.gpu-timeline", null, this.canvas);
		this.events = null;
		this.timing = null;
		this.selected = 0;
		this.boxes = [];
		this.canvas.addEventListener("click", (e) => this.click(e));
		new ResizeObserver(() => this.draw()).observe(this.el);
	}

	load(grid, queues) {
		this.grid = grid;
		this.queues = queues || [];
		this.draw();
	}

	setTiming(timing) {
		this.timing = { events: new Map(timing.events.map((r) => [r[0], [r[2], r[3]]])), markers: new Map(timing.markers.map((r) => [r[0], [r[2], r[3]]])), frameMs: timing.frameMs };
		this.draw();
	}

	select(eid) {
		this.selected = eid;
		this.draw();
	}

	// [start, end] of an event on the axis (0..1).
	span(event) {
		const total = this.grid.events.length || 1;
		if (this.timing) {
			const frame = this.timing.frameMs || 1;
			const known = event.kind === "marker" ? this.timing.markers.get(event.eid) : this.timing.events.get(event.eid);
			if (known) return [known[0] / frame, known[1] / frame];
			return null;
		}
		const end = event.kind === "marker" ? (event.last || event.eid) : event.eid;
		return [(event.eid - 1) / total, end / total];
	}

	draw() {
		const rect = this.el.getBoundingClientRect();
		const ratio = window.devicePixelRatio || 1;
		this.canvas.width = Math.max(1, Math.round(rect.width * ratio));
		this.canvas.height = Math.max(1, Math.round(rect.height * ratio));
		const g = this.canvas.getContext("2d");
		g.setTransform(ratio, 0, 0, ratio, 0, 0);
		g.clearRect(0, 0, rect.width, rect.height);
		this.boxes = [];
		if (!this.grid || !this.grid.events.length) return;
		const width = rect.width - LABEL - 12;
		const x = (f) => LABEL + f * width;
		g.font = "10px " + token("--font-ui");
		g.textBaseline = "middle";
		// Ruler.
		g.fillStyle = token("--text-2");
		g.strokeStyle = token("--border-1");
		const ticks = 10;
		for (let i = 0; i <= ticks; i++) {
			const px = Math.round(x(i / ticks)) + 0.5;
			g.beginPath();
			g.moveTo(px, 0);
			g.lineTo(px, rect.height);
			g.globalAlpha = 0.35;
			g.stroke();
			g.globalAlpha = 1;
			const label = this.timing ? (this.timing.frameMs * i / ticks).toFixed(this.timing.frameMs < 1 ? 3 : 2) : String(Math.round(this.grid.events.length * i / ticks));
			g.fillText(label, px + 3, RULER / 2);
		}
		g.fillText(this.timing ? "ms" : "event", 8, RULER / 2);
		// Markers by depth.
		g.fillText("Markers", 8, RULER + MARKER_ROW / 2);
		const zones = [token("--prof-zone-0"), token("--prof-zone-1"), token("--prof-zone-2")];
		for (const event of this.grid.events) {
			if (event.kind !== "marker" || event.hidden || event.last <= event.eid) continue;
			const depth = this.grid.depthOf(event);
			if (depth > 2) continue;
			const s = this.span(event);
			if (!s) continue;
			this.bar(g, x(s[0]), RULER + depth * MARKER_ROW + 1, Math.max(2, x(s[1]) - x(s[0])), MARKER_ROW - 2, zones[depth], event, event.name);
		}
		// One row per queue with its actions.
		const top = RULER + 3 * MARKER_ROW + 4;
		this.queues.forEach((queue, i) => {
			const y = top + i * QUEUE_ROW;
			g.fillStyle = token("--text-2");
			g.fillText(queue.type === "DIRECT" ? "Direct" : queue.type === "COMPUTE" ? "Compute" : queue.type === "COPY" ? "Copy" : queue.name, 8, y + QUEUE_ROW / 2);
			for (const event of this.grid.events) {
				if (event.queue !== queue.id || event.hidden || event.kind === "marker") continue;
				const s = this.span(event);
				if (!s) continue;
				this.bar(g, x(s[0]), y + 1, Math.max(1, x(s[1]) - x(s[0])), QUEUE_ROW - 2, token("--prof-zone-3"), event, null);
			}
		});
		// The selected event's cursor.
		const selected = this.grid.byEid.get(this.selected);
		const s = selected && this.span(selected);
		if (s) {
			g.fillStyle = token("--focus");
			g.fillRect(Math.round(x(s[0])), 0, 1, rect.height);
		}
	}

	bar(g, x, y, w, hgt, fill, event, label) {
		g.fillStyle = event.eid === this.selected ? token("--selection-active") : fill;
		g.fillRect(x, y, w, hgt);
		if (event.eid === this.selected) {
			g.strokeStyle = token("--focus");
			g.strokeRect(x + 0.5, y + 0.5, w - 1, hgt - 1);
		}
		if (label && w > 24) {
			g.save();
			g.beginPath();
			g.rect(x + 3, y, w - 6, hgt);
			g.clip();
			g.fillStyle = token("--text-0");
			g.fillText(label, x + 3, y + hgt / 2);
			g.restore();
		}
		this.boxes.push({ x, y, w, h: hgt, eid: event.eid });
	}

	click(e) {
		const rect = this.canvas.getBoundingClientRect();
		const px = e.clientX - rect.left, py = e.clientY - rect.top;
		// The innermost (last drawn) box under the pointer.
		for (let i = this.boxes.length - 1; i >= 0; i--) {
			const b = this.boxes[i];
			if (px >= b.x && px <= b.x + Math.max(b.w, 3) && py >= b.y && py <= b.y + b.h) {
				this.onSelect(b.eid);
				return;
			}
		}
	}
}
