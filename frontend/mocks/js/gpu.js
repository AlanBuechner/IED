// gpu.js - PIX-style timing timeline for the GPU debugger mocks:
// <div class="ptl" data-sel="Crate_03" data-cur="6.6" data-plots="busy,occupancy" data-range="3.69,4.60">.
// Nested marker rows (frame, passes, draws), one row per queue, then counter plots. Times are ms on the GPU
// from the start of frame 1842; the passes match the Events grid.
"use strict";

(() => {
	const SPAN = 11;
	const MARKERS = [
		[[0, 10.56, "Frame 1842"]],
		[[0.62, 1.54, "DepthPrepass"], [1.56, 4.60, "ShadowCascades", "slow"], [4.62, 5.00, "EntityID"], [5.02, 9.88, "Lit"], [9.90, 10.12, "Grid"], [10.14, 10.45, "GUI"], [10.47, 10.56, "Blit"]],
		[[1.56, 2.17, "Cascade 0"], [2.18, 2.88, "Cascade 1"], [2.89, 3.68, "Cascade 2"], [3.69, 4.60, "Cascade 3", "slow"],
			[5.02, 6.23, "Ground"], [6.23, 6.41, "Crate_01"], [6.41, 6.57, "Crate_02"], [6.57, 6.78, "Crate_03", "err"], [6.78, 6.92, "Barrel"],
			[6.92, 7.25, "Pillar_A"], [7.25, 9.29, "Foliage x412"], [9.29, 9.60, "Prop_far"], [9.60, 9.88, "Wall"]],
	];
	const QUEUES = [
		["Direct", [[0, 0.62, "waits on Copy fence", "bubble"], [0.62, 10.56, ""]]],
		["Compute", [[0.05, 0.55, "Culling (work graph)"], [4.05, 4.55, "Light clusters"]]],
		["Copy", [[0.00, 0.58, "Upload"], [6.10, 7.35, "Streaming upload"]]],
	];
	const PLOTS = {
		busy: ["GPU busy", (t) => (t < 0.62 ? 0.18 : t > 10.56 ? 0.05 : 0.9 + 0.06 * Math.sin(t * 7))],
		occupancy: ["Occupancy", (t) => (t >= 1.56 && t < 4.6 ? 0.28 + 0.05 * Math.sin(t * 11) : t >= 5.02 && t < 9.88 ? 0.62 + 0.12 * Math.sin(t * 5) : t < 0.62 ? 0.05 : 0.4)],
		bandwidth: ["VRAM bandwidth", (t) => (t < 0.58 ? 0.7 : t >= 6.1 && t < 7.35 ? 0.82 : t >= 1.56 && t < 4.6 ? 0.35 : 0.45 + 0.1 * Math.sin(t * 3))],
	};

	const pct = (t) => (t / SPAN * 100).toFixed(2) + "%";

	function bar(cls, s, e, label, title) {
		return `<div class="${cls}" style="left:${pct(s)};width:${pct(e - s)}" title="${title || label}">${label}</div>`;
	}

	function plot(name, fn) {
		const pts = [];
		for (let i = 0; i <= 220; i++) {
			const t = i / 220 * SPAN;
			pts.push(`${(i / 220 * 1000).toFixed(1)},${(26 - fn(t) * 24).toFixed(1)}`);
		}
		return `<div class="ptl-row plot"><span class="pl">${name}</span><div class="pt"><svg viewBox="0 0 1000 28" preserveAspectRatio="none"><polyline points="${pts.join(" ")}"/></svg></div></div>`;
	}

	function render(el) {
		const sel = el.dataset.sel || "";
		let html = '<div class="ptl-row ruler"><span class="pl">ms</span><div class="pt">';
		for (let t = 0; t <= SPAN; t++) html += `<div class="ptick" style="left:${pct(t)}">${t}</div>`;
		html += "</div></div>";
		MARKERS.forEach((row, depth) => {
			html += `<div class="ptl-row mk"><span class="pl">${depth === 0 ? "Markers" : ""}</span><div class="pt">`;
			for (const [s, e, label, kind] of row) html += bar(`pm d${depth}${kind ? " " + kind : ""}${label === sel ? " sel" : ""}`, s, e, label, `${label} ${(e - s).toFixed(2)} ms`);
			html += "</div></div>";
		});
		for (const [name, spans] of QUEUES) {
			html += `<div class="ptl-row q"><span class="pl">${name}</span><div class="pt">`;
			for (const [s, e, label, kind] of spans) html += bar(`pq${kind ? " " + kind : ""}`, s, e, label);
			html += "</div></div>";
		}
		for (const key of (el.dataset.plots || "").split(",").filter(Boolean)) html += plot(...PLOTS[key]);
		el.innerHTML = html;
		const tracks = el.querySelectorAll(".pt");
		const first = tracks[0].getBoundingClientRect();
		const base = el.getBoundingClientRect();
		const left = first.left - base.left;
		const width = first.width;
		if (el.dataset.range) {
			const [a, b] = el.dataset.range.split(",").map(Number);
			el.insertAdjacentHTML("beforeend", `<div class="prange" style="left:${left + a / SPAN * width}px;width:${(b - a) / SPAN * width}px"></div>`);
		}
		if (el.dataset.cur) el.insertAdjacentHTML("beforeend", `<div class="pcur" style="left:${left + Number(el.dataset.cur) / SPAN * width}px"></div>`);
	}

	(window.Mock = window.Mock || { hooks: [] }).hooks.push(() => {
		for (const el of document.querySelectorAll(".ptl")) render(el);
	});
})();
