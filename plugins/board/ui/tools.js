// tools.js - what the pointer and keys do on a board (docs/32 "The board in the UI"): select, move and resize; hand
// and zoom; pen, highlighter and the eraser (cutting strokes or taking whole ones); shapes, lines, arrows, connectors,
// text, notes and frames; text typed in place; images pasted, dropped, resized and cropped.

import { isBoxed, isNode, isPath, frameOf, hitDistance, overlaps, contains, inflate, union, EMPTY, isEmpty, box, simplify, roundPts, erase, tenth, segDist } from "./geom.js";
import { drawElement, fontOf, wrap } from "./render.js";

const HANDLE = 7;
const CLICK = 4;

// The tools on the toolbar: id, title, codicon, key.
export const TOOLS = [
	{ id: "select", title: "Select", icon: "inspect", key: "v" },
	{ id: "hand", title: "Hand: pan the board", icon: "move", key: "h" },
	{ id: "pen", title: "Pen", icon: "edit", key: "p" },
	{ id: "highlighter", title: "Highlighter", icon: "symbol-color", key: "i" },
	{ id: "eraser", title: "Eraser", icon: "clear-all", key: "e" },
	{ id: "rect", title: "Rectangle", icon: "primitive-square", key: "r" },
	{ id: "ellipse", title: "Ellipse", icon: "circle-large", key: "o" },
	{ id: "diamond", title: "Diamond", icon: "debug-breakpoint-log", key: "d" },
	{ id: "line", title: "Line", icon: "dash", key: "l" },
	{ id: "arrow", title: "Arrow", icon: "arrow-right", key: "a" },
	{ id: "connector", title: "Connector: drag from one element to another", icon: "git-merge", key: "c" },
	{ id: "text", title: "Text", icon: "text-size", key: "t" },
	{ id: "note", title: "Sticky note", icon: "note", key: "n" },
	{ id: "frame", title: "Frame: a group", icon: "layout", key: "f" },
];

export class BoardInput {
	constructor(view) {
		this.view = view;
		this.model = view.model;
		this.canvas = view.canvas;
		this.tool = "select";
		this.style = { color: "ink", fill: "none", width: 2, fontSize: 16 };
		this.eraser = { mode: "partial", size: 16 };
		this.selection = new Set();
		this.pointers = new Map();
		this.action = null;
		this.space = false;
		this.clipboard = null;
		this.editor = null;
		const el = this.canvas.canvas;
		this.handlers = {
			pointerdown: (e) => this.down(e),
			pointermove: (e) => this.move(e),
			pointerup: (e) => this.up(e),
			pointercancel: (e) => this.up(e, true),
			wheel: (e) => this.wheel(e),
			dblclick: (e) => this.dblclick(e),
			keydown: (e) => this.key(e),
			keyup: (e) => { if (e.code === "Space") this.space = false; },
			contextmenu: (e) => e.preventDefault(),
		};
		for (const [type, fn] of Object.entries(this.handlers)) el.addEventListener(type, fn, type === "wheel" ? { passive: false } : undefined);
		this.canvas.overlays.push((ctx) => this.overlay(ctx));
	}

	dispose() {
		const el = this.canvas.canvas;
		for (const [type, fn] of Object.entries(this.handlers)) el.removeEventListener(type, fn);
		this.closeEditor(true);
	}

	setTool(tool) {
		this.cancel();
		this.tool = tool;
		this.canvas.canvas.dataset.tool = tool;
		this.view.toolChanged();
	}

	activeLayer() {
		return this.view.activeLayer();
	}

	// --- selection -------------------------------------------------------------------------------------------------

	select(ids, add) {
		if (!add) this.selection.clear();
		for (const id of ids) this.selection.add(id);
		this.view.selectionChanged();
		this.canvas.request();
	}

	selected() {
		return [...this.selection].map((id) => this.model.get(id)).filter(Boolean);
	}

	editable(el) {
		const layer = this.model.layer(el.layer);
		return layer && layer.visible && !layer.locked;
	}

	hit(wx, wy) {
		const tolerance = 6 / this.canvas.camera.zoom;
		const drawn = this.model.drawn(true);
		let frameHit = null;
		for (let i = drawn.length - 1; i >= 0; i--) {
			const el = drawn[i];
			if (!this.editable(el)) continue;
			const d = hitDistance(el, this.model.routeOf(el), wx, wy);
			if (d > tolerance) continue;
			// A frame is taken only by its outline or title, after what is inside it.
			if (el.type === "frame") { frameHit = frameHit || el; continue; }
			return el;
		}
		return frameHit;
	}

	selectionBox() {
		let b = EMPTY;
		for (const el of this.selected()) b = union(b, el.type === "connector" ? this.model.boundsOf(el) : frameOf(el));
		return b;
	}

	handleAt(sx, sy) {
		const els = this.selected();
		if (els.length !== 1 || !isBoxed(els[0].type)) return null;
		const el = els[0];
		const [x0, y0] = this.canvas.toScreen(el.x, el.y);
		const [x1, y1] = this.canvas.toScreen(el.x + el.w, el.y + el.h);
		const handles = { nw: [x0, y0], ne: [x1, y0], sw: [x0, y1], se: [x1, y1], n: [(x0 + x1) / 2, y0], s: [(x0 + x1) / 2, y1], w: [x0, (y0 + y1) / 2], e: [x1, (y0 + y1) / 2] };
		for (const [name, [hx, hy]] of Object.entries(handles)) if (Math.abs(sx - hx) <= HANDLE && Math.abs(sy - hy) <= HANDLE) return name;
		return null;
	}

