// model.js - a board as this window sees it (docs/32 "Changes"): the server's state from the board's topic, with this
// window's own changes applied at once and kept pending until the server's event for them arrives. New elements get
// temporary ids (t<n>) the event maps to the board's.

import { route, bounds, isBoxed } from "./geom.js";

let opCounter = 0;

export class BoardModel {
	constructor(ied, wsId, path) {
		this.ied = ied;
		this.wsId = wsId;
		this.path = path;
		this.boardId = null;
		this.rev = -1;
		this.title = "";
		this.layers = [];
		this.elements = [];        // the server's order
		this.byId = new Map();
		this.temps = [];           // this window's new elements, not confirmed yet
		this.plans = [];
		this.history = [];
		this.pending = new Map();  // opId -> {tempIds, sent}
		this.realIds = new Map();  // temporary id -> the board's
		this.listeners = new Set();
		this.loaded = false;
		this.error = null;
		this.nextTemp = 1;
		this.stop = null;
	}

	async open() {
		try {
			const opened = await this.ied.client.request("ied.board/open", { wsId: this.wsId, path: this.path });
			this.boardId = opened.boardId;
			this.stop = this.ied.client.subscribe(opened.topic, {
				snap: (data) => this.snapshot(data),
				evt: (kind, data) => this.event(kind, data),
			});
		}
		catch (e) {
			this.error = e.message || String(e);
			this.emit({ kind: "error" });
		}
	}

	dispose() {
		if (this.stop) this.stop();
		this.listeners.clear();
	}

	on(fn) {
		this.listeners.add(fn);
		return () => this.listeners.delete(fn);
	}

	emit(change) {
		for (const fn of this.listeners) fn(change);
	}

	get(id) {
		return this.byId.get(id) || this.temps.find((t) => t.id === id) || null;
	}

	all() {
		return this.temps.length ? [...this.elements, ...this.temps] : this.elements;
	}

	layer(id) {
		return this.layers.find((l) => l.id === id) || null;
	}

	// Elements in drawing order: layer by layer, bottom to top.
	drawn(visibleOnly) {
		const out = [];
		for (const layer of this.layers) {
			if (visibleOnly && !layer.visible) continue;
			for (const el of this.all()) if (el.layer === layer.id) out.push(el);
		}
		return out;
	}

	routeOf(el) {
		return el.type === "connector" ? route(el, this.get(el.from), this.get(el.to)) : null;
	}

	boundsOf(el) {
		return bounds(el, this.routeOf(el));
	}

	connectorsOf(id) {
		return this.all().filter((el) => el.type === "connector" && (el.from === id || el.to === id));
	}

	snapshot(data) {
		this.rev = data.rev;
		this.title = data.title;
		this.layers = data.layers;
		this.elements = data.elements;
		this.byId = new Map(this.elements.map((el) => [el.id, el]));
		this.plans = data.plans;
		this.history = data.history || [];
		this.temps = [];
		this.pending.clear();
		this.loaded = true;
		this.emit({ kind: "snapshot" });
	}

	event(kind, data) {
		if (kind === "reloaded") return this.snapshot(data);
		if (kind === "plans") {
			this.plans = data.plans;
			return this.emit({ kind: "plans" });
		}
		if (kind !== "changed") return;
		if (data.rev <= this.rev) return;
		this.rev = data.rev;
		const touched = new Set();
		const before = new Map();
		const mine = data.opId && this.pending.get(data.opId);
		const mapped = new Map();
		if (mine) {
			for (const [ref, id] of Object.entries(data.refs || {})) if (mine.tempIds.includes(ref)) {
				mapped.set(ref, id);
				this.realIds.set(ref, id);
			}
			this.temps = this.temps.filter((t) => !mine.tempIds.includes(t.id));
			this.pending.delete(data.opId);
		}
		for (const id of data.removed || []) {
			const el = this.byId.get(id);
			if (el) before.set(id, this.boundsOf(el));
			touched.add(id);
		}
		for (const put of data.put || []) {
			const el = this.byId.get(put.el.id);
			if (el) before.set(el.id, this.boundsOf(el));
			touched.add(put.el.id);
		}
		this.elements = this.elements.filter((el) => !touched.has(el.id));
		for (const put of (data.put || []).slice().sort((a, b) => a.at - b.at)) this.elements.splice(Math.min(put.at, this.elements.length), 0, put.el);
		this.byId = new Map(this.elements.map((el) => [el.id, el]));
		if (data.layers) this.layers = data.layers;
		if (data.title !== undefined) this.title = data.title;
		this.recordHistory(data.change);
		this.emit({ kind: "changed", ids: [...touched], before, layers: !!data.layers, by: data.change && data.change.by, mapped, change: data.change });
	}

	recordHistory(change) {
		if (!change) return;
		const at = this.history.findIndex((c) => c.id === change.id);
		if (at >= 0) this.history[at] = change;
		else this.history.push(change);
		if (this.history.length > 100) this.history.shift();
	}

