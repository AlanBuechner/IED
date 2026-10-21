// diagnostics.js - the workspaces' diagnostics on this page (docs/24 "diag:<wsId>"): each language server's, per file,
// from the topic's snapshot and its diag.set events, for the editors' markers and the Error List. A workspace is
// followed while something watches it.

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

	// Every diagnostic: [{wsId, path, source, item}], files in path order.
	all() {
		const rows = [];
		const files = [...this.files.values()].sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
		for (const file of files) {
			for (const [source, items] of file.sources) for (const item of items) rows.push({ wsId: file.wsId, path: file.path, source, item });
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
