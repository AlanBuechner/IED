// compare.js - a capture's Compare view (F-GPU-60, docs/22 "Compare"): the capture beside another of the workspace -
// two builds, two frames, D3D12 against Vulkan - in the capture's own replay session (gpu.compare opens the other
// there, and the session remembers it). The events aligned by marker path and call, with what only one has and what
// changed; each pass's GPU time in both; and for the selected pair the state that differs and the output as here,
// the other, or their difference.

import { h, icon, replace } from "/ui-kit/dom.js";
import { TextureViewer } from "./texview.js";
import { DOT, captureTime, count, gpuMs } from "./format.js";

const MARKS = { same: "", changed: "~", removed: "-", added: "+" };
const MODES = [["difference", "Difference"], ["here", "Here"], ["other", "Other"]];

function rowHeight() {
	return window.matchMedia("(max-width: 719px)").matches ? 36 : 21;
}

export class CompareView {
	// `session` is the capture's ReplaySession; onShow(eid) shows an event of this capture in the Pipeline view;
	// onTimed(answer) gets compareTiming's answer, which comes back through setTiming.
	constructor({ ied, wsId, captureId, session, onShow, onTimed }) {
		this.ied = ied;
		this.wsId = wsId;
		this.captureId = captureId;
		this.session = session;
		this.onShow = onShow;
		this.onTimed = onTimed;
		this.rows = [];
		this.times = new Map();       // EID here -> [ms here, ms other]
		this.mode = "difference";
		this.picker = h("select.input-field.gpu-cmp-pick", { title: "The capture to compare with", on: { change: () => this.choose(this.picker.value) } });
		this.counts = h("span.gpu-cmp-counts.gpu-wide");
		this.onlyChanges = h("input", { type: "checkbox", on: { change: () => this.renderRows() } });
		this.timingButton = h("button.btn.flat", { title: "Replay both frames with timestamps: each pass's GPU time in both", disabled: true, on: { click: () => this.collectTiming() } }, icon("watch"), h("span.gpu-cmp-time", null, "Compare Timing"));
		this.bar = h("div.gpu-bar", null, h("span.gpu-cmp-label.gpu-wide", null, "Compare with"), this.picker, this.counts,
			h("label.gpu-cmp-only.gpu-wide", null, this.onlyChanges, "Differences only"), h("span.grow"), this.timingButton);
		this.spacer = h("div.rows");
		this.list = h("div.viewport", { on: { scroll: () => this.renderWindow() } }, this.spacer);
		this.grid = h("div.gpu-grid.gpu-cmpgrid", null,
			h("div.gpu-head", null, h("span"), h("span.n", null, "EID"), h("span", null, "Event"), h("span.n", null, "Other"), h("span.n", null, "GPU ms"), h("span.n", null, "Other ms"), h("span.n", null, "Delta")),
			this.list);
		this.title = h("div.gpu-title", null, h("b", null, "Pick a capture to compare with"));
		this.stateList = h("div.viewport");
		this.stateGrid = h("div.gpu-grid.gpu-cmpstate", null, h("div.gpu-head", null, h("span", null, "State that differs"), h("span", null, "Here"), h("span", null, "Other")), this.stateList);
		this.modes = h("span.gpu-cmp-modes");
		this.stats = h("span.gpu-cmp-stats");
		this.viewer = new TextureViewer({ onPick: (x, y) => this.pick(x, y), onHistory: () => {}, onDebug: () => {} });
		this.viewer.historyButton.hidden = true;
		this.viewer.debugButton.hidden = true;
		this.mip = 0;
		this.viewer.onChange = (what, v) => {
			if (what === "mip") this.mip = v;
			this.showImage();
		};
		this.detail = h("div.gpu-col.grow.gpu-cmp-detail", null, this.title, this.stateGrid, h("div.gpu-title", null, h("b", null, "Output"), this.modes, this.stats), this.viewer.el);
		this.el = h("div.gpu-col.grow", null, this.bar, h("div.gpu-main.gpu-cmp", null, this.grid, this.detail));
		this.renderModes();
		this.message("Pick a capture to set beside this one: another build or frame of the program, or the same frame through the other API.");
		this.stop = this.ied.client.subscribe("gpu:" + wsId, {
			snap: (data) => this.setCaptures(data.captures),
			evt: (kind, data) => data && data.captures && this.setCaptures(data.captures),
		});
	}

