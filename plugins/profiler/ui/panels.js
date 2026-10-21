// panels.js - the capture's bottom tool windows (mockup 06): Statistics (per zone, thread or task; sorted on the
// server; filtered; CSV) and Messages (severity and text filters; a click jumps the timeline there). Each panel shows
// the scope it is given: the timeline's selection, else its view, else the whole capture.

import { h, icon, replace } from "/ui-kit/dom.js";
import { duration, ms, count } from "./format.js";

const SEVERITIES = ["trace", "debug", "info", "warning", "error", "fatal"];

// A capture's range as the panels say it: {from, to, label} in nanoseconds, or the whole capture.
export function scopeLabel(scope) {
	if (!scope || scope.from === undefined) return "Range: whole capture";
	return "Range: " + scope.label + " \u00b7 " + duration(scope.to - scope.from);
}

function csvCell(text) {
	const value = String(text);
	return /[",\n]/.test(value) ? "\"" + value.replace(/"/g, "\"\"") + "\"" : value;
}

function download(name, text) {
	const url = URL.createObjectURL(new Blob([text], { type: "text/csv" }));
	const link = h("a", { href: url, download: name });
	document.body.append(link);
	link.click();
	link.remove();
	setTimeout(() => URL.revokeObjectURL(url), 1000);
}

const COLUMNS = [
	{ key: "name", title: "Zone", order: "name", cell: (r) => r.name },
	{ key: "group", title: "", cell: (r) => r.group || "", grouped: true },
	{ key: "source", title: "Source", cell: (r) => r.file ? r.file + (r.line ? ":" + r.line : "") : "", cls: "src" },
	{ key: "count", title: "Count", order: "count", cell: (r) => count(r.count) },
	{ key: "total", title: "Total ms", order: "total", cell: (r) => ms(r.totalNs) },
	{ key: "self", title: "Self ms", order: "self", cell: (r) => ms(r.selfNs) },
	{ key: "mean", title: "Mean", order: "mean", cell: (r) => ms(r.meanNs) },
	{ key: "median", title: "Median", order: "median", cell: (r) => ms(r.medianNs) },
	{ key: "p95", title: "p95", order: "p95", cell: (r) => ms(r.p95Ns) },
	{ key: "max", title: "Max", order: "max", cell: (r) => ms(r.maxNs) },
	{ key: "share", title: "% of range", cell: (r) => r.share },
];

export class StatsPanel {
	constructor({ request, onPick }) {
		this.request = request;
		this.onPick = onPick;
		this.groupBy = "zone";
		this.order = "total";
		this.filter = "";
		this.rows = [];
		this.filterField = h("input.input-field.prof-filter", { type: "search", placeholder: "Filter zones", "aria-label": "Filter zones", on: { input: () => this.filterChanged() } });
		this.rangeLabel = h("span.muted.prof-range");
		this.groups = h("span.seg", null, ...[["zone", "Zones"], ["thread", "Threads"], ["task", "Tasks"]].map(([key, title]) =>
			h("span" + (key === this.groupBy ? ".on" : ""), { dataset: { group: key }, on: { click: () => this.setGroup(key) } }, title)));
		this.table = h("table.stats.wide-stats");
		this.el = h("div.prof-panel", null,
			h("div.panel-tools", null, h("span.muted", null, "Statistics"), this.groups, this.rangeLabel, h("span.grow"),
				h("button.btn.flat", { title: "Save the rows as CSV", on: { click: () => this.exportCsv() } }, icon("export"), "CSV"), this.filterField),
			h("div.scroll.prof-table-scroll", null, this.table));
	}

	setGroup(key) {
		this.groupBy = key;
		for (const span of this.groups.children) span.classList.toggle("on", span.dataset.group === key);
		this.load();
	}

	filterChanged() {
		clearTimeout(this.filterTimer);
		this.filterTimer = setTimeout(() => {
			this.filter = this.filterField.value.trim();
			this.load();
		}, 200);
	}

	update(captureId, scope) {
		this.captureId = captureId;
		this.scope = scope;
		this.rangeLabel.textContent = scopeLabel(scope);
		this.load();
	}

	async load() {
		if (!this.captureId) return;
		const asked = { captureId: this.captureId, groupBy: this.groupBy, order: this.order, filter: this.filter, limit: 500 };
		if (this.scope && this.scope.from !== undefined) Object.assign(asked, { from: Math.round(this.scope.from), to: Math.round(this.scope.to) });
		const token = (this.token = {});
		const answer = await this.request("prof.stats", asked);
		if (token !== this.token || !answer) return;
		this.rows = answer.rows;
		this.render(answer.rowCount);
	}

	columns() {
		return COLUMNS.filter((c) => !c.grouped || this.groupBy !== "zone");
	}

	render(total) {
		const columns = this.columns();
		const head = h("tr", null, columns.map((c) => h("th" + (c.order === this.order ? ".sorted" : ""), {
			on: c.order ? { click: () => { this.order = c.order; this.load(); } } : null,
		}, c.key === "group" ? (this.groupBy === "thread" ? "Thread" : "Task") : c.title)));
		const most = Math.max(1e-9, ...this.rows.map((r) => r.share));
		const body = this.rows.map((row) => h("tr", { on: { click: () => this.onPick(row) }, title: row.function || row.name }, columns.map((c) => {
			if (c.key === "share") return h("td", null, h("span.pct", null, h("i", { style: { width: Math.round(row.share / most * 100) + "%" } })), (row.share * 100).toFixed(1));
			return h("td" + (c.cls ? "." + c.cls : ""), null, c.cell(row));
		})));
		replace(this.table, h("thead", null, head), h("tbody", null, body),
			total > this.rows.length ? h("tfoot", null, h("tr", null, h("td.muted", { colspan: String(columns.length) }, "Showing " + this.rows.length + " of " + total + " rows"))) : null);
	}

	exportCsv() {
		const columns = this.columns();
		const lines = [columns.map((c) => csvCell(c.title || "Group")).join(",")];
		for (const row of this.rows) lines.push(columns.map((c) => csvCell(c.key === "share" ? (row.share * 100).toFixed(2) : c.cell(row))).join(","));
		download("statistics.csv", lines.join("\n") + "\n");
	}
}

export class MessagesPanel {
	constructor({ request, onJump }) {
		this.request = request;
		this.onJump = onJump;
		this.severity = 0;
		this.text = "";
		this.list = h("div.prof-messages");
		this.countLabel = h("span.muted");
		this.severityPick = h("select.input-field.prof-select", { "aria-label": "Least severity", on: { change: () => { this.severity = Number(this.severityPick.value); this.load(); } } },
			SEVERITIES.map((name, i) => h("option", { value: String(i) }, i === 0 ? "All severities" : name + " and above")));
		this.filterField = h("input.input-field.prof-filter", { type: "search", placeholder: "Filter messages", "aria-label": "Filter messages", on: { input: () => {
			clearTimeout(this.timer);
			this.timer = setTimeout(() => { this.text = this.filterField.value.trim(); this.load(); }, 200);
		} } });
		this.el = h("div.prof-panel", null,
			h("div.panel-tools", null, h("span.muted", null, "Messages"), this.countLabel, h("span.grow"), this.severityPick, this.filterField),
			h("div.scroll", null, this.list));
	}

	// The whole capture's messages, or the selection's: a log reads better than the slice the view shows.
	update(captureId, scope) {
		this.captureId = captureId;
		this.scope = scope && scope.label === "selection" ? scope : null;
		this.load();
	}

	async load() {
		if (!this.captureId) return;
		const asked = { captureId: this.captureId, severity: this.severity, text: this.text, limit: 1000 };
		if (this.scope && this.scope.from !== undefined) Object.assign(asked, { from: Math.round(this.scope.from), to: Math.round(this.scope.to) });
		const answer = await this.request("prof.messages", asked);
		if (!answer) return;
		this.countLabel.textContent = count(answer.total) + " messages \u00b7 " + scopeLabel(this.scope).replace("Range: ", "");
		replace(this.list, answer.messages.map((m) => h("div.row.prof-message" + (m.severity >= 4 ? ".c-error" : m.severity >= 3 ? ".c-warn" : ""), {
			on: { click: () => this.onJump(m.timeNs) },
		}, h("span.meta.mono", null, duration(m.timeNs)), h("span.meta", null, SEVERITIES[m.severity] || ""), h("span.meta", null, m.thread), h("span.grow.ellip", null, m.text))));
	}
}
