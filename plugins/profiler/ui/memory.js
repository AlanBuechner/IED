// memory.js - Memory (F-PRF-30, F-PRF-31, F-PRF-33): each pool's and memory plot's bytes over the capture, each in its
// own lane with its range, the pools' allocations and frees, the collections and their pauses. On the right, what is
// still allocated at the end (or at the timeline's selection end), grouped by call stack, pool or thread; for a program
// that counts its allocations instead of sending each one (a collected program), what was allocated in the capture or
// the selection by zone, zone path or thread - or, from its collector's samples, by function or call stack - and its
// live objects by type from its censuses.

import { h, replace } from "/ui-kit/dom.js";
import { readTheme } from "./timelinedraw.js";
import { bytes, count, duration, instant } from "./format.js";

const LANE_HEIGHT = 40;

// The groupings the right side offers, by what it lists: allocations still live, counted ones (and the collector's
// samples beside them), or allocation samples.
const GROUPINGS = {
	live: [["callstack", "By call stack"], ["pool", "By pool"], ["thread", "By thread"]],
	zones: [["zone", "By zone"], ["path", "By zone path"], ["thread", "By thread"]],
	sampled: [["function", "By function (sampled)"], ["callstack", "By call stack (sampled)"]],
	samples: [["function", "By function"], ["callstack", "By call stack"], ["thread", "By thread"]],
	none: [],
	loading: [],
};
const FROM_SAMPLES = new Set(["function", "callstack"]);

function samples(n) {
	return count(n) + (n === 1 ? " sample" : " samples");
}

const TITLES = { live: "Live allocations", zones: "Allocated", samples: "Allocation samples", none: "Allocated", loading: "" };

export class MemoryPanel {
	constructor({ request, openSource }) {
		this.request = request;
		this.openSource = openSource;
		this.canvas = h("canvas.prof-memory-canvas", { "aria-label": "Memory in use" });
		this.pools = h("div.prof-pools");
		this.list = h("div.prof-leaks");
		this.census = h("div.prof-census");
		this.listTitle = h("span", null, TITLES.live);
		this.listHead = h("span.meta");
		this.groupPick = h("select.input-field.prof-select", { "aria-label": "Group allocations by", on: { change: () => { this.groupBy = this.groupPick.value; this.loadList(); } } });
		this.setMode("loading");
		this.el = h("div.prof-panel", null,
			h("div.panel-tools", null, h("span.muted", null, "Memory"), h("span.grow"), this.groupPick),
			h("div.prof-memory", null,
				h("div.prof-memory-left", null, this.canvas, this.pools),
				h("div.prof-memory-right", null, h("div.group-head", null, this.listTitle, this.listHead), h("div.scroll", null, this.list, this.census))));
	}

	update(captureId, scope) {
		this.captureId = captureId;
		this.scope = scope;
		this.load();
	}

	// `sampled`: a counted capture also has the collector's allocation samples to group by function or call stack.
	setMode(mode, sampled) {
		const groupings = GROUPINGS[mode].concat(mode === "zones" && sampled ? GROUPINGS.sampled : []);
		this.mode = mode;
		this.groupBy = groupings.length ? groupings[0][0] : "";
		this.listTitle.textContent = TITLES[mode];
		replace(this.groupPick, groupings.map(([v, t]) => h("option", { value: v }, t)));
		this.groupPick.hidden = !groupings.length;
	}

