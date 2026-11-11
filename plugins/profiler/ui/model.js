// model.js - the timeline's state that is not drawing: the range in view, the lanes laid out from a prof.timeline
// answer, and which zone, lock hold or note a point falls on. Pure, so the browser unit tests cover it.

export const MIN_SPAN_NS = 100;

// A lock row's segments: held alone, held while another thread waited, or waiting for it (docs/16 "Locks").
export const LOCK_HELD = 0;
export const LOCK_CONTENDED = 1;
export const LOCK_BLOCKED = 2;
export const LOCK_STATES = ["held", "held while others waited", "waited"];

// The visible time range in nanoseconds since the capture started.
export class TimeView {
	constructor(from = 0, to = 1e9) {
		this.from = from;
		this.to = to;
	}

	get span() {
		return this.to - this.from;
	}

	// Zooms by `factor` (< 1 in) keeping the time at `anchor` where it is on screen.
	zoomAt(anchor, factor, longest) {
		const span = Math.min(Math.max(this.span * factor, MIN_SPAN_NS), Math.max(longest || Infinity, MIN_SPAN_NS));
		const at = this.span > 0 ? (anchor - this.from) / this.span : 0.5;
		this.from = anchor - at * span;
		this.to = this.from + span;
		return this;
	}

	panBy(ns) {
		this.from += ns;
		this.to += ns;
		return this;
	}

	fit(from, to, margin = 0) {
		const pad = (to - from) * margin;
		this.from = from - pad;
		this.to = Math.max(to + pad, this.from + MIN_SPAN_NS);
		return this;
	}

	// Keeps the view inside [0, durationNs] where it can: a view longer than the capture shows all of it.
	clamp(durationNs) {
		const span = this.span;
		if (span >= durationNs) return this.fit(0, Math.max(durationNs, MIN_SPAN_NS));
		if (this.from < 0) this.panBy(-this.from);
		if (this.to > durationNs) this.panBy(durationNs - this.to);
		return this;
	}

	x(ns, width) {
		return (ns - this.from) / this.span * width;
	}

	ns(x, width) {
		return this.from + x / width * this.span;
	}
}

// --- The frame time graph's range --------------------------------------------------------------------------------

// The fewest frames the frame time graph zooms in to.
export const MIN_FRAMES = 4;

// The frames the frame time graph shows: [from, to) in frame numbers, fractional while zooming.
export class FrameSpan {
	constructor(from = 0, to = 1) {
		this.from = from;
		this.to = to;
	}

	get count() {
		return this.to - this.from;
	}

	// Zooms by `factor` (< 1 in) keeping the frame at `anchor` where it is on screen; never past `total` frames.
	zoomAt(anchor, factor, total) {
		const most = Math.max(total, 1);
		const count = Math.min(Math.max(this.count * factor, Math.min(MIN_FRAMES, most)), most);
		const at = this.count > 0 ? (anchor - this.from) / this.count : 0.5;
		this.from = anchor - at * count;
		this.to = this.from + count;
		return this.clamp(total);
	}

	panBy(frames, total) {
		this.from += frames;
		this.to += frames;
		return this.clamp(total);
	}

	// Keeps the span inside the capture's frames: one longer than them shows all of them.
	clamp(total) {
		const count = Math.min(this.count, Math.max(total, 1));
		this.from = Math.max(0, Math.min(this.from, total - count));
		this.to = this.from + count;
		return this;
	}

	// Whether it shows every one of `total` frames.
	whole(total) {
		return this.from <= 0 && this.to >= total;
	}

	x(frame, width) {
		return (frame - this.from) / this.count * width;
	}

	frame(x, width) {
		return this.from + x / width * this.count;
	}
}

// Whether a prof.frames answer still serves a span `width` pixels wide: it holds the span's frames, and its runs of
// frames per bar are about the size the pixels need.
export function barsServe(graph, span, width) {
	if (!graph || graph.first === undefined) return false;
	const first = Math.max(0, Math.floor(span.from));
	const last = Math.min(graph.total, Math.ceil(span.to)) - 1;
	if (first < graph.first || last > graph.last) return false;
	const need = Math.max(1, Math.ceil(span.count / Math.max(1, width)));
	return graph.perBar <= need * 1.6 && graph.perBar >= need / 1.6;
}

// What to ask prof.frames for a span `width` pixels wide: a span more on each side (so panning has bars before the
// answer comes), at the span's pixel size.
export function barsFetch(span, total, width) {
	const count = Math.max(1, span.count);
	const first = Math.max(0, Math.floor(span.from - count));
	const last = Math.max(first, Math.min(total, Math.ceil(span.to + count)) - 1);
	return { first, last, width: Math.max(1, Math.round(width * (last - first + 1) / count)) };
}

