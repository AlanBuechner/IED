// git.js - commit graph for the Git activity mock (screen 5). Rows and lanes come from COMMITS;
// the SVG is measured from the rendered rows so it lines up at every density.
"use strict";

(() => {
	// lane: column of the commit; parents: [row, lane] pairs (first parent first)
	const COMMITS = [
		{ id: "local", lane: 0, parents: [[1, 0]], msg: "Local changes", local: 4 },
		{ id: "3f2c9a1", lane: 0, parents: [[3, 0]], msg: "Log: Log.ch declarations and ConsoleSink", author: "owner", ai: true, date: "21:33", refs: [["head", "port/logging"]] },
		{ id: "c27e5f0", lane: 1, parents: [[4, 0]], msg: "Time: fix engine.time delta test after QPC fallback", author: "owner", ai: true, date: "21:20", refs: [["branch", "agents/fix-time-test"], ["worktree", "worktree"]] },
		{ id: "8d41b07", lane: 0, parents: [[4, 0]], msg: "Log: port LogLevel and LogRecord from RefEngine", author: "owner", ai: true, date: "21:12" },
		{ id: "d3e8f61", lane: 0, parents: [[6, 0], [5, 1]], msg: "Merge branch 'port/events'", author: "owner", date: "Yesterday 18:05", refs: [["branch", "master"], ["remote", "origin/master"]] },
		{ id: "9b0f4a4", lane: 1, parents: [[7, 1]], msg: "Events: window resize and key events in the blob format", author: "owner", date: "Yesterday 17:40", refs: [["branch", "port/events"], ["remote", "origin/port/events"]] },
		{ id: "51aa9e3", lane: 0, parents: [[8, 0]], msg: "Core: Ring<T> fixed-capacity ring buffer + tests", author: "owner", date: "Yesterday 16:42", sel: true },
		{ id: "e7c1d22", lane: 1, parents: [[8, 0]], msg: "Events: EventBlock blob format matches RefEngine byte-for-byte", author: "owner", ai: true, date: "Yesterday 15:10" },
		{ id: "4382b53", lane: 0, parents: [[9, 0]], msg: "Restore moduleLinks = [\"Engine\"] in the root manifest", author: "owner", date: "Oct 17" },
		{ id: "6c0a7d3", lane: 0, parents: [[10, 0]], msg: "Math: column-major Matrix4 to match RefEngine layout", author: "owner", date: "Oct 17" },
		{ id: "0b9e2f4", lane: 0, parents: [[11, 0]], msg: "Core: Time via QPC with GetTickCount64 fallback", author: "owner", ai: true, date: "Oct 16" },
		{ id: "f18d2c6", lane: 0, parents: [[12, 0]], msg: "Application: staged driver swap checked every frame", author: "owner", date: "Oct 16" },
		{ id: "77b03ae", lane: 0, parents: [[13, 0]], msg: "Windowing: headless WindowsManager path for tests", author: "owner", date: "Oct 15" },
		{ id: "a5fc91e", lane: 0, parents: [[14, 0]], msg: "Baseline: MyGame engine port (RefEngine -> C4)", author: "owner", date: "Oct 14", refs: [["tag", "m0-baseline"]] },
		{ id: "2e6b9d0", lane: 0, parents: [[15, 0]], msg: "Nuke: nuke.toml with the Engine module and Game root", author: "owner", date: "Oct 12" },
		{ id: "5d7e1a8", lane: 0, parents: [[16, 0]], msg: "Core: Hash (FNV-1a) port with test vectors from RefEngine", author: "owner", date: "Oct 11" },
		{ id: "b3c90f2", lane: 0, parents: [[17, 0]], msg: "Docs: 05-math-library.md records compiler workarounds B1-B8", author: "owner", date: "Oct 11" },
		{ id: "1f4a6e9", lane: 0, parents: [[18, 0]], msg: "Platform: DirectX12, Vulkan, Headless backend stubs", author: "owner", date: "Oct 10" },
		{ id: "e08b2d5", lane: 0, parents: [[19, 0]], msg: "Engine: module skeleton mirrors RefEngine folder layout", author: "owner", date: "Oct 10" },
		{ id: "7a61c3b", lane: 0, parents: [[20, 0]], msg: "Docs: WorkOrder.md port tracker", author: "owner", date: "Oct 9" },
		{ id: "c4e7f10", lane: 0, parents: [[21, 0]], msg: "Nuke: vendor C4.exe and the kit launcher", author: "owner", date: "Oct 9" },
		{ id: "9a04c11", lane: 0, parents: [], msg: "Initial commit", author: "owner", date: "Oct 9" },
	];

	const REF_ICON = { head: "ci-git-branch", branch: "ci-git-branch", remote: "ci-cloud", tag: "ci-tag", worktree: "ci-worktree" };

	function refChip([kind, name], lane) {
		return `<span class="ref ${kind} l${lane}"><i class="ci ${REF_ICON[kind]}"></i>${Mock.escapeHtml(name)}</span>`;
	}

	function rowHtml(c) {
		const refs = (c.refs || []).map((r) => refChip(r, c.lane)).join("");
		if (c.local) {
			return `<div class="grow-row local"><div class="g"></div><div class="desc"><span class="ref local-ref"><i class="ci ci-edit"></i>${c.local} changed files</span><span class="msg muted">Local changes - not committed</span></div><div class="au"></div><div class="dt"></div><div class="hs"></div></div>`;
		}
		const ai = c.ai ? '<i class="ci ci-sparkle c-ai" title="Co-authored by an IED agent"></i>' : "";
		return `<div class="grow-row${c.sel ? " sel" : ""}"><div class="g"></div><div class="desc">${refs}<span class="msg">${Mock.escapeHtml(c.msg)}</span><span class="sub2">${c.author} &middot; ${c.date} &middot; ${c.id}</span></div><div class="au">${ai}${c.author}</div><div class="dt">${c.date}</div><div class="hs">${c.id}</div></div>`;
	}

	function graphSvg(rowH, laneW, x0) {
		const cx = (lane) => x0 + lane * laneW;
		const cy = (row) => row * rowH + rowH / 2;
		let paths = "";
		let dots = "";
		COMMITS.forEach((c, row) => {
			c.parents.forEach(([prow, plane], index) => {
				const x1 = cx(c.lane);
				const x2 = cx(plane);
				const y1 = cy(row);
				const y2 = cy(prow);
				let d;
				if (x1 === x2) d = `M${x1} ${y1}V${y2}`;
				else if (index === 0) d = `M${x1} ${y1}V${y2 - rowH * 0.6}C${x1} ${y2 - rowH * 0.2} ${x2} ${y2 - rowH * 0.45} ${x2} ${y2}`;
				else d = `M${x1} ${y1}C${x1} ${y1 + rowH * 0.45} ${x2} ${y1 + rowH * 0.2} ${x2} ${y1 + rowH * 0.6}V${y2}`;
				const lane = index === 0 && x1 !== x2 ? c.lane : Math.max(c.lane, plane);
				paths += `<path class="l${lane}${c.local ? " dash" : ""}" d="${d}"/>`;
			});
			const x = cx(c.lane);
			const y = cy(row);
			if (c.local) dots += `<circle class="dot-local" cx="${x}" cy="${y}" r="4.5"/>`;
			else if (c.refs && c.refs[0][0] === "head") dots += `<circle class="dot-head l${c.lane}" cx="${x}" cy="${y}" r="5.5"/><circle class="fill l${c.lane}" cx="${x}" cy="${y}" r="2.5"/>`;
			else dots += `<circle class="fill l${c.lane}" cx="${x}" cy="${y}" r="${c.parents.length > 1 ? 3 : 4}"/>`;
		});
		return `<svg class="graph-svg" width="${x0 * 2 + laneW * 2}" height="${rowH * COMMITS.length}">${paths}${dots}</svg>`;
	}

	(window.Mock = window.Mock || { hooks: [] }).hooks.push(() => {
		const body = document.querySelector(".graph-body");
		if (!body) return;
		body.insertAdjacentHTML("beforeend", COMMITS.map(rowHtml).join(""));
		const first = body.querySelector(".grow-row");
		const rowH = first.getBoundingClientRect().height;
		const phone = window.matchMedia("(max-width: 719px)").matches;
		body.insertAdjacentHTML("afterbegin", graphSvg(rowH, phone ? 16 : 14, phone ? 14 : 12));
	});
})();
