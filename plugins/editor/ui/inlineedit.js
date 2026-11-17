// inlineedit.js - what the inline chat (inlinechat.js; F-EDT-100, docs/04 "Inline chat") says and reads, without the
// editor: the messages a session sends, the change a reply proposes (its code block, cleaned of what models add), the
// diff of the lines against it, and where the lines are again after a reload. Pure, for the tests.

export const INLINE_USE = "inline";       // the editor's aiUses contribution: what an inline chat is for
export const INLINE_SECTION = "Inline";   // the Chats list's section the inline chats are kept in (docs/04 "Chat sections")

const TITLE_MOST = 64;
const DIFF_MOST_CELLS = 250000;           // past this many line pairs the whole range counts as one change

// "src/main.c" -> "main.c".
export function fileNameOf(path) {
	return path.slice(path.lastIndexOf("/") + 1);
}

// "3-6", or "3" for one line.
export function linesLabel(startLine, endLine) {
	return endLine > startLine ? startLine + "-" + endLine : String(startLine);
}

// The chat's title: where, then what was asked ("main.c:3-6 clamp the velocity").
export function titleOf(path, startLine, endLine, prompt) {
	const title = fileNameOf(path) + ":" + linesLabel(startLine, endLine) + " " + prompt.split("\n")[0].trim();
	return title.length > TITLE_MOST ? title.slice(0, TITLE_MOST - 3).trimEnd() + "..." : title;
}