	async load() {
		if (!this.captureId) return;
		const answer = await this.request("prof.memory", { captureId: this.captureId, width: Math.max(100, this.canvas.clientWidth || 600) });
		if (!answer) return;
		this.answer = answer;
		const gc = answer.gc;
		replace(this.pools,
			answer.pools.map((p) => h("div.row", null, h("span.grow", null, p.name), h("span.meta", null, "peak " + bytes(p.peak)), h("span.meta", null, "now " + bytes(p.inUse)),
				h("span.meta", { title: p.earlierFrees ? count(p.earlierFrees) + " frees of blocks allocated before the capture began" : null }, count(p.allocs) + " allocs, " + count(p.frees) + " frees" + (p.earlierFrees ? " (+" + count(p.earlierFrees) + " earlier)" : "")))),
			answer.plots.map((p) => h("div.row", null, h("span.grow", null, p.name), h("span.meta", null, "peak " + bytes(p.peak)), h("span.meta", null, "now " + bytes(p.now)))),
			gc.count ? h("div.row", null, h("span.grow.c-warn", null, "Collections"), h("span.meta", null, count(gc.count) + "x"), h("span.meta", null, "paused " + duration(gc.pauseNs)),
				h("span.meta", null, "longest " + duration(gc.longestNs)), gc.freedBytes >= 0 ? h("span.meta", null, "freed " + bytes(gc.freedBytes)) : null) : null,
			!answer.pools.length && !answer.plots.length && !gc.count ? h("div.muted.prof-empty", null, "This capture reports no allocations or collections.") : null);
		this.canvas.style.height = Math.max(2, this.lanes().length) * LANE_HEIGHT + "px";
		this.paint();
		await this.loadList();
	}

	// The right side: a capture that sent its allocations lists those still live; one that counted them or sampled them,
	// what was allocated, with its censuses.
	async loadList() {
		if (!this.captureId || !this.answer) return;
		if (this.answer.pools.length) {
			if (this.mode !== "live") this.setMode("live");
			replace(this.census);
			return this.loadLive();
		}
		await this.loadAllocated();
		await this.loadCensus();
	}

	selection() {
		return this.scope && this.scope.label === "selection" && this.scope.from !== undefined ? this.scope : null;
	}

	async loadLive() {
		const asked = { captureId: this.captureId, groupBy: this.groupBy, limit: 200 };
		const picked = this.selection();
		if (picked) asked.at = Math.round(picked.to);
		const answer = await this.request("prof.leaks", asked);
		if (!answer) return;
		this.listHead.textContent = count(answer.count) + " (" + bytes(answer.bytes) + ") at " + instant(answer.atNs, 0);
		replace(this.list, answer.groups.map((g) => h("details.prof-leak", null,
			h("summary", null, h("span.mono", null, bytes(g.bytes)), h("span.meta", null, count(g.count) + "x"), h("span", null, g.label)),
			g.callstack.length ? h("div.mono.prof-stack", null, g.callstack.map((frame) => h("div", null, frame))) : null,
			h("div.meta.mono", null, g.addresses.join(" ")))));
	}

	async loadAllocated() {
		const asked = { captureId: this.captureId, order: "bytes", limit: 200 };
		if (this.mode === "zones") Object.assign(asked, { groupBy: this.groupBy, source: FROM_SAMPLES.has(this.groupBy) ? "samples" : "zones" });
		else if (this.mode === "samples") asked.groupBy = this.groupBy;
		const picked = this.selection();
		if (picked) Object.assign(asked, { from: Math.round(picked.from), to: Math.round(picked.to) });
		const answer = await this.request("prof.allocations", asked);
		if (!answer) return;
		const mode = answer.source === "zones" || answer.source === "samples" ? answer.source : "none";
		if (this.mode !== "zones" && mode !== this.mode) this.setMode(mode, answer.samples > 0);
		if (mode === "none") {
			this.listHead.textContent = "";
			replace(this.list, h("div.muted.prof-empty", null, this.mode === "zones" ? "No allocation was sampled here." : "This capture reports no allocations."));
			return;
		}
		const range = picked ? " in the selection" : "";
		this.listHead.textContent = mode === "samples"
			? samples(answer.samples) + ", " + bytes(answer.weight) + range
			: count(answer.count) + " (" + bytes(answer.bytes) + ")" + range + (answer.frames ? ", " + count(answer.frames.medianCount) + " a frame" : "")
				+ (answer.inZoneNativeBytes ? ", " + bytes(answer.inZoneNativeBytes) + " native" : "");
		replace(this.list, answer.groups.map((g) => this.allocatedRow(g, mode)));
	}

