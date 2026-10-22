// diagtools.js - Diagnostic Tools (F-DBG-52, docs/25 "Diagnostic Tools"): what the debugged program uses over the
// session, as VS draws it beside the code while debugging: its memory in MB (private bytes filled, the working set a
// line) and its share of all processors in %. The server reads the program's process twice a second, once the debug
// adapter names it; every window shows the same samples.

import { h, replace } from "/ui-kit/dom.js";

const MB = 1024 * 1024;
const GRAPH_PAD_PX = 2;

// The top of a graph's scale: `value` (at least `least`) rounded up to 1, 2, 2.5, 5 or 7.5 times a power of ten.
export function niceTop(value, least) {
	const v = Math.max(value, least);
	const power = Math.pow(10, Math.floor(Math.log10(v)));
	return [1, 2, 2.5, 5, 7.5, 10].map((m) => m * power).find((top) => top >= v - 1e-9);
}

// Where samples' column `column` goes on a `width` x `height` graph: x by time over the session so far, y by the value
// over `top` (values above it on the top edge).
export function graphPoints(samples, column, top, width, height) {
	if (!samples.length) return [];
	const end = Math.max(samples[samples.length - 1][0], 1);
	return samples.map((s) => [s[0] / end * width, height - Math.min(1, s[column] / top) * height]);
}

// "1:05" for 65 000 ms, "1:02:03" past an hour.
export function sessionClock(ms) {
	const seconds = Math.floor(ms / 1000);
	const two = (n) => String(n).padStart(2, "0");
	const hours = Math.floor(seconds / 3600);
	const minutes = Math.floor(seconds / 60) % 60;
	return (hours ? hours + ":" + two(minutes) : String(minutes)) + ":" + two(seconds % 60);
}

function megabytes(bytes) {
	return (bytes / MB).toFixed(1) + " MB";
}

function emptyText(debug, usage) {
	if (!debug.session) return "Start debugging (F5) to see the program's memory and CPU use.";
	if (usage) return "";
	if (!debug.live()) return "The debug adapter never named the program's process, so there is nothing to show.";
	return "Waiting for the debug adapter to name the program's process.";
}

export class DiagnosticTools {
	constructor(ctx, debug) {
		this.ctx = ctx;
		this.debug = debug;
		this.usage = null;
		this.head = h("div.dbg-head.dt-head");
		this.memoryValue = h("span.dt-value");
		this.cpuValue = h("span.dt-value");
		this.memory = h("canvas.dt-graph", { "aria-label": "Process memory" });
		this.cpu = h("canvas.dt-graph", { "aria-label": "CPU use" });
		this.empty = h("div.el-empty");
		this.graphs = h("div.dt-graphs", null,
			h("section.dt-section", null, h("div.dt-title", null, h("span.grow", null, "Process Memory (MB)"), this.memoryValue), this.memory),
			h("section.dt-section", null, h("div.dt-title", null, h("span.grow", null, "CPU (% of all processors)"), this.cpuValue), this.cpu));
		ctx.el.append(this.head, h("div.ed-fill.dt", null, this.empty, this.graphs));
		debug.follow(ctx.wsId);
		this.stops = [debug.on(() => this.render()), debug.onUsage((usage) => this.used(usage))];
		this.resizer = new ResizeObserver(() => this.paint());
		this.resizer.observe(this.graphs);
	}

	used(usage) {
		this.usage = usage;
		this.render();
	}

	render() {
		const usage = this.usage;
		const samples = usage ? usage.samples : [];
		const last = samples[samples.length - 1];
		const text = emptyText(this.debug, usage);
		this.empty.textContent = text;
		this.empty.hidden = !text;
		this.graphs.hidden = !usage;
		replace(this.head, usage
			? h("span.ellip", null, "Diagnostics session: " + sessionClock(last ? last[0] : 0) + " \u00b7 " + usage.name + " (" + usage.processId + ")")
			: h("span", null, "Diagnostics session"));
		this.memoryValue.textContent = last ? megabytes(last[2]) + " private \u00b7 " + megabytes(last[3]) + " working set" : "";
		this.cpuValue.textContent = last ? Math.round(last[1]) + " %" : "";
		this.paint();
	}

	paint() {
		if (!this.usage || this.graphs.hidden) return;
		const samples = this.usage.samples;
		const memoryTop = niceTop(Math.max(...samples.map((s) => Math.max(s[2], s[3]))) / MB, 1) * MB;
		const cpuTop = niceTop(Math.max(...samples.map((s) => s[1])), 10);
		const theme = readTheme();
		this.draw(this.memory, samples, theme, memoryTop, [
			{ column: 2, color: theme.series[0], fill: true },
			{ column: 3, color: theme.series[1], fill: false },
		], Math.round(memoryTop / MB) + " MB");
		this.draw(this.cpu, samples, theme, cpuTop, [{ column: 1, color: theme.series[2], fill: true }], Math.round(cpuTop) + " %");
	}

	// One graph: its scale's top labelled, a baseline, and each series a line (filled under it when `fill`).
	draw(canvas, samples, theme, top, series, topLabel) {
		const width = Math.max(1, canvas.clientWidth);
		const height = Math.max(1, canvas.clientHeight);
		const ratio = window.devicePixelRatio || 1;
		canvas.width = width * ratio;
		canvas.height = height * ratio;
		const ctx = canvas.getContext("2d");
		ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
		ctx.fillStyle = theme.bg1;
		ctx.fillRect(0, 0, width, height);
		ctx.strokeStyle = theme.border0;
		ctx.lineWidth = 1;
		ctx.beginPath();
		ctx.moveTo(0, GRAPH_PAD_PX + 0.5);
		ctx.lineTo(width, GRAPH_PAD_PX + 0.5);
		ctx.moveTo(0, height - 0.5);
		ctx.lineTo(width, height - 0.5);
		ctx.stroke();
		const plotHeight = height - GRAPH_PAD_PX;
		for (const line of series) {
			const points = graphPoints(samples, line.column, top, width, plotHeight).map(([x, y]) => [x, y + GRAPH_PAD_PX]);
			if (!points.length) continue;
			ctx.beginPath();
			points.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
			if (line.fill) {
				ctx.save();
				ctx.lineTo(points[points.length - 1][0], height);
				ctx.lineTo(points[0][0], height);
				ctx.closePath();
				ctx.globalAlpha = 0.22;
				ctx.fillStyle = line.color;
				ctx.fill();
				ctx.restore();
				ctx.beginPath();
				points.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
			}
			ctx.strokeStyle = line.color;
			ctx.lineWidth = 1.2;
			ctx.stroke();
		}
		ctx.fillStyle = theme.text2;
		ctx.font = theme.fontXs + "px " + theme.fontUi;
		ctx.textBaseline = "top";
		ctx.fillText(topLabel, 4, GRAPH_PAD_PX + 2);
	}

	focus() {
		this.ctx.el.focus({ preventScroll: true });
	}

	dispose() {
		for (const stop of this.stops) stop();
		this.resizer.disconnect();
	}
}

function readTheme() {
	const style = getComputedStyle(document.documentElement);
	const token = (name) => style.getPropertyValue("--" + name).trim();
	return {
		bg1: token("bg-1"), border0: token("border-0"), text2: token("text-2"), fontUi: token("font-ui"),
		fontXs: parseFloat(token("font-size-xs")) || 11, series: [1, 2, 3].map((n) => token("series-" + n)),
	};
}
