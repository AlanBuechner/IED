// windowdoc.js - which window of a device holds which activity (docs/15 "Windows"), kept in the layout document as
// `windows`: [{id, device, activities: [activity id], activity, panel}]. An activity no window of the device names is
// docked to that device's main window, the window with no id (shown here as null), so a document with no `windows` is one
// window holding everything, and another device opening the same document has every activity in its own main window.
// Pure, like layoutdoc.js.

function listOf(doc) {
	return (doc && doc.windows) || [];
}

function withList(doc, windows) {
	const next = Object.assign({}, doc, { windows });
	if (!windows.length) delete next.windows;
	return next;
}

export function windowsOf(doc, device) {
	return listOf(doc).filter((w) => w.device === device);
}

export function windowOf(doc, device, id) {
	return listOf(doc).find((w) => w.device === device && w.id === id) || null;
}

// The id of the window of `device` holding `activityId`; null: its main window.
export function holderOf(doc, device, activityId) {
	const holder = windowsOf(doc, device).find((w) => w.activities.includes(activityId));
	return holder ? holder.id : null;
}

// The ids among `all` (in the sidebar's order) docked to window `id`.
export function activitiesOf(doc, device, id, all) {
	if (id) {
		const own = windowOf(doc, device, id);
		return own ? all.filter((a) => own.activities.includes(a)) : [];
	}
	const elsewhere = new Set(windowsOf(doc, device).flatMap((w) => w.activities));
	return all.filter((a) => !elsewhere.has(a));
}

// What window `id` shows of `here`, its activities: the one it last showed, else the first; null with none.
export function shownIn(doc, device, id, here) {
	const own = id ? windowOf(doc, device, id) : null;
	const stored = id ? (own ? own.activity : null) : doc && doc.activity;
	return stored && here.includes(stored) ? stored : here[0] || null;
}

// `doc` with `activityId` shown by the window holding it, wherever that is: only that window changes what it shows.
export function withShown(doc, device, activityId) {
	const holder = holderOf(doc, device, activityId);
	if (!holder) return doc.activity === activityId ? doc : Object.assign({}, doc, { activity: activityId });
	if (windowOf(doc, device, holder).activity === activityId) return doc;
	return withWindowChanged(doc, device, holder, (w) => Object.assign({}, w, { activity: activityId }));
}

// `doc` with `activityId` docked to window `to` (null: the main window) and shown there. A window it leaves stays, empty.
export function withMoved(doc, device, activityId, to) {
	const windows = listOf(doc).map((w) => {
		if (w.device !== device) return w;
		const others = w.activities.filter((a) => a !== activityId);
		if (w.id === to) return Object.assign({}, w, { activities: [...others, activityId], activity: activityId });
		return others.length === w.activities.length ? w : Object.assign({}, w, { activities: others });
	});
	return withList(to ? doc : Object.assign({}, doc, { activity: activityId }), windows);
}

// `doc` with an empty window `id` of `device`.
export function withWindow(doc, device, id) {
	if (windowOf(doc, device, id)) return doc;
	return withList(doc, [...listOf(doc), { id, device, activities: [], activity: null }]);
}

// `doc` without window `id`: its activities are the main window's again.
export function withoutWindow(doc, device, id) {
	if (!windowOf(doc, device, id)) return doc;
	return withList(doc, listOf(doc).filter((w) => w.device !== device || w.id !== id));
}

export function withWindowChanged(doc, device, id, change) {
	return withList(doc, listOf(doc).map((w) => (w.device === device && w.id === id ? change(w) : w)));
}

// An id for a new window of `device`: named after the activity it opens for, so that activity's window comes back where it
// was (the desktop app keeps a window's placement by its id, docs/27), and so that the ids of a device stay few.
export function newWindowId(doc, device, activityId) {
	const base = "w-" + String(activityId || "window").replace(/[^A-Za-z0-9._-]/g, "-");
	const taken = new Set(windowsOf(doc, device).map((w) => w.id));
	if (!taken.has(base)) return base;
	for (let n = 2; ; n++) if (!taken.has(base + "-" + n)) return base + "-" + n;
}
