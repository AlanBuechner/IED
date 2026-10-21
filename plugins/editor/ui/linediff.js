// linediff.js - which lines of a text differ from an earlier one, for the track-changes margin: Myers' diff over lines
// (common start and end trimmed first, so an edit costs what it changes), giving the changed lines of the newer text.
// A deletion marks the line after it (the last line when it was at the end). Past MOST_EDITS the whole middle counts
// as changed rather than taking the time to say exactly.

const MOST_EDITS = 2000;

// The 1-based line numbers of `after` that are new or changed since `before`, ascending.
export function changedLines(before, after) {
	const a = before.split("\n");
	const b = after.split("\n");
	let start = 0;
	while (start < a.length && start < b.length && a[start] === b[start]) start++;
	let endA = a.length;
	let endB = b.length;
	while (endA > start && endB > start && a[endA - 1] === b[endB - 1]) {
		endA--;
		endB--;
	}
	const marked = new Set();
	if (start === endA && start === endB) return [];
	const script = editScript(a.slice(start, endA), b.slice(start, endB));
	if (script === null) {
		for (let line = start; line < Math.max(endB, start + 1); line++) marked.add(Math.min(line, b.length - 1) + 1);
	}
	else {
		for (const edit of script) {
			if (edit.kind === "insert") marked.add(start + edit.at + 1);
			else marked.add(Math.min(start + edit.at, b.length - 1) + 1);
		}
	}
	return [...marked].sort((x, y) => x - y);
}

// The inserts (at their index in `b`) and deletes (at the index in `b` they happened before) turning `a` into `b`;
// null past MOST_EDITS.
function editScript(a, b) {
	const n = a.length;
	const m = b.length;
	const max = Math.min(n + m, MOST_EDITS);
	const offset = max + 1;
	const v = new Int32Array(2 * max + 3);
	const trace = [];
	for (let d = 0; d <= max; d++) {
		trace.push(v.slice());
		for (let k = -d; k <= d; k += 2) {
			let x = k === -d || (k !== d && v[offset + k - 1] < v[offset + k + 1]) ? v[offset + k + 1] : v[offset + k - 1] + 1;
			let y = x - k;
			while (x < n && y < m && a[x] === b[y]) {
				x++;
				y++;
			}
			v[offset + k] = x;
			if (x >= n && y >= m) return backtrack(trace, d, n, m, offset);
		}
	}
	return null;
}

function backtrack(trace, last, n, m, offset) {
	const edits = [];
	let x = n;
	let y = m;
	for (let d = last; d > 0; d--) {
		const v = trace[d];
		const k = x - y;
		const down = k === -d || (k !== d && v[offset + k - 1] < v[offset + k + 1]);
		const prevK = down ? k + 1 : k - 1;
		const prevX = v[offset + prevK];
		const prevY = prevX - prevK;
		while (x > prevX && y > prevY) {
			x--;
			y--;
		}
		if (down) edits.push({ kind: "insert", at: prevY });
		else edits.push({ kind: "delete", at: prevY });
		x = prevX;
		y = prevY;
	}
	return edits.reverse();
}
