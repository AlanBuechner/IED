// layouttree.js - an activity's layout tree (docs/15 "Layout document") and the operations on it. Pure: each takes a
// tree and returns a new, normalized one; the argument is never changed.
//
// node: {kind: "split", dir: "row" | "column", sizes, children} | {kind: "tabs", tabs, active, well?, preview?, dock?}
// `well` marks a document group (tabs on top, documents open into it); `preview` is its VS preview tab; `dock` is the
// region a tool-window group was made for ("left", "right", "bottom"), so a tool window added later finds it.

const EDGE_SIZES = { left: 0.2, right: 0.22, bottom: 0.3, top: 0.3 };

export function group(tabs, extra) {
	return Object.assign({ kind: "tabs", tabs: [...tabs], active: tabs[0] || null }, extra || {});
}

export function isGroup(node) {
	return Boolean(node) && node.kind === "tabs";
}

export function isSplit(node) {
	return Boolean(node) && node.kind === "split";
}

function clone(tree) {
	return JSON.parse(JSON.stringify(tree));
}

// ---------------------------------------------------------------------------------------------------------------
// Queries

export function groupsOf(tree) {
	const out = [];
	walk(tree, (node) => {
		if (isGroup(node)) out.push(node);
	});
	return out;
}

function walk(node, visit) {
	if (!node) return;
	visit(node);
	if (isSplit(node)) for (const child of node.children) walk(child, visit);
}

export function refsOf(tree) {
	return groupsOf(tree).flatMap((g) => g.tabs);
}

export function groupOf(tree, ref) {
	return groupsOf(tree).find((g) => g.tabs.includes(ref)) || null;
}

export function wellsOf(tree) {
	return groupsOf(tree).filter((g) => g.well);
}

// The path (child indexes) of the node `target` (by identity), or null.
export function pathOf(tree, target) {
	if (tree === target) return [];
	if (!isSplit(tree)) return null;
	for (let i = 0; i < tree.children.length; i++) {
		const inner = pathOf(tree.children[i], target);
		if (inner) return [i, ...inner];
	}
	return null;
}

export function nodeAt(tree, path) {
	let node = tree;
	for (const index of path) {
		if (!isSplit(node) || index < 0 || index >= node.children.length) return null;
		node = node.children[index];
	}
	return node;
}

function parentOf(tree, target) {
	const path = pathOf(tree, target);
	if (!path || path.length === 0) return null;
	return { parent: nodeAt(tree, path.slice(0, -1)), index: path[path.length - 1] };
}

// ---------------------------------------------------------------------------------------------------------------
// Normal form: every ref once; a group's active among its tabs; no empty group but one document group; splits of two or
// more children, never nested in a split of their own direction, with positive sizes summing to 1.

export function normalize(tree) {
	const next = clone(tree || null);
	const seen = new Set();
	let root = normalizeNode(next, seen);
	root = dropEmptyGroups(root);
	if (!root) return group([], { well: true });
	if (wellsOf(root).length === 0) root = withEdge(root, group([], { well: true }), "right", 0.5);
	return root;
}

function normalizeNode(node, seen) {
	if (isGroup(node)) {
		node.tabs = (Array.isArray(node.tabs) ? node.tabs : []).filter((ref) => typeof ref === "string" && !seen.has(ref) && seen.add(ref));
		if (!node.tabs.includes(node.active)) node.active = node.tabs[0] || null;
		if (node.preview && !node.tabs.includes(node.preview)) delete node.preview;
		if (!node.well) delete node.well;
		return node;
	}
	if (!isSplit(node) || !Array.isArray(node.children)) return null;
	const dir = node.dir === "column" ? "column" : "row";
	const sizes = fixSizes(node.sizes, node.children.length);
	const children = [];
	const kept = [];
	node.children.forEach((child, i) => {
		const normal = normalizeNode(child, seen);
		if (!normal) return;
		if (isSplit(normal) && normal.dir === dir) {
			normal.children.forEach((inner, j) => {
				children.push(inner);
				kept.push(sizes[i] * normal.sizes[j]);
			});
		}
		else {
			children.push(normal);
			kept.push(sizes[i]);
		}
	});
	return { kind: "split", dir, sizes: fixSizes(kept, children.length), children };
}

// Removes empty groups (all but one document group when no document group holds a tab) and collapses the splits left.
function dropEmptyGroups(root) {
	const wells = wellsOf(root);
	const keepEmptyWell = wells.every((w) => w.tabs.length === 0) ? wells[0] : null;
	return prune(root, (g) => g.tabs.length > 0 || g === keepEmptyWell);
}

