// diffpane.js - the lower right of the Git activity (mockup 05): the chosen file's diff with its path, Inline or Side by
// side (this device's choice), and Open file. A commit's file is diffed against its first parent; a local change
// against the index (unstaged) or HEAD (staged), with the hunk and line actions; an untracked file shows whole. Its
// lines take the editor's syntax colors when the editor plugin provides them (the `editor.colorize` binding).

import { h, icon, replace } from "/ui-kit/dom.js";
import { DiffView, sides } from "./diffview.js";

// An untracked file as one hunk of added lines.
export function wholeFileDiff(path, text) {
	const lines = text.split("\n");
	if (lines.length && lines[lines.length - 1] === "") lines.pop();
	return { path, binary: false, truncated: false, hunks: lines.length ? [{ oldStart: 0, oldLines: 0, newStart: 1, newLines: lines.length, header: "@@ -0,0 +1," + lines.length + " @@", lines: lines.map((l) => "+" + l) }] : [] };
}

export class DiffPane {
	// events: {open(path), mode(mode)}
	constructor(ied, events, mode) {
		this.ied = ied;
		this.events = events;
		this.mode = mode === "split" ? "split" : "inline";
		this.view = new DiffView();
		this.path = h("span.git-diff-path");
		this.note = h("span.muted");
		this.modes = h("span.seg", { role: "group", "aria-label": "Diff layout" },
			h("button", { dataset: { mode: "inline" }, on: { click: () => this.setMode("inline") } }, "Inline"),
			h("button", { dataset: { mode: "split" }, on: { click: () => this.setMode("split") } }, "Side by side"));
		this.openButton = h("button.btn.flat", { on: { click: () => this.current && this.events.open(this.current) } }, icon("go-to-file"), "Open file");
		this.el = h("div.git-diff-pane", null, h("div.panel-tools", null, this.path, this.note, h("span.tb-spacer"), this.modes, this.openButton), this.view.el);
		this.current = null;
		this.last = null;
		this.paintModes();
	}

	setMode(mode) {
		this.mode = mode;
		this.events.mode(mode);
		this.paintModes();
		if (this.last) this.view.show(this.last.diff, Object.assign({}, this.last.options, { mode }));
	}

	paintModes() {
		for (const button of this.modes.children) button.classList.toggle("on", button.dataset.mode === this.mode);
	}

	clear(text) {
		this.current = null;
		this.last = null;
		this.path.textContent = "";
		this.note.textContent = text || "";
		this.view.show(null, {});
	}

	present(path, note, diff, options) {
		this.current = path;
		this.path.textContent = path;
		this.note.textContent = note;
		this.last = { diff, options };
		this.view.show(diff, Object.assign({}, options, { mode: this.mode }));
		this.colorize(path, diff);
	}

	// Both sides of `diff` colored by the editor, each as one text so a construct over several lines tokenizes whole.
	async colorize(path, diff) {
		if (diff.binary || !diff.hunks.length || !this.ied.bindings.has("editor.colorize")) return;
		const both = sides(diff);
		let colored;
		try {
			colored = await Promise.all([both.old, both.new].map((side) => this.ied.bindings.call("editor.colorize", { path, lines: side.lines })));
		}
		catch {
			return;
		}
		if (!this.last || this.last.diff !== diff) return;
		const colors = new Map();
		[both.old, both.new].forEach((side, s) => side.keys.forEach((key, i) => {
			const entry = colors.get(key) || { old: null, new: null };
			entry[s ? "new" : "old"] = colored[s][i];
			colors.set(key, entry);
		}));
		this.view.setColors(colors);
	}

	async showCommit(repoId, oid, file) {
		this.clear("Loading...");
		try {
			const diff = await this.ied.client.request("git.diff", { repoId, oid, path: file.path, oldPath: file.oldPath });
			const note = { A: "new file", D: "deleted", R: "renamed from " + file.oldPath }[file.status] || "";
			this.present(file.path, note, diff, {});
		}
		catch (e) {
			this.clear(e.message);
		}
	}

	// actions: {stage(hunk, lines), unstage(hunk, lines), discard(hunk)} bound to this file.
	async showLocal(repoId, change, staged, actions) {
		this.clear("Loading...");
		try {
			if (change.status === "?") {
				const file = await this.ied.client.request("git.fileAt", { repoId, path: change.path, rev: "" });
				const diff = file.binary ? { binary: true, hunks: [] } : wholeFileDiff(change.path, file.text);
				this.present(change.path, "untracked", diff, { actions: { stage: () => actions.stageFile(), unstage: () => {}, discard: () => actions.discardFile() }, staged: false });
				return;
			}
			const diff = await this.ied.client.request("git.diff", { repoId, path: change.path, staged });
			this.present(change.path, staged ? "staged" : "", diff, { actions, staged, empty: "No changes left in this file" });
		}
		catch (e) {
			this.clear(e.message);
		}
	}
}