	dispose() {
		this.stop();
	}

	// The view is shown: load what the session compares with, when it has not been.
	shown() {
		const other = this.session.compareWith || "";
		if (other && other !== this.loaded) this.load(other);
		this.viewer.draw();
	}

	// The session's state changed: another client (or the AI) may have picked another capture.
	sessionChanged(session) {
		const other = session.compareWith || "";
		this.picker.value = other;
		if (this.el.isConnected && other && other !== this.loaded && !this.choosing) this.load(other);
	}

	setCaptures(captures) {
		this.captures = (captures || []).filter((c) => c.id !== this.captureId);
		const label = (c) => {
			const meta = c.meta || {};
			return [(meta.program || c.id) + " f" + (meta.frame ?? "?"), meta.api, captureTime(meta.time), meta.comment].filter(Boolean).join(" " + DOT + " ");
		};
		replace(this.picker, h("option", { value: "" }, this.captures.length ? "(none)" : "(no other captures)"), this.captures.map((c) => h("option", { value: c.id }, label(c))));
		this.picker.value = this.session.compareWith || "";
	}

	message(text, error) {
		replace(this.stateList, h("div.gpu-state-msg" + (error ? ".error" : ""), null, text));
		this.viewer.message(text);
	}

	async choose(captureId) {
		this.choosing = true;
		try {
			if (!this.session.sessionId) await this.session.open();
			if (captureId) this.message("Opening the other capture beside this one...");
			await this.ied.client.request("gpu.compare", { sessionId: this.session.sessionId, captureId });
			this.session.compareWith = captureId;
			if (captureId) await this.load(captureId);
			else this.clear();
		}
		catch (e) {
			this.message(e.message || String(e), true);
		}
		finally {
			this.choosing = false;
		}
	}

	clear() {
		this.loaded = "";
		this.rows = [];
		this.times = new Map();
		this.timingButton.disabled = true;
		replace(this.counts);
		replace(this.title, h("b", null, "Pick a capture to compare with"));
		this.renderRows();
		this.message("Not comparing.");
	}

	async load(other) {
		this.loaded = other;
		this.picker.value = other;
		try {
			const { result } = await this.session.call("compareEvents");
			if (this.loaded !== other) return;
			this.sameApi = result.sameApi;
			this.rows = result.rows.map(([a, b, depth, status, kind, nameA, nameB]) => ({ a, b, depth, status, kind, nameA, nameB }));
			const c = result.counts;
			replace(this.counts, h("span", null, count(c.same) + " same"), DOT, h("span.gpu-cmp-changed", null, count(c.changed) + " changed"), DOT,
				h("span.gpu-cmp-removed", null, count(c.removed) + " only here"), DOT, h("span.gpu-cmp-added", null, count(c.added) + " only there"));
			this.timingButton.disabled = false;
			this.renderRows();
			const first = this.rows.find((r) => r.a && r.b && r.kind === "draw" && r.status !== "same") || [...this.rows].reverse().find((r) => r.a && r.b && r.kind === "draw");
			if (first) this.select(first);
		}
		catch (e) {
			this.message(e.message || String(e), true);
		}
	}

	shownRows() {
		return this.onlyChanges.checked ? this.rows.filter((r) => r.status !== "same") : this.rows;
	}

	renderRows() {
		this.visible = this.shownRows();
		this.spacer.style.height = this.visible.length * rowHeight() + "px";
		this.renderWindow();
	}

