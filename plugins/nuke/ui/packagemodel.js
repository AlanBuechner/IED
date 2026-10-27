// packagemodel.js - what the Package Manager shows, from what `nuke package status --json` and `browse --json` print
// (Nuke/README section 12): its rows, the update a package has, the requirement a chosen version writes, and the words
// of each `nuke package` action.

// Whether two spellings name one repository (Nuke's url_key, loosely: case, slashes, a trailing ".git").
export function sameRepository(a, b) {
	const key = (url) => String(url || "").trim().replace(/\\/g, "/").replace(/\/+$/, "").replace(/\.git$/i, "").toLowerCase();
	return key(a) === key(b);
}

// "claymore:Windowing", or the source alone for a repository that is one package.
export function packageName(source, path) {
	return path ? source + ":" + path : source;
}

// The versioned package's own description (its line's manifest), else the first of the packages it brings.
export function descriptionOf(item) {
	const packages = item.packages || [];
	const own = packages.find((p) => p.path === item.path) || packages[0];
	return own ? own.description || "" : "";
}

// The workspace's requirement ([packages]) that brings in the versioned package, or null when only other packages do.
export function requirementOf(body, item) {
	return (body.requirements || []).find((r) => sameRepository(r.url, item.url) && (item.uses || []).includes(r.path)) || null;
}

// The develop clone of the package's source, or null.
export function checkoutOf(body, item) {
	return (body.checkouts || []).find((c) => sameRepository(c.url, item.url)) || null;
}

// What an update gives: the newest release `update` reaches ({version, breaking: false}), else a newer one only a
// changed requirement reaches ({version, breaking: true}); null when the package is at its newest.
export function updateOf(item) {
	if (item.compatible) return { version: item.compatible, breaking: false };
	if (item.newest) return { version: item.newest, breaking: true };
	return null;
}

function parts(version) {
	const match = /^(\d+)\.(\d+)\.(\d+)/.exec(String(version || ""));
	return match ? [Number(match[1]), Number(match[2]), Number(match[3])] : null;
}

// Whether `later` keeps `version`'s API (Cargo's ^): the same major, or for 0.x the same minor (0.0.x: the same patch).
export function compatible(version, later) {
	const a = parts(version);
	const b = parts(later);
	if (!a || !b) return false;
	if (a[0] > 0) return a[0] === b[0];
	if (a[1] > 0) return b[0] === 0 && a[1] === b[1];
	return b[0] === 0 && b[1] === 0 && a[2] === b[2];
}

// The requirement choosing `version` writes: ^version when it is the newest of its compatible releases (later fixes
// still come with updates), =version for an older one (^ would keep the newer release locked).
export function requirementFor(versions, version) {
	const newer = (versions || []).filter((v) => v !== version && compatible(version, v) && isNewer(v, version));
	return (newer.length ? "=" : "^") + version;
}

function isNewer(a, b) {
	const x = parts(a);
	const y = parts(b);
	for (let i = 0; i < 3; i += 1) if (x[i] !== y[i]) return x[i] > y[i];
	return false;
}

// What a requirement means, for the line under the version picker.
export function requirementMeaning(requirement) {
	if (requirement.startsWith("=")) return "exactly " + requirement.slice(1) + "; updates leave it there";
	if (requirement.startsWith("^")) return requirement.slice(1) + " or a newer compatible release";
	return requirement;
}

// The tag a release of `line` has (Go's convention, docs/31).
export function tagOf(line, version) {
	return line ? line + "/v" + version : "v" + version;
}

// The page of `line` at `version` on the source's host ({web} from status or browse), or null for a local source.
export function treeLink(web, line, version) {
	return web ? web + "/tree/" + tagOf(line, version) + (line ? "/" + line : "") : null;
}

// The source a [dependencies] entry names the versioned package by: "<repository URL>/<its folder>".
export function sourceOf(item) {
	return item.path ? item.url + "/" + item.path : item.url;
}

// The words that make a project's [dependencies] entry `source` want `requirement` (`project`: the one declaring it,
// else Nuke's choice - the workspace's default project).
export function addWords(source, requirement, project) {
	return ["add", source, requirement].concat(project ? ["--project", project] : []);
}

// Rows of the Installed tab: every versioned package the lock holds, by name.
export function installedRows(body) {
	return (body.packages || []).map((item) => ({ key: packageName(item.source, item.path), item })).sort((a, b) => a.key.localeCompare(b.key));
}

// Rows of the Updates tab: those with a newer release.
export function updateRows(body) {
	return installedRows(body).filter((row) => updateOf(row.item) !== null);
}

// Rows of a source's packages (browse --json), by path.
export function browseRows(result) {
	return (result.packages || []).map((item) => ({ key: packageName(result.source, item.path), item })).sort((a, b) => a.key.localeCompare(b.key));
}

// The rows whose name or description has `text` (any case).
export function filtered(rows, text) {
	const wanted = String(text || "").trim().toLowerCase();
	if (!wanted) return rows;
	return rows.filter((row) => row.key.toLowerCase().includes(wanted) || (row.item.description || descriptionOf(row.item)).toLowerCase().includes(wanted));
}

// The installed row that brings package `path` (a Solution Explorer package node), or null.
export function rowBringing(body, path) {
	return installedRows(body).find((row) => (row.item.uses || []).includes(path) || row.item.path === path) || null;
}
