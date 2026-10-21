// framegraph.js - the frame time graph above the timeline (F-PRF-11): one bar per frame (or the worst of each run of
// frames when there are more than pixels), the budget line at the target frame rate, frames over it in the warning
// color and over twice it in the error color, the timeline's range outlined. A click zooms the timeline to a frame; a
// drag to a run of frames.

import { h } from "/ui-kit/dom.js";
import { readTheme } from "./timelinedraw.js";
import { duration } from "./format.js";

export class FrameGraph {
	constructor(host, { onPick }) {
		this.host = host;
		this.onPick = onPick;
		this.graph = null;
		this.view = null;
		this.budgetNs = 16666667;
		this.canvas = h("canvas.prof-fg-canvas", { "aria-label": "Frame times" });
		this.label = h("span.prof-fg-budget");
		host.append(this.canvas, this.label);
		this.canvas.addEventListener("pointerdown", (e) => this.down(e));
		this.canvas.addEventListener("pointerup", (e) => this.up(e));
		this.resizer = new ResizeObserver(() => this.paint());
		this.resizer.observe(host);
	}

	dispose() {
		this.resizer.disconnect();
	}

	set(graph, budgetNs) {
		this.graph = graph;
		this.budgetNs = budgetNs;
		this.paint();
	}

	setView(view) {
		this.view = view;
		this.paint();
	}

	barAt(x) {
		const bars = this.graph ? this.graph.bars : [];
		if (!bars.length) return null;
		const i = Math.min(bars.length - 1, Math.max(0, Math.floor(x / this.width * bars.length)));
		return bars[i];
	}

	down(e) {
		this.start = e.offsetX;
	}

	up(e) {
		if (this.start === undefined) return;
		const a = this.barAt(Math.min(this.start, e.offsetX));
		const b = this.barAt(Math.max(this.start, e.offsetX));
		this.start = undefined;
		if (a && b) this.onPick(a[2], b[3]);
	}

	paint() {
		const rect = this.host.getBoundingClientRect();
		this.width = Math.max(1, Math.floor(rect.width));
		const height = Math.max(1, Math.floor(rect.height));
		const ratio = window.devicePixelRatio || 1;
		this.canvas.width = this.width * ratio;
		this.canvas.height = height * ratio;
		this.canvas.style.width = this.width + "px";
		this.canvas.style.height = height + "px";
		const ctx = this.canvas.getContext("2d");
		ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
		const theme = readTheme();
		ctx.fillStyle = theme.bg1;
		ctx.fillRect(0, 0, this.width, height);
		const bars = this.graph ? this.graph.bars : [];
		this.label.textContent = duration(this.budgetNs) + " \u00b7 " + Math.round(1e9 / this.budgetNs) + " FPS";
		if (!bars.length) return;
		const top = Math.max(this.budgetNs * 1.6, Math.min(this.graph.maxNs, this.budgetNs * 4));
		const step = this.width / bars.length;
		const gap = step > 3 ? 1 : 0;
		bars.forEach(([, ns], i) => {
			const barHeight = Math.max(1, Math.min(1, ns / top) * (height - 4));
			ctx.fillStyle = ns > this.budgetNs * 2 ? theme.frameSpike : ns > this.budgetNs ? theme.frameOver : theme.bg4;
			ctx.fillRect(i * step, height - barHeight, Math.max(1, step - gap), barHeight);
		});
		const budgetY = height - this.budgetNs / top * (height - 4);
		ctx.strokeStyle = theme.text2;
		ctx.setLineDash([3, 3]);
		ctx.beginPath();
		ctx.moveTo(0, budgetY + 0.5);
		ctx.lineTo(this.width, budgetY + 0.5);
		ctx.stroke();
		ctx.setLineDash([]);
		if (!this.view) return;
		const first = bars.findIndex((bar) => bar[3] > this.view.from);
		let last = bars.length - 1;
		while (last > 0 && bars[last][2] > this.view.to) last--;
		if (first < 0 || last < first) return;
		ctx.strokeStyle = theme.focus;
		ctx.strokeRect(first * step + 0.5, 1.5, Math.max(2, (last - first + 1) * step - 1), height - 3);
	}
}
