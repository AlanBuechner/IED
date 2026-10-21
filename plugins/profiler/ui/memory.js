// memory.js - Memory (F-PRF-30, F-PRF-31, F-PRF-33): each pool's bytes in use over the capture with its peak, its
// allocations and frees, the collections and their pauses; and what is still allocated at the end (or at the
// timeline's selection end), grouped by call stack, pool or thread, largest first.

import { h, replace } from "/ui-kit/dom.js";
import { readTheme } from "./timelinedraw.js";
import { bytes, count, duration } from "./format.js";

export class MemoryPanel {
	constructor({ request }) {
		this.request = request;
		this.groupBy = "callstack";
		this.canvas = h("canvas.prof-memory-canvas", { "aria-label": "Memory in use" });
		this.pools = h("div.prof-pools");
		this.leaks = h("div.prof-leaks");
		this.leakHead = h("span.muted");
		this.groupPick = h("select.input-field.prof-select", { "aria-label": "Group live allocations by", on: { change: () => { this.groupBy = this.groupPick.value; this.loadLeaks(); } } },
			[["callstack", "By call stack"], ["pool", "By pool"], ["thread", "By thread"]].map(([v, t]) => h("option", { value: v }, t)));
		this.el = h("div.prof-panel", null,
			h("div.panel-tools", null, h("span.muted", null, "Memory"), h("span.grow"), this.groupPick),
			h("div.prof-memory", null,
				h("div.prof-memory-left", null, this.canvas, this.pools),
				h("div.prof-memory-right", null, h("div.group-head", null, "Live allocations", this.leakHead), h("div.scroll", null, this.leaks))));
	}

	update(captureId, scope) {
		this.captureId = captureId;
		this.scope = scope;
		this.load();
		this.loadLeaks();
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
			gc.count ? h("div.row", null, h("span.grow.c-warn", null, "Collections"), h("span.meta", null, count(gc.count) + "x"), h("span.meta", null, "paused " + duration(gc.pauseNs)),
				h("span.meta", null, "longest " + duration(gc.longestNs)), h("span.meta", null, "freed " + bytes(gc.freedBytes))) : null,
			!answer.pools.length && !gc.count ? h("div.muted.prof-empty", null, "This capture reports no allocations or collections.") : null);
		this.paint();
	}

	async loadLeaks() {
		if (!this.captureId) return;
		const asked = { captureId: this.captureId, groupBy: this.groupBy, limit: 200 };
		if (this.scope && this.scope.to !== undefined && this.scope.label === "selection") asked.at = Math.round(this.scope.to);
		const answer = await this.request("prof.leaks", asked);
		if (!answer) return;
		this.leakHead.textContent = " \u00b7 " + count(answer.count) + " (" + bytes(answer.bytes) + ") at " + duration(answer.atNs);
		replace(this.leaks, answer.groups.map((g) => h("details.prof-leak", null,
			h("summary", null, h("span.mono", null, bytes(g.bytes)), h("span.meta", null, count(g.count) + "x"), h("span", null, g.label)),
			g.callstack.length ? h("div.mono.prof-stack", null, g.callstack.map((frame) => h("div", null, frame))) : null,
			h("div.meta.mono", null, g.addresses.join(" ")))));
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
		const pools = this.answer ? this.answer.pools : [];
		const points = pools.flatMap((p) => p.usage);
		if (!points.length) return;
		const end = Math.max(...points.map((p) => p[0]), 1);
		const top = Math.max(...pools.map((p) => p.peak), 1);
		pools.forEach((pool, i) => {
			ctx.strokeStyle = theme.series[i % 8];
			ctx.lineWidth = 1.2;
			ctx.beginPath();
			pool.usage.forEach(([t, , high], k) => {
				const x = t / end * width;
				const y = height - 2 - high / top * (height - 6);
				if (k === 0) ctx.moveTo(x, y);
				else ctx.lineTo(x, y);
			});
			ctx.stroke();
		});
	}
}