function prune(node, keep) {
	if (isGroup(node)) return keep(node) ? node : null;
	if (!isSplit(node)) return null;
	const children = [];
	const sizes = [];
	node.children.forEach((child, i) => {
		const pruned = prune(child, keep);
		if (!pruned) return;
		if (isSplit(pruned) && pruned.dir === node.dir) {
			pruned.children.forEach((inner, j) => {
				children.push(inner);
				sizes.push(node.sizes[i] * pruned.sizes[j]);
			});
		}
		else {
			children.push(pruned);
			sizes.push(node.sizes[i]);
		}
	});
	if (children.length === 0) return null;
	if (children.length === 1) return children[0];
	return { kind: "split", dir: node.dir, sizes: fixSizes(sizes, children.length), children };
}

export function fixSizes(sizes, count) {
	const given = Array.isArray(sizes) && sizes.length === count ? sizes.map(Number) : [];
	const valid = given.length === count && given.every((s) => Number.isFinite(s) && s > 0);
	const raw = valid ? given : new Array(count).fill(1);
	const total = raw.reduce((a, b) => a + b, 0) || 1;
	return raw.map((s) => s / total);
}

// ---------------------------------------------------------------------------------------------------------------
// Tabs

// Opens `ref` (or activates it when the tree has it) in the document group at `groupPath`, the first one by default.
// A preview tab replaces the group's preview tab (VS); pinning a preview tab is `pinTab`.
export function openTab(tree, ref, options) {
	const opts = options || {};
	const next = clone(tree);
	const existing = groupOf(next, ref);
	if (existing) {
		existing.active = ref;
		if (!opts.preview && existing.preview === ref) delete existing.preview;
		return normalize(next);
	}
	const chosen = opts.groupPath ? nodeAt(next, opts.groupPath) : null;
	const target = isGroup(chosen) ? chosen : wellsOf(next)[0] || groupsOf(next)[0];
	const previewAt = opts.preview && target.preview ? target.tabs.indexOf(target.preview) : -1;
	if (previewAt >= 0) target.tabs[previewAt] = ref;
	else target.tabs.splice(target.active ? target.tabs.indexOf(target.active) + 1 : target.tabs.length, 0, ref);
	if (opts.preview) target.preview = ref;
	target.active = ref;
	return normalize(next);
}

export function activateTab(tree, ref) {
	const next = clone(tree);
	const owner = groupOf(next, ref);
	if (!owner) return tree;
	owner.active = ref;
	return normalize(next);
}

// Shows each of `partners` that is open in a group other than `ref`'s in its group (tethered tabs, docs/15): one tab of
// a group shows at a time, so a partner in `ref`'s own group stays behind it.
export function revealPartners(tree, ref, partners) {
	if (!partners.length) return tree;
	const next = clone(tree);
	const home = groupOf(next, ref);
	for (const partner of partners) {
		const owner = groupOf(next, partner);
		if (owner && owner !== home) owner.active = partner;
	}
	return normalize(next);
}

// Opens `ref` beside `anchor`: in another document group (the first that is not `anchor`'s), or in a new group to the
// right of `anchor`'s when there is none; a `ref` already open in another group is activated where it is.
export function openBeside(tree, anchor, ref) {
	const home = groupOf(tree, anchor);
	if (!home) return openTab(tree, ref, {});
	const existing = groupOf(tree, ref);
	if (existing && existing !== home) return activateTab(tree, ref);
	const other = wellsOf(tree).find((g) => g !== home);
	if (other && existing) return moveTab(tree, ref, pathOf(tree, other), "center");
	if (other) return openTab(tree, ref, { groupPath: pathOf(tree, other) });
	const opened = existing ? tree : openTab(tree, ref, { groupPath: pathOf(tree, home) });
	return activateTab(moveTab(opened, ref, pathOf(opened, groupOf(opened, anchor)), "right"), anchor);
}

// Keeps a preview tab open (it was edited or double-clicked).
export function pinTab(tree, ref) {
	const next = clone(tree);
	const owner = groupOf(next, ref);
	if (!owner || owner.preview !== ref) return tree;
	delete owner.preview;
	return normalize(next);
}

export function closeTab(tree, ref) {
	const next = clone(tree);
	const owner = groupOf(next, ref);
	if (!owner) return tree;
	removeRef(owner, ref);
	return normalize(next);
}

// Closes the other tabs of `ref`'s group, or every tab of it with `including`.
export function closeGroupTabs(tree, ref, including) {
	const next = clone(tree);
	const owner = groupOf(next, ref);
	if (!owner) return tree;
	owner.tabs = including ? [] : [ref];
	owner.active = including ? null : ref;
	return normalize(next);
}

function removeRef(owner, ref) {
	const index = owner.tabs.indexOf(ref);
	owner.tabs.splice(index, 1);
	if (owner.active === ref) owner.active = owner.tabs[Math.min(index, owner.tabs.length - 1)] || null;
	if (owner.preview === ref) delete owner.preview;
}