	// The board's id of an element this window made (its temporary id until the server answered).
	realId(id) {
		return this.realIds.get(id) || id;
	}

	// Ops with the temporary ids they name replaced by the board's.
	mapIds(ops) {
		const map = (v) => (typeof v === "string" ? this.realId(v) : v);
		return ops.map((op) => {
			const out = { ...op };
			for (const key of ["id", "from", "to", "near", "on", "about"]) if (out[key] !== undefined) out[key] = map(out[key]);
			for (const key of ["ids", "around"]) if (Array.isArray(out[key])) out[key] = out[key].map(map);
			return out;
		});
	}

	// Sends a change. Adds without an id get a temporary one (and a ref of it); the change shows at once. A change that
	// names an element still waiting for the server waits for it, then goes with the board's id.
	async edit(ops, label) {
		const names = JSON.stringify(ops.map((op) => [op.id, op.ids, op.from, op.to, op.near, op.on, op.about, op.around]));
		const waits = [...this.pending.values()].filter((p) => p.tempIds.some((t) => names.includes('"' + t + '"'))).map((p) => p.sent);
		if (waits.length) await Promise.all(waits);
		ops = this.mapIds(ops);
		const opId = "o" + Date.now().toString(36) + "-" + (++opCounter);
		const tempIds = [];
		const sent = ops.map((op) => {
			if (op.op !== "add" || op.ref) return op;
			const id = "t" + this.nextTemp++;
			tempIds.push(id);
			return { ...op, ref: id };
		});
		const pending = { tempIds, sent: null };
		this.pending.set(opId, pending);
		const before = this.applyLocally(sent);
		this.emit({ kind: "local", ids: [...before.keys()], before });
		pending.sent = this.ied.client.request("ied.board/edit", { boardId: this.boardId, ops: sent, opId, label: label || "" }).then((reply) => {
			// The answer can come before the event: its refs map the temporary ids as well.
			for (const [ref, id] of Object.entries((reply && reply.refs) || {})) if (tempIds.includes(ref)) this.realIds.set(ref, id);
			return reply;
		}, (e) => {
			this.pending.delete(opId);
			this.ied.toast("The board refused the change: " + (e.message || e), { level: "error" });
			this.resync();
			return null;
		});
		return pending.sent;
	}

	// The common ops applied to this window's copy at once; others wait for the server.
	applyLocally(ops) {
		const before = new Map();
		const note = (el) => { if (!before.has(el.id)) before.set(el.id, this.boundsOf(el)); };
		for (const op of ops) {
			if (op.op === "add") {
				const el = { ...op, id: op.ref };
				delete el.op;
				delete el.ref;
				this.temps.push(el);
				before.set(el.id, null);
			}
			else if (op.op === "update") {
				const el = this.get(op.id);
				if (!el) continue;
				note(el);
				for (const [k, v] of Object.entries(op)) if (k !== "op" && k !== "id") { if (v === null) delete el[k]; else el[k] = v; }
				for (const c of this.connectorsOf(el.id)) note(c);
			}
			else if (op.op === "delete") {
				const ids = new Set(op.ids);
				for (const id of op.ids) for (const c of this.connectorsOf(id)) ids.add(c.id);
				for (const id of ids) { const el = this.get(id); if (el) note(el); }
				this.elements = this.elements.filter((el) => !ids.has(el.id));
				this.temps = this.temps.filter((el) => !ids.has(el.id));
				for (const id of ids) this.byId.delete(id);
			}
			else if (op.op === "move") {
				for (const id of op.ids) {
					const el = this.get(id);
					if (!el) continue;
					note(el);
					for (const c of this.connectorsOf(id)) note(c);
					if (isBoxed(el.type)) { el.x += op.dx; el.y += op.dy; }
					else if (el.pts) el.pts = el.pts.map((v, i) => v + (i % 2 ? op.dy : op.dx));
				}
			}
		}
		return before;
	}

	async resync() {
		if (this.stop) this.stop();
		await this.open();
	}

	request(method, params) {
		return this.ied.client.request(method, { boardId: this.boardId, ...params }).catch((e) => {
			this.ied.toast((e.message || String(e)), { level: "error" });
			return null;
		});
	}

	undo() { return this.request("ied.board/undo", {}); }
	redo() { return this.request("ied.board/redo", {}); }
	revert(change, redo) { return this.request("ied.board/revert", { change, redo: !!redo }); }
	setPlan(params) { return this.request("ied.board/plan", { action: "set", ...params }); }
	deletePlan(id) { return this.request("ied.board/plan", { action: "delete", id }); }

	tool(tool, args) {
		return this.ied.client.request("ied.board/tool", { wsId: this.wsId, tool, args: { board: this.path, ...args } });
	}
}
