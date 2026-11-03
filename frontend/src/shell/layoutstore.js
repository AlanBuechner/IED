// layoutstore.js - the layout document of one workspace and device class (docs/15 "Layout document"), kept on the server:
// read from the layout topic, changed locally, written with layout.set 400 ms after the last change, and no later than
// a second after the first one not written (a page that keeps changing still writes, so another opening sees it). A window
// holding a change the server does not have yet keeps it through what another window writes meanwhile: the two are merged
// (layoutmerge.js) and a conflict retries with the result (the same part changed by both goes to the last writer, docs/11).
// A change from another client waits while the person drags here. A change made before the server's document arrives waits
// for it and is made to it, so a page that opens a tab while it loads never writes an empty layout over everyone's.

import { LAYOUT_DOC_VERSION, emptyDocument } from "./layoutdoc.js";
import { mergeLayouts } from "./layoutmerge.js";

const SAVE_AFTER_MS = 400;
const SAVE_WITHIN_MS = 1000;
const CATCH_UP_MS = 300;   // how long a conflict waits for the change it names, on its way on the same socket

export class LayoutStore {
	// `onChange(doc)` runs for every document the store adopts or makes.
	constructor(client, wsId, deviceClass, onChange) {
		this.client = client;
		this.wsId = wsId;
		this.deviceClass = deviceClass;
		this.onChange = onChange;
		this.revision = 0;
		this.heard = 0;          // the newest revision whose document arrived
		this.doc = null;
		this.base = null;        // the server's document as last heard: what this window's changes are changes of
		this.saveTimer = null;
		this.saving = false;
		this.dirty = false;
		this.holding = false;
		this.held = null;
		this.early = [];
		this.caughtUp = null;    // what a conflict waits on
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
		this.heard = Math.max(this.heard, revision);
		if (this.caughtUp) this.caughtUp();
		const doc = layout && layout.v === LAYOUT_DOC_VERSION ? layout : emptyDocument();
		if (by !== null && by === this.client.clientId) {
			this.base = doc;
			return;
		}
		if (first) {
			this.base = doc;
			this.adopt(doc);
			for (const fn of this.early.splice(0)) this.update(fn);
			this.resolveReady(this.doc);
		}
		else if (this.holding) this.held = doc;
		else this.take(doc);
	}

	// Another window's document: this one's as it comes, or with what this window has not got written yet kept. What that
	// leaves different from the server's is written.
	take(doc) {
		const pending = this.dirty || this.saving;
		const next = pending ? mergeLayouts(this.base, this.doc, doc) : doc;
		this.base = doc;
		this.adopt(next);
		if (pending && JSON.stringify(next) !== JSON.stringify(doc)) this.changed();
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
		this.changed();
		this.onChange(next);
	}

	changed() {
		this.dirty = true;
		if (!this.unsavedSince) this.unsavedSince = Date.now();
		clearTimeout(this.saveTimer);
		this.saveTimer = setTimeout(() => this.save(), Math.max(0, Math.min(SAVE_AFTER_MS, this.unsavedSince + SAVE_WITHIN_MS - Date.now())));
	}

	// Writes now what would be written soon (another window waits for it); resolves once it is written.
	flush() {
		clearTimeout(this.saveTimer);
		return this.dirty ? this.save() : Promise.resolve();
	}

	// While held (a drag), changes from other clients wait.
	hold(on) {
		this.holding = on;
		if (!on && this.held) {
			const doc = this.held;
			this.held = null;
			this.take(doc);
		}
	}

	// A conflict names a revision whose document comes on the same socket: the retry waits for it, to keep it.
	awaitRevision(revision) {
		if (this.heard >= revision) return Promise.resolve();
		return new Promise((resolve) => {
			const timer = setTimeout(done, CATCH_UP_MS);
			const self = this;
			function done() {
				clearTimeout(timer);
				self.caughtUp = null;
				resolve();
			}
			this.caughtUp = () => this.heard >= revision && done();
		});
	}

	async save() {
		if (this.saving) return;
		this.saving = true;
		this.dirty = false;
		this.unsavedSince = 0;
		let doc = this.doc;
		try {
			for (let attempt = 0; attempt < 3; attempt++) {
				try {
					doc = this.doc;
					const result = await this.client.request("layout.set", { wsId: this.wsId, deviceClass: this.deviceClass, layout: doc, expectedRevision: this.revision }, { opId: this.client.newOpId() });
					this.revision = Math.max(this.revision, result.revision);
					if (result.revision >= this.heard) this.base = doc;
					return;
				}
				catch (e) {
					if (e.code !== "conflict") throw e;
					this.revision = e.data.revision;
					await this.awaitRevision(e.data.revision);
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
