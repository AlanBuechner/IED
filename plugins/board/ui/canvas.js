// canvas.js - the board's canvas (docs/32 "Coordinates and tiles"): a camera over the infinite board, drawn as tiles -
// a tile rendered once into an offscreen bitmap for its scale and reused while panning, redrawn only when an element
// touching it changes - and the live layer above them (what is being drawn, the selection, the eraser, the AI's changes
// flashing). A tile holds the board's background, its grid and every visible layer, so it is opaque and its text gets
// the screen's subpixel antialiasing; once zooming settles, tiles are drawn at the camera's own zoom and placed on whole
// device pixels, so what is on the screen is the tile's pixels unscaled.

import { overlaps, union, EMPTY, isEmpty } from "./geom.js";
import { Palette, drawElement } from "./render.js";

const TILE_PX = 512;       // a tile's side in CSS pixels (in device pixels, rounded)
const MOST_TILES = 320;
const FLASH_MS = 1400;
const SETTLE_MS = 150;     // while zooming, the nearest power-of-two tiles are drawn scaled; this long after, exact ones

const isPowerOfTwo = (s) => Number.isInteger(Math.log2(s));

export class BoardCanvas {
	constructor(el, model, images) {
		this.el = el;
		this.model = model;
		this.images = images;
		this.canvas = document.createElement("canvas");
		this.canvas.className = "board-canvas";
		this.canvas.tabIndex = 0;
		el.append(this.canvas);
		this.ctx = this.canvas.getContext("2d", { alpha: false });
		this.palette = new Palette();
		this.camera = { x: -40, y: -40, zoom: 1 };
		this.tiles = new Map();        // "scale|tx|ty" -> {canvas, ctx, s, tx, ty, box, dirty, used}
		this.hidden = new Set();       // ids drawn live, not in tiles (being moved, erased, edited)
		this.overlays = [];            // fns(ctx) drawn above the tiles, in world coordinates
		this.flashes = new Map();      // id -> until (ms)
		this.frame = 0;
		this.used = 0;
		this.zoomedAt = 0;             // when the camera's zoom last changed by a step (wheel, pinch, buttons)
		this.devicePx = null;          // the canvas's size in device pixels, as the browser lays it out
		this.resize = new ResizeObserver((entries) => this.observed(entries));
		try {
			this.resize.observe(this.canvas, { box: "device-pixel-content-box" });
		}
		catch {
			this.resize.observe(this.canvas);
		}
		this.fit();
	}

	dispose() {
		this.resize.disconnect();
		if (this.frame) cancelAnimationFrame(this.frame);
		clearTimeout(this.timer);
		clearTimeout(this.settleTimer);
		this.tiles.clear();
	}

	observed(entries) {
		const entry = entries[entries.length - 1];
		const box = entry.devicePixelContentBoxSize && entry.devicePixelContentBoxSize[0];
		this.devicePx = box ? { w: box.inlineSize, h: box.blockSize, cssW: entry.contentRect.width, cssH: entry.contentRect.height } : null;
		this.fit();
	}

