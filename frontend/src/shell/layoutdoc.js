// layoutdoc.js - the whole layout document (docs/15): which activity shows, each dock host's tree in each layout mode,
// the phone's pane and back stack. Pure, like layouttree.js.

import { defaultTree, normalize } from "./layouttree.js";

export const LAYOUT_DOC_VERSION = 1;
// The layout mode whose trees are the document's `activities`; another's are under `modes.<id>.activities`, and the
// document's `mode` names the one shown, whose trees hold the documents as they are (docs/15 "Window layouts").
export const DEFAULT_MODE = "default";

export function emptyDocument() {
	return { v: LAYOUT_DOC_VERSION, activity: null, activities: {}, phone: { pane: null, back: [] } };
}

// The tree of a dock host in layout mode `mode`: the stored one, or the default from `tools` ([{id, dock, group,
// autoHide, open}] of the activity, placed for that mode), its tool windows auto-hidden with `autoHide` (a tablet's). A
// tool window the stored tree lacks (closed, or new) comes back when it is shown (ensureTool).
export function treeOf(doc, activityId, tools, autoHide, mode) {
	const held = treesOf(doc, mode);
	const stored = held ? held[activityId] : null;
	return stored ? normalize(stored) : defaultTree(tools, autoHide);
}

function treesOf(doc, mode) {
	if (!doc) return null;
	if (!mode || mode === DEFAULT_MODE) return doc.activities || null;
	return doc.modes && doc.modes[mode] ? doc.modes[mode].activities || null : null;
}

function withTrees(doc, mode, activities) {
	if (!mode || mode === DEFAULT_MODE) return Object.assign({}, doc, { activities });
	const modes = Object.assign({}, doc.modes);
	modes[mode] = Object.assign({}, modes[mode], { activities });
	return Object.assign({}, doc, { modes });
}

export function withTree(doc, activityId, tree, mode) {
	return withTrees(doc, mode, Object.assign({}, treesOf(doc, mode), { [activityId]: tree }));
}

export function withoutTree(doc, activityId, mode) {
	const activities = Object.assign({}, treesOf(doc, mode));
	delete activities[activityId];
	return withTrees(doc, mode, activities);
}

export function withActivity(doc, activityId) {
	return doc.activity === activityId ? doc : Object.assign({}, doc, { activity: activityId });
}

// Tethered tabs (docs/15 "Tethered tabs"): pairs that show together - activating one shows the other in its own group -
// and, when `closes`, the follower closes with its lead (a Markdown preview with its file).
export function withTether(doc, lead, follower, closes) {
	const others = (doc.tethers || []).filter((t) => !((t.lead === lead && t.follower === follower) || (t.lead === follower && t.follower === lead)));
	return Object.assign({}, doc, { tethers: [...others, { lead, follower, closes: Boolean(closes) }] });
}

// Without the tethers of `refs` (tabs that closed, or a tab untethered).
export function withoutTethers(doc, refs) {
	const gone = new Set(refs);
	const tethers = (doc.tethers || []).filter((t) => !gone.has(t.lead) && !gone.has(t.follower));
	return tethers.length === (doc.tethers || []).length ? doc : Object.assign({}, doc, { tethers });
}

// `from` renamed `to` wherever the document names a tab outside the trees: its tethers, a phone's pane and back stack.
export function withRenamedTab(doc, from, to) {
	const swap = (ref) => (ref === from ? to : ref);
	const tethers = (doc.tethers || []).map((t) => Object.assign({}, t, { lead: swap(t.lead), follower: swap(t.follower) }));
	const phone = doc.phone ? { pane: swap(doc.phone.pane), back: (doc.phone.back || []).map(swap) } : doc.phone;
	return Object.assign({}, doc, doc.tethers ? { tethers } : {}, doc.phone ? { phone } : {});
}

export function partnersOf(doc, ref) {
	return (doc.tethers || []).filter((t) => t.lead === ref || t.follower === ref).map((t) => (t.lead === ref ? t.follower : t.lead));
}

// The tabs that close when `ref` does.
export function followersOf(doc, ref) {
	return (doc.tethers || []).filter((t) => t.lead === ref && t.closes).map((t) => t.follower);
}

const BACK_LIMIT = 20;

// The phone shows `pane` (an activity id or a tab ref); the pane it leaves goes on the back stack.
export function withPhonePane(doc, pane) {
	const phone = doc.phone || { pane: null, back: [] };
	if (phone.pane === pane) return doc;
	const back = phone.pane ? [...phone.back.filter((p) => p !== phone.pane), phone.pane].slice(-BACK_LIMIT) : phone.back;
	return Object.assign({}, doc, { phone: { pane, back: back.filter((p) => p !== pane) } });
}

// Without `ref` in the phone's back stack (a tab closed while another shows).
export function withoutPhoneBack(doc, ref) {
	const phone = doc.phone || { pane: null, back: [] };
	if (!phone.back.includes(ref)) return doc;
	return Object.assign({}, doc, { phone: { pane: phone.pane, back: phone.back.filter((p) => p !== ref) } });
}

export function phoneBack(doc) {
	const phone = doc.phone || { pane: null, back: [] };
	if (!phone.back.length) return doc;
	const back = phone.back.slice(0, -1);
	return Object.assign({}, doc, { phone: { pane: phone.back[phone.back.length - 1], back } });
}
