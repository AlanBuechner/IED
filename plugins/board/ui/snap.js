// snap.js - alignment snapping for the board's pointer tools (docs/32 "Snapping"), in world units: what is moved,
// resized or drawn snaps its edges and center to the edges and centers of the boxes on the screen, and a moved
// selection also to equal gaps in its row or column (between two neighbours, or the gap two neighbours already keep).
// The guides say what it lined up with. Pure functions of boxes: the tools give the targets and draw the guides.

export const SNAP_PX = 6;          // how near (screen pixels) an edge or center comes before it snaps

const AXES = {
	x: { lo: "x0", hi: "x1", clo: "y0", chi: "y1" },
	y: { lo: "y0", hi: "y1", clo: "x0", chi: "x1" },
};

const mid = (b, lo, hi) => (b[lo] + b[hi]) / 2;

// The boxes things snap to, with their edges and centers on each axis.
export function snapTargets(boxes) {
	const lines = { x: [], y: [] };
	for (const b of boxes) {
		for (const name of ["x", "y"]) {
			const { lo, hi } = AXES[name];
			lines[name].push({ v: b[lo], b }, { v: mid(b, lo, hi), b }, { v: b[hi], b });
		}
	}
	return { boxes, lines };
}

// The smallest shift within `tol` that puts one of `values` on a line, or null.
function nearestShift(values, lines, tol) {
	let best = null;
	for (const m of values) {
		for (const l of lines) {
			const d = l.v - m;
			if (Math.abs(d) <= tol && (best === null || Math.abs(d) < Math.abs(best))) best = d;
		}
	}
	return best;
}

// Where `m` would sit at an equal gap on axis `name`: between its two nearest neighbours in its row (column), or a
// neighbour's own gap to the next one on. Each is {d, gaps}: the shift, and the gaps that would then be equal, as
// [from, to, cross] (cross: where across the axis the gap is drawn).
export function spacing(m, boxes, name, tol) {
	const { lo, hi, clo, chi } = AXES[name];
	const across = (a, b) => a[clo] < b[chi] && a[chi] > b[clo];
	const between = (a, b) => (Math.max(a[clo], b[clo]) + Math.min(a[chi], b[chi])) / 2;
	const size = m[hi] - m[lo];
	// The nearest box before `b` in its own row, and after it.
	const beforeOf = (b, slack, row) => {
		let best = null;
		for (const o of boxes) if (o !== b && across(o, row) && o[hi] <= b[lo] + slack && o[lo] < b[lo] && (!best || o[hi] > best[hi])) best = o;
		return best;
	};
	const afterOf = (b, slack, row) => {
		let best = null;
		for (const o of boxes) if (o !== b && across(o, row) && o[lo] >= b[hi] - slack && o[hi] > b[hi] && (!best || o[lo] < best[lo])) best = o;
		return best;
	};
	const out = [];
	const before = beforeOf(m, tol, m);
	const after = afterOf(m, tol, m);
	if (before && after && after[lo] - before[hi] > size) {
		const at = (before[hi] + after[lo] - size) / 2;
		const moved = { ...m, [lo]: at, [hi]: at + size };
		out.push({ d: at - m[lo], gaps: [[before[hi], at, between(before, moved)], [at + size, after[lo], between(moved, after)]] });
	}
	if (before) {
		const next = beforeOf(before, 0, before);
		const gap = next ? before[lo] - next[hi] : 0;
		if (gap > 0) {
			const at = before[hi] + gap;
			const moved = { ...m, [lo]: at, [hi]: at + size };
			out.push({ d: at - m[lo], gaps: [[next[hi], before[lo], between(next, before)], [before[hi], at, between(before, moved)]] });
		}
	}
	if (after) {
		const next = afterOf(after, 0, after);
		const gap = next ? next[lo] - after[hi] : 0;
		if (gap > 0) {
			const at = after[lo] - gap - size;
			const moved = { ...m, [lo]: at, [hi]: at + size };
			out.push({ d: at - m[lo], gaps: [[at + size, after[lo], between(moved, after)], [after[hi], next[lo], between(after, next)]] });
		}
	}
	return out.filter((c) => Math.abs(c.d) <= tol);
}

// The guides that hold for box `m`: lines through `values` (by axis; default its edges and center) that targets
// share, and with `withSpacing` the equal gaps it keeps. A line is {axis, v, from, to}; a gap guide {axis, gaps}.
export function guidesFor(m, targets, eps, values, withSpacing) {
	const guides = [];
	for (const name of ["x", "y"]) {
		const { lo, hi, clo, chi } = AXES[name];
		const own = values ? values[name] || [] : [m[lo], mid(m, lo, hi), m[hi]];
		const seen = new Set();
		for (const v of own) {
			let line = null;
			for (const l of targets.lines[name]) {
				if (Math.abs(l.v - v) > eps) continue;
				if (!line) line = { axis: name, v: l.v, from: m[clo], to: m[chi] };
				line.from = Math.min(line.from, l.b[clo]);
				line.to = Math.max(line.to, l.b[chi]);
			}
			if (line && !seen.has(line.v)) {
				seen.add(line.v);
				guides.push(line);
			}
		}
		if (withSpacing) for (const c of spacing(m, targets.boxes, name, eps)) guides.push({ axis: name, gaps: c.gaps });
	}
	return guides;
}

// Snaps a moved box: the shift on each axis (0 when nothing is near) and the guides at the snapped place.
export function snapBox(m, targets, tol, eps) {
	const shift = {};
	for (const name of ["x", "y"]) {
		const { lo, hi } = AXES[name];
		let d = nearestShift([m[lo], mid(m, lo, hi), m[hi]], targets.lines[name], tol);
		for (const c of spacing(m, targets.boxes, name, tol)) if (d === null || Math.abs(c.d) < Math.abs(d) - 1e-9) d = c.d;
		shift[name] = d || 0;
	}
	const moved = { x0: m.x0 + shift.x, y0: m.y0 + shift.y, x1: m.x1 + shift.x, y1: m.y1 + shift.y };
	return { dx: shift.x, dy: shift.y, guides: guidesFor(moved, targets, eps, null, true) };
}

// Snaps one coordinate on an axis to the nearest edge or center within `tol`; null leaves it.
export function snapValue(v, targets, name, tol) {
	if (v === null || v === undefined) return v;
	const d = nearestShift([v], targets.lines[name], tol);
	return d === null ? v : v + d;
}
