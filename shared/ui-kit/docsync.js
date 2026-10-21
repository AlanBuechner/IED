// docsync.js - one client's side of a server document (docs/10 "Document editing protocol"): the lease, the edits this
// client makes (sent one at a time, so the server applies them in order; the ones made meanwhile wait), the edits of
// other writers (another device, the AI, a reload from disk) moved over this client's pending ones before they reach the
// editor, saving, and starting over from a snapshot when an edit cannot be placed. An edit is acknowledged by its own
// doc.edit event, never by the response: events come in order, so no other writer's edit is passed over.
//     const sync = new DocumentSync(ied.client, target, { onState });
//     await sync.open(wsId, path);
//     sync.localEdit(changes);   // the editor changed; changes refer to the text before, as the protocol's do
// `target` is the editor: {setText(text), applyRemote(changes), setReadOnly(readOnly)}.

import { rebase, transformAfter, transformBefore, OverlapError } from "./textedits.js";

export class DocumentSync {
	constructor(client, target, options) {
		this.client = client;
		this.target = target;
		this.onState = (options && options.onState) || (() => {});
		this.docId = null;
		this.version = 0;
		this.holder = null;
		this.dirty = false;
		this.diskChanged = null;
		this.languageId = "plaintext";
		this.inflight = null;
		this.queue = [];
		this.unsubscribe = null;
		this.disposed = false;
		this.waiters = [];
		this.resyncing = false;
	}

	get holdsLease() {
		return this.holder === this.client.clientId;
	}

	get pending() {
		return (this.inflight ? 1 : 0) + this.queue.length;
	}

	// Opens the document and follows it; resolves with the first snapshot applied.
	async open(wsId, path) {
		const opened = await this.client.request("doc.open", { wsId, path });
		this.docId = opened.docId;
		return new Promise((resolve, reject) => {
			this.unsubscribe = this.client.subscribe("doc:" + this.docId, {
				snap: (data) => {
					this.snapshot(data);
					resolve(data);
				},
				evt: (kind, data) => this.event(kind, data),
				gone: (reason) => {
					this.state({ error: "This document is no longer open on the server (" + reason + ")." });
					reject(new Error("the document is gone"));
				},
			});
		});
	}

	dispose() {
		this.disposed = true;
		if (this.unsubscribe) this.unsubscribe();
	}

	// ---------------------------------------------------------------------------------------------------------------
	// From the server

	snapshot(data) {
		this.resyncing = false;
		this.version = data.version;
		this.holder = data.lease ? data.lease.holder : null;
		this.dirty = data.dirty;
		this.languageId = data.languageId || "plaintext";
		this.inflight = null;
		this.queue = [];
		this.wakeWaiters();
		this.target.setText(data.text);
		this.leaseChanged();
		this.state({});
	}

	// Events before the snapshot a resync waits for describe the text it is about to replace: they are passed over.
	event(kind, data) {
		if (this.resyncing) return;
		if (kind === "doc.edit") this.edited(data);
		else if (kind === "doc.lease") {
			this.holder = data.holder || null;
			this.leaseChanged();
		}
		else if (kind === "doc.saved") {
			this.dirty = false;
			this.diskChanged = null;
		}
		else if (kind === "doc.diskChanged") this.diskChanged = data.revision === 0 ? "deleted" : "changed";
		else if (kind === "doc.renamed") {
			this.state({ renamed: data.path });
			return;
		}
		this.state({});
	}

	edited(data) {
		if (this.inflight && data.opId === this.inflight.opId) {
			this.acknowledged(data.version);
			return;
		}
		if (data.version <= this.version) return;
		try {
			this.applyRemote(data.changes);
		}
		catch (e) {
			if (!(e instanceof OverlapError)) throw e;
			this.resync();
			return;
		}
		this.version = data.version;
		if (data.origin !== "disk") this.dirty = true;
		else if (this.pending === 0) this.dirty = false;
	}

	// Another writer's changes, made on the server before this client's pending ones: moved over those for the editor,
	// and the pending ones moved over them in turn. The server moves the edit in flight itself, but its moved form stays
	// the client's reference for the next remote change, which comes after this one.
	applyRemote(changes) {
		let remote = changes;
		const sets = [...(this.inflight ? [this.inflight.changes] : []), ...this.queue];
		const moved = [];
		for (const set of sets) {
			moved.push(rebase(set, [remote], transformAfter));
			remote = rebase(remote, [set], transformBefore);
		}
		if (this.inflight) this.inflight = { opId: this.inflight.opId, changes: moved.shift() };
		this.queue = moved;
		this.target.applyRemote(remote);
	}

	acknowledged(version) {
		this.version = Math.max(this.version, version);
		this.inflight = null;
		this.sendNext();
		this.wakeWaiters();
	}

	wakeWaiters() {
		if (this.pending > 0) return;
		const waiters = this.waiters;
		this.waiters = [];
		for (const resolve of waiters) resolve();
	}

	leaseChanged() {
		this.target.setReadOnly(!this.holdsLease);
	}

	// Starts over from the server's text: what was not yet applied there is dropped, and the person is told.
	resync() {
		if (this.unsubscribe) this.unsubscribe();
		this.resyncing = true;
		this.inflight = null;
		this.queue = [];
		this.wakeWaiters();
		this.state({ notice: "This document changed in a way your last edit could not follow; it now shows the server's text." });
		this.unsubscribe = this.client.subscribe("doc:" + this.docId, {
			snap: (data) => this.snapshot(data),
			evt: (kind, data) => this.event(kind, data),
			gone: () => this.state({ error: "This document is no longer open on the server." }),
		});
	}

	// ---------------------------------------------------------------------------------------------------------------
	// From the editor

	localEdit(changes) {
		if (!changes.length) return;
		this.queue.push(changes);
		this.dirty = true;
		this.state({});
		this.sendNext();
	}

	sendNext() {
		if (this.inflight || !this.queue.length || this.disposed) return;
		const changes = this.queue.shift();
		const opId = this.client.newOpId();
		this.inflight = { opId, changes };
		this.client.request("doc.edit", { docId: this.docId, baseVersion: this.version, changes }, { opId }).then(
			() => {},
			(error) => {
				if (!this.inflight || this.inflight.opId !== opId) return;
				this.state({ notice: "An edit was refused (" + error.code + ": " + error.message + ")." });
				this.resync();
			});
	}

	takeLease() {
		return this.client.request("doc.takeLease", { docId: this.docId }).then((result) => {
			this.holder = result.holder;
			this.leaseChanged();
			this.state({});
		});
	}

	// Saves once every edit made so far is on the server.
	async save() {
		await this.settled();
		const result = await this.client.request("doc.save", { docId: this.docId }, { opId: this.client.newOpId() });
		this.dirty = false;
		this.diskChanged = null;
		this.state({});
		return result;
	}

	// Resolves once every edit made so far is acknowledged (or dropped by a resync).
	settled() {
		if (this.pending === 0) return Promise.resolve();
		return new Promise((resolve) => this.waiters.push(resolve));
	}

	state(extra) {
		this.onState(Object.assign({ holder: this.holder, holdsLease: this.holdsLease, dirty: this.dirty, version: this.version, diskChanged: this.diskChanged, pending: this.pending }, extra));
	}
}
