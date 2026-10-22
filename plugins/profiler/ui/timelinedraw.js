// timelinedraw.js - paints the timeline (mockup 06) on a canvas: the ruler, the frame row, the notes row, each lane's
// label and its zones by depth (runs narrower than a pixel merged by the server), a thread's lock rows (held, held
// while others waited, waited), rename tints, task slices with their threads' colors, GPU lanes, messages, plots,
// collections, notes' bands and the selection. Colors are the theme's tokens, read once per paint from the document;
// translucent ones are drawn with globalAlpha rather than computed colors.

import { duration, tickLabel, tickStep, bytes } from "./format.js";
import { frameBands, seriesOf, LOCK_BLOCKED } from "./model.js";

export const RULER_HEIGHT = 22;
export const FRAME_ROW_HEIGHT = 20;
export const HEADER_HEIGHT = RULER_HEIGHT + FRAME_ROW_HEIGHT;
export const NOTE_ROW_HEIGHT = 18;

// The header's height: the ruler, the frame row, and the notes row when there are notes.
export function headerHeight(notes) {
	return HEADER_HEIGHT + (notes && notes.length ? NOTE_ROW_HEIGHT : 0);
}

const SEVERITY_WARNING = 3;
const SEVERITY_ERROR = 4;
const STRIP_HEIGHT = 5;
const MIN_TEXT_PX = 26;
const MIN_RENAME_PX = 3;

// The theme's colors and fonts the painter needs, from the document's computed tokens.
export function readTheme() {
	const style = getComputedStyle(document.documentElement);
	const token = (name) => style.getPropertyValue("--" + name).trim();
	const px = (name, fallback) => parseFloat(token(name)) || fallback;
	return {
		bg1: token("bg-1"), bg2: token("bg-2"), bg3: token("bg-3"), bg4: token("bg-4"),
		text0: token("text-0"), text1: token("text-1"), text2: token("text-2"), text3: token("text-3"),
		border0: token("border-0"), border1: token("border-1"), focus: token("focus"),
		zones: [token("prof-zone-0"), token("prof-zone-1"), token("prof-zone-2"), token("prof-zone-3")],
		zoneBorder: token("prof-zone-border"), zoneText: token("text-0"),
		renameEdge: token("text-2"), gc: token("accent-warn"), warn: token("accent-warn"), error: token("accent-error"),
		frameOver: token("accent-warn"), frameSpike: token("accent-error"), info: token("accent-info"),
		series: [1, 2, 3, 4, 5, 6, 7, 8].map((n) => token("series-" + n)),
		selectionActive: token("selection-active"),
		lockHeld: token("text-3"), noteOwner: token("accent-info"), noteAi: token("accent-ai"),
		fontUi: token("font-ui"), fontMono: token("font-mono"),
		fontXs: px("font-size-xs", 11), fontSm: px("font-size-sm", 12),
	};
}

// Text clipped to `maxWidth` pixels with an ellipsis; widths measured once per text and font.
class TextFitter {
	constructor() {
		this.widths = new Map();
	}

	width(ctx, text) {
		const key = ctx.font + "\u0000" + text;
		let w = this.widths.get(key);
		if (w === undefined) {
			w = ctx.measureText(text).width;
			if (this.widths.size > 20000) this.widths.clear();
			this.widths.set(key, w);
		}
		return w;
	}

	fit(ctx, text, maxWidth) {
		if (maxWidth <= 0) return "";
		if (this.width(ctx, text) <= maxWidth) return text;
		let low = 0;
		let high = text.length;
		while (low < high) {
			const mid = (low + high + 1) >> 1;
			if (this.width(ctx, text.slice(0, mid) + "\u2026") <= maxWidth) low = mid;
			else high = mid - 1;
		}
		return low > 0 ? text.slice(0, low) + "\u2026" : "";
	}
}

export class TimelinePainter {
	constructor() {
		this.fitter = new TextFitter();
	}

