// diffview.js - one file's diff (git.diff's hunks), unified as Fork shows it or side by side, in the editor's syntax
// colors once they come (setColors). In local changes each hunk carries Stage or Unstage (and Discard for the work
// tree), and changed lines can be picked - a click, Shift for a range - to stage or unstage only those
// (git.stagePatch's `lines`).

import { h, icon, replace } from "/ui-kit/dom.js";

function display(text) {
	return text.endsWith("\r") ? text.slice(0, -1) : text;
}

// A hunk's lines with their numbers in the old and the new file: [{index, kind, text, oldNo, newNo}].
export function numbered(hunk) {
	let oldNo = hunk.oldStart;
	let newNo = hunk.newStart;
	return hunk.lines.map((line, index) => {
		const kind = line.charAt(0);
		const row = { index, kind, text: display(line.slice(1)), oldNo: null, newNo: null };
		if (kind === " " || kind === "-") row.oldNo = oldNo++;
		if (kind === " " || kind === "+") row.newNo = newNo++;
		return row;
	});
}

// The texts to color for each side of `diff`: {old: {lines, keys}, new: {lines, keys}}, a key "hunk:line" per line -
// removed lines and context on the old side, added lines and context on the new.
export function sides(diff) {
	const out = { old: { lines: [], keys: [] }, new: { lines: [], keys: [] } };
	diff.hunks.forEach((hunk, h) => numbered(hunk).forEach((line) => {
		if (line.kind === " " || line.kind === "-") {
			out.old.lines.push(line.text);
			out.old.keys.push(h + ":" + line.index);
		}
		if (line.kind === " " || line.kind === "+") {
			out.new.lines.push(line.text);
			out.new.keys.push(h + ":" + line.index);
		}
	}));
	return out;
}

// The hunk side by side: [{left, right}], a run of removed lines beside the added run after it; context on both.
export function sideBySide(hunk) {
	const rows = [];
	const lines = numbered(hunk).filter((l) => l.kind !== "\\");
	for (let i = 0; i < lines.length;) {
		if (lines[i].kind === " ") {
			rows.push({ left: lines[i], right: lines[i] });
			i += 1;
			continue;
		}
		const removed = [];
		const added = [];
		while (i < lines.length && lines[i].kind === "-") removed.push(lines[i++]);
		while (i < lines.length && lines[i].kind === "+") added.push(lines[i++]);
		for (let k = 0; k < Math.max(removed.length, added.length); k++) rows.push({ left: removed[k] || null, right: added[k] || null });
	}
	return rows;
}

const KIND_CLASS = { "+": ".add", "-": ".del", " ": ".ctx", "\\": ".note" };

export class DiffView {
	// actions: {stage(hunkIndex, lines|null), unstage(hunkIndex, lines|null), discard(hunkIndex)} or null (a commit's diff)
	constructor() {
		this.el = h("div.git-diff", { role: "region", "aria-label": "Diff" });
		this.diff = null;
		this.options = {};
		this.picked = new Map();   // hunk index -> Set of line indexes
		this.anchor = null;
		this.colors = null;        // "hunk:line" -> {old, new} HTML from the editor's colorizer
	}

	show(diff, options) {
		this.diff = diff;
		this.options = options || {};
		this.picked = new Map();
		this.anchor = null;
		this.colors = null;
		this.render();
	}

	// The lines' HTML (sides() keys to {old, new}) for the diff shown now; the view keeps its scroll.
	setColors(colors) {
		this.colors = colors;
		const scroll = this.el.scrollTop;
		this.render();
		this.el.scrollTop = scroll;
	}

	// A line's text, colored when the colors are in: the removed side's for a removed line, the added side's otherwise.
	text(hunkIndex, line, side) {
		const colored = this.colors && this.colors.get(hunkIndex + ":" + line.index);
		const html = colored && (side === "old" || line.kind === "-" ? colored.old : colored.new);
		if (html === undefined || html === null) return h("span.tx", null, line.text);
		const span = h("span.tx");
		span.innerHTML = html;
		return span;
	}

