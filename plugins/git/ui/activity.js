// activity.js - the Git activity (docs/05 "git", mockup 05): the toolbar (repository, Fetch, Pull, Push, Branch,
// Stash, search), the running operation, the refs pane, the commit graph, and below it the chosen commit's details or
// the local changes with the composer, beside the chosen file's diff. The server owns the repository and its state
// (gitrepos:<wsId>, git:<repoId>); which repository, row and diff layout are shown is this device's (localStorage).

import { h, icon, replace } from "/ui-kit/dom.js";
import { MenuHost } from "/ui-kit/menu.js";
import { confirmDialog, promptDialog } from "/ui-kit/dialog.js";
import { RepoModel, ReposModel, labelsByOid } from "./model.js";
import { GraphList } from "./graphlist.js";
import { RefsPane } from "./refspane.js";
import { ChangesPane, CommitDetails } from "./details.js";
import { DiffPane } from "./diffpane.js";

const menus = new MenuHost();
const PAGE = 200;
const LOCAL = "local";

function stored(key, fallback) {
	try {
		const value = localStorage.getItem(key);
		return value === null ? fallback : JSON.parse(value);
	}
	catch {
		return fallback;
	}
}

function store(key, value) {
	try {
		localStorage.setItem(key, JSON.stringify(value));
	}
	catch {
		// a private window: the choice lasts as long as the page
	}
}

function item(id, label) {
	return { kind: "item", id, label, enabled: true };
}