	// Sizes the canvas's bitmap to its box in device pixels - exactly, where the browser says what that is - so the
	// bitmap is shown unscaled.
	fit() {
		const dpr = window.devicePixelRatio || 1;
		const rect = this.canvas.getBoundingClientRect();
		const w = Math.max(1, rect.width);
		const h = Math.max(1, rect.height);
		const d = this.devicePx;
		const exact = d && Math.abs(d.cssW - w) < 0.5 && Math.abs(d.cssH - h) < 0.5;
		const pw = exact ? Math.max(1, d.w) : Math.max(1, Math.round(w * dpr));
		const ph = exact ? Math.max(1, d.h) : Math.max(1, Math.round(h * dpr));
		if (dpr !== this.dpr) this.tiles.clear();
		this.width = w;
		this.height = h;
		this.dpr = dpr;
		this.tilePx = Math.round(TILE_PX * dpr);
		if (this.canvas.width !== pw) this.canvas.width = pw;
		if (this.canvas.height !== ph) this.canvas.height = ph;
		this.request();
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
		this.zoomedAt = performance.now();
		clearTimeout(this.settleTimer);
		this.settleTimer = setTimeout(() => this.request(), SETTLE_MS + 20);
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

	// The scale tiles are drawn at: the camera's zoom once zooming has settled, the nearest power of two while it goes on
	// (those tiles are kept, so a zoom in motion reuses them instead of drawing the board again at every step).
	scale() {
		const z = this.camera.zoom;
		if (performance.now() - this.zoomedAt >= SETTLE_MS) return z;
		return Math.pow(2, Math.max(-5, Math.min(3, Math.round(Math.log2(z)))));
	}

	// A tile's side in world units at scale `s`.
	tileWorld(s) {
		return this.tilePx / (s * this.dpr);
	}

	invalidateAll() {
		for (const t of this.tiles.values()) t.dirty = true;
		this.request();
	}

	// Marks the tiles that `box` (world) touches dirty, at every scale.
	invalidate(box) {
		if (!box || isEmpty(box)) return;
		for (const t of this.tiles.values()) {
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

	tile(s, tx, ty) {
		const key = s + "|" + tx + "|" + ty;
		let t = this.tiles.get(key);
		if (!t) {
			const px = this.tilePx;
			const size = this.tileWorld(s);
			const canvas = document.createElement("canvas");
			canvas.width = px;
			canvas.height = px;
			t = { canvas, ctx: canvas.getContext("2d", { alpha: false }), s, tx, ty, dirty: true, used: 0, box: { x0: tx * size, y0: ty * size, x1: (tx + 1) * size, y1: (ty + 1) * size } };
			this.tiles.set(key, t);
		}
		t.used = ++this.used;
		if (t.dirty) this.renderTile(t);
		return t;
	}

	// A transparent tile-sized bitmap, for a layer drawn below full opacity before it is laid on its tile.
	scratch() {
		const px = this.tilePx;
		if (!this.scratchCanvas || this.scratchCanvas.width !== px) {
			this.scratchCanvas = document.createElement("canvas");
			this.scratchCanvas.width = px;
			this.scratchCanvas.height = px;
		}
		return this.scratchCanvas.getContext("2d");
	}

	renderTile(t) {
		const ctx = t.ctx;
		const px = this.tilePx;
		const k = t.s * this.dpr;
		ctx.setTransform(1, 0, 0, 1, 0, 0);
		ctx.globalAlpha = 1;
		ctx.fillStyle = this.palette.surface;
		ctx.fillRect(0, 0, px, px);
		this.drawGrid(ctx, t);
		const near = this.model.all().filter((el) => !this.hidden.has(el.id) && overlaps(this.model.boundsOf(el), t.box));
		for (const layer of this.model.layers) {
			if (!layer.visible) continue;
			const els = near.filter((el) => el.layer === layer.id);
			if (!els.length) continue;
			const opacity = layer.opacity ?? 1;
			const target = opacity >= 1 ? ctx : this.scratch();
			if (target !== ctx) {
				target.setTransform(1, 0, 0, 1, 0, 0);
				target.clearRect(0, 0, px, px);
			}
			target.setTransform(k, 0, 0, k, -t.tx * px, -t.ty * px);
			for (const el of els) drawElement(target, this.palette, el, this.model, this.images);
			if (target !== ctx) {
				ctx.setTransform(1, 0, 0, 1, 0, 0);
				ctx.globalAlpha = opacity;
				ctx.drawImage(target.canvas, 0, 0);
				ctx.globalAlpha = 1;
			}
		}
		t.dirty = false;
	}

	// Drops the tiles of a settled zoom the camera has left (powers of two stay, for zooming), then the least recently
	// used past the most kept.
	evict(s) {
		if (!isPowerOfTwo(s)) {
			for (const [key, t] of this.tiles) if (t.s !== s && !isPowerOfTwo(t.s)) this.tiles.delete(key);
		}
		if (this.tiles.size <= MOST_TILES) return;
		const entries = [...this.tiles.entries()].sort((a, b) => a[1].used - b[1].used);
		for (let i = 0; i < entries.length - MOST_TILES; i++) this.tiles.delete(entries[i][0]);
	}

	// The dot grid, on whole device pixels.
	drawGrid(ctx, t) {
		const k = t.s * this.dpr;
		let step = 20;
		while (step * t.s < 14) step *= 5;
		const d = Math.max(1, Math.round(Math.max(0.8, Math.min(1.4, t.s)) * this.dpr));
		const ox = t.tx * this.tilePx;
		const oy = t.ty * this.tilePx;
		ctx.fillStyle = this.palette.gridStrong;
		for (let x = Math.floor(t.box.x0 / step) * step; x <= t.box.x1; x += step)
			for (let y = Math.floor(t.box.y0 / step) * step; y <= t.box.y1; y += step)
				ctx.fillRect(Math.round(x * k - ox - d / 2), Math.round(y * k - oy - d / 2), d, d);
	}

	draw() {
		const ctx = this.ctx;
		const z = this.camera.zoom;
		const zd = z * this.dpr;
		const s = this.scale();
		// The camera on whole device pixels: tiles of the camera's own zoom land unscaled.
		const ox = Math.round(this.camera.x * zd);
		const oy = Math.round(this.camera.y * zd);
		ctx.setTransform(1, 0, 0, 1, 0, 0);
		ctx.globalAlpha = 1;
		ctx.fillStyle = this.palette.surface;
		ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
		const px = this.tilePx;
		const size = this.tileWorld(s);
		const shown = px * (z / s);
		const view = this.viewBox();
		for (let tx = Math.floor(view.x0 / size); tx * size <= view.x1; tx++)
			for (let ty = Math.floor(view.y0 / size); ty * size <= view.y1; ty++) {
				const t = this.tile(s, tx, ty);
				if (s === z) ctx.drawImage(t.canvas, tx * px - ox, ty * px - oy);
				else ctx.drawImage(t.canvas, tx * shown - ox, ty * shown - oy, shown, shown);
			}
		ctx.save();
		ctx.setTransform(zd, 0, 0, zd, -ox, -oy);
		// Elements drawn live (being moved or edited), in their layers' order and opacity.
		if (this.hidden.size) {
			for (const layer of this.model.layers) {
				if (!layer.visible) continue;
				ctx.globalAlpha = layer.opacity ?? 1;
				for (const id of this.hidden) {
					const el = this.model.get(id);
					if (el && el.layer === layer.id && !el._erasing) drawElement(ctx, this.palette, el, this.model, this.images);
				}
			}
			ctx.globalAlpha = 1;
		}
		this.drawFlashes(ctx);
		for (const overlay of this.overlays) overlay(ctx, this);
		ctx.restore();
		this.evict(s);
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
