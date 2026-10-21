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
