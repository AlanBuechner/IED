// render.js - drawing board elements on a 2D canvas in world coordinates, in the theme's colors (docs/32: colors are
// palette names drawn with the --hue-* tokens; the server's pictures read the same tokens).

import { isNode, outline, roundedOutline, headLength, route } from "./geom.js";

const HUES = ["gray", "red", "orange", "amber", "yellow", "lime", "green", "teal", "cyan", "blue", "indigo", "violet", "pink"];

// The theme's colors, read once per page from the tokens on :root.
export class Palette {
	constructor() {
		const css = getComputedStyle(document.documentElement);
		const read = (name) => css.getPropertyValue(name).trim();
		this.ink = read("--text-0");
		this.surface = read("--bg-2");
		this.muted = read("--text-2");
		this.grid = read("--border-0");
		this.gridStrong = read("--border-1");
		this.ai = read("--accent-ai");
		this.info = read("--accent-info");
		this.font = read("--font-ui") || "sans-serif";
		this.hues = {};
		for (const hue of HUES) this.hues[hue] = read("--hue-" + hue);
	}

	color(name, fallback) {
		if (!name || name === "ink") return fallback || this.ink;
		return this.hues[name] || fallback || this.ink;
	}
}

export const PALETTE_NAMES = ["ink", ...HUES];

function tracePolygon(ctx, pts, closed) {
	ctx.beginPath();
	ctx.moveTo(pts[0], pts[1]);
	for (let i = 2; i + 1 < pts.length; i += 2) ctx.lineTo(pts[i], pts[i + 1]);
	if (closed) ctx.closePath();
}

function setDash(ctx, el) {
	const w = el.width ?? 2;
	if (el.style === "dashed") ctx.setLineDash([w * 4 + 4, w * 3 + 3]);
	else if (el.style === "dotted") ctx.setLineDash([w, w * 2 + 2]);
	else ctx.setLineDash([]);
}

// A stroke through points, smoothed through the midpoints so freehand ink is not a polygon.
function traceSmooth(ctx, pts) {
	ctx.beginPath();
	ctx.moveTo(pts[0], pts[1]);
	if (pts.length < 6) {
		for (let i = 2; i + 1 < pts.length; i += 2) ctx.lineTo(pts[i], pts[i + 1]);
		return;
	}
	for (let i = 2; i + 3 < pts.length; i += 2) {
		const mx = (pts[i] + pts[i + 2]) / 2;
		const my = (pts[i + 1] + pts[i + 3]) / 2;
		ctx.quadraticCurveTo(pts[i], pts[i + 1], mx, my);
	}
	ctx.lineTo(pts[pts.length - 2], pts[pts.length - 1]);
}

function head(ctx, tipX, tipY, fromX, fromY, length, dot) {
	const d = Math.hypot(tipX - fromX, tipY - fromY);
	if (d <= 0) return;
	const ux = (tipX - fromX) / d, uy = (tipY - fromY) / d;
	ctx.beginPath();
	if (dot) ctx.arc(tipX, tipY, length * 0.3, 0, Math.PI * 2);
	else {
		ctx.moveTo(tipX, tipY);
		ctx.lineTo(tipX - ux * length - uy * length * 0.42, tipY - uy * length + ux * length * 0.42);
		ctx.lineTo(tipX - ux * length + uy * length * 0.42, tipY - uy * length - ux * length * 0.42);
		ctx.closePath();
	}
	ctx.fill();
}

// The point `back` along the polyline from its end or its start.
function behind(pts, fromEnd, back) {
	const n = pts.length / 2;
	let left = back;
	for (let k = 0; k + 1 < n; k++) {
		const i = fromEnd ? n - 1 - k : k;
		const j = fromEnd ? i - 1 : i + 1;
		const len = Math.hypot(pts[j * 2] - pts[i * 2], pts[j * 2 + 1] - pts[i * 2 + 1]);
		if (len >= left || k + 2 === n) {
			const t = len > 0 ? Math.min(1, left / len) : 0;
			return [pts[i * 2] + (pts[j * 2] - pts[i * 2]) * t, pts[i * 2 + 1] + (pts[j * 2 + 1] - pts[i * 2 + 1]) * t];
		}
		left -= len;
	}
	return [pts[0], pts[1]];
}

