// refspane.js - the refs pane (Fork's left side, mockup 05): local branches with ahead/behind, remotes and their
// branches, tags, stashes, worktrees and submodules, each group folding, a filter on top. A click shows the ref's
// commit in the graph; a double click checks a branch out; the context menu has what can be done with it.

import { h, icon, replace } from "/ui-kit/dom.js";

const GROUPS = ["branches", "remotes", "tags", "stashes", "worktrees", "submodules"];
const TITLES = { branches: "Local branches", remotes: "Remotes", tags: "Tags", stashes: "Stashes", worktrees: "Worktrees", submodules: "Submodules" };

export class RefsPane {
	// events: {reveal(oid), checkout(branch, remote), menu(event, ref)}; folded: the groups shown closed
	constructor(events, folded) {
		this.events = events;
		this.folded = new Set(folded || ["submodules"]);
		this.refs = null;
		this.filter = h("input.input-field", { type: "search", placeholder: "Filter refs", "aria-label": "Filter refs", on: { input: () => this.render() } });
		this.list = h("div.git-refs-list", { role: "tree", "aria-label": "Refs" });
		this.el = h("aside.git-refs", null, h("div.git-refs-filter", null, this.filter), this.list);
	}

	show(refs) {
		this.refs = refs;
		this.render();
	}

	render() {
		if (!this.refs) return replace(this.list);
		const query = this.filter.value.trim().toLowerCase();
		const keep = (text) => !query || text.toLowerCase().includes(query);
		const sections = [];
		for (const group of GROUPS) {
			const rows = this.rows(group, keep);
			const count = group === "remotes" ? (this.refs.remotes || []).length : (this.refs[group] || []).length;
			const open = !this.folded.has(group) || Boolean(query);
			sections.push(h("div.group-head", {
				role: "treeitem", "aria-expanded": String(open), dataset: { group },
				on: { click: () => this.toggle(group) },
			}, icon(open ? "chevron-down" : "chevron-right", "sm"), TITLES[group], h("span.meta", null, String(count))));
			if (open) sections.push(rows);
		}
		replace(this.list, sections);
	}

	toggle(group) {
		if (this.folded.has(group)) this.folded.delete(group);
		else this.folded.add(group);
		this.render();
	}

	row(depth, glyph, name, meta, ref, extra) {
		return h("div.row" + (ref.current ? ".sel-dim" : ""), {
			role: "treeitem", style: { "--d": String(depth) }, title: ref.title || name, dataset: { ref: ref.kind + ":" + (ref.full || name) },
			on: {
				click: () => ref.oid && this.events.reveal(ref.oid),
				dblclick: () => (ref.kind === "branch" || ref.kind === "remote") && this.events.checkout(ref.full || name, ref.kind === "remote"),
				contextmenu: (e) => {
					e.preventDefault();
					this.events.menu(e, ref);
				},
			},
		}, icon(glyph), h("span.name", null, ref.current ? h("b", null, name) : name), meta, extra);
	}

	rows(group, keep) {
		const refs = this.refs;
		if (group === "branches") {
			return (refs.branches || []).filter((b) => keep(b.name)).map((b) => this.row(1, b.current ? "check" : "git-branch", b.name, aheadBehind(b), { kind: "branch", oid: b.oid, current: b.current, full: b.name, upstream: b.upstream }));
		}
		if (group === "remotes") {
			const rows = [];
			for (const remote of refs.remotes || []) {
				const branches = (remote.branches || []).filter((b) => keep(remote.name + "/" + b.name));
				if (!branches.length && !keep(remote.name)) continue;
				rows.push(this.row(1, "cloud", remote.name, null, { kind: "remoteName", title: remote.url, full: remote.name }));
				for (const b of branches) rows.push(this.row(2, "git-branch", b.name, null, { kind: "remote", oid: b.oid, full: remote.name + "/" + b.name }));
			}
			return rows;
		}
		if (group === "tags") return (refs.tags || []).filter((t) => keep(t.name)).map((t) => this.row(1, "tag", t.name, null, { kind: "tag", oid: t.oid }));
		if (group === "stashes") return (refs.stashes || []).filter((s) => keep(s.message)).map((s) => this.row(1, "git-stash", s.message, null, { kind: "stash", oid: s.oid, index: s.index, full: "stash@{" + s.index + "}" }));
		if (group === "worktrees") return (refs.worktrees || []).filter((w) => keep(w.path)).map((w) => this.row(1, w.current ? "root-folder" : "worktree", w.path, h("span.meta", null, w.branch || "detached"), { kind: "worktree", oid: w.oid, current: w.current, full: w.path }));
		return (refs.submodules || []).filter((s) => keep(s.path)).map((s) => this.row(1, "repo", s.path, null, { kind: "submodule", full: s.path }));
	}
}

function aheadBehind(branch) {
	if (branch.gone) return h("span.meta.ab", { title: "Its upstream is gone" }, "gone");
	if (!branch.ahead && !branch.behind) return null;
	return h("span.meta.ab", { title: branch.ahead + " to push, " + branch.behind + " to pull" },
		branch.ahead ? [icon("arrow-up"), String(branch.ahead)] : null,
		branch.behind ? [icon("arrow-down"), String(branch.behind)] : null);
}
