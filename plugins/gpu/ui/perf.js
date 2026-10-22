// perf.js - the Performance view (F-GPU-50 to F-GPU-52, docs/22 "Performance"): the frame's passes, or its costliest
// actions, with their GPU time and counters from the replay - pipeline statistics and the samples each draw passed -
// beside the selected pass's counters and what limits it, found by experiments: the pass timed again with every draw's
// pixel shader trivial, its scissor one pixel, its color writes off. When the capture is compared with another, each
// pass's time in both.

import { h, icon, replace } from "/ui-kit/dom.js";
import { DOT, count, gpuMs } from "./format.js";

const COUNTERS = [
	["iaVertices", "Vertices"], ["iaPrimitives", "Primitives"], ["vsInvocations", "VS invocations"], ["cInvocations", "Clipper in"],
	["cPrimitives", "Clipper out"], ["psInvocations", "PS invocations"], ["samples", "Samples passed"], ["csInvocations", "CS invocations"],
];
const SHARES = [
	["vertices", "Vertices and primitives", "left when no pixel is drawn"],
	["pixelShading", "Pixel shading", "saved by a trivial pixel shader"],
	["rasterization", "Rasterization and depth", "between those two"],
	["colorOutput", "Color output", "saved with color writes off"],
];
const ADVICE = {
	vertices: "fewer or lighter vertices: LODs, culling what is off screen, a cheaper vertex shader.",
	pixelShading: "a cheaper pixel shader, or fewer pixels shaded: less overdraw, a depth prepass, a lower resolution.",
	rasterization: "many small triangles or overdraw: pixels rasterized and depth tested cost more than their shading.",
	colorOutput: "blending and render-target bandwidth: fewer or narrower targets, less blending.",
};
const VARIANTS = { captured: "As captured", trivialPixelShader: "Trivial pixel shader", onePixel: "One-pixel scissor", noColorWrites: "Color writes off" };
const ACTION_COLUMNS = [["ms", "GPU ms"], ["iaPrimitives", "Prims"], ["psInvocations", "PS inv."], ["samples", "Samples"], ["csInvocations", "CS inv."]];

function ratio(value, digits) {
	return Number.isFinite(value) ? value.toFixed(digits) : "-";
}

export class PerformanceView {
	// `doc` is the capture document (capture.js): its session, events, timing and selection.
	constructor(doc) {
		this.doc = doc;
		this.mode = "passes";
		this.sort = "ms";
		this.experiments = new Map();     // eid -> the experiments' answer
		this.compareTimes = null;         // eid here -> ms in the compared capture
		this.selected = 0;
	}

	// Edits or a new timing: what was measured before no longer holds.
	invalidate() {
		this.experiments = new Map();
		this.compareTimes = null;
	}

	// What the server kept measured (gpu.measurements).
	restore(kept) {
		this.experiments = new Map((kept.experiments || []).map((answer) => [answer.eid, answer]));
		this.setCompareTiming(kept.compareTiming);
	}

	// compareTiming's answer as each pass's time in the compared capture, or none.
	setCompareTiming(result) {
		this.compareTimes = result ? new Map() : null;
		if (result) for (const [a, , , , msB] of [...result.markers, ...result.events]) if (a) this.compareTimes.set(a, msB);
	}

