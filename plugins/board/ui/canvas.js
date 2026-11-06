// canvas.js - the board's canvas (docs/32 "Coordinates and tiles"): a camera over the infinite board, each layer drawn
// as tiles - a tile rendered once into an offscreen bitmap for its zoom level and reused while panning, redrawn only
// when an element touching it changes - and the live layer above them (what is being drawn, the selection, the
// eraser, the AI's changes flashing).

import { overlaps, union, EMPTY, isEmpty } from "./geom.js";
import { Palette, drawElement } from "./render.js";

const TILE_PX = 512;
const MOST_TILES = 320;
const FLASH_MS = 1400;

export class BoardCanvas {
	constructor(el, model, images) {
		this.el = el;
		this.model = model;
		this.images = images;
		this.canvas = document.createElement("canvas");
		this.canvas.className = "board-canvas";
		this.canvas.tabIndex = 0;
		el.append(this.canvas);
		this.ctx = this.canvas.getContext("2d");
		this.palette = new Palette();
		this.camera = { x: -40, y: -40, zoom: 1 };
		this.tiles = new Map();        // "layer|level|tx|ty" -> {canvas, dirty, used}
		this.hidden = new Set();       // ids drawn live, not in tiles (being moved, erased, edited)
		this.overlays = [];            // fns(ctx) drawn above the tiles, in world coordinates
		this.flashes = new Map();      // id -> until (ms)
		this.frame = 0;
		this.used = 0;
		this.resize = new ResizeObserver(() => this.fit());
		this.resize.observe(el);
		this.fit();
	}

	dispose() {
		this.resize.disconnect();
		if (this.frame) cancelAnimationFrame(this.frame);
		this.tiles.clear();
	}

	fit() {
		const dpr = window.devicePixelRatio || 1;
		const w = Math.max(1, this.el.clientWidth);
		const h = Math.max(1, this.el.clientHeight);
		this.width = w;
		this.height = h;
		this.dpr = dpr;
		this.canvas.width = Math.round(w * dpr);
		this.canvas.height = Math.round(h * dpr);
		this.canvas.style.width = w + "px";
		this.canvas.style.height = h + "px";
		this.invalidateAll();
	}

	toWorld(sx, sy) {
		return [this.camera.x + sx / this.camera.zoom, this.camera.y + sy / this.camera.zoom];
	}

	toScreen(wx, wy) {
		return [(wx - this.camera.x) * this.camera.zoom, (wy - this.camera.y) * this.camera.zoom];
	}

	viewBox() {
		const [x0, y0] = this.toWorld(0, 0);
		const [x1, y1] = this.toWorld(this.width, this.height);
		return { x0, y0, x1, y1 };
	}

	zoomAt(sx, sy, factor) {
		const [wx, wy] = this.toWorld(sx, sy);
		const zoom = Math.min(8, Math.max(0.05, this.camera.zoom * factor));
		this.camera.zoom = zoom;
		this.camera.x = wx - sx / zoom;
		this.camera.y = wy - sy / zoom;
		this.request();
	}

	pan(dx, dy) {
		this.camera.x -= dx / this.camera.zoom;
		this.camera.y -= dy / this.camera.zoom;
		this.request();
	}

	// Shows a box of the board whole, with a margin.
	show(box, most) {
		if (isEmpty(box)) {
			this.camera = { x: -40, y: -40, zoom: 1 };
			return this.request();
		}
		const margin = 40;
		const zoom = Math.min(most || 1.5, (this.width - margin * 2) / Math.max(1, box.x1 - box.x0), (this.height - margin * 2) / Math.max(1, box.y1 - box.y0));
		this.camera.zoom = Math.max(0.05, zoom);
		this.camera.x = (box.x0 + box.x1) / 2 - this.width / 2 / this.camera.zoom;
		this.camera.y = (box.y0 + box.y1) / 2 - this.height / 2 / this.camera.zoom;
		this.request();
	}

	contentBox() {
		let b = EMPTY;
		for (const el of this.model.drawn(true)) b = union(b, this.model.boundsOf(el));
		return b;
	}

	level() {
		return Math.max(-5, Math.min(3, Math.round(Math.log2(this.camera.zoom))));
	}

	tileWorld(level) {
		return TILE_PX / Math.pow(2, level);
	}

	invalidateAll() {
		for (const t of this.tiles.values()) t.dirty = true;
		this.request();
	}

	// Marks the tiles that `box` (world) touches dirty, on every level.
	invalidate(box) {
		if (!box || isEmpty(box)) return;
		for (const [key, t] of this.tiles) {
			if (t.dirty) continue;
			if (overlaps(t.box, box)) t.dirty = true;
		}
		this.request();
	}

	flash(ids) {
		const until = performance.now() + FLASH_MS;
		for (const id of ids) this.flashes.set(id, until);
		this.request();
	}

