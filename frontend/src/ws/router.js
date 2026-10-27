// router.js - a tab's sockets (docs/10 "Routing"): the master server's (/ws) and one per workspace server the tab talks
// to, through the master's /w/<wsId>/ws. Each is a whole ProtocolClient - its own epoch, cursors and resume - so the
// master's stays up while a workspace server restarts. Requests and subscriptions go where the master's `routes` say:
// a method or topic they name to the master; one that names a workspace (params.wsId, or a topic key holding a wsId as
// a part) to that workspace's server; anything else to the shown workspace's server, or with none shown to the master.
// It offers what ProtocolClient does (request, subscribe, newOpId, clientId, state, onState), so the shell and the
// plugins' UI modules use it unchanged.

import { ProtocolClient } from "./client.js";

const WORKSPACE_ID = /^w\d+(\.\d+)?$/;
const SWEEP_MS = 10000;

// The master's workspace whose server holds `wsId`: a folder a plugin opened inside a workspace server is "<wsId>.<n>".
export function servingWorkspace(wsId) {
	return String(wsId).split(".")[0];
}

// The wsId a topic key holds among its parts ("settings:w2", "layout:w2:phone"), or null.
export function topicWorkspace(key) {
	const parts = String(key).split(":").slice(1);
	return parts.find((part) => WORKSPACE_ID.test(part)) || null;
}

// Where a request (`method`, `params`) or a subscription (`topic`) goes: "master", or the wsId of the workspace whose
// server takes it. Pure, for the tests.
export function routeOf(routes, shown, { method, params, topic }) {
	if (method !== undefined && routes.methods.has(method)) return "master";
	if (topic !== undefined && routes.topics.has(topic)) return "master";
	const named = topic !== undefined ? topicWorkspace(topic) : params && typeof params.wsId === "string" && params.wsId ? params.wsId : null;
	if (named) return servingWorkspace(named);
	return shown ? servingWorkspace(shown) : "master";
}

// The combined state the shell shows: the master's, else the shown workspace's socket's.
export function combinedState(masterState, shownState) {
	if (masterState !== "live") return masterState;
	return shownState && shownState !== "live" ? shownState : "live";
}

export class Router {
	// options: base ("ws://host:port"), identity, onLeave(reason), and for tests openSocket, timers, random.
	constructor(options) {
		this.options = options;
		this.base = options.base;
		this.routes = { methods: new Set(), topics: new Set() };
		this.shown = null;
		this.workspaces = new Map();   // serving wsId -> ProtocolClient
		this.gone = new Set();         // workspaces closed: no socket is opened to them
		this.listeners = new Set();
		this.lastState = null;
		this.master = this.makeClient(this.base + "/ws");
		this.sweeper = null;
	}

	makeClient(url) {
		const client = new ProtocolClient({ ...this.options, url, onLeave: (reason) => this.leave(reason) });
		client.onState(() => this.stateChanged());
		return client;
	}

	start() {
		this.master.start();
		const timers = this.options.timers;
		this.sweeper = timers ? timers.setInterval(() => this.sweep(), SWEEP_MS) : setInterval(() => this.sweep(), SWEEP_MS);
	}

	stop() {
		const timers = this.options.timers;
		if (timers) timers.clearInterval(this.sweeper);
		else clearInterval(this.sweeper);
		this.master.stop();
		for (const client of this.workspaces.values()) client.stop();
		this.workspaces.clear();
	}

	leave(reason) {
		if (this.left) return;
		this.left = true;
		this.stop();
		if (this.options.onLeave) this.options.onLeave(reason);
	}

	// The master's routes ({methods, topics}), from its server topic.
	setRoutes(routes) {
		this.routes = { methods: new Set((routes && routes.methods) || []), topics: new Set((routes && routes.topics) || []) };
	}

	// The workspace this tab shows: what a request naming none goes to.
	show(wsId) {
		this.shown = wsId || null;
		if (this.shown) {
			const key = servingWorkspace(this.shown);
			const old = this.workspaces.get(key);
			if (this.gone.delete(key) && old) this.workspaces.delete(key);
			this.clientOf(key);
		}
		this.stateChanged();
	}

	// A workspace whose server stops for good (it closes): its socket goes, and what is still asked of it waits instead
	// of knocking at a server that is going away, until it is shown again.
	forget(wsId) {
		const key = servingWorkspace(wsId);
		this.gone.add(key);
		const client = this.workspaces.get(key);
		if (!client) return;
		this.workspaces.delete(key);
		client.stop();
		this.stateChanged();
	}

	clientOf(target) {
		if (target === "master") return this.master;
		let client = this.workspaces.get(target);
		if (!client) {
			client = this.makeClient(this.base + "/w/" + encodeURIComponent(target) + "/ws");
			this.workspaces.set(target, client);
			if (this.gone.has(target)) client.stop();
			else client.start();
		}
		return client;
	}

	// The socket of a workspace's server, when this tab has one: its state for the shell.
	workspaceClient(wsId) {
		return wsId ? this.workspaces.get(servingWorkspace(wsId)) || null : null;
	}

	// A workspace server's socket the tab no longer uses (not shown, no topic, nothing waiting) is closed.
	sweep() {
		const shown = this.shown ? servingWorkspace(this.shown) : null;
		for (const [key, client] of [...this.workspaces]) {
			if (key === shown || client.topics.size || client.pending.size) continue;
			this.workspaces.delete(key);
			client.stop();
		}
	}

	request(method, params, options) {
		return this.clientOf(routeOf(this.routes, this.shown, { method, params })).request(method, params, options);
	}

	subscribe(key, handler) {
		return this.clientOf(routeOf(this.routes, this.shown, { topic: key })).subscribe(key, handler);
	}

	newOpId() {
		return this.master.newOpId();
	}

	get clientId() {
		return this.master.clientId;
	}

	get state() {
		const shown = this.workspaceClient(this.shown);
		return combinedState(this.master.state, shown ? shown.state : null);
	}

	onState(fn) {
		this.listeners.add(fn);
		return () => this.listeners.delete(fn);
	}

	stateChanged() {
		const state = this.state;
		if (state === this.lastState) return;
		this.lastState = state;
		for (const fn of [...this.listeners]) fn(state);
	}
}
