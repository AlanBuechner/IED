// textedits.js - the document protocol's edits on text (docs/10 "Document editing protocol"): positions are 0-based
// {line, col}, lines end at "\n", col counts UTF-16 code units (what JavaScript strings index); the changes of one edit
// all refer to the text before it and do not overlap.

export class EditError extends Error {}

// The offset of a position in `text`, given the offsets its lines start at. Throws EditError outside the text.
function offsetOf(lineStarts, text, position) {
	if (!position || position.line < 0 || position.line >= lineStarts.length || position.col < 0) throw new EditError("position " + JSON.stringify(position) + " is outside the text");
	const start = lineStarts[position.line];
	const end = position.line + 1 < lineStarts.length ? lineStarts[position.line + 1] - 1 : text.length;
	if (start + position.col > end) throw new EditError("position " + JSON.stringify(position) + " is past the end of its line");
	return start + position.col;
}

export function lineStartsOf(text) {
	const starts = [0];
	for (let i = text.indexOf("\n"); i >= 0; i = text.indexOf("\n", i + 1)) starts.push(i + 1);
	return starts;
}

// `text` with `changes` ([{range: {start, end}, text}]) applied. Throws EditError for a range outside the text, a range
// that ends before it starts, or changes that overlap.
export function applyChanges(text, changes) {
	const lineStarts = lineStartsOf(text);
	const spans = changes.map((change) => {
		const from = offsetOf(lineStarts, text, change.range.start);
		const to = offsetOf(lineStarts, text, change.range.end);
		if (to < from) throw new EditError("a range ends before it starts");
		return { from, to, text: change.text || "" };
	}).sort((a, b) => a.from - b.from || a.to - b.to);
	for (let i = 1; i < spans.length; i++) if (spans[i].from < spans[i - 1].to) throw new EditError("changes overlap");
	let out = "";
	let at = 0;
	for (const span of spans) {
		out += text.slice(at, span.from) + span.text;
		at = span.to;
	}
	return out + text.slice(at);
}

// ---------------------------------------------------------------------------------------------------------------
// Rebasing (the server's rule, src/fs/textedit.cf, and its mirror for a client with edits of its own in flight)

export class OverlapError extends EditError {}

function before(a, b) {
	return a.line < b.line || (a.line === b.line && a.col < b.col);
}

function atOrBefore(a, b) {
	return !before(b, a);
}

// Where text inserted at `start` ends.
function afterInsert(start, text) {
	const lines = text.split("\n");
	if (lines.length === 1) return { line: start.line, col: start.col + text.length };
	return { line: start.line + lines.length - 1, col: lines[lines.length - 1].length };
}

// `pos` (at or after `applied`'s end) moved over `applied`.
function shift(pos, applied) {
	const after = afterInsert(applied.range.start, applied.text || "");
	if (pos.line === applied.range.end.line) return { line: after.line, col: after.col + pos.col - applied.range.end.col };
	return { line: pos.line + after.line - applied.range.end.line, col: pos.col };
}

function moved(change, applied) {
	return { range: { start: shift(change.range.start, applied), end: shift(change.range.end, applied) }, text: change.text };
}

// One change set as the steps it was applied as: last position first, so each leaves the positions before it alone.
export function sequential(changes) {
	return changes.map((change, index) => ({ change, index })).sort((a, b) => {
		const s = a.change.range.start;
		const t = b.change.range.start;
		if (before(s, t) || before(t, s)) return before(s, t) ? 1 : -1;
		const e = a.change.range.end;
		const f = b.change.range.end;
		if (before(e, f) || before(f, e)) return before(e, f) ? 1 : -1;
		return b.index - a.index;
	}).map((entry) => entry.change);
}

// `change`, made concurrently with `applied`, as it applies after it - the server's rule: `applied` goes first where
// the two meet. Throws OverlapError when the ranges overlap.
export function transformAfter(change, applied) {
	if (atOrBefore(applied.range.end, change.range.start)) return moved(change, applied);
	if (atOrBefore(change.range.end, applied.range.start)) return change;
	throw new OverlapError("a change overlaps one made meanwhile");
}

// The mirror, for a change the server applied before `local` (a change this client made but the server has not yet):
// `change` goes first where the two meet, so both sides end with the same text.
export function transformBefore(change, local) {
	if (atOrBefore(change.range.end, local.range.start)) return change;
	if (atOrBefore(local.range.end, change.range.start)) return moved(change, local);
	throw new OverlapError("a change overlaps one made meanwhile");
}

// Each of `changes` moved over every step of the change sets `sets` (oldest first), with `rule`.
export function rebase(changes, sets, rule) {
	const steps = sets.flatMap((set) => sequential(set));
	return changes.map((change) => steps.reduce((c, step) => rule(c, step), change));
}
