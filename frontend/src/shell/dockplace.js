// dockplace.js - which dock host a new tab belongs to (docs/15 "Docking"): the one you are looking at when it takes
// tabs of its kind, else the first host naming that kind, else the first taking any. A tab therefore opens where its
// person is: a page from a chat stays in the Chat activity, one from the Editor in the Editor's documents.

// The tab kind of a ref "<kind>:<argument>" (a tool window id, which has none, is the whole id).
export function tabKindOf(ref) {
	const colon = ref ? ref.indexOf(":") : -1;
	return colon > 0 ? ref.slice(0, colon) : null;
}

// Whether `activity` is a dock host whose document well takes tabs of `kind`: hosts without a `view` hold documents,
// and their `documents` name the kinds they take - any kind when they name none.
export function takesKind(activity, kind) {
	return Boolean(activity) && !activity.view && (!activity.documents || activity.documents.includes(kind));
}

// The id of the dock host `kind` opens in: `current`'s when it takes it, else the first of `hosts` naming `kind`,
// else the first taking any kind, else the first at all; null when there is no host.
export function dockFor(kind, current, hosts) {
	if (takesKind(current, kind)) return current.id;
	const dockHosts = hosts.filter((a) => !a.view);
	const host = dockHosts.find((a) => takesKind(a, kind) && a.documents) || dockHosts.find((a) => takesKind(a, kind)) || dockHosts[0];
	return host ? host.id : null;
}