	// One full paint. `state`: {ctx, width, height, labelWidth, view, answer, layout, scrollY, theme, options,
	// selected, range, hover, budgetNs, live}.
	paint(state) {
		const { ctx, width, height, theme } = state;
		state.header = headerHeight(state.notes);
		ctx.fillStyle = theme.bg2;
		ctx.fillRect(0, 0, width, height);
		if (!state.answer) return;
		ctx.save();
		ctx.beginPath();
		ctx.rect(0, state.header, width, height - state.header);
		ctx.clip();
		this.lanes(state);
		this.collections(state);
		this.noteBands(state);
		this.selection(state);
		ctx.restore();
		this.ruler(state);
		this.frames(state);
		this.noteRow(state);
		this.labelEdge(state);
	}

	trackWidth(state) {
		return state.width - state.labelWidth;
	}

	// The x of a time inside the track area.
	x(state, ns) {
		return state.labelWidth + state.view.x(ns, this.trackWidth(state));
	}

	ruler(state) {
		const { ctx, theme, view, width, labelWidth } = state;
		ctx.fillStyle = theme.bg1;
		ctx.fillRect(0, 0, width, RULER_HEIGHT);
		ctx.fillStyle = theme.border0;
		ctx.fillRect(0, RULER_HEIGHT - 1, width, 1);
		const step = tickStep(view.span, this.trackWidth(state), 80);
		ctx.font = theme.fontXs + "px " + theme.fontUi;
		ctx.textBaseline = "middle";
		for (let t = Math.ceil(view.from / step) * step; t < view.to; t += step) {
			const x = Math.round(this.x(state, t)) + 0.5;
			if (x < labelWidth) continue;
			ctx.fillStyle = theme.border1;
			ctx.fillRect(x, 0, 1, RULER_HEIGHT);
			ctx.fillStyle = theme.text2;
			ctx.fillText(tickLabel(t, step), x + 4, RULER_HEIGHT / 2);
		}
		ctx.fillStyle = theme.bg1;
		ctx.fillRect(0, 0, labelWidth, RULER_HEIGHT);
		ctx.fillStyle = theme.text2;
		ctx.fillText(duration(view.span) + " in view", 8, RULER_HEIGHT / 2);
	}

	frames(state) {
		const { ctx, theme, width, labelWidth } = state;
		const top = RULER_HEIGHT;
		ctx.fillStyle = theme.bg1;
		ctx.fillRect(0, top, width, FRAME_ROW_HEIGHT);
		const bands = frameBands(state.answer.frames, state.budgetNs);
		ctx.font = theme.fontXs + "px " + theme.fontUi;
		ctx.textBaseline = "middle";
		for (const band of bands) {
			const x0 = Math.max(labelWidth, this.x(state, band.start));
			const x1 = Math.min(width, this.x(state, band.end));
			if (x1 <= labelWidth || x1 - x0 < 1) continue;
			ctx.fillStyle = band.index % 2 ? theme.bg3 : theme.bg1;
			ctx.fillRect(x0, top + 2, x1 - x0, FRAME_ROW_HEIGHT - 4);
			ctx.fillStyle = theme.text3;
			ctx.fillRect(Math.round(x0), top + 2, 1, FRAME_ROW_HEIGHT - 4);
			if (band.over) {
				ctx.fillStyle = band.spike ? theme.frameSpike : theme.frameOver;
				ctx.fillRect(x0, top + FRAME_ROW_HEIGHT - 4, x1 - x0, 2);
			}
			const label = this.fitter.fit(ctx, "Frame " + band.index + " \u00b7 " + duration(band.length), x1 - x0 - 8);
			if (label) {
				ctx.fillStyle = band.spike ? theme.error : band.over ? theme.warn : theme.text1;
				ctx.fillText(label, x0 + 5, top + FRAME_ROW_HEIGHT / 2);
			}
		}
		ctx.fillStyle = theme.border0;
		ctx.fillRect(0, top + FRAME_ROW_HEIGHT - 1, width, 1);
		ctx.fillStyle = theme.bg1;
		ctx.fillRect(0, top, labelWidth, FRAME_ROW_HEIGHT - 1);
		ctx.fillStyle = theme.text2;
		ctx.fillText(state.answer.frames ? state.answer.frames.set + "s" : "Frames", 8, top + FRAME_ROW_HEIGHT / 2);
	}

	labelEdge(state) {
		state.ctx.fillStyle = state.theme.border0;
		state.ctx.fillRect(state.labelWidth - 1, 0, 1, state.height);
	}

