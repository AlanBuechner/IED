// diagnostics.js - the workspaces' diagnostics on this page (docs/24 "diag:<wsId>"): each language server's and each
// build's problem matcher's, per file, from the topic's snapshot and its diag.set events, for the editors' markers and
// the Error List. A workspace is followed while something watches it.

// The same problem from several sources (a build's matcher and a language server, docs/06) shown once: per severity,
// start and message, the item with the widest range - a language server's token where a matcher marks one character.
// Map(source -> items) in, the same shape out.
export function distinctItems(sources) {
	const chosen = new Map();
	for (const [source, items] of sources) {
		for (const item of items) {
			const key = [item.severity, item.range.start.line, item.range.start.character, item.message].join("\u0000");
			const kept = chosen.get(key);
			if (!kept || extent(item.range) > extent(kept.item.range)) chosen.set(key, { source, item });
		}
	}
	const shown = new Map();
	for (const { source, item } of chosen.values()) {
		if (!shown.has(source)) shown.set(source, []);
		shown.get(source).push(item);
	}
	return shown;
}

function extent(range) {
	return (range.end.line - range.start.line) * 1e6 + (range.end.character - range.start.character);
}

export class Diagnostics {
	constructor(ied) {
		this.ied = ied;
		this.files = new Map();     // "<wsId> <path>" -> {wsId, path, sources: Map(source -> items)}
		this.watched = new Map();   // wsId -> {count, stop}
		this.listeners = new Set();
	}

	// Follows `wsId`; the returned function stops this watch.
	watch(wsId) {
		let entry = this.watched.get(wsId);
		if (!entry) {
			entry = { count: 0, stop: null };
			this.watched.set(wsId, entry);
			entry.stop = this.ied.client.subscribe("diag:" + wsId, {
				snap: (data) => this.reset(wsId, data.files || {}),
				evt: (kind, data) => {
					if (kind === "diag.set") this.set(wsId, data.path, data.source, data.items);
				},
				gone: () => this.reset(wsId, {}),
			});
		}
		entry.count += 1;
		let stopped = false;
		return () => {
			if (stopped) return;
			stopped = true;
			entry.count -= 1;
			if (entry.count > 0) return;
			entry.stop();
			this.watched.delete(wsId);
			this.reset(wsId, {});
		};
	}

	reset(wsId, files) {
		for (const [key, file] of this.files) if (file.wsId === wsId) this.files.delete(key);
		for (const [path, sources] of Object.entries(files)) {
			for (const [source, items] of Object.entries(sources)) this.store(wsId, path, source, items);
		}
		this.changed(wsId, null);
	}

	set(wsId, path, source, items) {
		this.store(wsId, path, source, items);
		this.changed(wsId, path);
	}

	store(wsId, path, source, items) {
		const key = wsId + " " + path;
		let file = this.files.get(key);
		if (!file) {
			file = { wsId, path, sources: new Map() };
			this.files.set(key, file);
		}
		if (items && items.length) file.sources.set(source, items);
		else file.sources.delete(source);
		if (!file.sources.size) this.files.delete(key);
	}

	// A file's diagnostics by source: Map(source -> items).
	of(wsId, path) {
		const file = this.files.get(wsId + " " + path);
		return file ? file.sources : new Map();
	}

	// A file's diagnostics as shown: each problem once (distinctItems).
	shown(wsId, path) {
		return distinctItems(this.of(wsId, path));
	}

	// Every diagnostic as shown: [{wsId, path, source, item}], files in path order.
	all() {
		const rows = [];
		const files = [...this.files.values()].sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
		for (const file of files) {
			for (const [source, items] of distinctItems(file.sources)) for (const item of items) rows.push({ wsId: file.wsId, path: file.path, source, item });
		}
		return rows;
	}

	// fn(wsId, path): `path` changed, or every file of `wsId` when null.
	onChange(fn) {
		this.listeners.add(fn);
		return () => this.listeners.delete(fn);
	}

	changed(wsId, path) {
		for (const fn of [...this.listeners]) fn(wsId, path);
	}
}
