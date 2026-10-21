// errorlist.js - the Error List (VS; docs/24 "Diagnostics"): every diagnostic of the workspace's language servers, with
// the Errors / Warnings / Messages toggles and their counts, a search over the text, columns Code, Description, File,
// Line and Source; Enter or a double click opens the place. Rows past MAX_ROWS are counted, not drawn.

import { h, icon, replace } from "/ui-kit/dom.js";

const MAX_ROWS = 5000;
const KINDS = [
	{ key: "errors", label: "Errors", glyph: "error", cls: "c-error", severities: [1, undefined] },
	{ key: "warnings", label: "Warnings", glyph: "warning", cls: "c-warn", severities: [2] },
	{ key: "messages", label: "Messages", glyph: "info", cls: "c-info", severities: [3, 4] },
];

function kindOf(item) {
	return KINDS.find((kind) => kind.severities.includes(item.severity)) || KINDS[0];
}

export class ErrorList {
	constructor(ctx, diagnostics, mounted) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.diagnostics = diagnostics;
		this.mounted = mounted;
		this.shown = { errors: true, warnings: true, messages: true };
		this.selected = -1;
		this.rows = [];
		this.toggles = KINDS.map((kind) => h("button.btn.flat.el-toggle.on", {
			"aria-pressed": "true",
			on: { click: () => this.toggle(kind.key) },
		}));
		this.search = h("input.input-field.el-search", { type: "search", placeholder: "Search Error List", "aria-label": "Search Error List", on: { input: () => this.render() } });
		this.list = h("div.el-rows", { tabindex: "0", role: "grid", "aria-label": "Error List", on: { keydown: (event) => this.key(event) } });
		ctx.el.append(
			h("div.ed-tools", null, ...this.toggles, h("span.grow"), this.search),
			h("div.el-head", null, h("span.el-icon"), h("span.el-code", null, "Code"), h("span.el-text", null, "Description"), h("span.el-file", null, "File"),
				h("span.el-line", null, "Line"), h("span.el-source", null, "Source")),
			h("div.ed-fill", null, this.list));
		mounted.add(this);
		this.stopWatch = diagnostics.watch(ctx.wsId);
		this.stopChanges = diagnostics.onChange((wsId) => {
			if (wsId === ctx.wsId) this.render();
		});
		this.render();
	}

	dispose() {
		this.mounted.delete(this);
		this.stopChanges();
		this.stopWatch();
	}

	focus() {
		this.list.focus({ preventScroll: true });
	}

	toggle(key) {
		this.shown[key] = !this.shown[key];
		this.render();
	}

	render() {
		const all = this.diagnostics.all().filter((row) => row.wsId === this.ctx.wsId);
		const counts = { errors: 0, warnings: 0, messages: 0 };
		for (const row of all) counts[kindOf(row.item).key] += 1;
		KINDS.forEach((kind, i) => {
			const on = this.shown[kind.key];
			this.toggles[i].classList.toggle("on", on);
			this.toggles[i].setAttribute("aria-pressed", String(on));
			replace(this.toggles[i], icon(kind.glyph, kind.cls), h("span", null, counts[kind.key] + " " + (counts[kind.key] === 1 ? kind.label.slice(0, -1) : kind.label)));
		});
		const needle = this.search.value.trim().toLowerCase();
		this.rows = all.filter((row) => this.shown[kindOf(row.item).key] && (!needle || describe(row).toLowerCase().includes(needle)));
		this.selected = Math.min(this.selected, this.rows.length - 1);
		const drawn = this.rows.slice(0, MAX_ROWS).map((row, i) => this.row(row, i));
		const more = this.rows.length > MAX_ROWS ? h("div.el-more.muted", null, (this.rows.length - MAX_ROWS) + " more not shown; search to narrow the list.") : null;
		const empty = this.rows.length ? null : h("div.el-more.muted", null, all.length ? "Nothing matches." : "No problems have been found.");
		replace(this.list, ...drawn, more, empty);
	}

	row(row, index) {
		const item = row.item;
		const kind = kindOf(item);
		const line = item.range.start.line + 1;
		return h("div.el-row" + (index === this.selected ? ".sel" : ""), {
			role: "row",
			title: item.message,
			on: { click: () => this.select(index), dblclick: () => this.open(index) },
		},
		h("span.el-icon", null, icon(kind.glyph, kind.cls)),
		h("span.el-code.ellip", null, item.code === undefined || item.code === null ? "" : String(item.code)),
		h("span.el-text.ellip", null, item.message),
		h("span.el-file.ellip", { title: row.path }, baseName(row.path)),
		h("span.el-line", null, String(line)),
		h("span.el-source.ellip", null, item.source || row.source));
	}

	select(index) {
		this.selected = index;
		const rows = this.list.querySelectorAll(".el-row");
		rows.forEach((el, i) => el.classList.toggle("sel", i === index));
		if (rows[index]) rows[index].scrollIntoView({ block: "nearest" });
	}

	open(index) {
		const row = this.rows[index];
		if (!row) return;
		const start = row.item.range.start;
		this.ied.commands.run("ied.editor.goTo", { path: row.path, line: start.line, col: start.character });
	}

	key(event) {
		const last = Math.min(this.rows.length, MAX_ROWS) - 1;
		if (event.key === "ArrowDown") this.select(Math.min(last, this.selected + 1));
		else if (event.key === "ArrowUp") this.select(Math.max(0, this.selected - 1));
		else if (event.key === "Enter") this.open(this.selected);
		else return;
		event.preventDefault();
	}
}

function baseName(path) {
	return path.slice(path.lastIndexOf("/") + 1);
}

function describe(row) {
	return [row.item.code, row.item.message, row.path, row.item.source || row.source].filter((part) => part !== undefined && part !== null).join(" ");
}