	lanes(state) {
		const { ctx, theme, layout, scrollY, width, height, labelWidth } = state;
		for (const lane of layout.lanes) {
			const top = state.header + lane.y - scrollY;
			if (top > height || top + lane.height < state.header) continue;
			ctx.fillStyle = lane.kind === "gpu" ? theme.bg1 : theme.bg2;
			ctx.fillRect(labelWidth, top, width - labelWidth, lane.height);
			ctx.save();
			ctx.beginPath();
			ctx.rect(labelWidth, top, width - labelWidth, lane.height);
			ctx.clip();
			if (lane.kind === "thread") this.renames(state, lane, top);
			if (lane.kind === "task") this.slices(state, lane, top);
			if (!lane.collapsed && (lane.kind === "thread" || lane.kind === "task" || lane.kind === "gpu")) this.zones(state, lane, top);
			if (lane.lockRows) this.lockRows(state, lane, top);
			if (lane.kind === "messages") this.messages(state, lane, top);
			if (lane.kind === "plot") this.plot(state, lane, top);
			ctx.restore();
			this.label(state, lane, top);
			ctx.fillStyle = theme.border0;
			ctx.fillRect(0, top + lane.height - 1, width, 1);
		}
	}

	label(state, lane, top) {
		const { ctx, theme, labelWidth, options } = state;
		ctx.fillStyle = theme.bg1;
		ctx.fillRect(0, top, labelWidth - 1, lane.height);
		ctx.textBaseline = "middle";
		const line = top + options.pad + options.rowHeight / 2;
		let x = 6;
		if (lane.kind === "thread" || lane.kind === "task" || lane.kind === "gpu") {
			ctx.fillStyle = theme.text2;
			ctx.font = theme.fontXs + "px " + theme.fontUi;
			ctx.fillText(lane.collapsed ? "\u25b8" : "\u25be", x, line);
			x += 12;
		}
		if (lane.kind === "thread") {
			ctx.fillStyle = theme.series[seriesOf(lane.source.track) - 1];
			ctx.fillRect(x, line - 4, 8, 8);
			x += 13;
		}
		ctx.fillStyle = theme.text0;
		ctx.font = theme.fontSm + "px " + theme.fontUi;
		const prefix = lane.kind === "gpu" ? "GPU: " : "";
		ctx.fillText(this.fitter.fit(ctx, prefix + lane.label, labelWidth - x - 8), x, line);
		if (lane.sub && lane.height - lane.lockRows * options.lockRowHeight >= options.rowHeight * 2 && !state.phone) {
			ctx.fillStyle = theme.text2;
			ctx.font = theme.fontXs + "px " + theme.fontUi;
			ctx.fillText(this.fitter.fit(ctx, lane.sub, labelWidth - x - 8), x, line + options.rowHeight);
		}
		this.lockLabels(state, lane, top);
	}

	// Each lock row's name beside it, under the thread's label.
	lockLabels(state, lane, top) {
		if (!lane.lockRows) return;
		const { ctx, theme, labelWidth, options } = state;
		ctx.font = "10px " + theme.fontUi;
		ctx.textBaseline = "middle";
		lane.source.locks.forEach((lock, row) => {
			const y = top + lane.lockTop + row * options.lockRowHeight + options.lockRowHeight / 2;
			ctx.fillStyle = theme.lockHeld;
			ctx.fillRect(20, y - 1, 6, 2);
			ctx.fillStyle = theme.text2;
			ctx.fillText(this.fitter.fit(ctx, lock.name, labelWidth - 38), 30, y);
		});
	}