	render() {
		const doc = this.doc;
		if (!doc.timing) {
			this.el = h("div.gpu-state-msg", null, h("p", null, "No timing data yet: the counters come with it."),
				h("button.btn", { on: { click: () => doc.collectTiming() } }, icon("watch"), "Collect Timing Data"));
			return this.el;
		}
		this.index();
		if (!this.selected || !this.byEid.has(this.selected)) {
			const first = (doc.events.roots || []).find((e) => e.kind === "marker" && !e.hidden);
			this.selected = first ? first.eid : 0;
		}
		this.grid = this.mode === "passes" ? this.passesGrid() : this.actionsGrid();
		this.detail = h("div.gpu-perf-detail");
		this.renderDetail();
		const compared = doc.session.compareWith;
		const tabs = h("span.gpu-perf-modes", null, [["passes", "Passes"], ["actions", "Actions"]].map(([id, label]) =>
			h("button.gpu-cmp-mode" + (this.mode === id ? ".on" : ""), { on: { click: () => this.setMode(id) } }, label)));
		const bar = h("div.gpu-title", null, tabs,
			h("span.muted.gpu-perf-sum", null, "GPU " + gpuMs(doc.timing.frameMs) + " ms " + DOT + " median of " + doc.timing.repeat + " replays"),
			doc.timingStale ? h("span.c-warn", { title: "Shaders were edited after the timing was collected" }, icon("warning", "sm"), "before the edits") : null,
			h("span.grow"),
			compared ? h("button.btn.flat", { title: "Replay both captures with timestamps: each pass's time in both", on: { click: () => this.timeBoth() } }, icon("diff-multiple"), this.compareTimes ? "B: " + compared : "Time with " + compared) : null,
			h("button.btn.flat", { title: "The grid as a CSV file", on: { click: () => this.exportCsv() } }, icon("save"), "CSV"));
		this.el = h("div.gpu-col.grow", null, bar, h("div.gpu-main.gpu-perf", null, this.grid, this.detail));
		return this.el;
	}

	rerender() {
		if (this.el && this.el.isConnected) this.el.replaceWith(this.render());
	}

	setMode(mode) {
		this.mode = mode;
		this.rerender();
	}

	// The timing answer by EID: each action's ms and counters, and the events by EID.
	index() {
		const timing = this.doc.timing;
		this.ms = new Map();
		const cols = timing.columns;
		for (const row of timing.events) this.ms.set(row[cols.indexOf("eid")], row[cols.indexOf("ms")]);
		for (const row of timing.markers) this.ms.set(row[0], row[1]);
		this.counters = new Map();
		for (const row of timing.stats) {
			const values = {};
			timing.statColumns.forEach((name, i) => { if (name !== "eid") values[name] = row[i]; });
			this.counters.set(row[0], values);
		}
		this.byEid = new Map(this.doc.events.events.map((e) => [e.eid, e]));
	}

	// The range an event spans: a marker its actions, an action itself.
	rangeOf(event) {
		const last = event.kind === "marker" ? event.last : event.eid;
		return this.doc.events.events.filter((e) => e.eid >= event.eid && e.eid <= last && e.kind !== "marker" && !e.hidden);
	}

	// The counters of a range, added up.
	sum(event) {
		const total = { draws: 0, dispatches: 0 };
		for (const [name] of COUNTERS) total[name] = 0;
		for (const action of this.rangeOf(event)) {
			if (action.kind === "draw") total.draws++;
			if (action.kind === "dispatch") total.dispatches++;
			const values = this.counters.get(action.eid);
			if (values) for (const [name] of COUNTERS) total[name] += values[name] || 0;
		}
		return total;
	}

	passesGrid() {
		const compared = !!this.compareTimes;
		const rows = [];
		const walk = (list, depth) => {
			for (const e of list) {
				if (e.kind !== "marker" || e.hidden) continue;
				const total = this.sum(e);
				const ms = this.ms.get(e.eid), other = compared ? this.compareTimes.get(e.eid) : undefined;
				const delta = ms !== undefined && other !== undefined && other !== null ? other - ms : null;
				rows.push(this.row(e, [
					h("span.nm", { style: { "--d": depth } }, e.name), h("span.n", null, gpuMs(ms)),
					compared ? h("span.n", null, other !== undefined && other !== null ? gpuMs(other) : "-") : null,
					compared ? h("span.n" + (delta > 0 ? ".gpu-cmp-slower" : ""), null, delta !== null ? (delta >= 0 ? "+" : "") + gpuMs(delta) : "") : null,
					h("span.n", null, String(total.draws)), h("span.n", null, count(total.iaPrimitives)), h("span.n", null, count(total.psInvocations)),
					h("span.n", null, count(total.samples)), h("span.n.d", null, String(e.eid))]));
				walk(e.children, depth + 1);
			}
		};
		walk(this.doc.events.roots || [], 0);
		return h("div.gpu-grid.gpu-pgrid" + (compared ? ".ab" : ""), null,
			h("div.gpu-head", null, h("span", null, "Pass"), h("span.n", null, compared ? "A ms" : "GPU ms"), compared ? h("span.n", null, "B ms") : null, compared ? h("span.n", null, "Delta") : null,
				h("span.n", null, "Draws"), h("span.n", null, "Prims"), h("span.n", null, "PS inv."), h("span.n", null, "Samples"), h("span.n", null, "EID")),
			h("div.viewport", null, rows.length ? rows : h("div.gpu-state-msg", null, "The frame has no markers: see its actions.")));
	}

