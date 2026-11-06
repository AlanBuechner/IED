// geom.js - the board's geometry in the browser, the same as the server's (code/core/Shapes.cf): outlines, connector
// routes, bounds, hit tests, and the eraser's cut of a stroke.

export const NODE_TYPES = ["rect", "round", "ellipse", "diamond", "cylinder", "hexagon", "parallelogram"];
export const BOXED_TYPES = [...NODE_TYPES, "text", "note", "frame", "image"];
export const PATH_TYPES = ["stroke", "line", "arrow"];

export const isNode = (t) => NODE_TYPES.includes(t);
export const isBoxed = (t) => BOXED_TYPES.includes(t);
export const isPath = (t) => PATH_TYPES.includes(t);

export function box(x0, y0, x1, y1) {
	return { x0: Math.min(x0, x1), y0: Math.min(y0, y1), x1: Math.max(x0, x1), y1: Math.max(y0, y1) };
}

export const EMPTY = { x0: Infinity, y0: Infinity, x1: -Infinity, y1: -Infinity };
export const isEmpty = (b) => b.x0 > b.x1 || b.y0 > b.y1;

export function union(a, b) {
	return { x0: Math.min(a.x0, b.x0), y0: Math.min(a.y0, b.y0), x1: Math.max(a.x1, b.x1), y1: Math.max(a.y1, b.y1) };
}

export function inflate(b, d) {
	return isEmpty(b) ? b : { x0: b.x0 - d, y0: b.y0 - d, x1: b.x1 + d, y1: b.y1 + d };
}

export function overlaps(a, b) {
	return !isEmpty(a) && !isEmpty(b) && a.x0 <= b.x1 && a.x1 >= b.x0 && a.y0 <= b.y1 && a.y1 >= b.y0;
}

export function contains(b, x, y) {
	return x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1;
}

export function pointsBox(pts) {
	let b = EMPTY;
	for (let i = 0; i + 1 < pts.length; i += 2) b = union(b, { x0: pts[i], y0: pts[i + 1], x1: pts[i], y1: pts[i + 1] });
	return b;
}

export function frameOf(el) {
	if (isBoxed(el.type)) return box(el.x, el.y, el.x + el.w, el.y + el.h);
	if (isPath(el.type)) return pointsBox(el.pts || []);
	return EMPTY;
}

export function segDist(px, py, ax, ay, bx, by) {
	const dx = bx - ax;
	const dy = by - ay;
	const l2 = dx * dx + dy * dy;
	let t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
	t = Math.max(0, Math.min(1, t));
	return Math.hypot(px - ax - t * dx, py - ay - t * dy);
}

export function polyDist(pts, px, py) {
	if (pts.length < 2) return Infinity;
	if (pts.length < 4) return Math.hypot(px - pts[0], py - pts[1]);
	let best = Infinity;
	for (let i = 0; i + 3 < pts.length; i += 2) best = Math.min(best, segDist(px, py, pts[i], pts[i + 1], pts[i + 2], pts[i + 3]));
	return best;
}

export function roundedOutline(x, y, w, h, r) {
	r = Math.min(r, Math.min(w, h) / 2);
	const pts = [];
	const cx = [x + w - r, x + w - r, x + r, x + r];
	const cy = [y + r, y + h - r, y + h - r, y + r];
	for (let c = 0; c < 4; c++)
		for (let i = 0; i <= 6; i++) {
			const a = (c - 1 + i / 6) * Math.PI / 2;
			pts.push(cx[c] + Math.cos(a) * r, cy[c] + Math.sin(a) * r);
		}
	return pts;
}