// Whether a fetched answer still serves the view: it covers it and its pixels are not much coarser or finer.
export function answerServes(answer, view, width) {
	if (!answer) return false;
	if (view.from < answer.from || view.to > answer.to) return false;
	const pixelNs = view.span / Math.max(1, width);
	const fetched = Math.max(1, answer.pixelNs);
	return pixelNs <= fetched * 1.6 && pixelNs >= fetched / 1.6;
}

// The range to fetch for a view: a screen's width more on each side, so panning shows zones before the answer comes.
export function fetchRange(view, durationNs) {
	const span = view.span;
	return { from: Math.max(0, view.from - span), to: Math.min(Math.max(durationNs, view.to), view.to + span) };
}

// The GPU queues' group lane: its id is what `collapsed` holds while the group is folded.
export const GPU_GROUP = "group:gpu";

// The lanes of an answer from top to bottom with their place: {kind, id, label, sub, y, height, rows, lockTop,
// lockRows, group, source}. The GPU queues come first, under their group lane (kind "group", folded while
// `collapsed` holds GPU_GROUP); then the threads or tasks; then the messages and the plots - each in the order the
// server sent them (docs/16 "Lane order": the queues by kind, the threads in the program's order). A thread's lock
// rows (`lockRowHeight` each) come under its zone rows, from `lockTop` down.
export function layoutLanes(answer, options) {
	const { rowHeight, pad, collapsed = new Set(), messageHeight, plotHeight, lockRowHeight = rowHeight } = options;
	const lanes = [];
	let y = 0;
	const add = (lane, rows, lockRows = 0) => {
		lane.rows = rows;
		lane.collapsed = collapsed.has(lane.id);
		lane.lockRows = lane.collapsed ? 0 : lockRows;
		lane.y = y;
		lane.lockTop = pad + Math.max(1, rows) * rowHeight;
		lane.height = lane.collapsed ? rowHeight + pad * 2 : Math.max(1, rows) * rowHeight + lane.lockRows * lockRowHeight + pad * 2;
		y += lane.height;
		lanes.push(lane);
	};
	const gpus = answer.gpu || [];
	if (gpus.length) {
		const folded = collapsed.has(GPU_GROUP);
		const sub = gpus.length + (gpus.length === 1 ? " queue" : " queues");
		lanes.push({ kind: "group", id: GPU_GROUP, label: "GPU", sub, collapsed: folded, y, height: rowHeight + pad * 2, rows: 1, lockRows: 0, source: gpus });
		y += rowHeight + pad * 2;
		if (!folded) for (const gpu of gpus) add({ kind: "gpu", id: gpu.id, label: gpu.label, sub: "", group: GPU_GROUP, source: gpu }, gpu.depth);
	}
	for (const lane of answer.lanes || []) {
		const sub = lane.kind === "task"
			? lane.runs + (lane.runs === 1 ? " run" : " runs") + " on " + lane.threads + (lane.threads === 1 ? " thread" : " threads")
			: lane.renamed ? "renamed " + lane.renamed + "x" : "";
		add({ kind: lane.kind, id: lane.id, label: lane.label, sub, source: lane }, lane.depth, (lane.locks || []).length);
	}
	if ((answer.messages || []).length || answer.messagesInRange) {
		lanes.push({ kind: "messages", id: "messages", label: "Messages", sub: "", y, height: messageHeight, rows: 1, source: answer.messages });
		y += messageHeight;
	}
	for (const plot of answer.plots || []) {
		lanes.push({ kind: "plot", id: "plot:" + plot.id, label: plot.name, sub: "", y, height: plotHeight, rows: 1, source: plot });
		y += plotHeight;
	}
	return { lanes, height: y };
}

export function laneAt(layout, y) {
	for (const lane of layout.lanes) if (y >= lane.y && y < lane.y + lane.height) return lane;
	return null;
}

// The zone under (x, y) of a lane: the entry of its zones list, or null. A zone narrower than `slop` pixels is hit
// within `slop` of its place.
export function zoneAt(lane, view, width, x, y, options) {
	if (!lane || lane.collapsed || !lane.source || !lane.source.zones) return null;
	const row = Math.floor((y - lane.y - options.pad) / options.rowHeight);
	if (row < 0) return null;
	const slop = options.slop || 2;
	let best = null;
	for (const zone of lane.source.zones) {
		if (zone[0] !== row) continue;
		const x0 = view.x(zone[1], width);
		const x1 = Math.max(view.x(zone[2], width), x0 + 1);
		if (x >= x0 - slop && x <= x1 + slop && (!best || x1 - x0 < view.x(best[2], width) - view.x(best[1], width))) best = zone;
	}
	return best;
}

