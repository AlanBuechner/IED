// attach.js - Attach to Process (docs/16 "Attaching to a running program"; owner, 2026-11-11: "the profiler should allow
// us to attatch to an already running process including child processes"): the programs that profile but were started
// without IED_PROFILER (prof.processes), as a tree - each under its nearest attachable ancestor - with a filter, their
// state (idle, or attached already) and Include child processes (on by default: those under it now, and those that
// start under it while it runs). Attach (Enter, a double click) calls prof.attach; the captures that come open as
// usual (the first one is opened). On a phone it is a bottom sheet.

import { h, icon, replace } from "/ui-kit/dom.js";

const PHONE = "(max-width: 719px)";

// The processes as a tree's rows, depth first: [{process, depth}], each under the attachable process it runs under, the
// roots by pid. Pure, for the tests.
export function processRows(processes) {
	const byPid = new Map(processes.map((p) => [p.pid, p]));
	const children = new Map();
	const roots = [];
	for (const process of processes) {
		const under = process.under && byPid.has(process.under) ? process.under : 0;
		if (!under) roots.push(process);
		else {
			if (!children.has(under)) children.set(under, []);
			children.get(under).push(process);
		}
	}
	const rows = [];
	const walk = (list, depth) => {
		for (const process of list.slice().sort((a, b) => a.pid - b.pid)) {
			rows.push({ process, depth });
			walk(children.get(process.pid) || [], depth + 1);
		}
	};
	walk(roots, 0);
	return rows;
}

// Whether a process matches the filter: its name, its program's file or its pid.
export function processMatches(process, query) {
	const q = query.trim().toLowerCase();
	return !q || (process.app || "").toLowerCase().includes(q) || (process.exe || "").toLowerCase().includes(q) || String(process.pid).includes(q);
}

// Shows the dialog for workspace `wsId`; `attached(answer)` hears what prof.attach said. Returns what closes it.
export function showAttach(ied, wsId, attached) {
	const sheet = window.matchMedia(PHONE).matches;
	const before = document.activeElement;
	let processes = [];
	let chosen = null;
	const filter = h("input.input-field.prof-attach-filter", { type: "search", placeholder: "Filter by name or pid", "aria-label": "Filter processes", spellcheck: "false", on: { input: () => draw() } });
	const children = h("input", { type: "checkbox", checked: true, "aria-label": "Include child processes" });
	const list = h("div.prof-attach-list", { role: "tree", "aria-label": "Processes" });
	const problem = h("div.dialog-problem.c-error", { hidden: true });
	const attachButton = h("button.btn.primary", { disabled: true, on: { click: () => attach() } }, "Attach");
	const close = () => {
		veil.remove();
		if (before && before.isConnected) before.focus({ preventScroll: true });
	};
	const panel = h("div.popup.dialog.prof-attach" + (sheet ? ".sheet" : ""), { role: "dialog", "aria-modal": "true", "aria-label": "Attach to Process" },
		h("div.dialog-title", null, "Attach to Process"),
		h("div.dialog-message", null, "Programs that profile (Bedrock's Profile, the C/C++ client) and run without IED_PROFILER can be attached: they start sending this workspace their zones, frames and plots from now on."),
		h("div.prof-attach-bar", null, filter, h("button.ib", { title: "Refresh", "aria-label": "Refresh", on: { click: () => load() } }, icon("refresh"))),
		list,
		h("label.prof-attach-children", null, children, " Include child processes - those under it now, and those that start under it while it runs"),
		problem,
		h("div.dialog-buttons", null, h("button.btn", { on: { click: close } }, "Cancel"), attachButton));
	const veil = h("div.dialog-veil", { on: {
		mousedown: (event) => {
			if (event.target === veil) close();
		},
		keydown: (event) => {
			event.stopPropagation();
			if (event.key === "Escape") close();
			else if (event.key === "Enter" && chosen && event.target !== filter) attach();
			else if (event.key === "ArrowDown" || event.key === "ArrowUp") step(event, event.key === "ArrowDown" ? 1 : -1);
		},
	} }, panel);
	document.body.append(veil);

	function draw() {
		const rows = processRows(processes).filter(({ process }) => processMatches(process, filter.value));
		if (!processes.length) {
			replace(list, h("div.muted.prof-attach-empty", null, "No program that profiles is running. A program started from IED's Profile task is connected already; one built without IED's profiling cannot be attached."));
			return;
		}
		replace(list, ...rows.map(({ process, depth }) => h("div.prof-attach-row" + (chosen === process.pid ? ".on" : ""), {
			role: "treeitem", tabindex: "-1", style: { "--depth": String(depth) }, dataset: { pid: String(process.pid) },
			title: (process.exe || "") + " (pid " + process.pid + ")" + (process.state === "attached" ? " - sending to the profiler already" : ""),
			on: { click: () => pick(process.pid), dblclick: () => {
				pick(process.pid);
				attach();
			} },
		}, icon(depth ? "debug-step-into" : "pulse"), h("span.name", null, process.app || process.exe || "?"), h("span.muted", null, process.exe && process.exe !== process.app ? process.exe : ""),
		h("span.grow"), h("span.meta", null, "pid " + process.pid), process.state === "attached" ? h("span.chip", null, "attached") : null)));
	}

	function pick(pid) {
		chosen = pid;
		attachButton.disabled = chosen === null;
		draw();
		const row = list.querySelector(".prof-attach-row.on");
		if (row) row.focus();
	}

	function step(event, by) {
		const rows = [...list.querySelectorAll(".prof-attach-row")];
		if (!rows.length) return;
		event.preventDefault();
		const at = rows.findIndex((row) => Number(row.dataset.pid) === chosen);
		const next = rows[Math.max(0, Math.min(rows.length - 1, at < 0 ? 0 : at + by))];
		pick(Number(next.dataset.pid));
	}

	async function load() {
		replace(list, h("div.muted.prof-attach-empty", null, "Looking for programs..."));
		try {
			processes = (await ied.client.request("prof.processes", { wsId })).processes || [];
		}
		catch (e) {
			processes = [];
			problem.textContent = e.message || String(e);
			problem.hidden = false;
		}
		if (chosen !== null && !processes.some((p) => p.pid === chosen)) chosen = null;
		attachButton.disabled = chosen === null;
		draw();
	}

	async function attach() {
		if (chosen === null) return;
		attachButton.disabled = true;
		problem.hidden = true;
		try {
			const answer = await ied.client.request("prof.attach", { wsId, pid: chosen, children: children.checked });
			close();
			attached(answer);
		}
		catch (e) {
			problem.textContent = e.message || String(e);
			problem.hidden = false;
			attachButton.disabled = false;
		}
	}

	load();
	filter.focus();
	return close;
}