// A fence that holds `lines`: longer than any run of backticks in them.
function fenceFor(lines) {
	let most = 2;
	for (const line of lines) for (const run of line.match(/`+/g) || []) most = Math.max(most, run.length);
	return "`".repeat(most + 1);
}

// The first message of a session: what was asked, then where and how to answer. The file goes with it as a mention
// (inlinechat.js), so the model reads the whole of it; the lines themselves are here, exactly as they are.
export function firstMessage({ prompt, path, startLine, endLine, lines, fence }) {
	const where = path + (endLine > startLine ? " lines " : " line ") + linesLabel(startLine, endLine);
	const empty = lines.length === 1 && lines[0].trim() === "";
	const marks = fenceFor(lines);
	const out = [prompt.trim(), "", "---", "Inline edit in the editor: " + where + (empty ? " (an empty line, where the caret is)." : ".")];
	out.push(empty
		? "Reply with the code to put on that line in one fenced code block, at its indentation, and at most a sentence outside the block."
		: "Reply with the new text of " + (endLine > startLine ? "those lines - all of them, as they should read" : "that line, as it should read") + ", at their indentation - in one fenced code block, and at most a sentence outside the block.");
	out.push("The editor shows your block as a change the user accepts or discards: do not edit files with tools. Read more of the code with the tools when you need it. If this asks a question rather than for a change, answer it without a code block.");
	if (!empty) out.push("", "The " + (endLine > startLine ? "lines" : "line") + " now:", marks + (fence || ""), ...lines, marks);
	return out.join("\n");
}

// A message after the first: what was asked, with the lines again when they changed since the model saw them (`lines`:
// the lines now, or null when they did not change).
export function followUpMessage({ prompt, path, startLine, endLine, lines, fence }) {
	const out = [prompt.trim(), "", "(Inline edit of " + path + " " + (endLine > startLine ? "lines " : "line ") + linesLabel(startLine, endLine) + ": reply with " + (endLine > startLine ? "all of them" : "it") + " again, changed as asked, in one code block.)"];
	if (lines) {
		const marks = fenceFor(lines);
		out.push("They read now:", marks + (fence || ""), ...lines, marks);
	}
	return out.join("\n");
}

// The fenced blocks of `text`: [{lines, closed, start, end}], `start`/`end` the indexes of their fence lines in the
// text's lines (`end` the last line for a block still open).
export function fencedBlocks(text) {
	const lines = text.replace(/\r\n?/g, "\n").split("\n");
	const blocks = [];
	let open = null;
	for (let i = 0; i < lines.length; i++) {
		const line = lines[i];
		if (!open) {
			const match = /^ {0,3}(`{3,}|~{3,})[^`]*$/.exec(line);
			if (match) open = { marks: match[1], start: i, lines: [] };
			continue;
		}
		const close = /^ {0,3}(`{3,}|~{3,})\s*$/.exec(line);
		if (close && close[1][0] === open.marks[0] && close[1].length >= open.marks.length) {
			blocks.push({ lines: open.lines, closed: true, start: open.start, end: i });
			open = null;
		}
		else open.lines.push(line);
	}
	if (open) blocks.push({ lines: open.lines, closed: false, start: open.start, end: lines.length - 1 });
	return blocks;
}

// Lines a model copied with fs_read's numbers ("12\tcode") lose them: when every line starts with a number and a tab and
// the numbers count up by one.
export function withoutLineNumbers(lines) {
	if (!lines.length) return lines;
	const numbers = [];
	for (const line of lines) {
		const match = /^\s*(\d+)\t/.exec(line);
		if (!match) return lines;
		numbers.push(Number(match[1]));
	}
	for (let i = 1; i < numbers.length; i++) if (numbers[i] !== numbers[i - 1] + 1) return lines;
	return lines.map((line) => line.replace(/^\s*\d+\t/, ""));
}

function indentOf(line) {
	return /^[ \t]*/.exec(line)[0];
}

// The indentation every non-blank line of `lines` starts with ("" when none or no lines).
export function commonIndent(lines) {
	let common = null;
	for (const line of lines) {
		if (!line.trim()) continue;
		const indent = indentOf(line);
		if (common === null) common = indent;
		else {
			let n = 0;
			while (n < common.length && n < indent.length && common[n] === indent[n]) n++;
			common = common.slice(0, n);
		}
	}
	return common || "";
}

// A block a model wrote flush left for lines that sit indented takes their indentation back: models often drop the
// indentation of a function's middle.
export function reindented(proposed, original) {
	const base = commonIndent(original);
	if (!base || commonIndent(proposed)) return proposed;
	return proposed.map((line) => (line.trim() ? base + line : line));
}

// The change a reply proposes for `original` (the lines it replaces): {lines, closed, prose} from its last code block -
// a block still open while the reply streams, without its unfinished last line - or null when it has none. `prose` is
// the text around the block. `streaming`: the reply is not over, so its last line may be cut.
export function proposalOf(text, original, streaming) {
	const blocks = fencedBlocks(text);
	if (!blocks.length) return null;
	const block = blocks[blocks.length - 1];
	let lines = block.lines.slice();
	// An open block's last line is still being written - or, after a newline, not begun.
	if (!block.closed && (streaming || lines[lines.length - 1] === "")) lines.pop();
	lines = reindented(withoutLineNumbers(lines), original);
	const all = text.replace(/\r\n?/g, "\n").split("\n");
	const prose = all.slice(0, block.start).concat(all.slice(block.end + 1)).join("\n").replace(/\n{3,}/g, "\n\n").trim();
	return { lines, closed: block.closed, prose };
}

// The text of a turn's reply: its text segments in order.
export function replyText(turn) {
	return (turn && turn.segments ? turn.segments : []).filter((s) => s.type === "text").map((s) => s.text || "").join("");
}

// The diff of `before` against `after` (arrays of lines) as hunks: [{at, removed, added}] - `removed` lines of `before`
// from index `at` give way to the lines `added`, in order. The longest common subsequence of the lines, past
// DIFF_MOST_CELLS one hunk of everything between the common start and end.
export function lineHunks(before, after) {
	let start = 0;
	while (start < before.length && start < after.length && before[start] === after[start]) start++;
	let endA = before.length;
	let endB = after.length;
	while (endA > start && endB > start && before[endA - 1] === after[endB - 1]) {
		endA--;
		endB--;
	}
	const a = before.slice(start, endA);
	const b = after.slice(start, endB);
	if (!a.length && !b.length) return [];
	if (!a.length || !b.length || a.length * b.length > DIFF_MOST_CELLS) return [{ at: start, removed: a.length, added: b }];
	const n = a.length;
	const m = b.length;
	const width = m + 1;
	const common = new Uint32Array((n + 1) * width);
	for (let i = n - 1; i >= 0; i--) {
		for (let j = m - 1; j >= 0; j--) {
			common[i * width + j] = a[i] === b[j] ? common[(i + 1) * width + j + 1] + 1 : Math.max(common[(i + 1) * width + j], common[i * width + j + 1]);
		}
	}
	const hunks = [];
	let hunk = null;
	let i = 0;
	let j = 0;
	const flush = () => {
		if (hunk) hunks.push(hunk);
		hunk = null;
	};
	while (i < n || j < m) {
		if (i < n && j < m && a[i] === b[j]) {
			flush();
			i++;
			j++;
		}
		else if (j < m && (i >= n || common[i * width + j + 1] >= common[(i + 1) * width + j])) {
			if (!hunk) hunk = { at: start + i, removed: 0, added: [] };
			hunk.added.push(b[j++]);
		}
		else {
			if (!hunk) hunk = { at: start + i, removed: 0, added: [] };
			hunk.removed++;
			i++;
		}
	}
	flush();
	return hunks;
}

// Lines added and removed by `hunks`.
export function hunkCounts(hunks) {
	return hunks.reduce((sum, h) => ({ plus: sum.plus + h.added.length, minus: sum.minus + h.removed }), { plus: 0, minus: 0 });
}

// FNV-1a of `text`, as 8 hex digits: the lines a session is about, to find them again (meta.inline.hash).
export function textHash(text) {
	let hash = 0x811c9dc5;
	for (let i = 0; i < text.length; i++) {
		hash ^= text.charCodeAt(i);
		hash = Math.imul(hash, 0x01000193) >>> 0;
	}
	return hash.toString(16).padStart(8, "0");
}

// Where a session's `count` lines whose text hashes to `hash` are in `lines` (the document's), 1-based: at `startLine`
// when they are still there, else the nearest place they are; 0 when they are nowhere.
export function locateLines(lines, startLine, count, hash) {
	const at = (first) => first >= 1 && first + count - 1 <= lines.length && textHash(lines.slice(first - 1, first - 1 + count).join("\n")) === hash;
	if (at(startLine)) return startLine;
	for (let distance = 1; distance <= lines.length; distance++) {
		if (at(startLine - distance)) return startLine - distance;
		if (at(startLine + distance)) return startLine + distance;
		if (startLine - distance < 1 && startLine + distance + count - 1 > lines.length) break;
	}
	return 0;
}

// The session a chat's summary describes ({path, startLine, endLine, lines, hash, state}), or null for another chat.
export function inlineOf(summary) {
	const inline = summary && summary.use === INLINE_USE && summary.meta && summary.meta.inline;
	return inline && typeof inline.path === "string" && inline.startLine >= 1 ? inline : null;
}
