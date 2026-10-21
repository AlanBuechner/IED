// model.js - what the Git activity knows (docs/10 "Git"): the workspace's repositories (gitrepos:<wsId>) and the shown
// one's head, changes, refs and running operation (git:<repoId>), applied as the events come; and the pure helpers the
// views share - a commit's labels, dates, status letters, counts.

const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
const OP_LINES = 20;

export class ReposModel {
	constructor() {
		this.repos = [];
		this.error = "";
		this.discovering = false;
	}

	snapshot(data) {
		this.repos = data.repos || [];
		this.error = data.error || "";
		this.discovering = Boolean(data.discovering);
	}

	event(kind, data) {
		if (kind === "repos.changed") this.snapshot(data);
	}
}

export class RepoModel {
	constructor() {
		this.head = null;
		this.changes = null;
		this.refs = null;
		this.op = null;
		this.lastOp = null;
	}

	snapshot(data) {
		this.head = data.head;
		this.changes = data.changes;
		this.refs = data.refs;
		this.op = data.op;
	}

	event(kind, data) {
		if (kind === "status.changed") {
			this.head = data.head;
			this.changes = data.changes;
		}
		else if (kind === "refs.changed") this.refs = data.refs;
		else if (kind === "op.progress") {
			if (!this.op || this.op.opId !== data.opId) this.op = { opId: data.opId, kind: data.kind, title: data.line, lines: [] };
			this.op.lines.push(data.line);
			if (this.op.lines.length > OP_LINES) this.op.lines.shift();
			if (data.percent !== undefined) this.op.percent = data.percent;
		}
		else if (kind === "op.end") {
			this.op = null;
			this.lastOp = data;
		}
	}

	// The paths with local changes, each once.
	localCount() {
		if (!this.changes) return 0;
		const paths = new Set();
		for (const list of ["staged", "unstaged", "untracked", "conflicted"]) for (const change of this.changes[list] || []) paths.add(change.path);
		return paths.size;
	}

	currentBranch() {
		return this.refs ? (this.refs.branches || []).find((b) => b.current) || null : null;
	}
}

// oid -> the labels its row shows, HEAD's branch first: [{kind: head|branch|remote|tag|worktree, name}].
export function labelsByOid(refs, head) {
	const labels = new Map();
	const add = (oid, label) => {
		if (!oid) return;
		if (!labels.has(oid)) labels.set(oid, []);
		labels.get(oid).push(label);
	};
	if (!refs) return labels;
	if (head && head.detached && head.oid) add(head.oid, { kind: "head", name: "HEAD" });
	const elsewhere = new Set((refs.worktrees || []).filter((w) => !w.current && w.branch).map((w) => w.branch));
	for (const branch of refs.branches || []) {
		add(branch.oid, { kind: branch.current ? "head" : "branch", name: branch.name });
		if (elsewhere.has(branch.name)) add(branch.oid, { kind: "worktree", name: "worktree" });
	}
	for (const remote of refs.remotes || []) for (const branch of remote.branches || []) add(branch.oid, { kind: "remote", name: remote.name + "/" + branch.name });
	for (const tag of refs.tags || []) add(tag.oid, { kind: "tag", name: tag.name });
	for (const list of labels.values()) list.sort((a, b) => Number(b.kind === "head") - Number(a.kind === "head"));
	return labels;
}

function pad(n) {
	return n < 10 ? "0" + n : String(n);
}

// "21:33" today, "Yesterday 18:05", "Oct 17" this year, "Oct 17, 2025" before.
export function shortDate(ms, now = Date.now()) {
	const when = new Date(ms);
	const today = new Date(now);
	const start = new Date(today.getFullYear(), today.getMonth(), today.getDate()).getTime();
	const time = pad(when.getHours()) + ":" + pad(when.getMinutes());
	if (ms >= start) return time;
	if (ms >= start - 86400000) return "Yesterday " + time;
	const day = MONTHS[when.getMonth()] + " " + when.getDate();
	return when.getFullYear() === today.getFullYear() ? day : day + ", " + when.getFullYear();
}

// "Oct 18, 16:42" - a commit's full date in its details.
export function longDate(ms) {
	const when = new Date(ms);
	return MONTHS[when.getMonth()] + " " + when.getDate() + ", " + when.getFullYear() + ", " + pad(when.getHours()) + ":" + pad(when.getMinutes());
}

// The class that colors a status letter: added green, deleted red, the rest plain.
export function statusClass(status) {
	if (status === "A" || status === "?") return "st-a";
	if (status === "D") return "st-d";
	if (status.length === 2) return "st-u";
	return "st-m";
}

// What a status letter means, for its tooltip.
export function statusTitle(status) {
	return { A: "Added", M: "Modified", D: "Deleted", R: "Renamed", C: "Copied", T: "Type changed", "?": "Untracked" }[status] || "Conflict (" + status + ")";
}

// The ahead/behind text of a branch: "3 to push", "2 to pull".
export function syncText(head) {
	if (!head) return "";
	const parts = [];
	if (head.ahead) parts.push(head.ahead + " to push");
	if (head.behind) parts.push(head.behind + " to pull");
	return parts.join(", ");
}

// The first line of a message, for a list.
export function firstLine(text) {
	const end = text.indexOf("\n");
	return end < 0 ? text : text.slice(0, end);
}