	// Draws at the next frame - or soon after, where frames do not come (a hidden pane, a background window).
	request() {
		if (this.frame) return;
		const run = () => {
			if (!this.frame) return;
			cancelAnimationFrame(this.frame);
			clearTimeout(this.timer);
			this.frame = 0;
			this.draw();
		};
		this.frame = requestAnimationFrame(run);
		this.timer = setTimeout(run, 120);
	}

	tile(layer, level, tx, ty) {
		const key = layer.id + "|" + level + "|" + tx + "|" + ty;
		let t = this.tiles.get(key);
		const size = this.tileWorld(level);
		if (!t) {
			const px = Math.round(TILE_PX * this.dpr);
			const canvas = document.createElement("canvas");
			canvas.width = px;
			canvas.height = px;
			t = { canvas, dirty: true, used: 0, box: { x0: tx * size, y0: ty * size, x1: (tx + 1) * size, y1: (ty + 1) * size } };
			this.tiles.set(key, t);
		}
		t.used = ++this.used;
		if (t.dirty) this.renderTile(t, layer, size);
		return t;
	}

	renderTile(t, layer, size) {
		const ctx = t.canvas.getContext("2d");
		const scale = t.canvas.width / size;
		ctx.setTransform(1, 0, 0, 1, 0, 0);
		ctx.clearRect(0, 0, t.canvas.width, t.canvas.height);
		ctx.setTransform(scale, 0, 0, scale, -t.box.x0 * scale, -t.box.y0 * scale);
		for (const el of this.model.all()) {
			if (el.layer !== layer.id || this.hidden.has(el.id)) continue;
			if (!overlaps(this.model.boundsOf(el), t.box)) continue;
			drawElement(ctx, this.palette, el, this.model, this.images);
		}
		t.dirty = false;
	}

	evict() {
		if (this.tiles.size <= MOST_TILES) return;
		const entries = [...this.tiles.entries()].sort((a, b) => a[1].used - b[1].used);
		for (let i = 0; i < entries.length - MOST_TILES; i++) this.tiles.delete(entries[i][0]);
	}

	drawGrid(ctx) {
		const z = this.camera.zoom;
		let step = 20;
		while (step * z < 14) step *= 5;
		const view = this.viewBox();
		ctx.fillStyle = this.palette.gridStrong;
		const r = Math.max(0.8, Math.min(1.4, z));
		for (let x = Math.floor(view.x0 / step) * step; x <= view.x1; x += step)
			for (let y = Math.floor(view.y0 / step) * step; y <= view.y1; y += step) {
				const [sx, sy] = this.toScreen(x, y);
				ctx.fillRect(sx - r / 2, sy - r / 2, r, r);
			}
	}

	draw() {
		const ctx = this.ctx;
		const dpr = this.dpr;
		ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
		ctx.fillStyle = this.palette.surface;
		ctx.fillRect(0, 0, this.width, this.height);
		this.drawGrid(ctx);
		const level = this.level();
		const size = this.tileWorld(level);
		const view = this.viewBox();
		const z = this.camera.zoom;
		for (const layer of this.model.layers) {
			if (!layer.visible) continue;
			ctx.globalAlpha = layer.opacity ?? 1;
			for (let tx = Math.floor(view.x0 / size); tx * size <= view.x1; tx++)
				for (let ty = Math.floor(view.y0 / size); ty * size <= view.y1; ty++) {
					const t = this.tile(layer, level, tx, ty);
					const [sx, sy] = this.toScreen(t.box.x0, t.box.y0);
					ctx.drawImage(t.canvas, sx, sy, size * z, size * z);
				}
			// Elements drawn live (being moved or edited) show on their layer.
			ctx.save();
			ctx.setTransform(dpr * z, 0, 0, dpr * z, -this.camera.x * dpr * z, -this.camera.y * dpr * z);
			for (const id of this.hidden) {
				const el = this.model.get(id);
				if (el && el.layer === layer.id && !el._erasing) drawElement(ctx, this.palette, el, this.model, this.images);
			}
			ctx.restore();
		}
		ctx.globalAlpha = 1;
		ctx.save();
		ctx.setTransform(dpr * z, 0, 0, dpr * z, -this.camera.x * dpr * z, -this.camera.y * dpr * z);
		this.drawFlashes(ctx);
		for (const overlay of this.overlays) overlay(ctx, this);
		ctx.restore();
		this.evict();
	}

	drawFlashes(ctx) {
		const now = performance.now();
		let any = false;
		for (const [id, until] of this.flashes) {
			if (until < now) {
				this.flashes.delete(id);
				continue;
			}
			const el = this.model.get(id);
			if (!el) continue;
			any = true;
			const b = this.model.boundsOf(el);
			ctx.globalAlpha = Math.min(1, (until - now) / 600);
			ctx.strokeStyle = this.palette.ai;
			ctx.lineWidth = 2 / this.camera.zoom;
			ctx.setLineDash([]);
			ctx.strokeRect(b.x0 - 3, b.y0 - 3, b.x1 - b.x0 + 6, b.y1 - b.y0 + 6);
		}
		ctx.globalAlpha = 1;
		if (any) this.request();
	}
}
