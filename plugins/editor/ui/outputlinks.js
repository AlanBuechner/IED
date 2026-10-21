// outputlinks.js - file locations in the Output window (VS: a build line that names a place opens it): the shapes
// compilers print - "path(line,col)" (MSVC), "path (line:col)" (C4), "path:line:col" (clang, gcc, rustc, Python) and
// rustc's "--> path:line:col" - found in a line's text, and the workspace file a found path means.

const SHAPES = [
	/(?:^|\s|-->\s)((?:[A-Za-z]:)?[^\s()<>"'|*?]+?\.[A-Za-z0-9_+-]+)\((\d+)(?:,(\d+))?\)/,
	/(?:^|\s|-->\s)((?:[A-Za-z]:)?[^\s()<>"'|*?]+?\.[A-Za-z0-9_+-]+) \((\d+):(\d+)\)/,
	/(?:^|\s|-->\s)((?:[A-Za-z]:)?[^\s():<>"'|*?]+?\.[A-Za-z0-9_+-]+):(\d+)(?::(\d+))?/,
];

// The first place `text` names: {start, end, path, line, col} (line and col 1-based as printed, col 0 when absent), or
// null. `start`..`end` covers the path and the position, for underlining.
export function findLocation(text) {
	let best = null;
	for (const shape of SHAPES) {
		const match = shape.exec(text);
		if (!match) continue;
		const start = match.index + match[0].indexOf(match[1]);
		if (best && best.start <= start) continue;
		best = { start, end: match.index + match[0].length, path: match[1], line: Number(match[2]), col: match[3] ? Number(match[3]) : 0 };
	}
	return best;
}

function slashed(path) {
	return path.replace(/\\/g, "/");
}

// `path` inside the workspace at `root` (absolute paths compared without regard to case), relative with "/"; null when
// it is absolute and elsewhere.
export function workspaceRelative(path, root) {
	const p = slashed(path);
	if (!/^[A-Za-z]:\//.test(p) && !p.startsWith("/")) return p.replace(/^\.\//, "");
	const r = slashed(root || "").replace(/\/$/, "");
	if (r && p.toLowerCase().startsWith(r.toLowerCase() + "/")) return p.slice(r.length + 1);
	return null;
}

// Among workspace files, the one `relative` means: itself, else the shortest path ending with it (a compiler prints a
// name relative to its project's source folder).
export function pickFile(relative, files) {
	const exact = files.find((f) => f === relative);
	if (exact) return exact;
	const tail = "/" + relative;
	const ending = files.filter((f) => f.endsWith(tail)).sort((a, b) => a.length - b.length);
	return ending.length ? ending[0] : null;
}
