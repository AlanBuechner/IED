// explorersearch.js - what Solution Explorer's search box keeps (VS "Search Solution Explorer", Ctrl+;, docs/15
// "Solution Explorer search"): a file, folder, project or solution group matches when its name holds the query (any
// case; a query with a '/' is looked for in the whole path), and every folder above a match stays, opened, to lead to
// it. The files come from the server's list of the workspace's files (fs.files, docs/10), so folders never opened are
// searched too; what that list skips (build outputs, vendored code, what .gitignore ignores) is not.

// The most files one search asks the server for: past it, the box says to type more.
export const SEARCH_LIMIT = 500;

// Whether `name` (or `path`, for a query with a '/') holds the query.
export function holds(name, path, query) {
	const wanted = query.trim().toLowerCase();
	if (!wanted) return false;
	return (wanted.includes("/") ? path : name).toLowerCase().includes(wanted);
}

// What `query` keeps of the workspace's files `paths` (workspace-relative, '/' separators): `matched`, the files and
// folders that match; `above`, the folders that lead to a match ("" the workspace's own folder).
export function searchPaths(paths, query) {
	const matched = new Set();
	const above = new Set();
	for (const path of paths) {
		const parts = path.split("/");
		for (let i = 1; i <= parts.length; i++) {
			const at = parts.slice(0, i).join("/");
			if (!holds(parts[i - 1], at, query)) continue;
			matched.add(at);
			for (let j = 0; j < i; j++) above.add(parts.slice(0, j).join("/"));
		}
	}
	return { matched, above };
}

// One search's answer for the tree: which rows stay and which are opened. `projects` are the solution's projects
// ({name, path}: path workspace-relative, "" the workspace's folder, null outside it), `groups` its groups.
export class SearchState {
	constructor(query, paths, solution, capped) {
		this.query = query;
		this.capped = capped;
		const found = searchPaths(paths, query);
		this.matched = found.matched;
		this.above = found.above;
		this.projects = new Set((solution ? solution.projects : []).filter((p) => holds(p.name, p.name, query)).map((p) => p.name));
		this.groups = new Set();
		if (solution) for (const group of solution.groups) this.collectGroups(group);
	}

	collectGroups(group) {
		if (holds(group.name, group.name, this.query)) this.groups.add(group.name);
		for (const inner of group.groups || []) this.collectGroups(inner);
	}

	// How many rows match.
	get count() {
		return this.matched.size + this.projects.size + this.groups.size;
	}

	// Whether the files and folders at `path` match or lead to a match.
	keepsPath(path) {
		return this.matched.has(path) || this.above.has(path);
	}

	// Whether a project at `path` stays: its name matches, or its folder holds a match.
	keepsProject(name, path) {
		if (this.projects.has(name)) return true;
		if (path === null || path === undefined) return false;
		return this.keepsPath(path);
	}

	// Whether the project at `path` is opened: something below its folder matches.
	opensProject(path) {
		return path !== null && path !== undefined && this.above.has(path);
	}

	// Whether a solution group stays: its name matches, or a project or group in it stays. `pathOf(name)` is a
	// project's path.
	keepsGroup(group, pathOf) {
		if (this.groups.has(group.name)) return true;
		if ((group.projects || []).some((name) => this.keepsProject(name, pathOf(name)))) return true;
		return (group.groups || []).some((inner) => this.keepsGroup(inner, pathOf));
	}

	// Whether a group is opened: something in it stays other than by the group's own name.
	opensGroup(group, pathOf) {
		if ((group.projects || []).some((name) => this.keepsProject(name, pathOf(name)))) return true;
		return (group.groups || []).some((inner) => this.keepsGroup(inner, pathOf));
	}
}

// `name` as text and marks around the parts that hold the query: [text | {mark: text}].
export function markedParts(name, query) {
	const wanted = query.trim().toLowerCase();
	if (!wanted || wanted.includes("/")) return [name];
	const parts = [];
	const lower = name.toLowerCase();
	let at = 0;
	for (let found = lower.indexOf(wanted); found >= 0; found = lower.indexOf(wanted, at)) {
		if (found > at) parts.push(name.slice(at, found));
		parts.push({ mark: name.slice(found, found + wanted.length) });
		at = found + wanted.length;
	}
	if (at < name.length) parts.push(name.slice(at));
	return parts;
}