	render() {
		const diff = this.diff;
		if (!diff) return replace(this.el);
		if (diff.binary) return replace(this.el, h("div.git-diff-note", null, "Binary file"));
		if (!diff.hunks.length) return replace(this.el, h("div.git-diff-note", null, this.options.empty || "No changes"));
		const split = this.options.mode === "split";
		replace(this.el,
			diff.hunks.map((hunk, index) => h("div.hunk" + (split ? ".split" : ""), { dataset: { hunk: String(index) } },
				this.hunkHead(hunk, index),
				split ? sideBySide(hunk).map((row) => this.splitLine(row, index)) : numbered(hunk).map((line) => this.line(line, index)))),
			diff.truncated ? h("div.git-diff-note", null, "The diff is too large to show whole") : null);
	}

	hunkHead(hunk, index) {
		const actions = this.options.actions;
		const picked = this.picked.get(index);
		const count = picked ? picked.size : 0;
		const what = count ? count + (count === 1 ? " line" : " lines") : "hunk";
		const buttons = [];
		if (actions && this.options.staged) buttons.push(h("button.btn.flat.sm", { on: { click: () => actions.unstage(index, count ? [...picked] : null) } }, icon("remove"), "Unstage " + what));
		else if (actions) {
			buttons.push(h("button.btn.flat.sm", { on: { click: () => actions.stage(index, count ? [...picked] : null) } }, icon("add"), "Stage " + what));
			if (!count) buttons.push(h("button.btn.flat.sm", { on: { click: () => actions.discard(index) } }, icon("discard"), "Discard hunk"));
		}
		return h("div.hunk-head", null, h("span.mono", null, hunk.header), h("span.grow"), buttons);
	}

	line(line, hunkIndex) {
		const changed = line.kind === "+" || line.kind === "-";
		const picked = this.picked.get(hunkIndex);
		const cls = "div.dl" + KIND_CLASS[line.kind] + (picked && picked.has(line.index) ? ".picked" : "");
		const props = { dataset: { line: String(line.index) } };
		if (changed && this.options.actions) props.on = { click: (event) => this.pick(hunkIndex, line.index, event.shiftKey) };
		return h(cls, props,
			h("span.no", null, line.oldNo === null ? "" : String(line.oldNo)),
			h("span.no", null, line.newNo === null ? "" : String(line.newNo)),
			h("span.sg", null, line.kind === "\\" ? "" : line.kind),
			line.kind === "\\" ? h("span.tx", null, line.text) : this.text(hunkIndex, line, "new"));
	}

	splitLine(row, hunkIndex) {
		const side = (line, which) => {
			if (!line) return h("div.side.empty");
			const kind = line.kind === " " ? ".ctx" : which === "left" ? ".del" : ".add";
			return h("div.side" + kind, null, h("span.no", null, String(which === "left" ? line.oldNo : line.newNo)), this.text(hunkIndex, line, which === "left" ? "old" : "new"));
		};
		return h("div.dl.pair", null, side(row.left, "left"), side(row.right, "right"));
	}

	// Picks or unpicks a changed line; with Shift, every changed line from the last one picked.
	pick(hunkIndex, index, range) {
		if (!this.picked.has(hunkIndex)) this.picked.set(hunkIndex, new Set());
		const picked = this.picked.get(hunkIndex);
		if (range && this.anchor && this.anchor.hunk === hunkIndex) {
			const lines = this.diff.hunks[hunkIndex].lines;
			const [from, to] = [Math.min(this.anchor.index, index), Math.max(this.anchor.index, index)];
			for (let i = from; i <= to; i++) if (lines[i].charAt(0) === "+" || lines[i].charAt(0) === "-") picked.add(i);
		}
		else if (picked.has(index)) picked.delete(index);
		else picked.add(index);
		this.anchor = { hunk: hunkIndex, index };
		if (!picked.size) this.picked.delete(hunkIndex);
		const scroll = this.el.scrollTop;
		this.render();
		this.el.scrollTop = scroll;
	}
}
