// fileviewers.js - which tab shows a file (docs/02 "fileViewers"): the plugins' fileViewers entries whose patterns match
// it. A pattern without "/" matches the file's name, one with "/" the workspace-relative path; "*" is any run of
// characters but "/", "**" any run, "?" one character; names match without case.
//     viewersFor("design/a.iedboard", ied.contributions("fileViewers"), (kind) => true)
//         -> [{id, title, view, priority, plugin}], the default viewer last

const cache = new Map();

function patternOf(glob) {
	if (cache.has(glob)) return cache.get(glob);
	let source = "";
	for (let i = 0; i < glob.length; i++) {
		const c = glob[i];
		if (c === "*" && glob[i + 1] === "*") {
			source += glob[i + 2] === "/" ? "(?:.*/)?" : ".*";
			i += glob[i + 2] === "/" ? 2 : 1;
		}
		else if (c === "*") source += "[^/]*";
		else if (c === "?") source += "[^/]";
		else source += c.replace(/[.+^${}()|[\]\\]/g, "\\$&");
	}
	const made = new RegExp("^" + source + "$", "i");
	cache.set(glob, made);
	return made;
}

export function matchesFilePattern(glob, path) {
	const name = path.slice(path.lastIndexOf("/") + 1);
	return patternOf(glob).test(glob.includes("/") ? path : name);
}

// The viewers of `path` among `entries` ([{plugin: {id}, value}], contributions in load order) whose tab kind `hasKind`
// says is shown now: those offered only in Open With ("option") first, then the defaults, a later plugin's after an
// earlier's - so the last is the one a plain open uses.
export function viewersFor(path, entries, hasKind) {
	const found = [];
	for (const { plugin, value } of entries) {
		if (!value || typeof value.view !== "string" || !Array.isArray(value.patterns)) continue;
		if (!value.patterns.some((glob) => typeof glob === "string" && matchesFilePattern(glob, path))) continue;
		if (!hasKind(value.view)) continue;
		found.push({ id: value.id, title: value.title || value.id, view: value.view, priority: value.priority === "option" ? "option" : "default", plugin: plugin.id });
	}
	return found.filter((v) => v.priority === "option").concat(found.filter((v) => v.priority === "default"));
}

// The tab that opens `path`: its default viewer's ("<view>:<path>"), else the text editor's ("doc:<path>").
export function fileTabRef(path, entries, hasKind) {
	const viewers = viewersFor(path, entries, hasKind);
	const last = viewers[viewers.length - 1];
	return last && last.priority === "default" ? last.view + ":" + path : "doc:" + path;
}
