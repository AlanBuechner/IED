// findinfiles.js - Find in Files (VS Ctrl+Shift+F; docs/10 fs.search): a docked view, not a dialog (docs/15): the
// query with Match Case and Regular Expression, the files to look in and to skip, and the results by file - Enter or
// a double click opens a match at its place. The query and options stay in the view while it is open.

import { h, icon, replace } from "/ui-kit/dom.js";
import { TreeView } from "/ui-kit/tree.js";

const INCLUDE_HINT = "e.g. **/*.txt, src/**";
const EXCLUDE_HINT = "default: build, bin, vendor, node_modules, ...";

function globs(text) {
	return text.split(",").map((part) => part.trim()).filter(Boolean);
}

export class FindInFiles {
	constructor(ctx, mounted) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.mounted = mounted;
		this.files = [];
		this.running = null;
		this.query = h("input.input-field.fif-query", { type: "search", placeholder: "Find in files", "aria-label": "Find what",
			on: { keydown: (event) => event.key === "Enter" && this.search() } });
		this.matchCase = this.toggle("case-sensitive", "Match Case");
		this.regex = this.toggle("regex", "Use Regular Expressions");
		this.include = h("input.input-field", { placeholder: INCLUDE_HINT, "aria-label": "Files to include", on: { keydown: (event) => event.key === "Enter" && this.search() } });
		this.exclude = h("input.input-field", { placeholder: EXCLUDE_HINT, "aria-label": "Files to exclude", on: { keydown: (event) => event.key === "Enter" && this.search() } });
		this.status = h("span.fif-status.muted");
		const results = h("div.ed-fill.fif-results");
		ctx.el.append(
			h("div.ed-tools.fif-row", null, this.query, this.matchCase, this.regex,
				h("button.btn", { on: { click: () => this.search() } }, "Find All")),
			h("div.ed-tools.fif-row", null, h("label", null, "Include"), this.include, h("label", null, "Exclude"), this.exclude),
			h("div.ed-tools.fif-row", null, this.status),
			results);
		this.tree = new TreeView(results, {
			roots: async () => this.files,
			children: async (node) => node.matches,
			render: (node, row) => this.renderRow(node, row),
			label: (node) => node.name,
			open: (node) => this.open(node),
		});
		mounted.add(this);
	}

	dispose() {
		this.mounted.delete(this);
		if (this.running) this.running.abort();
		this.tree.dispose();
	}

	focus() {
		this.query.focus({ preventScroll: true });
		this.query.select();
	}

	// Starts with `text` (the editor's selection) when there is one.
	start(text) {
		if (text) this.query.value = text;
		this.focus();
	}

	toggle(glyph, title) {
		const button = h("button.ib.fif-toggle", { title, "aria-label": title, "aria-pressed": "false",
			on: { click: () => {
				const on = button.getAttribute("aria-pressed") !== "true";
				button.setAttribute("aria-pressed", String(on));
				button.classList.toggle("on", on);
			} } }, icon(glyph));
		return button;
	}

	options() {
		const params = { wsId: this.ctx.wsId, query: this.query.value, regex: this.regex.classList.contains("on"), caseSensitive: this.matchCase.classList.contains("on") };
		if (this.include.value.trim()) params.include = globs(this.include.value);
		if (this.exclude.value.trim()) params.exclude = globs(this.exclude.value);
		return params;
	}

	async search() {
		if (!this.query.value) return;
		if (this.running) this.running.abort();
		const running = new AbortController();
		this.running = running;
		replace(this.status, icon("loading", "spin"), " Searching...");
		try {
			const result = await this.ied.client.request("fs.search", this.options(), { signal: running.signal });
			if (running !== this.running) return;
			this.show(result);
		}
		catch (e) {
			if (e.code === "cancelled") return;
			replace(this.status, h("span.c-error", null, e.code === "bad_request" ? e.message : "The search failed: " + e.message));
		}
		finally {
			if (running === this.running) this.running = null;
		}
	}

	show(result) {
		const byPath = new Map();
		for (const match of result.matches) {
			if (!byPath.has(match.path)) byPath.set(match.path, { id: "f:" + match.path, kind: "file", name: match.path, path: match.path, expandable: true, matches: [] });
			const file = byPath.get(match.path);
			file.matches.push({ id: "m:" + match.path + ":" + match.line, kind: "match", name: match.preview, match, expandable: false });
		}
		this.files = [...byPath.values()];
		const count = result.matches.length;
		replace(this.status, count + (count === 1 ? " match in " : " matches in ") + this.files.length + (this.files.length === 1 ? " file" : " files") +
			(result.truncated ? " (only the first " + count + ": refine the search)" : ""));
		this.tree.expanded = new Set(this.files.map((file) => file.id));
		this.tree.refresh();
	}

	renderRow(node, row) {
		if (node.kind === "file") {
			row.append(this.ied.icons.file(node.path), h("span.name", null, node.path), h("span.meta", null, String(node.matches.length)));
			return;
		}
		row.append(h("span.fif-line.muted", null, String(node.match.line + 1)), h("span.name.fif-preview", null, ...this.highlighted(node.match.preview)));
		row.title = node.match.path + ":" + (node.match.line + 1);
	}

	// The preview with the query marked, when it is a plain text found in it.
	highlighted(preview) {
		const query = this.query.value;
		if (!query || this.regex.classList.contains("on")) return [preview];
		const at = this.matchCase.classList.contains("on") ? preview.indexOf(query) : preview.toLowerCase().indexOf(query.toLowerCase());
		if (at < 0) return [preview];
		return [preview.slice(0, at), h("mark.fif-hit", null, preview.slice(at, at + query.length)), preview.slice(at + query.length)];
	}

	open(node) {
		if (node.kind !== "match") return;
		this.ied.commands.run("ied.editor.goTo", { path: node.match.path, line: node.match.line, col: node.match.col });
	}
}
