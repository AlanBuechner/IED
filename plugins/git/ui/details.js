// details.js - the lower left of the Git activity (mockup 05): a commit's details (subject, hash, author, parents, the
// branches it is on, its message and changed files), or for the "Local changes" row the work tree's changes - conflicts,
// unstaged and untracked, staged - with stage, unstage and discard per file or all, and the commit composer.

import { h, icon, replace } from "/ui-kit/dom.js";
import { firstLine, longDate, statusClass, statusTitle } from "./model.js";

function split(path) {
	const slash = path.lastIndexOf("/");
	return slash < 0 ? ["", path] : [path.slice(0, slash), path.slice(slash + 1)];
}

function fileRow(change, { selected, buttons, pick, counts, fileIcon }) {
	const [folder, name] = split(change.path);
	const status = change.status;
	return h("div.row.git-file" + (selected ? ".sel" : ""), {
		role: "option", "aria-selected": String(Boolean(selected)), title: (change.oldPath ? change.oldPath + " -> " : "") + change.path, dataset: { path: change.path },
		on: { click: pick },
	},
	h("span." + statusClass(status), { title: statusTitle(status) }, status === "?" ? "U" : status.charAt(0)),
	fileIcon ? fileIcon(change.path) : null,
	h("span.name", null, name, folder ? h("span.git-folder", null, folder) : null),
	counts,
	buttons ? h("span.git-file-actions", null, buttons) : null);
}

export class CommitDetails {
	// events: {pick(file), reveal(oid), copy(text), fileIcon(path)}
	constructor(events) {
		this.events = events;
		this.commit = null;
		this.file = null;
		this.meta = h("div.cmeta");
		this.count = h("span.count");
		this.files = h("div.cfiles", { role: "listbox", "aria-label": "Changed files" });
		this.el = h("div.git-meta-pane", null, this.meta, h("div.panel-tools", null, h("span.muted", null, "Changed files"), this.count), this.files);
	}

	// While the next commit loads.
	clear() {
		this.commit = null;
		this.file = null;
		replace(this.meta, h("div.muted", null, "Loading..."));
		this.count.textContent = "";
		replace(this.files);
	}

	show(commit, labels) {
		this.commit = commit;
		this.file = null;
		const parents = commit.parents.map((p) => h("a.mono.git-link", { href: "#", on: { click: (e) => { e.preventDefault(); this.events.reveal(p); } } }, p.slice(0, 7)));
		const on = (labels || []).filter((l) => l.kind === "head" || l.kind === "branch" || l.kind === "tag").map((l) => l.name);
		replace(this.meta,
			h("div.subject", null, commit.subject),
			h("div.kv", null,
				h("span.k", null, "Commit"), h("span.mono", null, commit.oid.slice(0, 20) + "\u2026 ", h("button.ib.sm", { title: "Copy the hash", "aria-label": "Copy the hash", on: { click: () => this.events.copy(commit.oid) } }, icon("copy"))),
				h("span.k", null, "Author"), h("span", null, commit.author + " \u00b7 " + longDate(commit.authorMs)),
				commit.committer !== commit.author ? [h("span.k", null, "Committer"), h("span", null, commit.committer + " \u00b7 " + longDate(commit.commitMs))] : null,
				h("span.k", null, "Parents"), h("span", null, parents.length ? parents : "none"),
				on.length ? [h("span.k", null, "On"), h("span", null, on.join(", "))] : null),
			commit.body ? h("div.body", null, commit.body) : null);
		this.count.textContent = String(commit.files.length);
		this.renderFiles();
		if (commit.files.length) this.pick(commit.files[0]);
	}

	renderFiles() {
		replace(this.files, this.commit.files.map((file) => fileRow(file, {
			selected: this.file && this.file.path === file.path,
			pick: () => this.pick(file),
			fileIcon: this.events.fileIcon,
			counts: h("span.meta", null, file.binary ? "binary" : [file.added ? h("span.c-saved", null, "+" + file.added) : null, file.removed ? h("span.c-error", null, " -" + file.removed) : null]),
		})));
	}

	pick(file) {
		this.file = file;
		this.renderFiles();
		this.events.pick(file);
	}
}

