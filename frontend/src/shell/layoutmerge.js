// layoutmerge.js - two windows changing one layout document (docs/15 "Windows"): a window whose own change is not yet
// written when another window's arrives keeps both, slice by slice. A slice is a dock host's tree, a window's field or any
// other top-level field: the slice this window changed is its own, every other is the other window's. The same slice
// changed by both goes to the last writer. Pure, like layoutdoc.js.

function same(a, b) {
	return JSON.stringify(a) === JSON.stringify(b);
}

function pick(base, mine, theirs) {
	return same(mine, base) ? theirs : mine;
}

function mergedKeys(base, mine, theirs, merge) {
	const out = {};
	for (const key of new Set([...Object.keys(theirs || {}), ...Object.keys(mine || {}), ...Object.keys(base || {})])) {
		const value = merge((base || {})[key], (mine || {})[key], (theirs || {})[key]);
		if (value !== undefined) out[key] = value;
	}
	return out;
}

function trees(base, mine, theirs) {
	return mergedKeys(base, mine, theirs, pick);
}

function mergedMode(base, mine, theirs) {
	if (!mine || !theirs) return pick(base, mine, theirs);
	return Object.assign({}, mine, { activities: trees(base && base.activities, mine.activities, theirs.activities) });
}

function byWindow(list) {
	return Object.fromEntries((list || []).map((w) => [w.device + "\n" + w.id, w]));
}

function mergedWindow(base, mine, theirs) {
	return mine && theirs ? mergedKeys(base, mine, theirs, pick) : pick(base, mine, theirs);
}

// An activity is docked to one window of a device: two moves made at once leave it with the first.
function eachInOne(windows) {
	const seen = new Set();
	return windows.map((w) => {
		const activities = w.activities.filter((a) => {
			const key = w.device + "\n" + a;
			if (seen.has(key)) return false;
			seen.add(key);
			return true;
		});
		return activities.length === w.activities.length ? w : Object.assign({}, w, { activities });
	});
}

function setOrRemove(doc, key, value, present) {
	if (present) doc[key] = value;
	else delete doc[key];
}

// `mine` (`base` with this window's changes) over `theirs` (`base` with another window's).
export function mergeLayouts(base, mine, theirs) {
	const out = mergedKeys(base, mine, theirs, pick);
	out.activities = trees(base.activities, mine.activities, theirs.activities);
	const modes = mergedKeys(base.modes, mine.modes, theirs.modes, mergedMode);
	setOrRemove(out, "modes", modes, Object.keys(modes).length > 0);
	const windows = eachInOne(Object.values(mergedKeys(byWindow(base.windows), byWindow(mine.windows), byWindow(theirs.windows), mergedWindow)));
	setOrRemove(out, "windows", windows, windows.length > 0);
	return out;
}
