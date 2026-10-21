// graphlist.js - the commit graph as a virtualized list (mockup 05): a row per commit - its lanes, labels, subject,
// author, date and short hash - with a "Local changes" row on top while the work tree differs, only the rows in view
// built. A search result lists its commits without lanes (they do not connect).

import { h, icon, replace } from "/ui-kit/dom.js";
import { layoutGraph, rowSvg } from "./graph.js";
import { shortDate } from "./model.js";

const LABEL_ICONS = { head: "git-branch", branch: "git-branch", remote: "cloud", tag: "tag", worktree: "worktree" };
const OVERSCAN = 8;
const LOCAL = "local";

function rowHeight() {
	return window.matchMedia("(max-width: 719px)").matches ? 64 : 24;
}

export class GraphList {
	// events: {select(id), more(), context(event, commit)}
	constructor(events) {
		this.events = events;
		this.commits = [];
		this.layout = { rows: [], width: 1 };
		this.labels = new Map();
		this.local = 0;
		this.head = null;
		this.selected = null;
		this.searching = false;
		this.more = false;
		this.spacer = h("div.git-graph-spacer");
		this.rows = h("div.git-graph-rows");
		this.body = h("div.git-graph-body", { tabindex: "0", role: "listbox", "aria-label": "Commits", on: { scroll: () => this.paint(), keydown: (e) => this.key(e) } }, this.spacer, this.rows);
		this.el = h("div.git-graph", null,
			h("div.git-graph-head", null, h("div", null, "Graph"), h("div", null, "Description"), h("div", null, "Author"), h("div", null, "Date"), h("div", null, "Commit")),
			this.body);
		new ResizeObserver(() => this.paint()).observe(this.body);
	}

	// commits: the loaded ones, newest first; local: how many paths changed (0: no local row).
	set({ commits, labels, local, head, more, searching }) {
		this.commits = commits;
		this.labels = labels;
		this.local = local;
		this.head = head;
		this.more = more;
		this.searching = searching;
		const items = this.items();
		this.layout = searching ? { rows: items.map(() => ({ lane: 0, color: 0, pass: [], in: [], out: [] })), width: 1 } : layoutGraph(items.map((c) => ({ oid: c.oid, parents: c.parents })));
		this.paint();
	}

	items() {
		const items = this.local && !this.searching && this.head && this.head.oid ? [{ oid: LOCAL, parents: [this.head.oid], local: true }] : [];
		return items.concat(this.commits);
	}

	select(id) {
		this.selected = id;
		this.paint();
	}

	paint() {
		const items = this.items();
		const height = rowHeight();
		this.spacer.style.height = items.length * height + "px";
		const top = this.body.scrollTop;
		const first = Math.max(0, Math.floor(top / height) - OVERSCAN);
		const last = Math.min(items.length, Math.ceil((top + this.body.clientHeight) / height) + OVERSCAN);
		this.rows.style.transform = "translateY(" + first * height + "px)";
		const lanes = Math.min(this.layout.width, 12);
		this.el.style.setProperty("--git-graph-w", Math.max(40, 12 * 2 + (lanes - 1) * 14) + "px");
		const built = [];
		for (let i = first; i < last; i++) built.push(this.row(items[i], this.layout.rows[i], height, lanes));
		replace(this.rows, built);
		if (this.more && last >= items.length - OVERSCAN) this.events.more();
	}

	row(commit, layout, height, lanes) {
		const id = commit.local ? LOCAL : commit.oid;
		const svg = layout ? rowSvg(layout, { rowHeight: height, laneWidth: 14, x0: 12, lanes, head: this.head && commit.oid === this.head.oid, local: Boolean(commit.local), merge: (commit.parents || []).length > 1 }) : null;
		const props = {
			role: "option", "aria-selected": String(this.selected === id), dataset: { oid: id },
			on: { click: () => this.events.select(id), contextmenu: (e) => { e.preventDefault(); if (!commit.local) this.events.context(e, commit); } },
		};
		if (commit.local) {
			return h("div.git-row.local" + (this.selected === id ? ".sel" : ""), props,
				h("div.g", null, svg),
				h("div.desc", null, h("span.ref.local-ref", null, icon("edit"), this.local + (this.local === 1 ? " changed file" : " changed files")), h("span.msg.muted", null, "Local changes - not committed")),
				h("div.au"), h("div.dt"), h("div.hs"));
		}
		const labels = (this.labels.get(commit.oid) || []).map((l) => h("span.ref." + l.kind + ".l" + (layout ? layout.color : 0), { title: l.name }, icon(LABEL_ICONS[l.kind]), l.name));
		const date = shortDate(commit.authorMs);
		return h("div.git-row" + (this.selected === id ? ".sel" : ""), props,
			h("div.g", null, svg),
			h("div.desc", null, labels, h("span.msg", null, commit.subject), h("span.sub2", null, commit.author + " \u00b7 " + date + " \u00b7 " + commit.oid.slice(0, 7))),
			h("div.au", { title: commit.email }, commit.author),
			h("div.dt", null, date),
			h("div.hs", null, commit.oid.slice(0, 7)));
	}

	// Up and Down move the selection; Home and End jump.
	key(event) {
		const items = this.items();
		const ids = items.map((c) => (c.local ? LOCAL : c.oid));
		let at = ids.indexOf(this.selected);
		if (event.key === "ArrowDown") at = Math.min(ids.length - 1, at + 1);
		else if (event.key === "ArrowUp") at = Math.max(0, at - 1);
		else if (event.key === "Home") at = 0;
		else if (event.key === "End") at = ids.length - 1;
		else return;
		event.preventDefault();
		if (at < 0) return;
		this.events.select(ids[at]);
		const height = rowHeight();
		const top = at * height;
		if (top < this.body.scrollTop) this.body.scrollTop = top;
		else if (top + height > this.body.scrollTop + this.body.clientHeight) this.body.scrollTop = top + height - this.body.clientHeight;
	}
}
