// findzone.js - Find Zone (F-PRF-21): every instance of one zone name in the scope as a histogram of durations (log
// bins when they span decades), its count, mean, median, p95 and extremes; a drag on the histogram picks a duration
// range whose instances are listed slowest first, and a click on one shows it in the timeline.

import { h, replace } from "/ui-kit/dom.js";
import { readTheme } from "./timelinedraw.js";
import { duration, count, instant } from "./format.js";
import { scopeLabel } from "./panels.js";

export class FindZonePanel {
	constructor({ request, onJump }) {
		this.request = request;
		this.onJump = onJump;
		this.name = "";
		this.nameField = h("input.input-field.prof-filter", { type: "search", placeholder: "Zone name", "aria-label": "Zone name", on: { change: () => this.find(this.nameField.value.trim()) } });
		this.summary = h("span.muted");
		this.canvas = h("canvas.prof-histo-canvas", { "aria-label": "Durations" });
		this.axis = h("div.histo-axis");
		this.list = h("div.prof-instances", null, h("div.prof-empty.muted", null, "Name a zone, or pick one in Statistics, the timeline's details or the flame graph."));
		this.el = h("div.prof-panel", null,
			h("div.panel-tools", null, h("span.muted", null, "Find Zone"), this.nameField, this.summary),
			h("div.prof-find", null, h("div.prof-histo", null, this.canvas, this.axis), h("div.scroll", null, this.list)));
		this.canvas.addEventListener("pointerdown", (e) => { this.dragFrom = e.offsetX; });
		this.canvas.addEventListener("pointerup", (e) => this.pick(e.offsetX));
	}

	update(captureId, scope) {
		this.captureId = captureId;
		this.scope = scope;
		if (this.name) this.load();
	}

	find(name) {
		this.name = name;
		this.nameField.value = name;
		this.low = undefined;
		this.high = undefined;
		this.load();
	}

	async load() {
		if (!this.captureId || !this.name) return;
		const asked = { captureId: this.captureId, name: this.name, bins: 64, limit: 200 };
		if (this.scope && this.scope.from !== undefined) Object.assign(asked, { from: Math.round(this.scope.from), to: Math.round(this.scope.to) });
		if (this.low !== undefined) Object.assign(asked, { low: Math.floor(this.low), high: Math.ceil(this.high) });
		const answer = await this.request("prof.findZone", asked);
		if (!answer) return;
		this.answer = answer;
		this.summary.textContent = count(answer.count) + " calls \u00b7 total " + duration(answer.totalNs) + " \u00b7 mean " + duration(answer.meanNs) +
			" \u00b7 median " + duration(answer.medianNs) + " \u00b7 p95 " + duration(answer.p95Ns) + " \u00b7 max " + duration(answer.maxNs) + " \u00b7 " + scopeLabel(this.scope).replace("Range: ", "");
		replace(this.axis, h("span", null, duration(answer.minNs)), h("span", null, "median " + duration(answer.medianNs)), h("span", null, "p95 " + duration(answer.p95Ns)), h("span", null, duration(answer.maxNs)));
		replace(this.list, answer.instances.map((i) => h("div.row", { on: { click: () => this.onJump(i.startNs, i.startNs + i.durationNs, i.ref) } },
			h("span.mono", null, duration(i.durationNs)), h("span.meta", null, "at " + instant(i.startNs, i.durationNs)), h("span.grow.ellip.meta", null, i.thread))));
		this.paint();
	}

	pick(x) {
		if (!this.answer || this.dragFrom === undefined) return;
		const bins = this.answer.bins;
		const at = (px) => bins[Math.min(bins.length - 1, Math.max(0, Math.floor(px / this.canvas.clientWidth * bins.length)))];
		const a = at(Math.min(this.dragFrom, x));
		const b = at(Math.max(this.dragFrom, x));
		this.dragFrom = undefined;
		this.low = a[0];
		this.high = b[1];
		this.load();
	}

	paint() {
		const width = Math.max(1, this.canvas.clientWidth);
		const height = Math.max(1, this.canvas.clientHeight);
		const ratio = window.devicePixelRatio || 1;
		this.canvas.width = width * ratio;
		this.canvas.height = height * ratio;
		const ctx = this.canvas.getContext("2d");
		ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
		const theme = readTheme();
		ctx.fillStyle = theme.bg1;
		ctx.fillRect(0, 0, width, height);
		const bins = this.answer.bins;
		const most = Math.max(1, ...bins.map((b) => b[2]));
		const step = width / bins.length;
		bins.forEach(([low, high, n], i) => {
			const chosen = this.low !== undefined && high > this.low && low < this.high;
			const median = this.answer.medianNs >= low && this.answer.medianNs < high;
			ctx.fillStyle = chosen || median ? theme.text1 : theme.bg4;
			const barHeight = n ? Math.max(1, n / most * (height - 2)) : 0;
			ctx.fillRect(i * step, height - barHeight, Math.max(1, step - 1), barHeight);
		});
	}
}