	allocatedRow(g, mode) {
		const size = mode === "samples" ? g.weight : g.bytes;
		const times = mode === "samples" ? samples(g.samples) : count(g.count) + "x" + (g.perFrame ? ", " + g.perFrame + " a frame" : "");
		const source = g.file ? g.file.split(/[\\/]/).pop() + (g.line ? ":" + g.line : "") : "";
		const open = (e) => {
			e.preventDefault();
			e.stopPropagation();
			this.openSource(g);
		};
		return h("details.prof-leak", null,
			h("summary", null, h("span.mono", null, bytes(size)), h("span.meta", null, times),
				g.nativeBytes ? h("span.meta", { title: "Native memory its zones took: containers' buffers and raw blocks" }, "+" + bytes(g.nativeBytes) + " native") : null,
				h("span.ellip.grow", null, g.label),
				source && this.openSource ? h("a.mono.prof-link", { href: "#", title: g.file, on: { click: open } }, source) : null),
			g.callstack && g.callstack.length ? h("div.mono.prof-stack", null, g.callstack.map((frame) => h("div", null, frame))) : null);
	}

	// Live objects by type from the program's censuses, those that grew most first; a type that keeps accumulating is
	// marked.
	async loadCensus() {
		const asked = { captureId: this.captureId, limit: 50 };
		const picked = this.selection();
		if (picked) Object.assign(asked, { from: Math.round(picked.from), to: Math.round(picked.to) });
		const answer = await this.request("prof.census", asked);
		if (!answer || !answer.censuses) {
			replace(this.census);
			return;
		}
		replace(this.census,
			h("div.group-head", null, "Live objects by type", h("span.meta", null, count(answer.censuses) + " censuses")),
			answer.types.map((t) => h("div.row", { title: count(t.firstObjects) + " (" + bytes(t.firstBytes) + ") at the first census" },
				h("span.mono", null, bytes(t.lastBytes)), h("span.meta", null, count(t.lastObjects) + "x"), h("span.ellip.grow", null, t.name),
				t.accumulating ? h("span.c-warn", null, "keeps accumulating") : h("span.meta", null, (t.lastBytes >= t.firstBytes ? "+" : "-") + bytes(Math.abs(t.lastBytes - t.firstBytes))))));
	}

	lanes() {
		if (!this.answer) return [];
		return [...this.answer.pools, ...this.answer.plots].filter((s) => s.usage.length).map((s) => ({ name: s.name, usage: s.usage }));
	}

	// Each series in its own lane, scaled to its own lowest and highest, so a small heap's sawtooth shows beside a
	// large process's climb.
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
		const lanes = this.lanes();
		if (!lanes.length) return;
		const end = Math.max(1, ...lanes.flatMap((s) => s.usage.map((p) => p[0])));
		const lane = height / lanes.length;
		ctx.font = theme.fontXs + "px " + theme.fontUi;
		lanes.forEach((series, i) => {
			const top = i * lane;
			const low = Math.min(...series.usage.map((p) => p[1]));
			const high = Math.max(...series.usage.map((p) => p[2]));
			const span = Math.max(1, high - low);
			if (i > 0) {
				ctx.fillStyle = theme.border0;
				ctx.fillRect(0, Math.round(top), width, 1);
			}
			ctx.strokeStyle = theme.series[i % 8];
			ctx.lineWidth = 1.2;
			ctx.beginPath();
			series.usage.forEach(([t, , value], k) => {
				const x = t / end * width;
				const y = top + lane - 3 - (value - low) / span * (lane - 16);
				if (k === 0) ctx.moveTo(x, y);
				else ctx.lineTo(x, y);
			});
			ctx.stroke();
			ctx.fillStyle = theme.text2;
			ctx.fillText(series.name + "  " + bytes(low) + " - " + bytes(high), 4, top + 11);
		});
	}
}