	actionsGrid() {
		const value = (e, name) => (name === "ms" ? this.ms.get(e.eid) : (this.counters.get(e.eid) || {})[name]) || 0;
		const actions = this.doc.events.events.filter((e) => e.kind !== "marker" && !e.hidden && this.ms.has(e.eid));
		actions.sort((a, b) => value(b, this.sort) - value(a, this.sort));
		const rows = actions.slice(0, 300).map((e) => this.row(e, [
			h("span.n.d", null, String(e.eid)), h("span.nm", null, e.name),
			...ACTION_COLUMNS.map(([name]) => h("span.n", null, name === "ms" ? gpuMs(value(e, name)) : count(value(e, name))))]));
		return h("div.gpu-grid.gpu-agrid", null,
			h("div.gpu-head", null, h("span.n", null, "EID"), h("span", null, "Action"),
				...ACTION_COLUMNS.map(([name, label]) => h("span.n.sort" + (this.sort === name ? ".on" : ""), { title: "Sort by " + label, on: { click: () => { this.sort = name; this.rerender(); } } }, label))),
			h("div.viewport", null, rows));
	}

	row(event, cells) {
		return h("div.gpu-r" + (event.eid === this.selected ? ".sel" : ""), {
			dataset: { eid: String(event.eid) },
			title: "Double click: the event in the Pipeline view",
			on: { click: () => this.select(event.eid), dblclick: () => { this.doc.setView("pipeline"); this.doc.select(event.eid); } },
		}, ...cells.filter(Boolean));
	}

	select(eid) {
		this.selected = eid;
		for (const el of this.grid.querySelectorAll(".gpu-r")) el.classList.toggle("sel", el.dataset.eid === String(eid));
		this.renderDetail();
	}

	renderDetail() {
		const event = this.byEid.get(this.selected);
		if (!event) {
			replace(this.detail, h("div.gpu-state-msg", null, "Select a pass or an action."));
			return;
		}
		const total = this.sum(event);
		const ms = this.ms.get(event.eid);
		const rows = [h("div.gpu-srow.hd", null, h("span.k", null, "Counters"), h("span.v", null, total.draws + " draws " + DOT + " " + total.dispatches + " dispatches"))];
		for (const [name, label] of COUNTERS) rows.push(h("div.gpu-srow", { style: { "--d": 1 } }, h("span.k", null, label), h("span.v", null, count(total[name]))));
		const rejected = total.cInvocations ? (total.cInvocations - total.cPrimitives) / total.cInvocations : NaN;
		const derived = [
			["VS invocations per vertex", ratio(total.vsInvocations / total.iaVertices, 2), "under 1: the vertex cache reused shaded vertices"],
			["Primitives rejected", Number.isFinite(rejected) ? Math.round(rejected * 100) + "%" : "-", "clipped or culled before rasterization"],
			["Samples per primitive", ratio(total.samples / total.cPrimitives, 1), "small triangles shade whole quads for a few pixels"],
			["PS invocations per sample", ratio(total.psInvocations / total.samples, 2), "above 1: shaded pixels that did not land"],
			["GPU ms per million pixels shaded", ratio(ms / (total.psInvocations / 1e6), 4), ""],
		];
		rows.push(h("div.gpu-srow.hd", null, h("span.k", null, "Derived"), h("span.v")));
		for (const [label, value, hint] of derived) rows.push(h("div.gpu-srow", { style: { "--d": 1 }, title: hint }, h("span.k", null, label), h("span.v", null, value)));
		const title = h("div.gpu-title", null, h("b", null, event.name), h("span.muted", null, "EID " + event.eid + (event.kind === "marker" ? "-" + event.last : "") + " " + DOT + " " + gpuMs(ms) + " ms"));
		replace(this.detail, title, h("div.gpu-perf-counters", null, rows), this.limiter(event, total));
	}