	// A thread's lock rows: what it held alone (gray), held while another thread waited (warning) and waited for
	// (error, a thinner bar); a run narrower than a pixel in its worst state.
	lockRows(state, lane, top) {
		const { ctx, theme, options } = state;
		const colors = [theme.lockHeld, theme.warn, theme.error];
		const barHeight = options.lockRowHeight - 4;
		lane.source.locks.forEach((lock, row) => {
			const y = top + lane.lockTop + row * options.lockRowHeight + 2;
			for (const item of lock.items) {
				const x0 = this.x(state, item[0]);
				if (x0 > state.width) continue;
				const x1 = Math.max(this.x(state, item[1]), x0 + 1);
				if (x1 < state.labelWidth) continue;
				const blocked = item[2] === LOCK_BLOCKED;
				ctx.fillStyle = colors[item[2]] || theme.lockHeld;
				ctx.fillRect(x0, y + (blocked ? barHeight / 4 : 0), x1 - x0, blocked ? barHeight / 2 : barHeight);
			}
			const selected = state.selectedLock;
			if (selected && selected.laneId === lane.id && selected.lock === lock.lock) {
				const x0 = this.x(state, selected.item[0]);
				const x1 = Math.max(this.x(state, selected.item[1]), x0 + 2);
				ctx.strokeStyle = theme.focus;
				ctx.lineWidth = 2;
				ctx.strokeRect(x0, y - 1, x1 - x0, barHeight + 2);
			}
		});
	}

	// The notes row under the frames: each note a tag over its range (a moment's at least a sliver), the owner's in
	// the info color, the AI's in its own; a label stops where the next note starts.
	noteRow(state) {
		const notes = state.notes || [];
		if (!notes.length) return;
		const { ctx, theme, width, labelWidth } = state;
		const top = HEADER_HEIGHT;
		ctx.fillStyle = theme.bg1;
		ctx.fillRect(0, top, width, NOTE_ROW_HEIGHT);
		ctx.font = theme.fontXs + "px " + theme.fontUi;
		ctx.textBaseline = "middle";
		const ordered = [...notes].sort((a, b) => a.fromNs - b.fromNs);
		ordered.forEach((note, i) => {
			const start = this.x(state, note.fromNs);
			const x0 = Math.max(labelWidth, start);
			const x1 = Math.min(width, Math.max(this.x(state, note.toNs), start + 2));
			if (x1 < labelWidth || x0 > width) return;
			const next = i + 1 < ordered.length ? Math.max(labelWidth, this.x(state, ordered[i + 1].fromNs)) : width;
			const color = note.by === "ai" ? theme.noteAi : theme.noteOwner;
			ctx.globalAlpha = state.selectedNote === note.id ? 0.5 : 0.28;
			ctx.fillStyle = color;
			ctx.fillRect(x0, top + 3, Math.max(2, x1 - x0), NOTE_ROW_HEIGHT - 6);
			ctx.globalAlpha = 1;
			ctx.fillRect(Math.round(x0), top + 2, 2, NOTE_ROW_HEIGHT - 4);
			const text = this.fitter.fit(ctx, note.text, Math.min(Math.max(x1 - x0, 140), next - x0, width - x0) - 8);
			if (text) {
				ctx.fillStyle = theme.text0;
				ctx.fillText(text, x0 + 5, top + NOTE_ROW_HEIGHT / 2);
			}
		});
		ctx.fillStyle = theme.border0;
		ctx.fillRect(0, top + NOTE_ROW_HEIGHT - 1, width, 1);
		ctx.fillStyle = theme.bg1;
		ctx.fillRect(0, top, labelWidth, NOTE_ROW_HEIGHT - 1);
		ctx.fillStyle = theme.text2;
		ctx.fillText("Notes", 8, top + NOTE_ROW_HEIGHT / 2);
	}

	// Each note's range faintly over the lanes, with its edges.
	noteBands(state) {
		const { ctx, theme, height } = state;
		for (const note of state.notes || []) {
			const x0 = Math.max(state.labelWidth, this.x(state, note.fromNs));
			const x1 = this.x(state, note.toNs);
			if (x1 < state.labelWidth || x0 > state.width) continue;
			ctx.fillStyle = note.by === "ai" ? theme.noteAi : theme.noteOwner;
			ctx.globalAlpha = state.selectedNote === note.id ? 0.12 : 0.05;
			if (x1 - x0 >= 1) ctx.fillRect(x0, state.header, x1 - x0, height - state.header);
			ctx.globalAlpha = 0.6;
			ctx.fillRect(Math.round(x0), state.header, 1, height - state.header);
			if (x1 - x0 >= 2) ctx.fillRect(Math.round(x1), state.header, 1, height - state.header);
			ctx.globalAlpha = 1;
		}
	}