	// --- pointer ---------------------------------------------------------------------------------------------------

	point(e) {
		const r = this.canvas.canvas.getBoundingClientRect();
		const sx = e.clientX - r.left;
		const sy = e.clientY - r.top;
		const [wx, wy] = this.canvas.toWorld(sx, sy);
		return { sx, sy, wx, wy };
	}

	down(e) {
		this.canvas.canvas.focus({ preventScroll: true });
		if (this.editor) this.closeEditor(false);
		try {
			this.canvas.canvas.setPointerCapture(e.pointerId);
		}
		catch (_) {
			// A pointer the browser no longer tracks (a synthetic one) cannot be captured; moves still arrive.
		}
		this.pointers.set(e.pointerId, this.point(e));
		if (this.pointers.size === 2) {
			this.cancel();
			const [a, b] = [...this.pointers.values()];
			this.action = { kind: "pinch", distance: Math.hypot(a.sx - b.sx, a.sy - b.sy), mid: [(a.sx + b.sx) / 2, (a.sy + b.sy) / 2] };
			return;
		}
		if (this.pointers.size > 2) return;
		const p = this.point(e);
		if (e.button === 1 || this.space || this.tool === "hand" || (e.button === 2)) {
			this.action = { kind: "pan", last: p };
			return;
		}
		if (e.button !== 0) return;
		this.start(p, e);
	}

	move(e) {
		if (!this.pointers.has(e.pointerId)) {
			this.hover = this.point(e);
			if (this.tool === "eraser" || this.tool === "connector") this.canvas.request();
			return;
		}
		this.pointers.set(e.pointerId, this.point(e));
		const a = this.action;
		if (!a) return;
		if (a.kind === "pinch") {
			const [p, q] = [...this.pointers.values()];
			const distance = Math.hypot(p.sx - q.sx, p.sy - q.sy);
			const mid = [(p.sx + q.sx) / 2, (p.sy + q.sy) / 2];
			this.canvas.pan(mid[0] - a.mid[0], mid[1] - a.mid[1]);
			if (a.distance > 0) this.canvas.zoomAt(mid[0], mid[1], distance / a.distance);
			a.distance = distance;
			a.mid = mid;
			return;
		}
		const events = e.getCoalescedEvents ? e.getCoalescedEvents() : [e];
		for (const one of events.length ? events : [e]) this.drag(this.point(one), e);
	}

	up(e, cancelled) {
		this.pointers.delete(e.pointerId);
		const a = this.action;
		if (!a) return;
		if (a.kind === "pinch") {
			if (this.pointers.size === 0) this.action = null;
			return;
		}
		this.action = null;
		if (cancelled) return this.abandon(a);
		this.finish(a, this.point(e), e);
	}

	wheel(e) {
		e.preventDefault();
		const p = this.point(e);
		if (e.ctrlKey || e.metaKey) this.canvas.zoomAt(p.sx, p.sy, Math.exp(-e.deltaY * 0.0025));
		else this.canvas.pan(-(e.shiftKey ? e.deltaY : e.deltaX), -(e.shiftKey ? 0 : e.deltaY));
	}

	cancel() {
		if (this.action) this.abandon(this.action);
		this.action = null;
	}

	// --- starting, dragging and finishing what a press does -------------------------------------------------------

	start(p, e) {
		const t = this.tool;
		if (t === "select") return this.startSelect(p, e);
		if (t === "pen" || t === "highlighter") return (this.action = { kind: "ink", pts: [p.wx, p.wy], last: p });
		if (t === "eraser") return this.startErase(p);
		if (["rect", "ellipse", "diamond", "frame", "line", "arrow"].includes(t)) return (this.action = { kind: "shape", type: t, from: p, to: p });
		if (t === "connector") {
			const from = this.hit(p.wx, p.wy);
			if (from && from.type !== "connector" && !isPath(from.type)) this.action = { kind: "connect", from, to: p };
			return;
		}
		if (t === "text") return this.newText(p);
		if (t === "note") return this.newNote(p);
	}

	startSelect(p, e) {
		const handle = this.handleAt(p.sx, p.sy);
		if (handle) {
			const el = this.selected()[0];
			this.action = { kind: "resize", handle, el, from: p, original: { x: el.x, y: el.y, w: el.w, h: el.h, fontSize: el.fontSize } };
			this.hide([el.id, ...this.model.connectorsOf(el.id).map((c) => c.id)]);
			return;
		}
		const el = this.hit(p.wx, p.wy);
		if (!el) {
			if (!e.shiftKey) this.select([], false);
			this.action = { kind: "marquee", from: p, to: p };
			return;
		}
		if (e.shiftKey) {
			if (this.selection.has(el.id)) this.selection.delete(el.id);
			else this.selection.add(el.id);
			this.view.selectionChanged();
			this.canvas.request();
		}
		else if (!this.selection.has(el.id)) this.select([el.id], false);
		this.startMove(p);
	}