	// Only the rows in view are made: a frame can have many thousands of events.
	renderWindow() {
		const rowH = rowHeight();
		const first = Math.max(0, Math.floor(this.list.scrollTop / rowH) - 10);
		const last = Math.min(this.visible.length, first + Math.ceil((this.list.clientHeight || 600) / rowH) + 20);
		const els = [];
		for (let i = first; i < last; i++) els.push(this.rowEl(this.visible[i], i * rowH));
		replace(this.spacer, els);
	}

	rowEl(row, top) {
		const [here, there] = this.times.get(row.a || -row.b) || [];
		const delta = here !== undefined && there !== undefined && here !== null && there !== null ? there - here : null;
		const name = row.nameA || row.nameB;
		const title = row.status === "changed" ? row.nameA + "\n" + row.nameB : name;
		return h("div.gpu-r" + (row.kind === "marker" ? ".mk" : "") + (this.sel === row ? ".sel" : ""), { style: { top: top + "px" }, title,
			on: { click: () => this.select(row), dblclick: () => row.a && this.onShow(row.a) } },
			h("span.gpu-cmp-" + row.status, null, MARKS[row.status]), h("span.n.d", null, row.a ? String(row.a) : ""),
			h("span.nm", { style: { "--d": row.depth } }, name), h("span.n.d", null, row.b ? String(row.b) : ""),
			h("span.n", null, here !== undefined && here !== null ? gpuMs(here) : ""), h("span.n", null, there !== undefined && there !== null ? gpuMs(there) : ""),
			h("span.n" + (delta > 0 ? ".gpu-cmp-slower" : ""), { title: delta !== null && here ? (delta >= 0 ? "+" : "") + (100 * delta / here).toFixed(1) + "%" : "" }, delta !== null ? (delta >= 0 ? "+" : "") + gpuMs(delta) : ""));
	}

	// A marker compares at the last action inside it that both captures have.
	actionOf(row) {
		if (row.kind !== "marker") return row;
		const at = this.rows.indexOf(row);
		let found = null;
		for (let i = at + 1; i < this.rows.length && this.rows[i].depth > row.depth; i++)
			if (this.rows[i].kind !== "marker" && this.rows[i].a && this.rows[i].b) found = this.rows[i];
		return found;
	}

	async select(row) {
		this.sel = row;
		this.renderWindow();
		const action = row.a && row.b ? this.actionOf(row) : null;
		const name = row.nameA || row.nameB;
		replace(this.title, h("b", null, name), h("span.muted", null, row.a ? "EID " + row.a : "only in the other", DOT, row.b ? "other EID " + row.b : "only here"),
			row.a ? h("button.btn.flat", { title: "Show it in the Pipeline view (double click)", on: { click: () => this.onShow(row.a) } }, icon("symbol-structure"), "Pipeline") : null);
		this.image = null;
		replace(this.stats);
		if (!action) {
			this.message(!row.a || !row.b ? "The event is in one capture only." : "The marker holds no action both captures have.");
			return;
		}
		this.pair = { eid: action.a, otherEid: action.b };
		this.mip = 0;
		replace(this.stateList, h("div.gpu-state-msg", null, "Comparing the state..."));
		this.viewer.message("Comparing the output...");
		try {
			const { result } = await this.session.call("compareState", this.pair);
			if (this.sel !== row) return;
			const rows = result.differences.map(([path, a, b]) => h("div.gpu-r.info", { title: path + "\n" + a + "\n" + b }, h("span", null, path), h("span.m", null, a || "(none)"), h("span.m", null, b || "(none)")));
			replace(this.stateList, rows.length ? rows : h("div.gpu-state-msg", null, "The state is the same: " + count(result.compared) + " items compared."));
		}
		catch (e) {
			if (this.sel === row) replace(this.stateList, h("div.gpu-state-msg.error", null, e.message || String(e)));
		}
		this.showImage();
	}