export class ChangesPane {
	// events: {pick(change, staged), stage(paths), unstage(paths), discard(paths), commit(message, amend) -> Promise<bool>,
	// lastMessage() -> Promise<string>, draft(text) (to keep it), fileIcon(path)}
	constructor(events, draft) {
		this.events = events;
		this.changes = null;
		this.selected = null;   // {path, staged}
		this.lists = h("div.git-changes", { role: "listbox", "aria-label": "Local changes" });
		this.message = h("textarea.input-field.git-message", {
			placeholder: "Summary\n\nDescription", "aria-label": "Commit message", rows: "4",
			on: { input: () => { this.events.draft(this.message.value); this.renderComposer(); }, keydown: (e) => { if (e.key === "Enter" && e.ctrlKey) { e.preventDefault(); this.commit(); } } },
		});
		this.message.value = draft || "";
		this.amend = h("input", { type: "checkbox", "aria-label": "Amend the last commit", on: { change: () => this.toggleAmend() } });
		this.commitButton = h("button.btn.primary", { on: { click: () => this.commit() } }, icon("check"), "Commit");
		this.summaryLength = h("span.muted.git-count");
		this.el = h("div.git-meta-pane.git-local", null, this.lists,
			h("div.git-composer", null, this.message,
				h("div.git-composer-bar", null, h("label.git-amend", null, this.amend, "Amend"), this.summaryLength, h("span.grow"), this.commitButton)));
	}

	show(changes) {
		this.changes = changes;
		this.render();
		this.renderComposer();
	}

	staged() {
		return (this.changes && this.changes.staged) || [];
	}

	render() {
		const c = this.changes || { staged: [], unstaged: [], untracked: [], conflicted: [] };
		const unstaged = c.unstaged.concat(c.untracked);
		const sections = [];
		const section = (title, list, staged, all) => {
			sections.push(h("div.group-head", null, title, h("span.meta", null, String(list.length)), h("span.grow"), list.length && all ? all : null));
			for (const change of list) sections.push(fileRow(change, {
				selected: this.selected && this.selected.path === change.path && this.selected.staged === staged,
				pick: () => this.pick(change, staged),
				buttons: this.buttons(change, staged),
				fileIcon: this.events.fileIcon,
			}));
		};
		if (c.conflicted.length) section("Conflicts", c.conflicted, false, null);
		section("Unstaged", unstaged, false, h("button.btn.flat.sm", { on: { click: () => this.events.stage(unstaged.map((x) => x.path)) } }, "Stage all"));
		section("Staged", c.staged, true, h("button.btn.flat.sm", { on: { click: () => this.events.unstage(c.staged.map((x) => x.path)) } }, "Unstage all"));
		if (!unstaged.length && !c.staged.length && !c.conflicted.length) sections.push(h("div.git-empty", null, "No local changes"));
		replace(this.lists, sections);
	}

	buttons(change, staged) {
		const stop = (fn) => (e) => {
			e.stopPropagation();
			fn();
		};
		if (staged) return h("button.ib.sm", { title: "Unstage", "aria-label": "Unstage " + change.path, on: { click: stop(() => this.events.unstage([change.path])) } }, icon("remove"));
		return [
			h("button.ib.sm", { title: "Discard", "aria-label": "Discard " + change.path, on: { click: stop(() => this.events.discard([change.path])) } }, icon("discard")),
			h("button.ib.sm", { title: "Stage", "aria-label": "Stage " + change.path, on: { click: stop(() => this.events.stage([change.path])) } }, icon("add")),
		];
	}

	pick(change, staged) {
		this.selected = { path: change.path, staged };
		this.render();
		this.events.pick(change, staged);
	}

	renderComposer() {
		const summary = firstLine(this.message.value.trim());
		this.summaryLength.textContent = summary ? String(summary.length) : "";
		this.summaryLength.classList.toggle("c-warn", summary.length > 72);
		const staged = this.staged().length;
		this.commitButton.disabled = !this.message.value.trim() || (!staged && !this.amend.checked);
		this.commitButton.title = staged ? "Commit " + staged + (staged === 1 ? " file" : " files") + " (Ctrl+Enter)" : this.amend.checked ? "Amend the last commit" : "Stage changes to commit";
	}

	async toggleAmend() {
		if (this.amend.checked && !this.message.value.trim()) {
			this.message.value = await this.events.lastMessage();
			this.events.draft(this.message.value);
		}
		this.renderComposer();
	}

	async commit() {
		if (this.commitButton.disabled) return;
		this.commitButton.disabled = true;
		const done = await this.events.commit(this.message.value, this.amend.checked);
		if (done) {
			this.message.value = "";
			this.amend.checked = false;
			this.events.draft("");
		}
		this.renderComposer();
	}
}