	// What moves with the selection: frames take their members.
	movingIds() {
		const ids = new Set(this.selection);
		for (const el of this.selected()) {
			if (el.type !== "frame") continue;
			const fb = frameOf(el);
			for (const other of this.model.all()) {
				if (other.layer !== el.layer || other.id === el.id || other.type === "connector") continue;
				const b = this.model.boundsOf(other);
				if (contains(fb, (b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2)) ids.add(other.id);
			}
		}
		return [...ids].filter((id) => { const el = this.model.get(id); return el && el.type !== "connector"; });
	}

	startMove(p) {
		const ids = this.movingIds();
		const original = new Map(ids.map((id) => { const el = this.model.get(id); return [id, { x: el.x, y: el.y, pts: el.pts && el.pts.slice() }]; }));
		const connectors = new Set();
		for (const id of ids) for (const c of this.model.connectorsOf(id)) connectors.add(c.id);
		this.action = { kind: "move", from: p, ids, original, dx: 0, dy: 0 };
		this.hide([...ids, ...connectors]);
	}

	hide(ids) {
		for (const id of ids) {
			const el = this.model.get(id);
			if (!el) continue;
			this.canvas.hidden.add(id);
			this.canvas.invalidate(this.model.boundsOf(el));
		}
	}

	unhide() {
		for (const id of this.canvas.hidden) {
			const el = this.model.get(id);
			if (el) this.canvas.invalidate(this.model.boundsOf(el));
		}
		this.canvas.hidden.clear();
		this.canvas.invalidateAll();
	}

	placeMoved(a) {
		for (const id of a.ids) {
			const el = this.model.get(id);
			const o = a.original.get(id);
			if (!el || !o) continue;
			if (isBoxed(el.type)) { el.x = o.x + a.dx; el.y = o.y + a.dy; }
			else if (o.pts) el.pts = o.pts.map((v, i) => v + (i % 2 ? a.dy : a.dx));
		}
	}

	drag(p, e) {
		const a = this.action;
		if (a.kind === "pan") {
			this.canvas.pan(p.sx - a.last.sx, p.sy - a.last.sy);
			a.last = p;
			return;
		}
		if (a.kind === "ink") {
			const min = 1.2 / this.canvas.camera.zoom;
			const n = a.pts.length;
			if (Math.hypot(p.wx - a.pts[n - 2], p.wy - a.pts[n - 1]) >= min) a.pts.push(p.wx, p.wy);
			this.canvas.request();
			return;
		}
		if (a.kind === "erase") return this.dragErase(p);
		if (a.kind === "move") {
			a.dx = tenth(p.wx - a.from.wx);
			a.dy = tenth(p.wy - a.from.wy);
			this.placeMoved(a);
			this.canvas.request();
			return;
		}
		if (a.kind === "resize") return this.dragResize(a, p, e);
		if (a.kind === "crop") return this.dragCrop(a, p);
		a.to = p;
		this.canvas.request();
	}

	abandon(a) {
		if (a.kind === "move") {
			a.dx = 0;
			a.dy = 0;
			this.placeMoved(a);
		}
		if (a.kind === "resize") Object.assign(a.el, a.original);
		if (a.kind === "erase") for (const el of this.model.all()) delete el._erasing;
		this.unhide();
	}

	finish(a, p, e) {
		if (a.kind === "pan") return;
		if (a.kind === "ink") return this.finishInk(a);
		if (a.kind === "erase") return this.finishErase(a);
		if (a.kind === "move") {
			const { dx, dy } = a;
			a.dx = 0;
			a.dy = 0;
			this.placeMoved(a);
			this.unhide();
			if (Math.abs(dx) * this.canvas.camera.zoom < 1 && Math.abs(dy) * this.canvas.camera.zoom < 1) return;
			this.model.edit([{ op: "move", ids: [...this.selection].filter((id) => this.model.get(id) && this.model.get(id).type !== "connector"), dx, dy }], "move");
			return;
		}
		if (a.kind === "resize") return this.finishResize(a);
		if (a.kind === "crop") return;
		if (a.kind === "marquee") {
			const m = box(a.from.wx, a.from.wy, a.to.wx, a.to.wy);
			const ids = this.model.drawn(true).filter((el) => this.editable(el) && (() => { const b = this.model.boundsOf(el); return b.x0 >= m.x0 && b.x1 <= m.x1 && b.y0 >= m.y0 && b.y1 <= m.y1; })()).map((el) => el.id);
			this.select(ids, e.shiftKey);
			return;
		}
		if (a.kind === "shape") return this.finishShape(a);
		if (a.kind === "connect") return this.finishConnect(a, p);
	}

	// --- ink ------------------------------------------------------------------------------------------------------

	inkElement(pts) {
		const highlighter = this.tool === "highlighter";
		return { type: "stroke", pts, color: this.style.color, width: highlighter ? Math.max(12, this.style.width * 5) : this.style.width, opacity: highlighter ? 0.35 : undefined };
	}

	finishInk(a) {
		const layer = this.activeLayer();
		if (!layer) return;
		const pts = a.pts.length >= 4 ? roundPts(simplify(a.pts, 0.45 / this.canvas.camera.zoom)) : [tenth(a.pts[0]), tenth(a.pts[1]), tenth(a.pts[0] + 0.1), tenth(a.pts[1])];
		const el = this.inkElement(pts);
		const op = { op: "add", layer: layer.id, ...el };
		if (op.opacity === undefined) delete op.opacity;
		this.model.edit([op], this.tool);
		this.canvas.request();
	}

	// --- eraser ---------------------------------------------------------------------------------------------------

	startErase(p) {
		this.action = { kind: "erase", last: p, cut: new Map() };
		this.eraseAlong(this.action, p, p);
	}

	dragErase(p) {
		const a = this.action;
		this.eraseAlong(a, a.last, p);
		a.last = p;
		this.canvas.request();
	}

	// The eraser swept from `from` to `to` (world): strokes on the active layer cut (partial) or taken whole.
	eraseAlong(a, from, to) {
		const layer = this.activeLayer();
		if (!layer || layer.locked) return;
		const r = this.eraser.size / 2 / this.canvas.camera.zoom;
		const swept = inflate(box(from.wx, from.wy, to.wx, to.wy), r);
		for (const el of this.model.all()) {
			if (el.layer !== layer.id) continue;
			const known = a.cut.get(el.id);
			if (known && known.length === 0) continue;
			if (!overlaps(this.model.boundsOf(el), swept)) continue;
			if (el.type === "stroke" && this.eraser.mode === "partial") {
				const pieces = known || [el.pts];
				let changed = false;
				const next = [];
				for (const piece of pieces) {
					const left = erase(piece, from.wx, from.wy, to.wx, to.wy, r);
					if (left === null) next.push(piece);
					else { changed = true; next.push(...left); }
				}
				if (!changed) continue;
				a.cut.set(el.id, next);
			}
			else {
				const route = this.model.routeOf(el);
				let touched = false;
				for (let k = 0; k <= 8 && !touched; k++) {
					const x = from.wx + (to.wx - from.wx) * k / 8, y = from.wy + (to.wy - from.wy) * k / 8;
					touched = hitDistance(el, route, x, y) <= r;
				}
				if (!touched) continue;
				a.cut.set(el.id, []);
			}
			if (!this.canvas.hidden.has(el.id)) {
				el._erasing = true;
				this.hide([el.id]);
			}
		}
	}

	finishErase(a) {
		const ops = [];
		const doomed = [];
		for (const [id, pieces] of a.cut) {
			const el = this.model.get(id);
			if (!el) continue;
			delete el._erasing;
			doomed.push(id);
			for (const pts of pieces) {
				if (pts.length < 4) continue;
				const op = { op: "add", type: "stroke", layer: el.layer, pts, width: el.width ?? 2 };
				if (el.color) op.color = el.color;
				if (el.opacity !== undefined) op.opacity = el.opacity;
				ops.push(op);
			}
		}
		this.unhide();
		if (!doomed.length) return;
		this.model.edit([{ op: "delete", ids: doomed }, ...ops], "erase");
	}

	// --- shapes, lines, connectors ---------------------------------------------------------------------------------

	finishShape(a) {
		const layer = this.activeLayer();
		if (!layer) return;
		const t = a.type;
		const dragged = Math.hypot(a.to.sx - a.from.sx, a.to.sy - a.from.sy) > CLICK;
		if (t === "line" || t === "arrow") {
			if (!dragged) return;
			const op = { op: "add", type: t, layer: layer.id, pts: roundPts([a.from.wx, a.from.wy, a.to.wx, a.to.wy]), color: this.style.color, width: this.style.width };
			this.model.edit([op], t).then(() => this.setTool("select"));
			return;
		}
		let b = box(a.from.wx, a.from.wy, a.to.wx, a.to.wy);
		if (!dragged) {
			const w = t === "frame" ? 400 : 160, h = t === "frame" ? 260 : t === "diamond" ? 100 : 80;
			b = { x0: a.from.wx - w / 2, y0: a.from.wy - h / 2, x1: a.from.wx + w / 2, y1: a.from.wy + h / 2 };
		}
		const op = { op: "add", type: t, layer: layer.id, x: tenth(b.x0), y: tenth(b.y0), w: tenth(b.x1 - b.x0), h: tenth(b.y1 - b.y0) };
		if (t !== "frame") {
			op.color = this.style.color;
			if (this.style.fill !== "none") op.fill = this.style.fill;
			if (this.style.width !== 2) op.width = this.style.width;
		}
		this.model.edit([op], t);
		const temp = this.model.temps[this.model.temps.length - 1];
		this.setTool("select");
		if (temp) {
			this.select([temp.id], false);
			this.editText(temp);
		}
	}

	finishConnect(a, p) {
		const to = this.hit(p.wx, p.wy);
		if (!to || to.id === a.from.id || to.type === "connector" || isPath(to.type)) return this.canvas.request();
		const op = { op: "add", type: "connector", layer: this.activeLayer().id, from: a.from.id, to: to.id };
		if (this.style.color !== "ink") op.color = this.style.color;
		this.model.edit([op], "connect");
	}

	// --- text -----------------------------------------------------------------------------------------------------

	newText(p) {
		const layer = this.activeLayer();
		if (!layer) return;
		this.editText({ type: "text", x: tenth(p.wx), y: tenth(p.wy - this.style.fontSize * 0.65), w: 4, h: this.style.fontSize * 1.3, fontSize: this.style.fontSize, color: this.style.color, text: "", layer: layer.id, _new: true });
	}

	newNote(p) {
		const layer = this.activeLayer();
		if (!layer) return;
		this.model.edit([{ op: "add", type: "note", layer: layer.id, x: tenth(p.wx - 90), y: tenth(p.wy - 60), w: 180, h: 120, text: "", fill: this.style.fill !== "none" ? this.style.fill : "yellow" }], "note");
		const temp = this.model.temps[this.model.temps.length - 1];
		this.setTool("select");
		if (temp) {
			this.select([temp.id], false);
			this.editText(temp);
		}
	}

	// A text field over an element, its text the element's until it closes.
	editText(el) {
		this.closeEditor(false);
		const z = this.canvas.camera.zoom;
		const textarea = document.createElement("textarea");
		textarea.className = "board-editor board-editor-" + el.type;
		textarea.value = el.text || "";
		textarea.spellcheck = false;
		const size = (el.type === "connector" ? 13 : el.type === "frame" ? 15 : el.fontSize ?? 16) * z;
		textarea.style.fontSize = size + "px";
		textarea.style.lineHeight = "1.3";
		const place = () => {
			let x, y, w, h;
			if (el.type === "connector") {
				const r = this.model.routeOf(el) || [];
				const [cx, cy] = r.length >= 4 ? [(r[0] + r[r.length - 2]) / 2, (r[1] + r[r.length - 1]) / 2] : [0, 0];
				[x, y] = this.canvas.toScreen(cx - 80, cy - 12);
				w = 160 * z; h = 26 * z;
			}
			else if (el.type === "frame") {
				[x, y] = this.canvas.toScreen(el.x + 6, el.y + 4);
				w = Math.max(120, el.w - 12) * z; h = 26 * z;
			}
			else {
				[x, y] = this.canvas.toScreen(el.x, el.y);
				w = Math.max(el.type === "text" ? 160 : el.w, 40) * z;
				h = Math.max(el.h, size / z * 1.4) * z;
			}
			Object.assign(textarea.style, { left: x + "px", top: y + "px", width: w + "px", height: h + "px" });
		};
		place();
		if (isNode(el.type)) textarea.classList.add("centered");
		this.canvas.el.append(textarea);
		if (!el._new) this.hide([el.id]);
		const editing = { el, textarea, original: el.text || "" };
		this.editor = editing;
		// The element as the model has it now: a new one's temporary id gives way to the board's while it is edited.
		const current = () => this.model.get(this.model.realId(el.id)) || el;
		editing.current = current;
		textarea.addEventListener("input", () => {
			if (el._new) return;
			const now = current();
			if (!this.canvas.hidden.has(now.id)) this.hide([now.id]);
			now.text = textarea.value;
			this.canvas.request();
		});
		textarea.addEventListener("keydown", (e) => {
			e.stopPropagation();
			if (e.key === "Escape" || (e.key === "Enter" && (e.ctrlKey || e.metaKey)) || (e.key === "Enter" && !e.shiftKey && (isNode(el.type) || el.type === "connector" || el.type === "frame"))) {
				e.preventDefault();
				this.closeEditor(false);
			}
		});
		textarea.addEventListener("blur", () => setTimeout(() => { if (this.editor === editing) this.closeEditor(false); }, 0));
		setTimeout(() => { textarea.focus(); textarea.select(); }, 0);
	}

	measure(el, text) {
		const ctx = this.canvas.ctx;
		ctx.font = fontOf(this.canvas.palette, el.fontSize ?? 16);
		const lines = String(text).split("\n");
		const w = Math.max(...lines.map((l) => ctx.measureText(l).width), 4);
		return { w: tenth(w + 2), h: tenth(lines.length * (el.fontSize ?? 16) * 1.3) };
	}

	closeEditor(discard) {
		const editing = this.editor;
		if (!editing) return;
		this.editor = null;
		const { textarea, original } = editing;
		const el = editing.current ? editing.current() : editing.el;
		const text = textarea.value;
		textarea.remove();
		if (!el._new) {
			el.text = original;
			this.unhide();
		}
		this.canvas.canvas.focus({ preventScroll: true });
		if (discard || text === original) {
			if (!el._new) this.canvas.request();
			return;
		}
		if (el._new) {
			if (!text.trim()) return;
			const size = this.measure(el, text);
			const op = { op: "add", type: "text", layer: el.layer, x: el.x, y: el.y, w: size.w, h: size.h, text, color: el.color };
			if ((el.fontSize ?? 16) !== 16) op.fontSize = el.fontSize;
			this.model.edit([op], "text").then(() => this.setTool("select"));
			return;
		}
		const op = { op: "update", id: el.id, text };
		if (el.type === "text") Object.assign(op, this.measure(el, text));
		this.model.edit([op], "text");
	}

	dblclick(e) {
		const p = this.point(e);
		const el = this.hit(p.wx, p.wy);
		if (!el) {
			if (this.tool === "select") this.newText(p);
			return;
		}
		if (el.type === "image") return this.startCrop(el);
		if (isBoxed(el.type) || el.type === "connector") {
			this.select([el.id], false);
			this.editText(el);
		}
	}

	// --- resize and crop ------------------------------------------------------------------------------------------

	dragResize(a, p, e) {
		const el = a.el, o = a.original;
		let x0 = o.x, y0 = o.y, x1 = o.x + o.w, y1 = o.y + o.h;
		const h = a.handle;
		if (h.includes("w")) x0 = Math.min(p.wx, x1 - 8);
		if (h.includes("e")) x1 = Math.max(p.wx, x0 + 8);
		if (h.includes("n")) y0 = Math.min(p.wy, y1 - 8);
		if (h.includes("s")) y1 = Math.max(p.wy, y0 + 8);
		// Images keep their aspect unless Shift frees them; Shift keeps it for the rest.
		const keep = (el.type === "image") !== e.shiftKey && h.length === 2;
		if (keep) {
			const ratio = o.w / o.h;
			const w = x1 - x0, hh = y1 - y0;
			if (w / hh > ratio) { const nw = hh * ratio; if (h.includes("w")) x0 = x1 - nw; else x1 = x0 + nw; }
			else { const nh = w / ratio; if (h.includes("n")) y0 = y1 - nh; else y1 = y0 + nh; }
		}
		el.x = tenth(x0); el.y = tenth(y0); el.w = tenth(x1 - x0); el.h = tenth(y1 - y0);
		if (el.type === "text") el.fontSize = Math.max(6, tenth((o.fontSize ?? 16) * el.h / o.h));
		this.canvas.request();
	}

	finishResize(a) {
		const el = a.el;
		const next = { x: el.x, y: el.y, w: el.w, h: el.h, fontSize: el.fontSize };
		Object.assign(el, a.original);
		this.unhide();
		const op = { op: "update", id: el.id, x: next.x, y: next.y, w: next.w, h: next.h };
		if (el.type === "text") op.fontSize = next.fontSize;
		this.model.edit([op], "resize");
	}

	startCrop(el) {
		const crop = el.crop ? el.crop.slice() : [0, 0, el.iw, el.ih];
		this.select([el.id], false);
		this.cropping = { el, crop, scale: el.w / crop[2] };
		this.view.status("Cropping: drag the edges, Enter to keep, Esc to cancel");
		this.canvas.request();
	}

	// The full picture's box while cropping (world), where the crop is now.
	cropFull(c) {
		const { el, crop, scale } = c;
		const x = el.x - (crop[0] - (el.crop ? el.crop[0] : 0)) * 0;
		const fx = c.anchorX ?? (el.x - (el.crop ? el.crop[0] : 0) * scale);
		const fy = c.anchorY ?? (el.y - (el.crop ? el.crop[1] : 0) * scale);
		c.anchorX = fx;
		c.anchorY = fy;
		return { x: fx, y: fy, w: el.iw * scale, h: el.ih * scale, unused: x };
	}

	dragCrop(a, p) {
		const c = this.cropping;
		const full = this.cropFull(c);
		let [cx, cy, cw, ch] = a.start;
		const px = (p.wx - full.x) / c.scale, py = (p.wy - full.y) / c.scale;
		const h = a.handle;
		const clampX = (v) => Math.max(0, Math.min(c.el.iw, v));
		const clampY = (v) => Math.max(0, Math.min(c.el.ih, v));
		if (h.includes("w")) { const nx = Math.min(clampX(px), cx + cw - 4); cw += cx - nx; cx = nx; }
		if (h.includes("e")) cw = Math.max(4, clampX(px) - cx);
		if (h.includes("n")) { const ny = Math.min(clampY(py), cy + ch - 4); ch += cy - ny; cy = ny; }
		if (h.includes("s")) ch = Math.max(4, clampY(py) - cy);
		c.crop = [Math.round(cx), Math.round(cy), Math.round(cw), Math.round(ch)];
		this.canvas.request();
	}

	cropHandleAt(p) {
		const c = this.cropping;
		const full = this.cropFull(c);
		const [cx, cy, cw, ch] = c.crop;
		const [x0, y0] = this.canvas.toScreen(full.x + cx * c.scale, full.y + cy * c.scale);
		const [x1, y1] = this.canvas.toScreen(full.x + (cx + cw) * c.scale, full.y + (cy + ch) * c.scale);
		const handles = { nw: [x0, y0], ne: [x1, y0], sw: [x0, y1], se: [x1, y1], n: [(x0 + x1) / 2, y0], s: [(x0 + x1) / 2, y1], w: [x0, (y0 + y1) / 2], e: [x1, (y0 + y1) / 2] };
		for (const [name, [hx, hy]] of Object.entries(handles)) if (Math.abs(p.sx - hx) <= HANDLE + 2 && Math.abs(p.sy - hy) <= HANDLE + 2) return name;
		return null;
	}

	endCrop(keep) {
		const c = this.cropping;
		if (!c) return;
		this.cropping = null;
		this.view.status("");
		this.canvas.request();
		if (!keep) return;
		const full = this.cropFull(c);
		const [cx, cy, cw, ch] = c.crop;
		const whole = cx === 0 && cy === 0 && cw === c.el.iw && ch === c.el.ih;
		this.model.edit([{ op: "update", id: c.el.id, crop: whole ? null : c.crop, x: tenth(full.x + cx * c.scale), y: tenth(full.y + cy * c.scale), w: tenth(cw * c.scale), h: tenth(ch * c.scale) }], "crop");
	}

	// --- keys -----------------------------------------------------------------------------------------------------

	key(e) {
		if (this.editor) return;
		const ctrl = e.ctrlKey || e.metaKey;
		const k = e.key.toLowerCase();
		if (this.cropping) {
			if (e.key === "Enter") { e.preventDefault(); return this.endCrop(true); }
			if (e.key === "Escape") { e.preventDefault(); return this.endCrop(false); }
		}
		if (e.code === "Space") { this.space = true; e.preventDefault(); return; }
		if (ctrl && k === "z") { e.preventDefault(); return e.shiftKey ? this.model.redo() : this.model.undo(); }
		if (ctrl && k === "y") { e.preventDefault(); return this.model.redo(); }
		if (ctrl && k === "a") { e.preventDefault(); return this.select(this.model.drawn(true).filter((el) => this.editable(el)).map((el) => el.id), false); }
		if (ctrl && k === "d") { e.preventDefault(); return this.duplicate(20); }
		if (ctrl && k === "c") { e.preventDefault(); return this.copy(); }
		if (ctrl && k === "x") { e.preventDefault(); this.copy(); return this.deleteSelection(); }
		if (ctrl && (k === "=" || k === "+")) { e.preventDefault(); return this.canvas.zoomAt(this.canvas.width / 2, this.canvas.height / 2, 1.25); }
		if (ctrl && k === "-") { e.preventDefault(); return this.canvas.zoomAt(this.canvas.width / 2, this.canvas.height / 2, 0.8); }
		if (ctrl) return;
		if (e.key === "Delete" || e.key === "Backspace") { e.preventDefault(); return this.deleteSelection(); }
		if (e.key === "Escape") { this.cancel(); if (this.tool !== "select") return this.setTool("select"); return this.select([], false); }
		if (e.key.startsWith("Arrow") && this.selection.size) {
			e.preventDefault();
			const step = e.shiftKey ? 10 : 1;
			const dx = e.key === "ArrowLeft" ? -step : e.key === "ArrowRight" ? step : 0;
			const dy = e.key === "ArrowUp" ? -step : e.key === "ArrowDown" ? step : 0;
			return this.model.edit([{ op: "move", ids: this.movingIdsForKeys(), dx, dy }], "nudge");
		}
		if (e.key === "]" && this.selection.size) return this.model.edit([{ op: "order", ids: [...this.selection], to: e.shiftKey ? "front" : "forward" }], "order");
		if (e.key === "[" && this.selection.size) return this.model.edit([{ op: "order", ids: [...this.selection], to: e.shiftKey ? "back" : "backward" }], "order");
		if (e.shiftKey && e.code === "Digit1") return this.canvas.show(this.canvas.contentBox());
		if (e.shiftKey && e.code === "Digit2") return this.canvas.show(this.selectionBox(), 4);
		if (e.key === "0" && e.shiftKey) { this.canvas.camera.zoom = 1; return this.canvas.request(); }
		const tool = TOOLS.find((t) => t.key === k);
		if (tool && !e.shiftKey && !e.altKey) this.setTool(tool.id);
	}

	movingIdsForKeys() {
		return [...this.selection].filter((id) => { const el = this.model.get(id); return el && el.type !== "connector"; });
	}

	deleteSelection() {
		if (!this.selection.size) return;
		const ids = [...this.selection].filter((id) => this.model.get(id));
		this.select([], false);
		this.model.edit([{ op: "delete", ids }], "delete");
	}

	// Elements as ops that add them again, `dx` along: connectors among them follow their copies.
	copyOps(els, dx) {
		const ids = new Set(els.map((el) => el.id));
		const ops = [];
		for (const el of els) {
			if (el.type === "connector") continue;
			const op = { op: "add", ref: "copy_" + el.id, ...JSON.parse(JSON.stringify(el)) };
			delete op.id; delete op.by; delete op.key; delete op._new; delete op._erasing;
			if (isBoxed(el.type)) { op.x = tenth(el.x + dx); op.y = tenth(el.y + dx); }
			else if (el.pts) op.pts = el.pts.map((v) => tenth(v + dx));
			op.layer = this.activeLayer() ? (els.length && this.model.layer(el.layer) ? el.layer : this.activeLayer().id) : el.layer;
			ops.push(op);
		}
		for (const el of els) {
			if (el.type !== "connector" || !ids.has(el.from) || !ids.has(el.to)) continue;
			const op = { op: "add", ...JSON.parse(JSON.stringify(el)), from: "@copy_" + el.from, to: "@copy_" + el.to };
			delete op.id; delete op.by;
			ops.push(op);
		}
		return ops;
	}

	duplicate(dx) {
		const els = this.selected();
		if (!els.length) return;
		this.model.edit(this.copyOps(els, dx), "duplicate");
	}

	copy() {
		const els = this.selected();
		if (!els.length) return;
		this.clipboard = JSON.parse(JSON.stringify(els));
		this.pasted = 0;
		if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(JSON.stringify({ iedBoard: 1, elements: this.clipboard })).catch(() => {});
	}

	pasteElements(els) {
		this.pasted = (this.pasted || 0) + 1;
		this.model.edit(this.copyOps(els, 20 * this.pasted), "paste");
	}

	// --- overlay ------------------------------------------------------------------------------------------------

	overlay(ctx) {
		const z = this.canvas.camera.zoom;
		const pal = this.canvas.palette;
		const a = this.action;
		if (a && a.kind === "ink" && a.pts.length >= 2) drawElement(ctx, pal, this.inkElement(a.pts.length >= 4 ? a.pts : [...a.pts, a.pts[0] + 0.1, a.pts[1]]), this.model, null);
		if (a && a.kind === "erase") {
			for (const [id, pieces] of a.cut) {
				const el = this.model.get(id);
				if (!el) continue;
				for (const pts of pieces) if (pts.length >= 4) drawElement(ctx, pal, { ...el, pts }, this.model, null);
			}
		}
		if (a && a.kind === "shape") {
			const t = a.type;
			if (t === "line" || t === "arrow") drawElement(ctx, pal, { type: t, pts: [a.from.wx, a.from.wy, a.to.wx, a.to.wy], color: this.style.color, width: this.style.width }, this.model, null);
			else {
				const b = box(a.from.wx, a.from.wy, a.to.wx, a.to.wy);
				drawElement(ctx, pal, { type: t, x: b.x0, y: b.y0, w: b.x1 - b.x0, h: b.y1 - b.y0, color: this.style.color, fill: this.style.fill, width: this.style.width }, this.model, null);
			}
		}
		if (a && a.kind === "connect") {
			const fb = frameOf(a.from);
			const target = this.hit(a.to.wx, a.to.wy);
			ctx.strokeStyle = pal.info;
			ctx.lineWidth = 2 / z;
			ctx.setLineDash([6 / z, 4 / z]);
			ctx.beginPath();
			ctx.moveTo((fb.x0 + fb.x1) / 2, (fb.y0 + fb.y1) / 2);
			ctx.lineTo(a.to.wx, a.to.wy);
			ctx.stroke();
			ctx.setLineDash([]);
			if (target && target.id !== a.from.id && !isPath(target.type) && target.type !== "connector") {
				const tb = frameOf(target);
				ctx.strokeRect(tb.x0 - 3, tb.y0 - 3, tb.x1 - tb.x0 + 6, tb.y1 - tb.y0 + 6);
			}
		}
		if (a && a.kind === "marquee") {
			const b = box(a.from.wx, a.from.wy, a.to.wx, a.to.wy);
			ctx.fillStyle = pal.info;
			ctx.globalAlpha = 0.08;
			ctx.fillRect(b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0);
			ctx.globalAlpha = 1;
			ctx.strokeStyle = pal.info;
			ctx.lineWidth = 1 / z;
			ctx.strokeRect(b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0);
		}
		if (this.tool === "eraser" && this.hover) {
			ctx.strokeStyle = pal.muted;
			ctx.lineWidth = 1 / z;
			ctx.beginPath();
			ctx.arc(this.hover.wx, this.hover.wy, this.eraser.size / 2 / z, 0, Math.PI * 2);
			ctx.stroke();
		}
		if (this.cropping) return this.cropOverlay(ctx);
		this.selectionOverlay(ctx);
	}

	selectionOverlay(ctx) {
		const z = this.canvas.camera.zoom;
		const pal = this.canvas.palette;
		const els = this.selected();
		if (!els.length) return;
		ctx.strokeStyle = pal.info;
		ctx.lineWidth = 1 / z;
		for (const el of els) {
			const b = el.type === "connector" || isPath(el.type) ? this.model.boundsOf(el) : frameOf(el);
			ctx.strokeRect(b.x0 - 2 / z, b.y0 - 2 / z, b.x1 - b.x0 + 4 / z, b.y1 - b.y0 + 4 / z);
		}
		if (els.length === 1 && isBoxed(els[0].type)) {
			const el = els[0];
			ctx.fillStyle = pal.surface;
			const s = HANDLE / z;
			for (const [hx, hy] of [[el.x, el.y], [el.x + el.w, el.y], [el.x, el.y + el.h], [el.x + el.w, el.y + el.h], [el.x + el.w / 2, el.y], [el.x + el.w / 2, el.y + el.h], [el.x, el.y + el.h / 2], [el.x + el.w, el.y + el.h / 2]]) {
				ctx.fillRect(hx - s / 2, hy - s / 2, s, s);
				ctx.strokeRect(hx - s / 2, hy - s / 2, s, s);
			}
		}
	}

	cropOverlay(ctx) {
		const c = this.cropping;
		const z = this.canvas.camera.zoom;
		const pal = this.canvas.palette;
		const full = this.cropFull(c);
		const img = this.view.image(c.el.src);
		if (img) {
			ctx.globalAlpha = 0.35;
			ctx.drawImage(img, full.x, full.y, full.w, full.h);
			ctx.globalAlpha = 1;
			const [cx, cy, cw, ch] = c.crop;
			ctx.drawImage(img, cx, cy, cw, ch, full.x + cx * c.scale, full.y + cy * c.scale, cw * c.scale, ch * c.scale);
		}
		const [cx, cy, cw, ch] = c.crop;
		const x = full.x + cx * c.scale, y = full.y + cy * c.scale, w = cw * c.scale, h = ch * c.scale;
		ctx.strokeStyle = pal.info;
		ctx.lineWidth = 2 / z;
		ctx.strokeRect(x, y, w, h);
		ctx.fillStyle = pal.info;
		const s = (HANDLE + 2) / z;
		for (const [hx, hy] of [[x, y], [x + w, y], [x, y + h], [x + w, y + h], [x + w / 2, y], [x + w / 2, y + h], [x, y + h / 2], [x + w, y + h / 2]]) ctx.fillRect(hx - s / 2, hy - s / 2, s, s);
	}
}

// While cropping, a press on a crop handle drags it; elsewhere it ends the crop.
const startSelect = BoardInput.prototype.startSelect;
BoardInput.prototype.startSelect = function (p, e) {
	if (this.cropping) {
		const handle = this.cropHandleAt(p);
		if (handle) {
			this.action = { kind: "crop", handle, start: this.cropping.crop.slice() };
			return;
		}
		this.endCrop(true);
		return;
	}
	return startSelect.call(this, p, e);
};

export { segDist };