	renderModes() {
		replace(this.modes, MODES.map(([id, label]) => h("button.gpu-cmp-mode" + (this.mode === id ? ".on" : ""), { on: { click: () => { this.mode = id; this.renderModes(); this.showImage(); } } }, label)));
	}

	// The output in the chosen mode: the difference (compareTexture), or either side's texture at its event.
	async showImage() {
		const pair = this.pair;
		if (!pair) return;
		const mip = this.mip;
		const view = this.viewer.view;
		const maxSize = Math.min(4096, Math.max(512, Math.round(Math.max(view.clientWidth, view.clientHeight) * (window.devicePixelRatio || 1) * 1.5)));
		try {
			if (!this.image || this.image.eid !== pair.eid || this.image.mip !== mip) {
				const { result, binary } = await this.session.call("compareTexture", Object.assign({ mip, maxSize }, pair));
				if (this.pair !== pair) return;
				this.image = Object.assign(result, { mip, binary });
				const d = this.image;
				const shown = d.step > 1 ? " at 1:" + d.step : "";
				replace(this.stats, d.changed ? count(d.changed) + " of " + count(d.pixels) + " px" + shown + " differ (" + (100 * d.changed / d.pixels).toFixed(2) + "%) " + DOT + " largest " + d.largest.toPrecision(3) +
					" " + DOT + " in (" + d.box[0] + ", " + d.box[1] + ")-(" + d.box[2] + ", " + d.box[3] + ")" + (d.nan ? " " + DOT + " " + count(d.nan) + " NaN/Inf on one side" : "") : "the same in both");
				this.stats.className = "gpu-cmp-stats" + (d.changed ? " gpu-cmp-changed" : "");
			}
			const d = this.image;
			if (this.mode === "difference") {
				this.viewer.show(d, d.binary);
				return;
			}
			const here = this.mode === "here";
			const { result, binary } = await this.session.call("texture", here ? { id: d.id, eid: pair.eid, mip, maxSize } : { side: "b", id: d.otherId, eid: pair.otherEid, mip, maxSize });
			if (this.pair !== pair) return;
			result.mip = mip;
			result.slice = 0;
			this.viewer.show(result, binary);
		}
		catch (e) {
			if (this.pair === pair) this.viewer.message(e.message || String(e));
		}
	}

	async pick(x, y) {
		const d = this.image;
		const pair = this.pair;
		if (!d || !pair) return;
		try {
			const [a, b] = await Promise.all([this.session.call("pixel", { id: d.id, eid: pair.eid, x, y, mip: d.mip }), this.session.call("pixel", { side: "b", id: d.otherId, eid: pair.otherEid, x, y, mip: d.mip })]);
			this.viewer.setPick("(" + x + ", " + y + ") here " + a.result.text + "  other " + b.result.text);
		}
		catch (e) {
			this.viewer.setPick(e.message || String(e));
		}
	}

	async collectTiming() {
		this.timingButton.disabled = true;
		this.busy = true;
		try {
			const { result } = await this.session.call("compareTiming", { repeat: this.repeat || 5 });
			this.onTimed(result);
		}
		catch (e) {
			this.ied.report("Compare timing failed", e);
			this.timingButton.lastChild.textContent = "Compare Timing";
		}
		finally {
			this.busy = false;
			this.timingButton.disabled = false;
		}
	}

	// Each pass's time in both captures (compareTiming's answer), or none.
	setTiming(result) {
		this.times = new Map();
		if (result) for (const [a, b, , msA, msB] of [...result.markers, ...result.events]) this.times.set(a || -b, [msA, msB]);
		this.timingButton.lastChild.textContent = result ? "GPU " + gpuMs(result.frameMs[0]) + " / " + gpuMs(result.frameMs[1]) + " ms" : "Compare Timing";
		this.renderRows();
	}

	progress(p) {
		if (p.what === "timing") this.timingButton.lastChild.textContent = "Timing " + Math.round(p.fraction * 100) + "%";
	}
}
