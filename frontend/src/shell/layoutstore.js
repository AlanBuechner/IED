// layoutstore.js - the layout document of one workspace and device class (docs/15 "Layout document"), kept on the server:
// read from the layout topic, changed locally, written with layout.set 400 ms after the last change, and no later than
// a second after the first one not written (a page that keeps changing still writes, so another opening sees it). A conflict
// retries on the server's revision (last writer wins, docs/11); a change from another client is taken unless the
// person is dragging. A change made before the server's document arrives waits for it and is made to it, so a page
// that opens a tab while it loads never writes an empty layout over everyone's.

import { LAYOUT_DOC_VERSION, emptyDocument } from "./layoutdoc.js";

const SAVE_AFTER_MS = 400;
const SAVE_WITHIN_MS = 1000;

export class LayoutStore {
	// `onChange(doc)` runs for every document the store adopts or makes.
	constructor(client, wsId, deviceClass, onChange) {
		this.client = client;
		this.wsId = wsId;
		this.deviceClass = deviceClass;
		this.onChange = onChange;
		this.revision = 0;
		this.doc = null;
		this.saveTimer = null;
		this.saving = false;
		this.dirty = false;
		this.holding = false;
		this.held = null;
		this.early = [];
		this.unsavedSince = 0;   // when the first change not yet written was made
		this.ready = new Promise((resolve) => {
			this.resolveReady = resolve;
		});
		this.unsubscribe = client.subscribe(this.topic, {
			snap: (data) => this.received(data.revision, data.layout, null),
			evt: (kind, data) => {
				if (kind === "layout.changed") this.received(data.revision, data.layout, data.by);
			},
			gone: () => {},
		});
	}

	get topic() {
		return "layout:" + this.wsId + ":" + this.deviceClass;
	}

	dispose() {
		clearTimeout(this.saveTimer);
		if (this.dirty) this.save();
		this.unsubscribe();
	}

	received(revision, layout, by) {
		const first = this.doc === null;
		this.revision = Math.max(this.revision, revision);
		const mine = by !== null && by === this.client.clientId;
		if (mine || (this.dirty && !first)) return this.settle(first);
		const doc = layout && layout.v === LAYOUT_DOC_VERSION ? layout : emptyDocument();
		if (this.holding && !first) this.held = doc;
		else this.adopt(doc);
		if (first) for (const fn of this.early.splice(0)) this.update(fn);
		return this.settle(first);
	}

	settle(first) {
		if (first) this.resolveReady(this.doc);
	}

	adopt(doc) {
		this.doc = doc;
		this.onChange(doc);
	}

	// Changes the document: `fn(doc)` returns the new one.
	update(fn) {
		if (this.doc === null) {
			this.early.push(fn);
			return;
		}
		const next = fn(this.doc);
		if (next === this.doc) return;
		this.doc = next;
		this.dirty = true;
		if (!this.unsavedSince) this.unsavedSince = Date.now();
		this.onChange(next);
		clearTimeout(this.saveTimer);
		this.saveTimer = setTimeout(() => this.save(), Math.max(0, Math.min(SAVE_AFTER_MS, this.unsavedSince + SAVE_WITHIN_MS - Date.now())));
	}

	// While held (a drag), changes from other clients wait.
	hold(on) {
		this.holding = on;
		if (!on && this.held) {
			const doc = this.held;
			this.held = null;
			if (!this.dirty) this.adopt(doc);
		}
	}

	async save() {
		if (this.saving) return;
		this.saving = true;
		this.dirty = false;
		this.unsavedSince = 0;
		const doc = this.doc;
		try {
			for (let attempt = 0; attempt < 3; attempt++) {
				try {
					const result = await this.client.request("layout.set", { wsId: this.wsId, deviceClass: this.deviceClass, layout: doc, expectedRevision: this.revision }, { opId: this.client.newOpId() });
					this.revision = Math.max(this.revision, result.revision);
					return;
				}
				catch (e) {
					if (e.code !== "conflict") throw e;
					this.revision = e.data.revision;
				}
			}
		}
		catch (e) {
			this.dirty = true;
			console.warn("IED: the layout could not be saved:", e.message);
		}
		finally {
			this.saving = false;
			if (this.dirty && this.doc !== doc) this.save();
		}
	}
}