// The lock hold under (x, y) of a lane: {row, lock, name, item} with `item` the row's [start, end, state, ref] (ref a
// hold's index, or minus the count of a merged run), or null.
export function lockAt(lane, view, width, x, y, options) {
	if (!lane || !lane.lockRows || !lane.source || !lane.source.locks) return null;
	const row = Math.floor((y - lane.y - lane.lockTop) / options.lockRowHeight);
	if (row < 0 || row >= lane.lockRows) return null;
	const lock = lane.source.locks[row];
	const slop = options.slop || 2;
	let best = null;
	for (const item of lock.items) {
		const x0 = view.x(item[0], width);
		const x1 = Math.max(view.x(item[1], width), x0 + 1);
		if (x >= x0 - slop && x <= x1 + slop && (!best || item[2] > best[2] || (item[2] === best[2] && x1 - x0 < view.x(best[1], width) - view.x(best[0], width)))) best = item;
	}
	return best ? { row, lock: lock.lock, name: lock.name, item: best } : null;
}

// The note under x on the notes row: the narrowest whose range (widened to `slop` pixels) holds it, or null.
export function noteAt(notes, view, width, x, slop = 4) {
	let best = null;
	for (const note of notes || []) {
		const x0 = view.x(note.fromNs, width);
		const x1 = view.x(note.toNs, width);
		if (x >= x0 - slop && x <= x1 + slop && (!best || note.toNs - note.fromNs < best.toNs - best.fromNs)) best = note;
	}
	return best;
}

// Text for one lock segment: "Queue - held while others waited, 3.20 ms".
export function lockLabel(name, item, format) {
	if (item[3] < 0) return name + ": " + (-item[3]) + " holds and waits";
	return name + ": " + LOCK_STATES[item[2]] + ", " + format(item[1] - item[0]);
}

// A thread's color slot (1..8) in the task view's strips and legend.
export function seriesOf(track) {
	return (track % 8) + 1;
}

// The frames whose bands the frame row draws, with whether each is over the budget or a spike (twice it).
export function frameBands(frames, budgetNs) {
	if (!frames || frames.dense) return [];
	return (frames.items || []).map(([index, start, end]) => {
		const length = end - start;
		return { index, start, end, length, over: length > budgetNs, spike: length > budgetNs * 2 };
	});
}

// --- The cursor and the guides -------------------------------------------------------------------------------------

// A plot's value at `t`: the bucket the server merged at or before it ([low, high]; equal when one sample), null before
// its first point. The same value the server's ValueAt gives, to the pixel the answer was fetched at.
export function plotValueAt(points, t) {
	if (!points || !points.length || t < points[0][0]) return null;
	let low = 0;
	let high = points.length - 1;
	while (low < high) {
		const mid = (low + high + 1) >> 1;
		if (points[mid][0] <= t) low = mid;
		else high = mid - 1;
	}
	return [points[low][1], points[low][2]];
}

// A plot value as its kind shows it: 0 a number, 1 bytes, 2 a percent; `format` is format.js's bytes().
export function plotText(kind, value, format) {
	if (kind === 1) return format(Math.round(value));
	const rounded = Math.abs(value) >= 100 ? Math.round(value).toString() : (Math.round(value * 100) / 100).toString();
	return kind === 2 ? rounded + "%" : rounded;
}

// What the cursor at `t` reads off every plot of the answer (scrolled out of view or not): [{label, text, kind,
// value}], a merged bucket as "low - high".
export function cursorReadout(layout, t, format) {
	const rows = [];
	for (const lane of (layout && layout.lanes) || []) {
		if (lane.kind !== "plot") continue;
		const value = plotValueAt(lane.source.points, t);
		if (!value) continue;
		const [low, high] = value;
		const text = low === high ? plotText(lane.source.kind, high, format) : plotText(lane.source.kind, low, format) + " – " + plotText(lane.source.kind, high, format);
		rows.push({ label: lane.label, text, kind: lane.source.kind, value: high, laneId: lane.id });
	}
	return rows;
}

// The frame `t` falls in, [index, start, end], or null (none, or the answer drew them dense).
export function frameAtTime(frames, t) {
	if (!frames || frames.dense) return null;
	return (frames.items || []).find(([, start, end]) => t >= start && t < end) || null;
}

// The span the guides mark across every lane: the selected zone's or lock hold's, else null.
export function guideSpan(selected, selectedLock) {
	if (selected && selected.zone) return { from: selected.zone[1], to: selected.zone[2] };
	if (selectedLock && selectedLock.item) return { from: selectedLock.item[0], to: selectedLock.item[1] };
	return null;
}
