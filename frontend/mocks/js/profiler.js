// profiler.js - timeline for the Profiler mock (screen 6). One capture model, two layouts:
// ?view=threads (OS threads; renamed segments tinted, name drawn at the boundary) and
// ?view=tasks (lanes rebuilt by task name; a strip under each slice shows which OS thread ran it).
// Times are ms from the view origin; the capture clock at the origin is ORIGIN_S seconds.
"use strict";

(() => {
	const ORIGIN_S = 74.230;
	const FRAMES = [[0, 16.4, 1841], [16.4, 23.8, 1842], [40.2, 16.1, 1843], [56.3, 16.6, 1844]];
	const GC_PAUSE = 5.1;
	const GC = { s: 16.4 + 4.0, e: 16.4 + 4.0 + GC_PAUSE };
	const THREADS = [
		{ id: "main", name: "Main Thread", series: 8 },
		{ id: "render", name: "Render Thread", series: 7 },
		{ id: "w0", name: "Job Worker 0", series: 1, short: "W0" },
		{ id: "w1", name: "Job Worker 1", series: 2, short: "W1" },
		{ id: "w2", name: "Job Worker 2", series: 3, short: "W2" },
		{ id: "w3", name: "Job Worker 3", series: 5, short: "W3" },
	];
	const SELECTED = { lane: "w1", task: "Physics", s: 16.4 + 9.1, name: "Physics::Step" };

	function zone(list, s, e, d, n, extra) {
		list.push(Object.assign({ s, e, d, n }, extra));
	}

	function gcShift(i) {
		return i === 1 ? GC_PAUSE : 0;
	}

	function mainZones() {
		const zs = [];
		FRAMES.forEach(([F, L], i) => {
			const g = gcShift(i);
			zone(zs, F + 0.05, F + L - 0.1, 0, "Frame");
			zone(zs, F + 0.1, F + 0.5, 1, "Input.Poll");
			zone(zs, F + 0.6, F + 7.5 + g, 1, "Update");
			zone(zs, F + 0.8, F + 3.9, 2, "Scene.Update");
			zone(zs, F + 0.9, F + 2.4, 3, "Transform.Propagate");
			zone(zs, F + 2.5, F + 3.7, 3, "Animation.Sample");
			if (g) zone(zs, F + 4.0, F + 4.0 + g, 2, "GC::Collect gen 1", { gc: true });
			zone(zs, F + 4.0 + g, F + 5.6 + g, 2, "Physics.Wait");
			zone(zs, F + 5.7 + g, F + 7.3 + g, 2, "Scripts.Tick");
			zone(zs, F + 7.8 + g, F + 11 + g, 1, "Render.Prepare");
			zone(zs, F + 7.9 + g, F + 9.2 + g, 2, "Culling");
			zone(zs, F + 9.3 + g, F + 10.8 + g, 2, "BuildDrawLists");
			zone(zs, F + 11.2 + g, F + L - 0.4, 1, "WaitForGPU");
		});
		return zs;
	}

	function renderStart(i) {
		return FRAMES[i][0] + 11.1 + gcShift(i);
	}

	function renderZones() {
		const zs = [];
		FRAMES.forEach((_, i) => {
			const R = renderStart(i);
			zone(zs, R, R + 9.6, 0, "RenderFrame");
			zone(zs, R + 0.1, R + 0.7, 1, "Renderer::BeginFrame");
			zone(zs, R + 0.8, R + 2.6, 1, "DrawGUI");
			zone(zs, R + 2.7, R + 7.9, 1, "Submit");
			zone(zs, R + 2.8, R + 4.1, 2, "Shadow.Record");
			zone(zs, R + 4.2, R + 6.2, 2, "GBuffer.Record");
			zone(zs, R + 6.3, R + 7.8, 2, "Lighting.Record");
			zone(zs, R + 8.0, R + 9.5, 1, "Present");
		});
		return zs;
	}

	// job-system tasks: [task, worker, start, end]
	const TASKS = [
		["AI.Tick", "w1", 0.9, 3.4], ["Audio.Mix", "w3", 1.0, 2.2], ["Physics", "w0", 4.0, 5.5], ["Decompress", "w2", 6.0, 13.5],
		["Audio.Mix", "w3", 9.0, 10.2], ["Streaming", "w3", 11.0, 14.6],
		["AI.Tick", "w0", 17.3, 19.8], ["Audio.Mix", "w3", 17.4, 18.6], ["Physics", "w1", 25.5, 27.0], ["Streaming", "w2", 26.4, 32.4],
		["Decompress", "w0", 27.4, 35.4], ["Audio.Mix", "w3", 30.4, 31.6],
		["AI.Tick", "w2", 41.1, 43.6], ["Audio.Mix", "w3", 41.2, 42.4], ["Physics", "w0", 44.2, 45.7], ["Decompress", "w1", 46.2, 52.2],
		["Audio.Mix", "w3", 49.2, 50.4], ["Streaming", "w2", 51.0, 55.6], ["AI.Tick", "w1", 57.2, 59.7],
	];

	function taskZones(task, s, e) {
		const zs = [];
		const len = e - s;
		if (task === "Physics") {
			zone(zs, s, e, 0, "Physics::Step");
			zone(zs, s + 0.04, s + len * 0.24, 1, "Broadphase");
			zone(zs, s + len * 0.26, s + len * 0.6, 1, "Narrowphase");
			zone(zs, s + len * 0.62, e - 0.04, 1, "Solve");
		} else if (task === "AI.Tick") {
			zone(zs, s, e, 0, "AI::Tick");
			zone(zs, s + 0.05, s + len * 0.45, 1, "AI::Perception");
			zone(zs, s + len * 0.47, e - 0.05, 1, "AI::Plan");
		} else if (task === "Decompress") {
			zone(zs, s, e, 0, "Zstd::Decompress");
			zone(zs, s + 0.1, s + len * 0.7, 1, "arena_albedo.dds");
		} else if (task === "Streaming") {
			zone(zs, s, e, 0, "Streaming::Load");
			zone(zs, s + 0.1, s + len * 0.55, 1, "FS::Read");
			zone(zs, s + len * 0.58, e - 0.1, 1, "Mesh::Upload");
		} else {
			zone(zs, s, e, 0, "Audio::Mix");
		}
		for (const z of zs) {
			z.task = task;
			if (task === SELECTED.task && Math.abs(s - SELECTED.s) < 0.01 && z.n === SELECTED.name) z.sel = true;
		}
		return zs;
	}

	function gpuZones() {
		const zs = [];
		FRAMES.forEach((_, i) => {
			let t = renderStart(i) + 4.2;
			for (const [n, d] of [["Shadow pass", 1.8], ["GBuffer", 3.1], ["Lighting", 2.4], ["Post", 1.2], ["UI", 0.4]]) {
				zone(zs, t, t + d, 0, n, { gpu: true });
				t += d + 0.08;
			}
		});
		return zs;
	}

	function copyZones() {
		const zs = [];
		for (const [s, e, n] of [[13.6, 15.4, "Upload arena_albedo"], [33.0, 34.6, "Upload mesh batch 12"], [36.0, 36.9, "Upload cbuffers"], [53.0, 54.5, "Upload arena_normal"]]) zone(zs, s, e, 0, n, { gpu: true });
		return zs;
	}

	// ---------- lane models ----------
	function threadLanes() {
		const lanes = [
			{ id: "main", label: "Main Thread", thread: THREADS[0], zones: mainZones() },
			{ id: "render", label: "Render Thread", thread: THREADS[1], zones: renderZones() },
		];
		for (const t of THREADS.slice(2)) {
			const mine = TASKS.filter((x) => x[1] === t.id);
			lanes.push({
				id: t.id,
				label: t.name,
				thread: t,
				renames: mine.map(([task, , s, e]) => ({ s, e, name: task })),
				zones: mine.flatMap(([task, , s, e]) => taskZones(task, s, e)),
				note: `renamed ${mine.length * 2}x`,
			});
		}
		return lanes;
	}

	function taskLanes() {
		const lanes = [
			{ id: "main", label: "Main", thread: THREADS[0], zones: mainZones(), sub: "Main Thread" },
			{ id: "render", label: "Render", thread: THREADS[1], zones: renderZones(), sub: "Render Thread" },
		];
		for (const task of ["Physics", "AI.Tick", "Decompress", "Streaming", "Audio.Mix"]) {
			const mine = TASKS.filter((x) => x[0] === task);
			const ran = [...new Set(mine.map((x) => x[1]))].length;
			lanes.push({
				id: task,
				label: task,
				task: true,
				slices: mine.map(([, w, s, e]) => ({ s, e, thread: THREADS.find((t) => t.id === w) })),
				zones: mine.flatMap(([, , s, e]) => taskZones(task, s, e)),
				sub: `${mine.length} runs on ${ran} thread${ran > 1 ? "s" : ""}`,
			});
		}
		return lanes;
	}

	function gpuLanes() {
		return [
			{ id: "gpu", label: "GPU: Direct queue", gpu: true, zones: gpuZones() },
			{ id: "copy", label: "GPU: Copy queue", gpu: true, zones: copyZones() },
		];
	}

	// ---------- rendering ----------
	const esc = (s) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;");

	function scale(view, width) {
		const k = width / (view[1] - view[0]);
		return (t) => (t - view[0]) * k;
	}

	function zoneHtml(z, x, zoneH, band) {
		const left = x(z.s);
		const w = Math.max(1, x(z.e) - left);
		const cls = `pz d${Math.min(z.d, 3)}${z.sel ? " sel" : ""}${z.gc ? " gc" : ""}${z.gpu ? " gpu" : ""}`;
		const dur = z.e - z.s;
		const label = w > 34 ? `${esc(z.n)}${w > 110 ? ` <span class="t">${dur.toFixed(2)} ms</span>` : ""}` : "";
		return `<div class="${cls}" style="left:${left.toFixed(1)}px;width:${w.toFixed(1)}px;top:${z.d * zoneH + 2 + band}px">${label}</div>`;
	}

	function laneHtml(lane, x, zoneH, view) {
		const depth = Math.max(1, ...lane.zones.map((z) => z.d + 1));
		const strip = lane.task ? 5 : 0;
		const band = lane.renames ? 13 : 0;
		const h = Math.max(depth * zoneH + 4 + strip + band, lane.note || lane.sub ? 38 : 22);
		const visible = lane.zones.filter((z) => z.e > view[0] && z.s < view[1]);
		let back = "";
		(lane.renames || []).forEach((r, i) => {
			if (r.e < view[0] || r.s > view[1]) return;
			back += `<div class="rn ${i % 2 ? "b" : "a"}" style="left:${x(r.s).toFixed(1)}px;width:${(x(r.e) - x(r.s)).toFixed(1)}px"></div>`;
			back += `<div class="rn-tag" style="left:${x(r.s).toFixed(1)}px">${esc(r.name)}</div>`;
		});
		let strips = "";
		for (const sl of lane.slices || []) {
			if (sl.e < view[0] || sl.s > view[1]) continue;
			const w = x(sl.e) - x(sl.s);
			strips += `<div class="ts s${sl.thread.series}" style="left:${x(sl.s).toFixed(1)}px;width:${w.toFixed(1)}px">${w > 26 ? sl.thread.short : ""}</div>`;
		}
		const chip = lane.thread ? `<span class="sw s${lane.thread.series}"></span>` : lane.gpu ? '<i class="ci ci-circuit-board"></i>' : '<i class="ci ci-symbol-event"></i>';
		const sub = lane.note || lane.sub ? `<span class="sub">${esc(lane.note || lane.sub)}</span>` : "";
		return `<div class="lane${lane.gpu ? " gpu" : ""}" data-lane="${lane.id}" style="height:${h}px"><div class="lane-label"><i class="ci ci-chevron-down tw"></i>${chip}<span class="nm">${esc(lane.label)}</span>${sub}</div><div class="track">${back}${visible.map((z) => zoneHtml(z, x, zoneH, band)).join("")}${strips}</div></div>`;
	}

	function rulerHtml(view, x) {
		let html = "";
		const first = Math.ceil(view[0] / 5) * 5;
		for (let t = first; t <= view[1]; t += 5) {
			html += `<div class="tick" style="left:${x(t).toFixed(1)}px">${(ORIGIN_S + t / 1000).toFixed(3)} s</div>`;
		}
		return html;
	}

	function framesHtml(view, x) {
		return FRAMES.filter(([s, l]) => s + l > view[0] && s < view[1]).map(([s, l, n]) => {
			const cls = l > 33.3 ? "spike" : l > 16.7 ? "over" : "";
			const w = x(s + l) - x(s);
			return `<div class="fband ${cls}" style="left:${x(s).toFixed(1)}px;width:${w.toFixed(1)}px">${w > 120 ? `Frame ${n} &middot; ${l.toFixed(1)} ms` : w > 50 ? `${l.toFixed(1)} ms` : ""}</div>`;
		}).join("");
	}

	function messagesHtml(x, view) {
		const msgs = [[3.2, "info", "Arena: 2048 bodies active"], [GC.e + 0.2, "warn", "GC gen 1: 5.1 ms pause, 38 MB freed"], [44.0, "info", "arena_albedo.dds resident (4.2 MB)"]];
		return msgs.filter(([t]) => t > view[0] && t < view[1]).map(([t, sev, text]) => `<div class="msg ${sev}" style="left:${x(t).toFixed(1)}px"><i></i><span>${esc(text)}</span></div>`).join("");
	}

	function plotHtml(x, view, width) {
		const pts = [];
		for (let t = view[0]; t <= view[1]; t += 0.5) {
			let mb = 410 + ((t + 10) % 40) * 0.95;
			if (t >= GC.s && t < GC.e) mb = 448 - ((t - GC.s) / GC_PAUSE) * 38;
			else if (t >= GC.e) mb = 410 + (t - GC.e) * 0.95;
			pts.push(`${x(t).toFixed(1)},${(30 - (mb - 405) * 0.6).toFixed(1)}`);
		}
		return `<svg class="plot" width="${width}" height="32"><polyline points="${pts.join(" ")}"/></svg><span class="plot-val">GC heap 431 MB</span>`;
	}

	function frameGraph(el) {
		let bars = "";
		const n = 180;
		for (let i = 0; i < n; i++) {
			let ms = 14.6 + ((i * 37) % 17) / 10;
			if (i === 64 || i === 131) ms = 19.6;
			if (i === 97) ms = 34.8;
			if (i === 168) ms = 23.8;
			const cls = ms > 33.3 ? "spike" : ms > 16.7 ? "over" : "";
			bars += `<i class="${cls}${i >= 167 && i <= 169 ? " in" : ""}" style="height:${Math.min(100, ms / 40 * 100).toFixed(1)}%"></i>`;
		}
		el.innerHTML = `<div class="fg-bars">${bars}</div><div class="fg-budget" style="bottom:${(16.7 / 40 * 100).toFixed(1)}%"><span>16.7 ms &middot; 60 FPS</span></div>`;
	}

	function connectors(lanesEl, x, zoneH) {
		const render = lanesEl.querySelector('[data-lane="render"] .track');
		const gpu = lanesEl.querySelector('[data-lane="gpu"] .track');
		if (!render || !gpu) return;
		const base = lanesEl.getBoundingClientRect();
		const r = render.getBoundingClientRect();
		const g = gpu.getBoundingClientRect();
		const R = renderStart(1);
		const pairs = [[R + 4.1, R + 4.2], [R + 6.2, R + 4.2 + 1.88], [R + 7.8, R + 4.2 + 1.88 + 3.18]];
		const left = r.left - base.left;
		const lines = pairs.map(([a, b]) => `<path d="M${(left + x(a)).toFixed(1)} ${(r.top - base.top + 3 * zoneH + 2).toFixed(1)}L${(left + x(b)).toFixed(1)} ${(g.top - base.top + 2).toFixed(1)}"/>`).join("");
		lanesEl.insertAdjacentHTML("beforeend", `<svg class="gpu-links" width="${base.width}" height="${base.height}">${lines}</svg>`);
	}

	function gcBand(lanesEl, x) {
		const cpu = [...lanesEl.querySelectorAll(".lane:not(.gpu)")];
		if (!cpu.length) return;
		const base = lanesEl.getBoundingClientRect();
		const top = cpu[0].getBoundingClientRect().top - base.top;
		const bottom = cpu[cpu.length - 1].getBoundingClientRect().bottom - base.top;
		const left = cpu[0].querySelector(".track").getBoundingClientRect().left - base.left;
		lanesEl.insertAdjacentHTML("beforeend", `<div class="gc-band" style="left:${(left + x(GC.s)).toFixed(1)}px;width:${(x(GC.e) - x(GC.s)).toFixed(1)}px;top:${top}px;height:${bottom - top}px"><span>GC 5.1 ms</span></div>`);
	}

	(window.Mock = window.Mock || { hooks: [] }).hooks.push(() => {
		const root = document.querySelector(".tl");
		if (!root) return;
		const tasks = document.documentElement.dataset.view === "tasks";
		const phone = window.matchMedia("(max-width: 719px)").matches;
		const view = phone ? [15.4, 41.6] : [-1, 58];
		const zoneH = phone ? 20 : 18;
		const lanesEl = root.querySelector(".tl-lanes");
		const width = root.querySelector(".tl-ruler .track").getBoundingClientRect().width;
		const x = scale(view, width);
		root.querySelector(".tl-ruler .track").innerHTML = rulerHtml(view, x);
		root.querySelector(".tl-frames .track").innerHTML = framesHtml(view, x);
		let lanes = (tasks ? taskLanes() : threadLanes()).concat(gpuLanes());
		if (phone) lanes = lanes.filter((l) => !["w3", "Streaming", "Audio.Mix", "copy"].includes(l.id));
		lanesEl.innerHTML = lanes.map((l) => laneHtml(l, x, zoneH, view)).join("") +
			`<div class="lane msgs"><div class="lane-label"><i class="ci ci-chevron-down tw"></i><i class="ci ci-output"></i><span class="nm">Messages</span></div><div class="track">${messagesHtml(x, view)}</div></div>` +
			`<div class="lane plot-lane"><div class="lane-label"><i class="ci ci-chevron-down tw"></i><i class="ci ci-graph-line"></i><span class="nm">GC heap</span></div><div class="track">${plotHtml(x, view, width)}</div></div>`;
		gcBand(lanesEl, x);
		connectors(lanesEl, x, zoneH);
		const fg = document.querySelector(".fg");
		if (fg) frameGraph(fg);
		const histo = document.querySelector(".histo");
		if (histo) histo.innerHTML = Array.from({ length: 48 }, (_, i) => {
			const h = Math.exp(-((i - 13) ** 2) / 40) * 92 + (i > 20 ? Math.exp(-((i - 30) ** 2) / 30) * 14 : 0) + 3;
			return `<i class="${i >= 15 && i <= 16 ? "p" : ""}" style="height:${h.toFixed(1)}%"></i>`;
		}).join("");
		for (const el of document.querySelectorAll("[data-view-btn]")) el.classList.toggle("on", el.dataset.viewBtn === (tasks ? "tasks" : "threads"));
		for (const el of document.querySelectorAll("[data-view-only]")) el.hidden = el.dataset.viewOnly !== (tasks ? "tasks" : "threads");
	});
})();