// ---------------------------------------------------------------------------------------------------------------
// Docking

// Moves `ref` to the group at `targetPath`: `zone` "center" joins it at `index` (the end by default); "left", "right",
// "top", "bottom" split it with a new group. An empty `targetPath` with an edge zone docks at the activity's edge.
export function moveTab(tree, ref, targetPath, zone, index) {
	const next = clone(tree);
	const source = groupOf(next, ref);
	const target = nodeAt(next, targetPath || []);
	if (!source || !target) return tree;
	if (zone === "center") {
		if (!isGroup(target)) return tree;
		const sourceIndex = source.tabs.indexOf(ref);
		removeRef(source, ref);
		let at = index === undefined || index === null ? target.tabs.length : Math.max(0, Math.min(index, target.tabs.length + 1));
		if (source === target && sourceIndex < at) at -= 1;
		target.tabs.splice(Math.min(at, target.tabs.length), 0, ref);
		target.active = ref;
		return normalize(next);
	}
	if (source === target && source.tabs.length === 1) return tree;
	removeRef(source, ref);
	const made = group([ref], source.well ? { well: true } : {});
	const atEdge = (targetPath || []).length === 0 && isSplit(target);
	return normalize(atEdge ? withEdge(next, made, zone, EDGE_SIZES[zone]) : beside(next, target, made, zone));
}

const ZONE_DIR = { left: "row", right: "row", top: "column", bottom: "column" };

// `made` next to `target` on the `zone` side, taking `share` of target's room (half by default): in target's split when
// it runs that way, else in a new split of the two.
function beside(root, target, made, zone, share) {
	const part = share || 0.5;
	const dir = ZONE_DIR[zone];
	const before = zone === "left" || zone === "top";
	const place = parentOf(root, target);
	if (place && place.parent.dir === dir) {
		const room = place.parent.sizes[place.index];
		place.parent.sizes[place.index] = room * (1 - part);
		place.parent.children.splice(before ? place.index : place.index + 1, 0, made);
		place.parent.sizes.splice(before ? place.index : place.index + 1, 0, room * part);
		return root;
	}
	const split = { kind: "split", dir, sizes: before ? [part, 1 - part] : [1 - part, part], children: before ? [made, target] : [target, made] };
	if (!place) return split;
	place.parent.children[place.index] = split;
	return root;
}

// `made` along one edge of the whole tree, taking `share` of it.
function withEdge(root, made, zone, share) {
	const dir = ZONE_DIR[zone];
	const before = zone === "left" || zone === "top";
	if (isSplit(root) && root.dir === dir) {
		root.sizes = root.sizes.map((s) => s * (1 - share));
		if (before) {
			root.children.unshift(made);
			root.sizes.unshift(share);
		}
		else {
			root.children.push(made);
			root.sizes.push(share);
		}
		return root;
	}
	return { kind: "split", dir, sizes: before ? [share, 1 - share] : [1 - share, share], children: before ? [made, root] : [root, made] };
}

export function setSizes(tree, splitPath, sizes) {
	const next = clone(tree);
	const split = nodeAt(next, splitPath);
	if (!isSplit(split) || !Array.isArray(sizes) || sizes.length !== split.children.length) return tree;
	split.sizes = fixSizes(sizes, sizes.length);
	return normalize(next);
}

// ---------------------------------------------------------------------------------------------------------------
// Tool windows

// The tree with tool window `id` in it, placed by `dock` ("left", "right", "bottom", "document") when it is missing:
// in the group made for that dock, else in a new one. `activate` makes it the active tab.
export function ensureTool(tree, id, dock, activate) {
	if (refsOf(tree).includes(id)) return activate ? activateTab(tree, id) : tree;
	const next = clone(tree);
	if (dock === "document" || !EDGE_SIZES[dock]) {
		const well = wellsOf(next)[0] || groupsOf(next)[0];
		well.tabs.push(id);
		if (activate || !well.active) well.active = id;
		return normalize(next);
	}
	const home = groupsOf(next).find((g) => g.dock === dock);
	if (home) {
		home.tabs.push(id);
		if (activate || !home.active) home.active = id;
		return normalize(next);
	}
	const made = group([id], { dock });
	if (dock === "bottom") {
		const well = wellsOf(next)[0];
		if (well) return normalize(beside(next, well, made, "bottom", EDGE_SIZES.bottom));
	}
	return normalize(withEdge(next, made, dock, EDGE_SIZES[dock]));
}

// The VS starting point: tool windows by their dock around one document group, the first of each group active.
export function defaultTree(toolWindows) {
	let tree = group([], { well: true });
	for (const tool of toolWindows) tree = ensureTool(tree, tool.id, tool.dock, false);
	return tree;
}