	// What limits the range: the experiments' answer, or the button that runs them.
	limiter(event, total) {
		const head = h("div.gpu-srow.hd", null, h("span.k", null, "Limiter"), h("span.v", null, "by experiments"));
		if (!total.draws) return h("div.gpu-perf-limiter", null, head, h("div.gpu-state-msg", null, "The experiments change draws; this range has none."));
		const result = this.experiments.get(event.eid);
		if (!result) {
			this.runButton = h("button.btn", { title: "Replay the range as captured and with every draw changed (a trivial pixel shader, a one-pixel scissor, color writes off)", disabled: this.running === event.eid, on: { click: () => this.run(event.eid) } },
				icon("play"), this.running === event.eid ? "Running..." : "Run Experiments");
			return h("div.gpu-perf-limiter", null, head, h("div.gpu-perf-run", null, this.runButton,
				h("span.muted", null, "Times the range " + 4 * (this.doc.timingRepeat || 5) + " more times: no vendor counters needed.")));
		}
		const captured = result.variants[0].ms;
		const variants = result.variants.map((v) => h("div.gpu-srow", { style: { "--d": 1 }, title: v.problem || "" },
			h("span.k", null, VARIANTS[v.id] || v.id),
			h("span.v", null, v.ms === null ? "did not run" : gpuMs(v.ms) + " ms" + (v.id !== "captured" && captured ? " (" + Math.round(100 * (v.ms - captured) / captured) + "%, " + v.changed + " draws)" : ""))));
		if (result.tooShort) return h("div.gpu-perf-limiter", null, head, ...variants, h("div.gpu-state-msg", null, "Under 2 microseconds: what the experiments save is the timer's noise. Try the pass around it."));
		const bars = SHARES.map(([id, label, hint]) => {
			const share = result.shares[id];
			return h("div.gpu-share" + (result.limiter === id ? ".lim" : ""), { title: label + ": " + hint },
				h("span.k", null, label), h("span.bar", null, h("i", { style: { width: (share === null ? 0 : Math.round(share * 100)) + "%" } })),
				h("span.v", null, share === null ? "-" : Math.round(share * 100) + "%"));
		});
		const name = (SHARES.find(([id]) => id === result.limiter) || [])[1];
		return h("div.gpu-perf-limiter", null, head, h("div.gpu-shares", null, bars),
			h("div.gpu-perf-verdict", null, name ? h("b", null, "Limiter: " + name + ". ") : null, name ? ADVICE[result.limiter] : "No experiment changed the range's draws."),
			h("div.gpu-srow.hd", null, h("span.k", null, "Replays"), h("span.v", null, "median of " + result.repeat)), ...variants);
	}

	async run(eid) {
		this.running = eid;
		this.renderDetail();
		try {
			const { result } = await this.doc.session.call("experiments", { eid, repeat: this.doc.timingRepeat || 5 });
			this.experiments.set(eid, result);
		}
		catch (e) {
			this.doc.ied.report("The experiments failed", e);
		}
		finally {
			this.running = 0;
			if (this.selected === eid && this.detail && this.detail.isConnected) this.renderDetail();
		}
	}

	progress(p) {
		if (this.runButton && this.runButton.isConnected) this.runButton.lastChild.textContent = "Running " + Math.round(p.fraction * 100) + "%";
	}

	async timeBoth() {
		try {
			const { result } = await this.doc.session.call("compareTiming", { repeat: this.doc.timingRepeat || 5 });
			this.doc.compareTimed(result);
		}
		catch (e) {
			this.doc.ied.report("Timing both captures failed", e);
		}
	}

	exportCsv() {
		const cells = (el) => [...el.children].map((c) => '"' + c.textContent.trim().replace(/"/g, '""') + '"').join(",");
		const lines = [cells(this.grid.querySelector(".gpu-head")), ...[...this.grid.querySelectorAll(".viewport .gpu-r")].map(cells)];
		const link = h("a", { href: URL.createObjectURL(new Blob([lines.join("\n") + "\n"], { type: "text/csv" })), download: this.doc.captureId + "-" + this.mode + ".csv" });
		document.body.append(link);
		link.click();
		link.remove();
	}
}