function line(ctx, el, pts, color, defaultEnd) {
	if (pts.length < 4) return;
	ctx.strokeStyle = color;
	ctx.fillStyle = color;
	ctx.lineWidth = el.width ?? 2;
	setDash(ctx, el);
	tracePolygon(ctx, pts, false);
	ctx.stroke();
	ctx.setLineDash([]);
	const len = headLength(el.width ?? 2);
	const end = el.end || (defaultEnd ? "arrow" : "none");
	const start = el.start || "none";
	if (end !== "none") {
		const [bx, by] = behind(pts, true, len);
		head(ctx, pts[pts.length - 2], pts[pts.length - 1], bx, by, len, end === "dot");
	}
	if (start !== "none") {
		const [bx, by] = behind(pts, false, len);
		head(ctx, pts[0], pts[1], bx, by, len, start === "dot");
	}
}

// Text wrapped to `width` (lines kept), measured with the canvas.
export function wrap(ctx, text, width) {
	const out = [];
	for (const raw of String(text || "").split("\n")) {
		const words = raw.split(" ");
		let current = "";
		for (const word of words) {
			const trial = current ? current + " " + word : word;
			if (current && ctx.measureText(trial).width > width) {
				out.push(current);
				current = word;
			}
			else current = trial;
		}
		out.push(current);
	}
	return out;
}

export function fontOf(palette, size, weight) {
	return (weight || "") + " " + size + "px " + palette.font;
}

function textIn(ctx, palette, text, x, y, w, h, size, color, centered, pad) {
	if (!text) return;
	ctx.font = fontOf(palette, size);
	ctx.fillStyle = color;
	ctx.textBaseline = "top";
	const lines = wrap(ctx, text, Math.max(size, w - pad * 2));
	const lh = size * 1.3;
	let ty = centered ? y + h / 2 - (lh * lines.length) / 2 : y + pad;
	for (const ln of lines) {
		const tw = ctx.measureText(ln).width;
		ctx.fillText(ln, centered ? x + (w - tw) / 2 : x + pad, ty + size * 0.12);
		ty += lh;
	}
}

function node(ctx, palette, el) {
	const color = palette.color(el.color);
	const pts = el.type === "round" ? roundedOutline(el.x, el.y, el.w, el.h, 12) : el.type === "cylinder" ? null : outline(el);
	ctx.lineWidth = el.width ?? 2;
	ctx.strokeStyle = color;
	setDash(ctx, el);
	if (el.type === "cylinder") {
		const ry = Math.min(el.h * 0.15, 14);
		const cx = el.x + el.w / 2;
		if (el.fill && el.fill !== "none") {
			ctx.globalAlpha *= 0.28;
			ctx.fillStyle = palette.color(el.fill);
			ctx.fillRect(el.x, el.y + ry, el.w, el.h - ry * 2);
			ctx.beginPath(); ctx.ellipse(cx, el.y + el.h - ry, el.w / 2, ry, 0, 0, Math.PI); ctx.fill();
			ctx.beginPath(); ctx.ellipse(cx, el.y + ry, el.w / 2, ry, 0, 0, Math.PI * 2); ctx.fill();
			ctx.globalAlpha /= 0.28;
		}
		ctx.beginPath(); ctx.ellipse(cx, el.y + ry, el.w / 2, ry, 0, 0, Math.PI * 2); ctx.stroke();
		ctx.beginPath(); ctx.ellipse(cx, el.y + el.h - ry, el.w / 2, ry, 0, 0, Math.PI); ctx.stroke();
		ctx.beginPath(); ctx.moveTo(el.x, el.y + ry); ctx.lineTo(el.x, el.y + el.h - ry); ctx.moveTo(el.x + el.w, el.y + ry); ctx.lineTo(el.x + el.w, el.y + el.h - ry); ctx.stroke();
	}
	else {
		tracePolygon(ctx, pts, true);
		if (el.fill && el.fill !== "none") {
			ctx.globalAlpha *= 0.28;
			ctx.fillStyle = palette.color(el.fill);
			ctx.fill();
			ctx.globalAlpha /= 0.28;
		}
		ctx.stroke();
	}
	ctx.setLineDash([]);
	const inset = el.type === "diamond" || el.type === "ellipse" ? el.w * 0.16 : 8;
	textIn(ctx, palette, el.text, el.x + inset, el.y, el.w - inset * 2, el.h, el.fontSize ?? 16, palette.ink, true, 0);
}

