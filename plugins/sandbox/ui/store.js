// store.js - one workspace's sandboxes as the server has them (topic sandbox:<wsId>, docs/34), shared by every view of
// the Sandbox activity and its sidebar badge: the snapshot, the runs' outputs kept across "changed" events, the chats
// the AI's runs came from (their titles, from chat.list), and which run the Activity view shows. Nothing here is the
// truth: a reload gets all of it from the server again.

import { appendOutput, keepOutputs } from "./model.js";

const stores = new Map();   // wsId -> SandboxStore

export function storeFor(ied, wsId) {
	let store = stores.get(wsId);
	if (!store) {
		store = new SandboxStore(ied, wsId);
		stores.set(wsId, store);
	}
	return store;
}

export class SandboxStore {
	constructor(ied, wsId) {
		this.ied = ied;
		this.wsId = wsId;
		this.snapshot = null;
		this.outputs = new Map();     // runId -> text
		this.chatTitles = new Map();  // chatId -> title
		this.asked = new Set();       // chatIds chat.list was asked about
		this.listeners = new Set();
		this.outputListeners = new Set();
		this.shownRun = null;         // the run the Activity view shows; null: it follows the newest
		this.problem = "";
		this.catalog = null;
		this.stop = ied.client.subscribe("sandbox:" + wsId, {
			snap: (data) => this.received(data, true),
			evt: (kind, data) => {
				if (kind === "changed") this.received(data, false);
				else if (kind === "output") this.output(data);
			},
		});
	}

	// `fn()` after each change; returns the function that stops it.
	on(fn) {
		this.listeners.add(fn);
		return () => this.listeners.delete(fn);
	}

	// `fn(runId, text)` for each piece of output; returns the function that stops it.
	onOutput(fn) {
		this.outputListeners.add(fn);
		return () => this.outputListeners.delete(fn);
	}

	changed() {
		for (const fn of [...this.listeners]) {
			try {
				fn();
			}
			catch (e) {
				console.error(e);
			}
		}
	}

	received(data, fromSnapshot) {
		this.snapshot = data;
		keepOutputs(this.outputs, data.runs, fromSnapshot);
		this.learnChats(data.runs);
		this.changed();
	}

	output(data) {
		const text = appendOutput(this.outputs, data.runId, data.text || "");
		for (const fn of [...this.outputListeners]) fn(data.runId, text);
	}

	// The titles of the chats the AI's runs came from, asked of the chat plugin once per chat it does not know yet.
	learnChats(runs) {
		const unknown = (runs || []).some((run) => run.chatId && !this.chatTitles.has(run.chatId) && !this.asked.has(run.chatId));
		if (!unknown) return;
		for (const run of runs || []) if (run.chatId) this.asked.add(run.chatId);
		this.ied.client.request("chat.list", { wsId: this.wsId }).then((list) => {
			for (const chat of (list && list.chats) || []) this.chatTitles.set(chat.chatId, chat.title || "Chat");
			for (const chat of (list && list.archived) || []) this.chatTitles.set(chat.chatId, chat.title || "Chat");
			this.changed();
		}, () => {});
	}

	sandboxes() {
		return (this.snapshot && this.snapshot.sandboxes) || [];
	}

	runs() {
		return (this.snapshot && this.snapshot.runs) || [];
	}

	sandbox(name) {
		return this.sandboxes().find((box) => box.name === name) || null;
	}

	engine() {
		return this.snapshot && this.snapshot.engine;
	}

	ready() {
		const engine = this.engine();
		return !!(engine && engine.state === "ready");
	}

	async request(method, params) {
		this.problem = "";
		try {
			return await this.ied.client.request(method, Object.assign({ wsId: this.wsId }, params));
		}
		catch (e) {
			this.problem = (e && e.message) || String(e);
			this.ied.toast(this.problem, { level: "error" });
			this.changed();
			return null;
		}
	}

	async loadCatalog() {
		if (!this.catalog) this.catalog = await this.request("ied.sandbox/catalog", {});
		return this.catalog;
	}

	// Shows run `runId` in the Activity view (null: follow the newest again).
	showRun(runId) {
		this.shownRun = runId;
		this.changed();
	}
}