	renames(state, lane, top) {
		const { ctx, theme } = state;
		const renames = lane.source.renames || [];
		for (let i = 0; i < renames.length; i++) {
			const [at, name] = renames[i];
			const x0 = this.x(state, at);
			const x1 = i + 1 < renames.length ? this.x(state, renames[i + 1][0]) : state.width;
			if (x1 - x0 < MIN_RENAME_PX) continue;
			ctx.globalAlpha = i % 2 ? 0.10 : 0.06;
			ctx.fillStyle = theme.text0;
			ctx.fillRect(x0, top, x1 - x0, lane.height);
			ctx.globalAlpha = 1;
			ctx.fillStyle = theme.renameEdge;
			ctx.fillRect(Math.round(x0), top, 1, lane.height);
			ctx.fillStyle = theme.text0;
			ctx.font = "10px " + theme.fontUi;
			ctx.textBaseline = "top";
			ctx.fillText(this.fitter.fit(ctx, name, x1 - x0 - 8), x0 + 4, top + 1);
		}
	}

	slices(state, lane, top) {
		const { ctx, theme, options } = state;
		const rowDepth = lane.source.rowDepth || 1;
		for (const [start, end, track, row] of lane.source.slices || []) {
			const x0 = this.x(state, start);
			const x1 = Math.max(this.x(state, end), x0 + 1);
			const y = top + options.pad + (row + 1) * rowDepth * options.rowHeight - STRIP_HEIGHT;
			ctx.fillStyle = theme.series[seriesOf(track) - 1];
			ctx.fillRect(x0, y, x1 - x0, STRIP_HEIGHT - 1);
		}
	}

	zones(state, lane, top) {
		const { ctx, theme, options, answer } = state;
		const names = answer.names || [];
		const rowHeight = options.rowHeight;
		const zoneHeight = rowHeight - 1;
		ctx.font = theme.fontXs + "px " + theme.fontUi;
		ctx.textBaseline = "middle";
		for (const zone of lane.source.zones || []) {
			const [depth, start, end, nameIndex] = zone;
			const x0 = this.x(state, start);
			if (x0 > state.width) continue;
			const x1 = Math.max(this.x(state, end), x0 + 1);
			if (x1 < state.labelWidth) continue;
			const y = top + options.pad + depth * rowHeight;
			const w = x1 - x0;
			const merged = nameIndex < 0;
			ctx.fillStyle = merged ? theme.zones[3] : theme.zones[depth % 4];
			ctx.fillRect(x0, y, w, zoneHeight);
			if (w >= 3) {
				ctx.strokeStyle = merged ? theme.border1 : theme.zoneBorder;
				ctx.lineWidth = 1;
				ctx.strokeRect(Math.round(x0) + 0.5, y + 0.5, Math.max(0, Math.round(w) - 1), zoneHeight - 1);
			}
			if (w < MIN_TEXT_PX) continue;
			const visible0 = Math.max(x0, state.labelWidth) + 4;
			const room = Math.min(x1, state.width) - visible0 - 4;
			const name = merged ? zone[4] + " zones" : names[nameIndex] || "?";
			const text = this.fitter.fit(ctx, name, room);
			if (!text) continue;
			ctx.fillStyle = merged ? theme.text2 : theme.zoneText;
			ctx.fillText(text, visible0, y + zoneHeight / 2);
			const used = this.fitter.width(ctx, text);
			if (text === name && !merged && room - used > 40) {
				ctx.fillStyle = theme.text2;
				ctx.fillText(this.fitter.fit(ctx, duration(end - start), room - used - 6), visible0 + used + 6, y + zoneHeight / 2);
			}
		}
		const selected = state.selected;
		if (selected && selected.laneId === lane.id) {
			const [depth, start, end] = selected.zone;
			const x0 = this.x(state, start);
			const x1 = Math.max(this.x(state, end), x0 + 2);
			ctx.strokeStyle = theme.focus;
			ctx.lineWidth = 2;
			ctx.strokeRect(x0, top + options.pad + depth * rowHeight, x1 - x0, zoneHeight);
		}
	}

