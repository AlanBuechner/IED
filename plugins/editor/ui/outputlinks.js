// outputlinks.js - file locations in the Output window (VS: a build line that names a place opens it): the shapes the
// language plugins say their tools print (`outputLinks`: MSVC's "path(line,col)", C4's "path (line:col)", clang's
// "path:line:col", a Python traceback's 'File "path", line N'), found in a line's text, and the workspace file a found
// path means.

// The plugins' `outputLinks` ([{plugin, value: {regexp, file, line, column?}}]) as shapes to match, each regular
// expression once; one that does not compile is reported through `report` and left out.
export function compileShapes(contributions, report) {
	const shapes = [];
	const seen = new Set();
	for (const { plugin, value } of contributions || []) {
		if (seen.has(value.regexp)) continue;
		seen.add(value.regexp);
		try {
			shapes.push({ regex: new RegExp(value.regexp), file: value.file, line: value.line, column: value.column });
		}
		catch (e) {
			if (report) report("the output link '" + value.id + "' of " + (plugin ? plugin.id : "a plugin") + " is not a regular expression: " + e.message);
		}
	}
	return shapes;
}

// The first place `text` names among `shapes`: {start, end, path, line, col} (line and col 1-based as printed, col 0
// when absent), or null. `start`..`end` covers the path and the position, for underlining.
export function findLocation(text, shapes) {
	let best = null;
	for (const shape of shapes) {
		const match = shape.regex.exec(text);
		if (!match || !match[shape.file]) continue;
		const start = match.index + match[0].indexOf(match[shape.file]);
		if (best && best.start <= start) continue;
		const column = shape.column ? match[shape.column] : undefined;
		best = { start, end: match.index + match[0].length, path: match[shape.file], line: Number(match[shape.line]), col: column ? Number(column) : 0 };
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
