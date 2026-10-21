// layoutdoc.js - the whole layout document (docs/15): which activity shows, each dock host's tree, the phone's pane and
// back stack. Pure, like layouttree.js.

import { defaultTree, normalize } from "./layouttree.js";

export const LAYOUT_DOC_VERSION = 1;

export function emptyDocument() {
	return { v: LAYOUT_DOC_VERSION, activity: null, activities: {}, phone: { pane: null, back: [] } };
}

// The tree of a dock host: the stored one, or the default from `tools` ([{id, dock}] of the activity). A tool window
// the stored tree lacks (closed, or new) comes back when it is shown (ensureTool).
export function treeOf(doc, activityId, tools) {
	const stored = doc && doc.activities ? doc.activities[activityId] : null;
	return stored ? normalize(stored) : defaultTree(tools);
}

export function withTree(doc, activityId, tree) {
	const next = Object.assign({}, doc, { activities: Object.assign({}, doc.activities, { [activityId]: tree }) });
	return next;
}

export function withoutTree(doc, activityId) {
	const activities = Object.assign({}, doc.activities);
	delete activities[activityId];
	return Object.assign({}, doc, { activities });
}

export function withActivity(doc, activityId) {
	return doc.activity === activityId ? doc : Object.assign({}, doc, { activity: activityId });
}

const BACK_LIMIT = 20;

// The phone shows `pane` (an activity id or a tab ref); the pane it leaves goes on the back stack.
export function withPhonePane(doc, pane) {
	const phone = doc.phone || { pane: null, back: [] };
	if (phone.pane === pane) return doc;
	const back = phone.pane ? [...phone.back.filter((p) => p !== phone.pane), phone.pane].slice(-BACK_LIMIT) : phone.back;
	return Object.assign({}, doc, { phone: { pane, back: back.filter((p) => p !== pane) } });
}

export function phoneBack(doc) {
	const phone = doc.phone || { pane: null, back: [] };
	if (!phone.back.length) return doc;
	const back = phone.back.slice(0, -1);
	return Object.assign({}, doc, { phone: { pane: phone.back[phone.back.length - 1], back } });
}