export function outline(el) {
	const { x, y, w, h } = el;
	switch (el.type) {
		case "ellipse": {
			const pts = [];
			for (let i = 0; i < 48; i++) {
				const a = i * 2 * Math.PI / 48;
				pts.push(x + w / 2 + Math.cos(a) * w / 2, y + h / 2 + Math.sin(a) * h / 2);
			}
			return pts;
		}
		case "diamond": return [x + w / 2, y, x + w, y + h / 2, x + w / 2, y + h, x, y + h / 2];
		case "hexagon": {
			const i = Math.min(w * 0.25, h * 0.5);
			return [x + i, y, x + w - i, y, x + w, y + h / 2, x + w - i, y + h, x + i, y + h, x, y + h / 2];
		}
		case "parallelogram": {
			const s = Math.min(w * 0.2, h * 0.6);
			return [x + s, y, x + w, y, x + w - s, y + h, x, y + h];
		}
		default: return [x, y, x + w, y, x + w, y + h, x, y + h];
	}
}

export function insidePolygon(poly, px, py) {
	let inside = false;
	const n = poly.length / 2;
	for (let i = 0, j = n - 1; i < n; j = i++) {
		const xi = poly[i * 2], yi = poly[i * 2 + 1], xj = poly[j * 2], yj = poly[j * 2 + 1];
		if ((yi > py) !== (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) inside = !inside;
	}
	return inside;
}

// The crossing of segment a-b with the closed polygon nearest to b, or null.
function clip(poly, ax, ay, bx, by) {
	const n = poly.length / 2;
	let best = -1;
	const rx = bx - ax, ry = by - ay;
	for (let i = 0; i < n; i++) {
		const j = (i + 1) % n;
		const cx = poly[i * 2], cy = poly[i * 2 + 1];
		const sx = poly[j * 2] - cx, sy = poly[j * 2 + 1] - cy;
		const d = rx * sy - ry * sx;
		if (Math.abs(d) < 1e-12) continue;
		const t = ((cx - ax) * sy - (cy - ay) * sx) / d;
		const u = ((cx - ax) * ry - (cy - ay) * rx) / d;
		if (t >= 0 && t <= 1 && u >= 0 && u <= 1 && t > best) best = t;
	}
	return best < 0 ? null : [ax + rx * best, ay + ry * best];
}

export function center(el) {
	const b = frameOf(el);
	return isEmpty(b) ? [0, 0] : [(b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2];
}

function exit(el, ix, iy, ox, oy) {
	const poly = isBoxed(el.type) ? outline(el) : (() => { const b = inflate(frameOf(el), 4); return [b.x0, b.y0, b.x1, b.y0, b.x1, b.y1, b.x0, b.y1]; })();
	return clip(poly, ox, oy, ix, iy) || [ix, iy];
}

function elbow(a, b) {
	const fa = frameOf(a), fb = frameOf(b);
	const acx = (fa.x0 + fa.x1) / 2, acy = (fa.y0 + fa.y1) / 2, bcx = (fb.x0 + fb.x1) / 2, bcy = (fb.y0 + fb.y1) / 2;
	const dx = bcx - acx, dy = bcy - acy;
	if (Math.abs(dx) >= Math.abs(dy)) {
		const sx = dx >= 0 ? fa.x1 : fa.x0, ex = dx >= 0 ? fb.x0 : fb.x1, mx = (sx + ex) / 2;
		return Math.abs(acy - bcy) > 0.5 ? [sx, acy, mx, acy, mx, bcy, ex, bcy] : [sx, acy, ex, bcy];
	}
	const sy = dy >= 0 ? fa.y1 : fa.y0, ey = dy >= 0 ? fb.y0 : fb.y1, my = (sy + ey) / 2;
	return Math.abs(acx - bcx) > 0.5 ? [acx, sy, acx, my, bcx, my, bcx, ey] : [acx, sy, bcx, ey];
}

// A connector's route between the elements it binds (from and to the elements themselves).
export function route(conn, from, to) {
	if (!from || !to) return [];
	if (conn.route === "elbow" && isBoxed(from.type) && isBoxed(to.type)) return elbow(from, to);
	const [ax, ay] = center(from);
	const [bx, by] = center(to);
	return [...exit(from, ax, ay, bx, by), ...exit(to, bx, by, ax, ay)];
}

export const headLength = (width) => 10 + width * 2;

export function bounds(el, routePts) {
	const pad = (el.width ?? 2) / 2 + 1;
	if (el.type === "connector") return inflate(pointsBox(routePts || []), pad + headLength(el.width ?? 2) * 0.6);
	if (isPath(el.type)) return inflate(pointsBox(el.pts || []), pad + headLength(el.width ?? 2) * 0.6);
	if (el.type === "frame") return inflate(box(el.x, el.y - 4, el.x + el.w, el.y + el.h), pad);
	return inflate(frameOf(el), pad);
}

export function hitDistance(el, routePts, px, py) {
	const half = (el.width ?? 2) / 2;
	if (el.type === "connector") return Math.max(0, polyDist(routePts, px, py) - half);
	if (isPath(el.type)) return Math.max(0, polyDist(el.pts, px, py) - half);
	const b = frameOf(el);
	if (el.type === "frame") {
		const inTitle = px >= b.x0 && px <= b.x1 && py >= b.y0 && py <= b.y0 + 30;
		const o = outline(el);
		return inTitle ? 0 : polyDist([...o, o[0], o[1]], px, py);
	}
	if (el.type === "text" || el.type === "note" || el.type === "image") return contains(b, px, py) ? 0 : Math.max(0, Math.max(b.x0 - px, px - b.x1, b.y0 - py, py - b.y1));
	const o = outline(el);
	if (insidePolygon(o, px, py)) return 0;
	return polyDist([...o, o[0], o[1]], px, py);
}

// Ramer-Douglas-Peucker.
export function simplify(pts, eps) {
	const n = pts.length / 2;
	if (n <= 2) return pts.slice();
	const keep = new Uint8Array(n);
	keep[0] = keep[n - 1] = 1;
	const stack = [[0, n - 1]];
	while (stack.length) {
		const [a, b] = stack.pop();
		let worst = -1, at = -1;
		for (let i = a + 1; i < b; i++) {
			const d = segDist(pts[i * 2], pts[i * 2 + 1], pts[a * 2], pts[a * 2 + 1], pts[b * 2], pts[b * 2 + 1]);
			if (d > worst) { worst = d; at = i; }
		}
		if (worst > eps) {
			keep[at] = 1;
			stack.push([a, at], [at, b]);
		}
	}
	const out = [];
	for (let i = 0; i < n; i++) if (keep[i]) out.push(pts[i * 2], pts[i * 2 + 1]);
	return out;
}

const tenth = (v) => Math.round(v * 10) / 10;
export const roundPts = (pts) => pts.map(tenth);
export { tenth };

// What is left of a polyline after an eraser of `radius` swept along the segment a-b: the pieces outside it (each with
// at least two points), or null when the eraser missed it.
export function erase(pts, ax, ay, bx, by, radius) {
	const n = pts.length / 2;
	const inside = (x, y) => segDist(x, y, ax, ay, bx, by) <= radius;
	// Points along each segment, finer than the eraser, so a cut lands near the edge of the swept band.
	const step = Math.max(1, radius / 3);
	const dense = [];
	for (let i = 0; i < n - 1; i++) {
		const x0 = pts[i * 2], y0 = pts[i * 2 + 1], x1 = pts[i * 2 + 2], y1 = pts[i * 2 + 3];
		const len = Math.hypot(x1 - x0, y1 - y0);
		const k = Math.max(1, Math.ceil(len / step));
		for (let j = 0; j < k; j++) dense.push(x0 + (x1 - x0) * j / k, y0 + (y1 - y0) * j / k);
	}
	if (n >= 1) dense.push(pts[pts.length - 2], pts[pts.length - 1]);
	let hit = false;
	const pieces = [];
	let current = [];
	for (let i = 0; i < dense.length; i += 2) {
		if (inside(dense[i], dense[i + 1])) {
			hit = true;
			if (current.length >= 4) pieces.push(current);
			current = [];
		}
		else current.push(dense[i], dense[i + 1]);
	}
	if (current.length >= 4) pieces.push(current);
	if (!hit) return null;
	return pieces.map((p) => roundPts(simplify(p, 0.4)));
}