// Draws one element. `model` gives a connector its ends; `images` loads pictures (returns an image or null).
export function drawElement(ctx, palette, el, model, images) {
	ctx.save();
	ctx.globalAlpha *= el.opacity ?? 1;
	ctx.lineCap = "round";
	ctx.lineJoin = "round";
	const color = palette.color(el.color);
	switch (el.type) {
		case "stroke":
			if ((el.pts || []).length < 2) break;
			ctx.strokeStyle = color;
			ctx.lineWidth = el.width ?? 2;
			if (el.pts.length < 4) {
				ctx.fillStyle = color;
				ctx.beginPath();
				ctx.arc(el.pts[0], el.pts[1], (el.width ?? 2) / 2, 0, Math.PI * 2);
				ctx.fill();
				break;
			}
			traceSmooth(ctx, el.pts);
			ctx.stroke();
			break;
		case "line":
		case "arrow":
			line(ctx, el, el.pts || [], color, el.type === "arrow");
			break;
		case "connector": {
			const pts = route(el, model.get(el.from), model.get(el.to));
			if (pts.length < 4) break;
			line(ctx, el, pts, el.color ? color : palette.muted, true);
			if (el.text) {
				const size = 13;
				const [cx, cy] = behind(pts, false, polyLength(pts) / 2);
				ctx.font = fontOf(palette, size);
				const tw = ctx.measureText(el.text).width;
				ctx.fillStyle = palette.surface;
				ctx.globalAlpha *= 0.9;
				ctx.fillRect(cx - tw / 2 - 4, cy - size * 0.7, tw + 8, size * 1.4);
				ctx.globalAlpha /= 0.9;
				ctx.fillStyle = palette.ink;
				ctx.textBaseline = "middle";
				ctx.fillText(el.text, cx - tw / 2, cy);
			}
			break;
		}
		case "text":
			textIn(ctx, palette, el.text, el.x, el.y, Math.max(el.w, 4) + 100000, el.h, el.fontSize ?? 16, color, false, 0);
			break;
		case "note": {
			const fill = palette.color(el.fill || "yellow");
			ctx.globalAlpha *= 0.3;
			ctx.fillStyle = fill;
			ctx.fillRect(el.x, el.y, el.w, el.h);
			ctx.globalAlpha /= 0.3;
			ctx.strokeStyle = fill;
			ctx.lineWidth = 1;
			ctx.strokeRect(el.x, el.y, el.w, el.h);
			textIn(ctx, palette, el.text, el.x, el.y, el.w, el.h, el.fontSize ?? 16, palette.ink, false, 10);
			break;
		}
		case "frame": {
			const c = el.color ? color : palette.muted;
			ctx.strokeStyle = c;
			ctx.lineWidth = 1.5;
			ctx.setLineDash([6, 4]);
			tracePolygon(ctx, roundedOutline(el.x, el.y, el.w, el.h, 6), true);
			if (el.fill && el.fill !== "none") {
				ctx.globalAlpha *= 0.12;
				ctx.fillStyle = palette.color(el.fill);
				ctx.fill();
				ctx.globalAlpha /= 0.12;
			}
			ctx.stroke();
			ctx.setLineDash([]);
			ctx.font = fontOf(palette, 15, "600");
			ctx.fillStyle = c;
			ctx.textBaseline = "top";
			ctx.fillText(el.text || "", el.x + 10, el.y + 8);
			break;
		}
		case "image": {
			const img = images ? images(el.src) : null;
			if (img) {
				const [cx, cy, cw, ch] = el.crop || [0, 0, el.iw || img.naturalWidth, el.ih || img.naturalHeight];
				ctx.drawImage(img, cx, cy, cw, ch, el.x, el.y, el.w, el.h);
			}
			else {
				ctx.strokeStyle = palette.muted;
				ctx.setLineDash([4, 4]);
				ctx.strokeRect(el.x, el.y, el.w, el.h);
				ctx.setLineDash([]);
			}
			break;
		}
		default:
			if (isNode(el.type)) node(ctx, palette, el);
	}
	ctx.restore();
}

export function polyLength(pts) {
	let len = 0;
	for (let i = 0; i + 3 < pts.length; i += 2) len += Math.hypot(pts[i + 2] - pts[i], pts[i + 3] - pts[i + 1]);
	return len;
}

export { behind };