// Why a branch name cannot be one, or "".
export function branchProblem(name) {
	if (/\s/.test(name)) return "No spaces in a branch name";
	if (name.startsWith("-") || name.startsWith("/") || name.endsWith("/") || name.endsWith(".lock") || name.includes("..") || /[~^:?*[\\]/.test(name)) return "Git does not take that name";
	return "";
}

export class GitActivityView {
	constructor(ctx) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.wsId = ctx.wsId;
		this.repos = new ReposModel();
		this.model = new RepoModel();
		this.repoId = null;
		this.stopRepo = null;
		this.commits = [];
		this.more = false;
		this.query = "";
		this.loadedFor = "";
		this.selected = stored("ied.git.selected:" + this.wsId, LOCAL);
		this.graph = new GraphList({ select: (id) => this.select(id), more: () => this.loadMore(), context: (e, c) => this.commitMenu(e, c) });
		this.refsPane = new RefsPane({ reveal: (oid) => this.reveal(oid), checkout: (name, remote) => this.checkout(name, remote), menu: (e, ref) => this.refMenu(e, ref) }, stored("ied.git.folded", null));
		this.details = new CommitDetails({ pick: (file) => this.diff.showCommit(this.repoId, this.details.commit.oid, file), reveal: (oid) => this.reveal(oid), copy: (text) => this.copy(text), fileIcon: (path) => this.ied.icons.file(path) });
		this.changes = new ChangesPane({
			pick: (change, staged) => this.showChange(change, staged),
			stage: (paths) => this.call("git.stage", { paths }),
			unstage: (paths) => this.call("git.unstage", { paths }),
			discard: (paths) => this.discard(paths),
			commit: (message, amend) => this.commit(message, amend),
			lastMessage: async () => ((await this.call("git.lastMessage", {})) || { message: "" }).message,
			draft: (text) => store("ied.git.draft:" + this.wsId, text),
			fileIcon: (path) => this.ied.icons.file(path),
		}, stored("ied.git.draft:" + this.wsId, ""));
		this.diff = new DiffPane(this.ied, { open: (path) => this.open(path), mode: (mode) => store("ied.git.diffMode", mode) }, stored("ied.git.diffMode", "inline"));
		this.build();
		this.stopRepos = this.ied.client.subscribe("gitrepos:" + this.wsId, {
			snap: (data) => this.reposChanged(data),
			evt: (kind, data) => this.reposChanged(data),
		});
	}

	dispose() {
		if (this.stopRepos) this.stopRepos();
		if (this.stopRepo) this.stopRepo();
	}

	// --- Layout ---------------------------------------------------------------------------------------------------------

	build() {
		const button = (glyph, text, title, run) => h("button.btn.flat", { title, on: { click: run } }, icon(glyph), text);
		this.repoButton = h("button.btn.flat.git-repo", { title: "Repository", on: { click: (e) => this.repoMenu(e) } }, icon("repo"), h("span.name"), icon("chevron-down", "sm"));
		this.pushCount = h("span.count");
		this.pullCount = h("span.count");
		this.search = h("input.input-field.git-search", { type: "search", placeholder: "Search message, author, hash", "aria-label": "Search commits", on: { input: () => this.searchSoon() } });
		this.localChip = h("button.chip.solid.phone-only", { on: { click: () => this.select(LOCAL) } });
		this.syncChip = h("button.chip.phone-only", { on: { click: () => this.push() } });
		this.moreChip = h("button.chip.phone-only", { title: "Git actions", "aria-label": "Git actions", on: { click: (e) => this.phoneMenu(e) } }, icon("ellipsis"));
		this.bar = h("div.git-bar", null,
			this.repoButton,
			h("span.tb-sep"),
			button("repo-fetch", "Fetch", "Fetch from every remote", () => this.fetch()),
			h("span.split-btn", null,
				h("button.btn.flat", { title: "Pull", on: { click: () => this.pull() } }, icon("repo-pull"), "Pull", this.pullCount),
				h("button.btn.flat", { title: "Pull options", "aria-label": "Pull options", on: { click: (e) => this.pullMenu(e) } }, icon("chevron-down", "sm"))),
			h("button.btn.flat", { title: "Push", on: { click: () => this.push() } }, icon("repo-push"), "Push", this.pushCount),
			h("span.tb-sep"),
			button("git-branch", "Branch", "New branch from HEAD", () => this.newBranch(null)),
			button("git-stash", "Stash", "Stash local changes", () => this.stash()),
			h("span.grow"),
			this.search,
			h("button.ib", { title: "Look again", "aria-label": "Look again", on: { click: () => this.reload() } }, icon("refresh")),
			this.localChip, this.syncChip, h("span.grow.phone-only"), this.moreChip);
		this.op = h("div.git-op", { hidden: true, role: "status" });
		this.lower = h("div.git-details");
		this.back = h("button.btn.flat.phone-only.git-back", { on: { click: () => this.root.classList.remove("detail-open") } }, icon("arrow-left"), "Commits");
		this.main = h("div.git-main", null, this.graph.el, h("div.git-lower", null, this.back, this.lower));
		this.message = h("div.git-message-area", { hidden: true });
		this.root = h("div.git-act", null, this.bar, this.op, h("div.git-body", null, this.refsPane.el, this.main), this.message);
		replace(this.ctx.el, this.root);
	}

	// --- Repositories ---------------------------------------------------------------------------------------------------

	reposChanged(data) {
		this.repos.snapshot(data);
		const repos = this.repos.repos;
		if (!repos.length) {
			this.showRepo(null);
			const why = this.repos.error === "untrusted" ? "Git does not run in an untrusted workspace: a repository's hooks and settings are programs. Trust the workspace to use it."
				: this.repos.error ? "Git cannot run: " + this.repos.error
					: this.repos.discovering ? "Looking for repositories..." : "No repository in this workspace.";
			return this.showMessage(why);
		}
		const wanted = stored("ied.git.repo:" + this.wsId, "");
		const repo = repos.find((r) => r.root === wanted) || repos[0];
		if (repo.repoId !== this.repoId) this.showRepo(repo);
		this.paintRepoButton();
	}

	showMessage(text) {
		this.message.hidden = !text;
		this.message.textContent = text || "";
		this.root.classList.toggle("empty", Boolean(text));
	}

	showRepo(repo) {
		if (this.stopRepo) this.stopRepo();
		this.stopRepo = null;
		this.repoId = repo ? repo.repoId : null;
		this.model = new RepoModel();
		this.commits = [];
		this.loadedFor = "";
		this.shown = false;
		if (!repo) return this.paint();
		this.showMessage("");
		store("ied.git.repo:" + this.wsId, repo.root);
		this.stopRepo = this.ied.client.subscribe("git:" + repo.repoId, {
			snap: (data) => {
				this.model.snapshot(data);
				this.paint();
			},
			evt: (kind, data) => {
				this.model.event(kind, data);
				if (kind === "op.end") this.opEnded(data);
				this.paint();
			},
		});
	}

	paintRepoButton() {
		const repo = this.repos.repos.find((r) => r.repoId === this.repoId);
		this.repoButton.querySelector(".name").textContent = repo ? repo.name + (repo.root && repo.root !== ".." ? " (" + repo.root + ")" : "") : "";
	}

	repoMenu(event) {
		const rows = this.repos.repos.map((r) => Object.assign(item(r.repoId, r.name + (r.root && r.root !== ".." ? "  " + r.root : "")), { icon: r.repoId === this.repoId ? "$(check)" : null }));
		menus.show({ rows, at: event.currentTarget, pick: (row) => this.showRepo(this.repos.repos.find((r) => r.repoId === row.id)) });
	}

	// --- Painting -------------------------------------------------------------------------------------------------------

	paint() {
		const m = this.model;
		const head = m.head;
		const local = m.localCount();
		this.pushCount.textContent = head && head.ahead ? String(head.ahead) : "";
		this.pullCount.textContent = head && head.behind ? String(head.behind) : "";
		this.localChip.hidden = !local;
		replace(this.localChip, icon("edit"), "Local changes " + local);
		this.syncChip.hidden = !(head && head.ahead);
		replace(this.syncChip, icon("repo-push"), (head && head.ahead) + " to push");
		this.paintOp();
		this.refsPane.show(m.refs);
		const key = head ? head.oid + "|" + JSON.stringify(m.refs ? m.refs.branches.map((b) => b.oid) : []) + "|" + JSON.stringify(m.refs ? m.refs.remotes : []) : "";
		if (head && key !== this.loadedFor && !this.query) {
			this.loadedFor = key;
			this.load(false);
		}
		this.paintGraph();
		if (head && !this.shown) this.select(this.selected || LOCAL, false);
		else if (this.selected === LOCAL) this.changes.show(m.changes);
	}

	paintGraph() {
		const m = this.model;
		this.graph.set({ commits: this.commits, labels: labelsByOid(m.refs, m.head), local: m.localCount(), head: m.head, more: this.more, searching: Boolean(this.query) });
		this.graph.select(this.selected);
	}

	paintOp() {
		const op = this.model.op;
		this.op.hidden = !op;
		if (!op) return;
		const last = op.lines.length ? op.lines[op.lines.length - 1] : "";
		replace(this.op, icon("sync", "spin"), h("span.git-op-title", null, op.title), h("span.muted.git-op-line", null, last),
			op.percent !== undefined ? h("span.git-op-bar", null, h("span", { style: { width: op.percent + "%" } })) : null);
	}

	opEnded(data) {
		if (data.ok) this.ied.toast(data.kind.charAt(0).toUpperCase() + data.kind.slice(1) + " done.", { level: "info" });
		else this.ied.toast(data.kind.charAt(0).toUpperCase() + data.kind.slice(1) + " failed: " + data.error, { level: "error" });
	}

	// --- Commits --------------------------------------------------------------------------------------------------------

	async load(append) {
		if (!this.repoId) return;
		const repoId = this.repoId;
		const skip = append ? this.commits.length : 0;
		const limit = append ? PAGE : Math.max(PAGE, Math.min(2000, this.commits.length));
		try {
			const page = await this.ied.client.request("git.log", { repoId, skip, limit, query: this.query || undefined });
			if (repoId !== this.repoId) return;
			this.commits = append ? this.commits.concat(page.commits) : page.commits;
			this.more = page.more;
			this.paintGraph();
			if (this.selected === LOCAL && !this.model.localCount() && !this.query && this.commits.length) this.select(this.commits[0].oid, false);
		}
		catch (e) {
			this.ied.toast(e.message, { level: "error" });
		}
		finally {
			this.loading = false;
		}
	}

	loadMore() {
		if (this.loading || !this.more) return;
		this.loading = true;
		this.load(true);
	}

	reload() {
		this.loadedFor = "";
		this.paint();
	}

	searchSoon() {
		clearTimeout(this.searchTimer);
		this.searchTimer = setTimeout(() => {
			this.query = this.search.value.trim();
			this.commits = [];
			this.load(false);
		}, 250);
	}

	// Shows `id` ("local" or a commit) below the graph; `open` (a person's choice) brings it over the graph on a phone.
	async select(id, open = true) {
		this.selected = id;
		this.shown = true;
		store("ied.git.selected:" + this.wsId, id);
		this.graph.select(id);
		if (open) this.root.classList.add("detail-open");
		if (id === LOCAL) {
			replace(this.lower, this.changes.el, this.diff.el);
			this.details.commit = null;
			this.changes.show(this.model.changes);
			this.diff.clear("Pick a file to see its changes");
			return;
		}
		replace(this.lower, this.details.el, this.diff.el);
		this.details.clear();
		this.diff.clear("");
		try {
			const commit = await this.ied.client.request("git.show", { repoId: this.repoId, oid: id });
			if (this.selected !== id) return;
			this.details.show(commit, labelsByOid(this.model.refs, this.model.head).get(commit.oid));
			if (!commit.files.length) this.diff.clear("No files changed");
		}
		catch (e) {
			if (e.code === "not_found" && this.model.head && this.model.head.oid && id !== this.model.head.oid) return this.select(this.model.head.oid, false);
			this.diff.clear(e.message);
		}
	}

	// Shows `oid` in the graph (loading pages until it is there) and selects it.
	async reveal(oid) {
		for (let i = 0; i < 10 && !this.commits.some((c) => c.oid === oid) && this.more; i++) await this.load(true);
		const at = this.commits.findIndex((c) => c.oid === oid);
		if (at < 0) return this.ied.toast("That commit is not in the graph.", { level: "warn" });
		this.select(oid);
		const height = this.graph.body.querySelector(".git-row") ? this.graph.body.querySelector(".git-row").offsetHeight : 24;
		const row = at + (this.model.localCount() && !this.query ? 1 : 0);
		this.graph.body.scrollTop = Math.max(0, row * height - this.graph.body.clientHeight / 3);
	}

	// --- Local changes --------------------------------------------------------------------------------------------------

	showChange(change, staged) {
		const path = change.path;
		const hunkCall = (method, extra) => async (hunk, lines) => {
			const diff = this.diff.last && this.diff.last.diff;
			if (!diff || !diff.hunks[hunk]) return;
			const ok = await this.call(method, Object.assign({ path, hunk, header: diff.hunks[hunk].header, lines: lines || undefined }, extra));
			if (ok !== null) this.diff.showLocal(this.repoId, change, staged, actions);
		};
		const actions = {
			stage: hunkCall("git.stagePatch", { staged: false }),
			unstage: hunkCall("git.stagePatch", { staged: true }),
			discard: async (hunk) => {
				if (!await confirmDialog({ title: "Discard", message: "This hunk's changes to " + path + " will be lost.", confirm: "Discard", danger: true })) return;
				await hunkCall("git.discardPatch", {})(hunk, null);
			},
			stageFile: () => this.call("git.stage", { paths: [path] }),
			discardFile: () => this.discard([path]),
		};
		this.diff.showLocal(this.repoId, change, staged, actions);
	}

	async discard(paths) {
		const what = paths.length === 1 ? paths[0] : paths.length + " files";
		if (!await confirmDialog({ title: "Discard changes", message: "The local changes to " + what + " will be lost (an untracked file goes to the trash).", confirm: "Discard", danger: true })) return;
		await this.call("git.discard", { paths });
		this.diff.clear("");
	}

	async commit(message, amend) {
		const made = await this.call("git.commit", { message, amend });
		if (!made) return false;
		this.ied.toast("Committed " + made.oid.slice(0, 7) + ".", { level: "info" });
		return true;
	}

	// --- Toolbar --------------------------------------------------------------------------------------------------------

	fetch() {
		return this.call("git.fetch", { prune: true });
	}

	pull(mode) {
		return this.call("git.pull", { mode });
	}

	pullMenu(event) {
		const rows = [item("merge", "Pull (merge)"), item("rebase", "Pull (rebase)"), item("ffOnly", "Pull (fast-forward only)")];
		menus.show({ rows, at: event.currentTarget, pick: (row) => this.pull(row.id) });
	}

	async push() {
		const branch = this.model.currentBranch();
		if (!branch) return this.ied.toast("Nothing to push from a detached HEAD.", { level: "warn" });
		if (branch.upstream) return this.call("git.push", {});
		const remote = this.model.refs && this.model.refs.remotes.length ? this.model.refs.remotes[0].name : null;
		if (!remote) return this.ied.toast("This repository has no remote to push to.", { level: "warn" });
		if (!await confirmDialog({ title: "Push", message: branch.name + " has no upstream: push it to " + remote + "/" + branch.name + " and track it?", confirm: "Push" })) return;
		return this.call("git.push", { remote, branch: branch.name, setUpstream: true });
	}

	async newBranch(from) {
		const name = await promptDialog({ title: "New branch", message: from ? "From " + from.slice(0, 7) : "From HEAD", placeholder: "feature/name", confirm: "Create and check out", check: branchProblem });
		if (name) await this.call("git.branch", { name, from: from || undefined, checkout: true });
	}

	async stash() {
		if (!this.model.localCount()) return this.ied.toast("No local changes to stash.", { level: "info" });
		const message = await promptDialog({ title: "Stash local changes", message: "Untracked files are stashed too.", placeholder: "What they are", confirm: "Stash", check: () => "" });
		if (message !== null) await this.call("git.stash", { message, untracked: true });
	}

	async checkout(name, remote) {
		try {
			await this.ied.client.request("git.checkout", { repoId: this.repoId, branch: name, remote: remote || undefined });
		}
		catch (e) {
			if (e.code !== "conflict" || !e.data || !e.data.files) return this.ied.toast(e.message, { level: "error" });
			const files = e.data.files;
			if (!await confirmDialog({ title: "Checkout " + name, message: "Local changes to " + files.join(", ") + " would be overwritten. Stash them first, then check out?", confirm: "Stash and check out" })) return;
			if (await this.call("git.stash", { message: "Before checking out " + name, untracked: true }) !== null) await this.call("git.checkout", { branch: name, remote: remote || undefined });
		}
	}

	// --- Menus ----------------------------------------------------------------------------------------------------------

	refMenu(event, ref) {
		const rows = [];
		const actions = {};
		const add = (id, label, run) => {
			rows.push(item(id, label));
			actions[id] = run;
		};
		if (ref.kind === "branch") {
			if (!ref.current) add("checkout", "Checkout", () => this.checkout(ref.full, false));
			add("branch", "New branch from here...", () => this.newBranch(ref.full));
			add("rename", "Rename...", async () => {
				const to = await promptDialog({ title: "Rename branch", value: ref.full, confirm: "Rename", check: branchProblem });
				if (to && to !== ref.full) await this.call("git.branchRename", { name: ref.full, to });
			});
			if (!ref.current) add("delete", "Delete", () => this.deleteBranch(ref.full));
		}
		else if (ref.kind === "remote") add("checkout", "Checkout (track it)", () => this.checkout(ref.full, true));
		else if (ref.kind === "stash") {
			add("apply", "Apply", () => this.call("git.stashApply", { index: ref.index }));
			add("pop", "Pop", () => this.call("git.stashApply", { index: ref.index, pop: true }));
			add("drop", "Drop", async () => {
				if (await confirmDialog({ title: "Drop stash", message: "The stash's changes will be lost.", confirm: "Drop", danger: true })) await this.call("git.stashDrop", { index: ref.index });
			});
		}
		if (ref.oid) add("copy", "Copy hash", () => this.copy(ref.oid));
		if (rows.length) menus.show({ rows, at: event, pick: (row) => actions[row.id]() });
	}

	phoneMenu(event) {
		const actions = { fetch: () => this.fetch(), pull: () => this.pull(), push: () => this.push(), branch: () => this.newBranch(null), stash: () => this.stash() };
		const rows = [item("fetch", "Fetch"), item("pull", "Pull"), item("push", "Push"), item("branch", "New branch..."), item("stash", "Stash local changes...")];
		menus.show({ rows, at: event.currentTarget, pick: (row) => actions[row.id]() });
	}

	commitMenu(event, commit) {
		const actions = { copy: () => this.copy(commit.oid), branch: () => this.newBranch(commit.oid) };
		menus.show({ rows: [item("branch", "New branch here..."), item("copy", "Copy hash")], at: event, pick: (row) => actions[row.id]() });
	}

	async deleteBranch(name) {
		try {
			await this.ied.client.request("git.branchDelete", { repoId: this.repoId, name });
		}
		catch (e) {
			if (e.code !== "conflict") return this.ied.toast(e.message, { level: "error" });
			if (await confirmDialog({ title: "Delete " + name, message: name + " is not merged: its commits will only be reachable from the reflog.", confirm: "Delete anyway", danger: true })) await this.call("git.branchDelete", { name, force: true });
		}
	}

	// --- Helpers --------------------------------------------------------------------------------------------------------

	// A git method on the shown repository; its answer, or null after saying what went wrong.
	async call(method, params) {
		if (!this.repoId) return null;
		try {
			return await this.ied.client.request(method, Object.assign({ repoId: this.repoId }, params));
		}
		catch (e) {
			this.ied.toast(e.message, { level: e.code === "busy" ? "warn" : "error" });
			return null;
		}
	}

	copy(text) {
		navigator.clipboard.writeText(text).then(() => this.ied.toast("Copied " + text.slice(0, 12) + ".", { level: "info" }), () => this.ied.toast("The clipboard is not available here.", { level: "warn" }));
	}

	open(path) {
		const repo = this.repos.repos.find((r) => r.repoId === this.repoId);
		if (!repo || repo.root === "..") return this.ied.toast("That file is outside the workspace folder.", { level: "warn" });
		this.ied.commands.run("ied.editor.goTo", { path: repo.root ? repo.root + "/" + path : path });
	}
}