	messages(state, lane, top) {
		const { ctx, theme } = state;
		ctx.font = theme.fontXs + "px " + theme.fontUi;
		ctx.textBaseline = "middle";
		const list = lane.source || [];
		for (let i = 0; i < list.length; i++) {
			const [at, severity, text] = list[i];
			const x = this.x(state, at);
			const next = i + 1 < list.length ? this.x(state, list[i + 1][0]) : state.width;
			const color = severity >= SEVERITY_ERROR ? theme.error : severity >= SEVERITY_WARNING ? theme.warn : theme.text2;
			ctx.fillStyle = color;
			ctx.fillRect(Math.round(x), top + 4, 2, lane.height - 8);
			ctx.fillStyle = severity >= SEVERITY_WARNING ? color : theme.text1;
			ctx.fillText(this.fitter.fit(ctx, text, next - x - 10), x + 6, top + lane.height / 2);
		}
	}

	plot(state, lane, top) {
		const { ctx, theme } = state;
		const points = lane.source.points || [];
		if (!points.length) return;
		let low = Infinity;
		let high = -Infinity;
		for (const [, a, b] of points) {
			if (a < low) low = a;
			if (b > high) high = b;
		}
		if (high === low) {
			high += 1;
			low -= 1;
		}
		const y = (v) => top + lane.height - 4 - (v - low) / (high - low) * (lane.height - 10);
		ctx.strokeStyle = theme.text1;
		ctx.lineWidth = 1.2;
		ctx.beginPath();
		points.forEach(([at, a, b], i) => {
			const x = this.x(state, at);
			if (i === 0) ctx.moveTo(x, y(b));
			else ctx.lineTo(x, y(b));
			if (a !== b) ctx.lineTo(x, y(a));
		});
		ctx.stroke();
		const last = points[points.length - 1];
		const value = lane.source.kind === 1 ? bytes(Math.round(last[2])) : (Math.round(last[2] * 100) / 100).toString();
		ctx.font = theme.fontXs + "px " + theme.fontUi;
		ctx.textBaseline = "top";
		ctx.fillStyle = theme.text2;
		const text = lane.label + " " + value;
		ctx.fillText(text, state.width - this.fitter.width(ctx, text) - 8, top + 2);
	}

	collections(state) {
		const { ctx, theme, height } = state;
		ctx.font = "10px " + theme.fontUi;
		ctx.textBaseline = "bottom";
		for (const [start, end] of state.answer.gc || []) {
			const x0 = Math.max(state.labelWidth, this.x(state, start));
			const x1 = Math.max(this.x(state, end), x0 + 1);
			if (x1 < state.labelWidth || x0 > state.width) continue;
			ctx.globalAlpha = 0.09;
			ctx.fillStyle = theme.gc;
			ctx.fillRect(x0, state.header, x1 - x0, height - state.header);
			ctx.globalAlpha = 1;
			ctx.fillRect(x0, state.header, 1, height - state.header);
			if (x1 - x0 > 1) ctx.fillRect(x1 - 1, state.header, 1, height - state.header);
			if (x1 - x0 > 40) ctx.fillText("GC " + duration(end - start), x0 + 3, height - 2);
		}
	}

	selection(state) {
		const range = state.range;
		if (!range) return;
		const { ctx, theme, height } = state;
		const x0 = Math.max(state.labelWidth, this.x(state, Math.min(range.from, range.to)));
		const x1 = this.x(state, Math.max(range.from, range.to));
		const top = state.header;
		ctx.globalAlpha = 0.10;
		ctx.fillStyle = theme.text0;
		ctx.fillRect(x0, top, x1 - x0, height - top);
		ctx.globalAlpha = 1;
		ctx.fillStyle = theme.focus;
		ctx.fillRect(x0, top, 1, height - top);
		ctx.fillRect(x1, top, 1, height - top);
		ctx.font = theme.fontXs + "px " + theme.fontUi;
		ctx.textBaseline = "top";
		const text = duration(Math.abs(range.to - range.from));
		ctx.fillStyle = theme.bg4;
		const w = this.fitter.width(ctx, text) + 8;
		ctx.fillRect((x0 + x1) / 2 - w / 2, top + 2, w, 16);
		ctx.fillStyle = theme.text0;
		ctx.fillText(text, (x0 + x1) / 2 - w / 2 + 4, top + 4);
	}
}
