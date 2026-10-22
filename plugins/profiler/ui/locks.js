// locks.js - the Locks tool window (F-PRF-18): each lock's waits, contended holds and holds over the selection or
// the view, sorted on the server; a row picks the lock's longest wait on the timeline.

import { h, replace } from "/ui-kit/dom.js";
import { ms, count } from "./format.js";
import { scopeLabel } from "./panels.js";

const COLUMNS = [
	{ title: "Lock", order: "name", cell: (r) => r.name },
	{ title: "Source", cell: (r) => (r.file ? r.file + (r.line ? ":" + r.line : "") : ""), cls: "src" },
	{ title: "Waits", order: "wait", cell: (r) => count(r.blocked) + (r.stuck ? " (" + r.stuck + " stuck)" : "") },
	{ title: "Waited ms", order: "wait", cell: (r) => ms(r.waitNs) },
	{ title: "Max wait", order: "maxWait", cell: (r) => ms(r.maxWaitNs) },
	{ title: "Contended", order: "contended", cell: (r) => count(r.contended) + " (" + (r.contention * 100).toFixed(1) + "%)" },
	{ title: "Holds", order: "acquisitions", cell: (r) => count(r.acquisitions) },
	{ title: "Held ms", order: "hold", cell: (r) => ms(r.holdNs) },
	{ title: "Max hold", cell: (r) => ms(r.maxHoldNs) },
	{ title: "Threads", cell: (r) => count(r.threads) },
];

export class LocksPanel {
	constructor({ request, onPick }) {
		this.request = request;
		this.onPick = onPick;
		this.order = "wait";
		this.rangeLabel = h("span.muted.prof-range");
		this.table = h("table.stats.wide-stats");
		this.empty = h("div.muted.prof-empty", { hidden: true }, "No locks in this capture. A program reports its locks through its ITP client: Profile::Mutex in C4, itp::Lockable in C and C++, profiler.Mutex in Go, ied_profiler::Mutex in Rust.");
		this.el = h("div.prof-panel", null,
			h("div.panel-tools", null, h("span.muted", null, "Locks"), this.rangeLabel, h("span.grow"),
				h("span.lock-key", null, h("span", null, h("i.sw.lock-held"), "held"), h("span", null, h("i.sw.lock-contended"), "held while others waited"), h("span", null, h("i.sw.lock-blocked"), "waited"))),
			h("div.scroll.prof-table-scroll", null, this.table, this.empty));
	}

	update(captureId, scope) {
		this.captureId = captureId;
		this.scope = scope;
		this.rangeLabel.textContent = scopeLabel(scope);
		this.load();
	}

	async load() {
		if (!this.captureId) return;
		const asked = { captureId: this.captureId, order: this.order, limit: 300 };
		if (this.scope && this.scope.from !== undefined) Object.assign(asked, { from: Math.round(this.scope.from), to: Math.round(this.scope.to) });
		const token = (this.token = {});
		const answer = await this.request("prof.locks", asked);
		if (token !== this.token || !answer) return;
		this.empty.hidden = answer.locks > 0;
		this.table.hidden = answer.locks === 0;
		this.render(answer);
	}

	render(answer) {
		const head = h("tr", null, COLUMNS.map((c) => h("th" + (c.order && c.order === this.order ? ".sorted" : ""), {
			on: c.order ? { click: () => { this.order = c.order; this.load(); } } : null,
		}, c.title)));
		const body = answer.rows.map((row) => h("tr" + (row.stuck ? ".c-error" : ""), {
			title: row.worst ? "Click: the longest wait, " + row.worst.thread : row.function || row.name,
			on: { click: () => this.onPick(row) },
		}, COLUMNS.map((c) => h("td" + (c.cls ? "." + c.cls : ""), null, c.cell(row)))));
		const note = answer.rows.length === 0 && answer.locks > 0 ? h("tfoot", null, h("tr", null, h("td.muted", { colspan: String(COLUMNS.length) }, "No lock was used in this range."))) : null;
		replace(this.table, h("thead", null, head), h("tbody", null, body), note);
	}
}
