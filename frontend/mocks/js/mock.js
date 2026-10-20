// mock.js - shared behavior for the M0 static review mocks. Plain DOM, no framework, no server.
// - URL params land on <html> as data attributes (?syntax=grayscale, ?view=tasks, ?step=diff).
// - Injects the window chrome shared by every activity (sidebar, menu bar, toolbar, terminal, phone nav).
// - Renders <div class="code"> blocks: `kinds:text` token markup, gutters, indent guides, diffs, map scrollbar.
// Page scripts load before this file and register render hooks: (window.Mock = window.Mock || { hooks: [] }).hooks.push(fn).
"use strict";

window.Mock = window.Mock || { hooks: [] };
Mock.onReady = Mock.onReady || ((fn) => Mock.hooks.push(fn));

(() => {
	const TOKEN = /`([a-z][a-z0-9 -]*):([^`]*)`/g;

	function escapeHtml(text) {
		return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
	}

	function icon(name, extra) {
		return `<i class="ci ${name}${extra ? " " + extra : ""}"></i>`;
	}

	function parseRanges(text) {
		const set = new Set();
		for (const part of (text || "").split(",").filter(Boolean)) {
			const [a, b] = part.split("-").map(Number);
			for (let n = a; n <= (b || a); n++) set.add(n);
		}
		return set;
	}

	const SYNTAX = new Set(["kw", "ctl", "ty", "fn", "var", "par", "mem", "con", "ns", "mac", "str", "num", "com", "op", "bad"]);

	function tokenClass(kinds) {
		return kinds.split(" ").filter(Boolean).map((k) => (SYNTAX.has(k) ? "tok-" + k : k)).join(" ");
	}

	// `kinds:text` -> spans; leading tabs -> indent guides
	function renderLine(raw) {
		const tabs = raw.match(/^\t*/)[0].length;
		let html = '<span class="ig"></span>'.repeat(tabs);
		const body = raw.slice(tabs);
		let last = 0;
		for (const m of body.matchAll(TOKEN)) {
			html += escapeHtml(body.slice(last, m.index));
			html += `<span class="${tokenClass(m[1])}">${escapeHtml(m[2])}</span>`;
			last = m.index + m[0].length;
		}
		return html + escapeHtml(body.slice(last));
	}

	function mapRow(raw) {
		const tabs = raw.match(/^\t*/)[0].length;
		const body = raw.slice(tabs).replace(/^[+\- ]/, "");
		let html = `<span style="width:${tabs * 4}px"></span>`;
		let last = 0;
		const plain = (text) => {
			for (const word of text.split(/(\s+)/)) {
				if (!word) continue;
				html += /\s/.test(word) ? `<span style="width:${word.length}px"></span>` : `<i style="width:${word.length}px"></i>`;
			}
		};
		for (const m of body.matchAll(TOKEN)) {
			plain(body.slice(last, m.index));
			html += `<i class="${tokenClass(m[1])}" style="width:${m[2].length}px"></i>`;
			last = m.index + m[0].length;
		}
		plain(body.slice(last));
		return `<div class="mr">${html}</div>`;
	}

	function sourceLines(el) {
		const pre = el.querySelector("pre");
		const text = pre ? pre.textContent : "";
		if (pre) pre.remove();
		return text.replace(/^\n/, "").replace(/\n\s*$/, "").split("\n");
	}

	function renderCode(el) {
		const lines = sourceLines(el);
		const d = el.dataset;
		const marks = {
			saved: parseRanges(d.saved), unsaved: parseRanges(d.unsaved), ai: parseRanges(d.ai),
			bp: parseRanges(d.bp), fold: parseRanges(d.fold), err: parseRanges(d.err), warn: parseRanges(d.warn),
		};
		const cur = Number(d.cur || 0);
		let n = Number(d.first || 1);
		let html = "";
		const numbered = [];
		for (let i = 0; i < lines.length; i++) {
			const raw = lines[i];
			if (raw.startsWith("@lens ")) {
				const indent = (lines[i + 1] || "").match(/^\t*/)[0].length;
				html += `<div class="ln lens"><span class="gl"></span><span class="no"></span><span class="chg"></span><span class="fd"></span><span class="tx">${'<span class="ig blank"></span>'.repeat(indent)}${escapeHtml(raw.slice(6))}</span></div>`;
				continue;
			}
			const chg = marks.unsaved.has(n) ? "unsaved" : marks.saved.has(n) ? "saved" : marks.ai.has(n) ? "ai" : "";
			const glyph = marks.bp.has(n) ? '<i class="bp"></i>' : "";
			const fold = marks.fold.has(n) ? icon("ci-chevron-down", "xs") : "";
			html += `<div class="ln${n === cur ? " cur" : ""}"><span class="gl">${glyph}</span><span class="no">${n}</span><span class="chg ${chg}"></span><span class="fd">${fold}</span><span class="tx">${renderLine(raw)}</span></div>`;
			numbered.push({ n, raw, chg });
			n++;
		}
		el.insertAdjacentHTML("beforeend", `<div class="lines">${html}</div>`);
		if (d.map !== undefined) renderMap(el, numbered, marks, Number(d.map || 1));
	}

	function renderMap(el, numbered, marks, repeat) {
		let rows = "";
		let markers = "";
		const total = numbered.length * repeat;
		numbered.forEach((line, i) => {
			const top = (i * 3) + "px";
			if (line.chg) markers += `<b class="mk ${line.chg}" style="top:${top}"></b>`;
			if (marks.err.has(line.n)) markers += `<b class="mk err" style="top:${top}"></b>`;
			if (marks.warn.has(line.n)) markers += `<b class="mk warn" style="top:${top}"></b>`;
		});
		for (let r = 0; r < repeat; r++) for (const line of numbered) rows += mapRow(line.raw);
		const extra = (el.dataset.mapMarks || "").split(",").filter(Boolean).map((m) => {
			const [kind, pct] = m.split(":");
			return `<b class="mk ${kind}" style="top:${pct}%"></b>`;
		}).join("");
		el.insertAdjacentHTML("beforeend", `<div class="map"><div class="map-rows">${rows}</div><div class="map-view" style="height:${numbered.length * 3}px"></div>${markers}${extra}</div>`);
		el.classList.add("has-map");
		el.dataset.mapRows = String(total);
	}

	// unified diff source: lines start with "@@ -a,b +c,d @@ title", "+", "-" or " "
	function parseDiff(lines) {
		let oldN = 1;
		let newN = 1;
		return lines.map((raw) => {
			const hunk = raw.match(/^@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@(.*)$/);
			if (hunk) {
				oldN = Number(hunk[1]);
				newN = Number(hunk[2]);
				return { kind: "hunk", text: raw };
			}
			const sign = raw[0] || " ";
			const text = raw.slice(1);
			if (sign === "+") return { kind: "add", newN: newN++, text };
			if (sign === "-") return { kind: "del", oldN: oldN++, text };
			return { kind: "ctx", oldN: oldN++, newN: newN++, text };
		});
	}

	function diffRow(cls, a, b, sign, text) {
		return `<div class="ln ${cls}"><span class="no">${a ?? ""}</span><span class="no">${b ?? ""}</span><span class="sign">${sign}</span><span class="tx">${text}</span></div>`;
	}

	function renderUnified(el) {
		let html = "";
		for (const row of parseDiff(sourceLines(el))) {
			if (row.kind === "hunk") html += el.dataset.nohunk === undefined ? `<div class="ln hunk"><span class="tx">${escapeHtml(row.text)}</span></div>` : "";
			else if (row.kind === "add") html += diffRow("add", "", row.newN, "+", renderLine(row.text));
			else if (row.kind === "del") html += diffRow("del", row.oldN, "", "-", renderLine(row.text));
			else html += diffRow("ctx", row.oldN, row.newN, "", renderLine(row.text));
		}
		el.insertAdjacentHTML("beforeend", `<div class="lines">${html}</div>`);
	}

	function sideRow(cls, n, text) {
		return `<div class="ln ${cls}"><span class="no">${n ?? ""}</span><span class="tx">${text}</span></div>`;
	}

	function renderSplit(el) {
		const rows = parseDiff(sourceLines(el));
		let left = "";
		let right = "";
		for (let i = 0; i < rows.length; i++) {
			const row = rows[i];
			if (row.kind === "hunk") {
				const h = el.dataset.nohunk === undefined ? `<div class="ln hunk"><span class="tx">${escapeHtml(row.text)}</span></div>` : "";
				left += h;
				right += h;
				continue;
			}
			if (row.kind === "ctx") {
				left += sideRow("ctx", row.oldN, renderLine(row.text));
				right += sideRow("ctx", row.newN, renderLine(row.text));
				continue;
			}
			const dels = [];
			const adds = [];
			while (i < rows.length && rows[i].kind === "del") dels.push(rows[i++]);
			while (i < rows.length && rows[i].kind === "add") adds.push(rows[i++]);
			i--;
			for (let k = 0; k < Math.max(dels.length, adds.length); k++) {
				left += dels[k] ? sideRow("del", dels[k].oldN, renderLine(dels[k].text)) : '<div class="ln fill"></div>';
				right += adds[k] ? sideRow("add", adds[k].newN, renderLine(adds[k].text)) : '<div class="ln fill"></div>';
			}
		}
		el.insertAdjacentHTML("beforeend", `<div class="split-l lines">${left}</div><div class="split-r lines">${right}</div>`);
	}

	// ---------- shared chrome ----------
	const ACTIVITIES = [
		["chat", "ci-comment-discussion", "Chat"],
		["editor", "ci-files", "Editor"],
		["git", "ci-source-control", "Git"],
		["profiler", "ci-pulse", "Profiler"],
		["sandbox", "ci-vm", "Sandbox"],
	];

	function sidebar(active) {
		const acts = ACTIVITIES.map(([id, ic, title]) => {
			const extra = id === "chat" ? '<span class="badge warn">1</span>' : id === "git" ? '<span class="badge">4</span>' : "";
			return `<div class="sb-act${id === active ? " on" : ""}" title="${title}">${icon(ic)}${extra}</div>`;
		}).join("");
		return `<nav class="sidebar"><div class="sb-ws" title="Workspace: Claymore">C</div><div class="sb-sep"></div>${acts}<div class="sb-spacer"></div><div class="sb-act" title="Notifications">${icon("ci-bell")}<span class="dot warn sm"></span></div><div class="sb-act${active === "settings" ? " on" : ""}" title="Settings">${icon("ci-settings-gear")}</div></nav>`;
	}

	function menubar(openMenu) {
		const menus = ["File", "Edit", "View", "Git", "Project", "Build", "Debug", "Test", "Analyze", "Tools", "Extensions", "Window", "Help"]
			.map((m) => `<span class="mi${m === openMenu ? " open" : ""}">${m}</span>`).join("");
		return `<header class="menubar"><div class="menus">${menus}</div><div class="title"><div class="search-box">${icon("ci-search", "sm")}<span>Feature search</span><span class="kbd">Ctrl+Q</span></div><span class="ws-title"><b>Claymore</b> &middot; Development x64</span><span class="conn" title="Server connection"><span class="dot saved sm"></span>live</span></div></header>`;
	}

	function toolbar() {
		return [
			`<span class="ib">${icon("ci-arrow-left")}</span><span class="ib dim">${icon("ci-arrow-right")}</span>`,
			'<span class="tb-sep"></span>',
			`<span class="ib">${icon("ci-save")}</span><span class="ib">${icon("ci-save-all")}</span><span class="ib">${icon("ci-discard")}</span><span class="ib dim">${icon("ci-redo")}</span>`,
			'<span class="tb-sep"></span>',
			'<span class="dd" title="Configuration">Development</span><span class="dd" title="Platform">x64</span><span class="dd" title="Startup project">Game</span>',
			'<span class="tb-sep"></span>',
			`<span class="split-btn"><span class="btn">${icon("ci-play")}Start</span><span class="btn">${icon("ci-chevron-down", "sm")}</span></span>`,
			`<span class="btn flat">${icon("ci-tools")}Build</span><span class="btn flat">${icon("ci-beaker")}Test</span><span class="btn flat">${icon("ci-pulse")}Profile</span>`,
		].join("");
	}

	const TERMINAL = [
		'<span class="a-gray">PS C:\\GameDev\\Claymore&gt;</span> nuke build -c Development',
		'<span class="a-cyan">[nuke]</span> Claymore: 2 modules (Engine, Game), 3 files changed',
		'<span class="a-cyan">[nuke]</span> Engine: compiling Core/Log.cf, Core/Log.ch, Engine/Core/Application.cf',
		'<span class="loc">Engine/src/Core/Log.cf (30:9)</span> <span class="a-yellow">warning</span>: unused variable \'level\'',
		'<span class="loc">Engine/src/Core/Ring.ch (31:5)</span> <span class="a-yellow">warning</span>: implicit int64 -&gt; int32 narrowing in \'Mask\'',
		'<span class="a-cyan">[nuke]</span> Game: linking build/bin/Development-windows-x64/Game/Game.exe',
		'<span class="a-green a-bold">Build succeeded</span> in 18.2 s <span class="a-gray">(2 warnings, 0 errors)</span>',
		'<span class="a-gray">PS C:\\GameDev\\Claymore&gt;</span> git status -sb',
		'<span class="a-green">## port/logging</span>...<span class="a-red">origin/port/logging</span> [ahead 3]',
		' <span class="a-red">M</span> Engine/src/Core/Log.cf',
		'<span class="a-red">??</span> Engine/src/tests/log.cf',
		'<span class="a-gray">PS C:\\GameDev\\Claymore&gt;</span> <span class="term-cursor"></span>',
	];

	function terminal(el) {
		const head = `<div class="term-head"><span class="term-tab on">${icon("ci-terminal-powershell")}1: pwsh${icon("ci-chevron-down", "sm")}</span><span class="term-tab">${icon("ci-terminal")}2: nuke watch</span><span class="term-tab">${icon("ci-robot")}3: agent: Port logging</span><span class="ib">${icon("ci-add")}</span><span class="ib">${icon("ci-split-horizontal")}</span><span class="ib">${icon("ci-trash")}</span><span class="tb-spacer"></span><span class="muted desktop-wide" style="padding-right:8px">Ctrl+\`</span><span class="ib">${icon("ci-screen-full")}</span><span class="ib">${icon(el.classList.contains("collapsed") ? "ci-chevron-up" : "ci-chevron-down")}</span></div>`;
		const lines = TERMINAL.slice(-(Number(el.dataset.lines) || TERMINAL.length));
		el.innerHTML = head + `<div class="term-body">${lines.map((l) => `<div>${l}</div>`).join("")}</div>`;
	}

	const PHONE_NAV = [["chat", "ci-comment-discussion", "Chat"], ["editor", "ci-code", "Code"], ["git", "ci-source-control", "Git"], ["output", "ci-output", "Output"], ["more", "ci-menu", "More"]];

	function phoneNav(active) {
		return '<nav class="phone-nav">' + PHONE_NAV.map(([id, ic, label]) => {
			const extra = id === "chat" ? '<span class="badge warn">1</span>' : id === "git" ? '<span class="badge">4</span>' : "";
			return `<div class="pn${id === active ? " on" : ""}">${icon(ic)}${extra}<span>${label}</span></div>`;
		}).join("") + "</nav>";
	}

	function injectShell() {
		const app = document.querySelector(".app");
		if (!app) return;
		const d = app.dataset;
		app.insertAdjacentHTML("afterbegin", sidebar(d.activity) + menubar(d.menu));
		const tb = app.querySelector(".toolbar");
		if (tb) tb.insertAdjacentHTML("afterbegin", toolbar() + (tb.children.length ? '<span class="tb-sep"></span>' : ""));
		const term = app.querySelector(".terminal-dock");
		if (term) terminal(term);
		app.insertAdjacentHTML("beforeend", phoneNav(d.phone || d.activity));
	}

	function applyParams() {
		for (const [key, value] of new URLSearchParams(location.search)) document.documentElement.dataset[key] = value || "1";
	}

	function render() {
		applyParams();
		injectShell();
		for (const el of document.querySelectorAll(".code")) {
			const kind = el.dataset.kind || "code";
			if (kind === "diff") renderUnified(el);
			else if (kind === "split") renderSplit(el);
			else renderCode(el);
		}
		for (const hook of Mock.hooks) hook();
		for (const el of document.querySelectorAll('[data-scroll="bottom"]')) el.scrollTop = el.scrollHeight;
		document.documentElement.dataset.ready = "1";
	}

	Mock.icon = icon;
	Mock.escapeHtml = escapeHtml;
	Mock.renderLine = renderLine;
	if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", render);
	else render();
})();
