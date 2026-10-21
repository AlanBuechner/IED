// capture.js - a capture as a document (F-GPU-14), PIX's layout: a bar with Collect Timing Data and the filter, the
// view tabs (Overview, Pipeline, Performance), the timing timeline, then per view - Pipeline: the events grid, the
// state tree beside the resource viewer (or a shader, or a draw's vertex inputs), and the docked API Calls, Messages,
// Resources and Warnings grids. Everything comes from the capture's replay session on the server.

import { h, icon, replace } from "/ui-kit/dom.js";
import { EventsGrid } from "./events.js";
import { Timeline } from "./timeline.js";
import { StateTree } from "./statetree.js";
import { TextureViewer } from "./texview.js";
import { ShaderPanel, MeshPanel } from "./panels.js";
import { ReplaySession } from "./session.js";
import { DOT, bytes, captureTime, count, gpuMs } from "./format.js";

const VIEWS = [["overview", "Overview", "info"], ["pipeline", "Pipeline", "symbol-structure"], ["performance", "Performance", "dashboard"]];
const BOTTOM = [["calls", "API Calls"], ["messages", "Messages"], ["resources", "Resources"], ["warnings", "Warnings"]];
const PANES = [["output", "Output"], ["state", "State"], ["calls", "Calls"]];

export class CaptureView {
	constructor(ctx, captureId) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.captureId = captureId;
		this.view = "pipeline";
		this.bottomTab = "calls";
		this.generation = 0;
		this.resource = null;      // the resource the viewer shows: {id, name, pinned}
		this.eid = 0;
		this.outputEid = 0;   // the action the selection shows: itself, or a marker's last action
		this.session = new ReplaySession(this.ied, ctx.wsId, captureId, {
			onState: (s) => this.sessionChanged(s),
			onProgress: (p) => this.progress(p),
		});
		this.timingRepeat = 5;
		this.stopSettings = this.ied.client.subscribe("settings:" + ctx.wsId, {
			snap: (data) => this.settings(data.values || {}),
			evt: (kind, data) => kind === "settings.changed" && data.values && this.settings(data.values),
		});
		this.build();
		this.start();
	}

	settings(values) {
		const repeat = values["gpu.timingRepeat"];
		this.timingRepeat = repeat && repeat.value ? Math.max(1, Math.min(25, repeat.value)) : 5;
	}

	build() {
		this.events = new EventsGrid({ onSelect: (eid) => this.select(eid) });
		this.timeline = new Timeline({ onSelect: (eid) => this.select(eid) });
		this.tree = new StateTree({ onResource: (id, item) => this.pinResource(id, item), onShader: (stage) => this.showShader(stage) });
		this.viewer = new TextureViewer({ onPick: (x, y) => this.pick(x, y) });
		this.viewer.onChange = (what, v) => this.reloadTexture({ [what]: v });
		this.shader = new ShaderPanel({ onClose: () => this.showPanel(this.viewer.el) });
		this.mesh = new MeshPanel({ onClose: () => this.showPanel(this.viewer.el) });
		this.panel = h("div.gpu-col.grow.gpu-rv-host", null, this.viewer.el);
		this.timingButton = h("button.btn.flat", { title: "Replay the frame several times with timestamps around every action", on: { click: () => this.collectTiming() } }, icon("watch"), "Collect Timing Data");
		this.timingChip = h("span.chip.gpu-wide", { hidden: true });
		this.filter = h("input.input-field.gpu-filter.gpu-wide", { placeholder: "Filter events: kind:draw Cube >0.01ms", on: { input: () => this.events.setFilter(this.filter.value) } });
		this.backend = h("span.gpu-backend.gpu-wide");
		this.meshButton = h("button.btn.flat.gpu-wide", { title: "The selected draw's vertex inputs", on: { click: () => this.showMesh() } }, icon("symbol-structure"), "Mesh");
		this.bar = h("div.gpu-bar", null, this.timingButton, this.timingChip, this.filter, this.meshButton, h("span.grow"), this.backend);
		this.vtabs = h("div.gpu-vtabs");
		this.calls = h("div.gpu-calls");
		this.bottomBody = h("div.gpu-bbody");
		this.bottomTabs = h("div.gpu-btabs");
		this.bottom = h("div.gpu-bottom", null, this.bottomBody, this.bottomTabs);
		this.panes = h("div.gpu-panes");
		this.body = h("div.gpu-col.grow");
		this.ctx.el.classList.add("gpu-doc");
		this.ctx.el.dataset.pane = "output";
		replace(this.ctx.el, this.bar, this.vtabs, this.timeline.el, this.body);
		this.renderTabs();
		this.renderView();
	}

	renderTabs() {
		const tail = h("span.tail", null, this.replayText || "");
		replace(this.vtabs, VIEWS.map(([id, label, glyph]) => h("button.gpu-vtab" + (this.view === id ? ".on" : ""), { on: { click: () => this.setView(id) } }, icon(glyph), label)), tail);
		replace(this.bottomTabs, BOTTOM.map(([id, label]) => h("button.gpu-btab" + (this.bottomTab === id ? ".on" : ""), { on: { click: () => this.setBottom(id) } }, label + this.bottomCount(id))));
		const pane = this.ctx.el.dataset.pane;
		replace(this.panes, PANES.map(([id, label]) => h("button" + (pane === id ? ".on" : ""), { on: { click: () => this.setPane(id) } }, label)));
	}

	// At phone width one of the output, the state tree and the grids shows above the events.
	setPane(id) {
		this.ctx.el.dataset.pane = id;
		this.renderTabs();
		if (id === "output") this.viewer.draw();
	}

	bottomCount(id) {
		if (id === "warnings") {
			const n = this.warnings().length;
			return n ? " " + n : "";
		}
		if (id === "messages" && this.messages) return this.messages.length ? " " + this.messages.length : "";
		return "";
	}

	setView(id) {
		this.view = id;
		this.renderTabs();
		this.renderView();
	}

	setBottom(id) {
		this.bottomTab = id;
		this.renderTabs();
		this.renderBottom();
	}

	renderView() {
		if (this.view === "pipeline") {
			replace(this.body, h("div.gpu-main.pipeline", null, this.events.el, h("div.gpu-col.grow", null, this.panes, h("div.gpu-main", null, this.tree.el, this.panel), this.bottom)));
			this.renderBottom();
			this.viewer.draw();
		}
		else if (this.view === "overview") replace(this.body, this.overview());
		else replace(this.body, this.performance());
	}

	showPanel(el) {
		replace(this.panel, el);
		if (el === this.viewer.el) this.viewer.draw();
	}

	// --- Opening ---------------------------------------------------------------------------------------------------

	async start() {
		this.tree.message("Opening the capture...");
		this.viewer.message("Starting the replay...");
		try {
			const summary = await this.session.open();
			this.loaded(summary);
		}
		catch (e) {
			this.failed("The capture could not be opened: " + (e.message || e));
		}
	}

	failed(text) {
		this.tree.message(text, true);
		this.viewer.message(text);
		replace(this.backend, h("button.btn.flat", { on: { click: () => this.start() } }, icon("refresh"), "Reopen"));
	}

	sessionChanged(session) {
		if (session.state === "failed") this.failed("The replay stopped: " + (session.problem || "the replayer exited") + ". Reopen starts it again.");
	}

	progress(p) {
		if (p.what === "timing") this.timingButton.lastChild.textContent = "Timing " + Math.round(p.fraction * 100) + "%";
	}

	async loaded(summary) {
		this.summary = summary;
		const meta = summary.meta || {};
		this.ctx.tab.setTitle((meta.program || this.captureId) + " f" + (meta.frame ?? ""));
		const replay = summary.replayDevice || {};
		this.replayText = replay.adapter ? "replay on " + replay.adapter + " " + DOT + " " + summary.api : replay.problem || "";
		replace(this.backend, "Flashbang " + DOT + " " + summary.api + " " + DOT + " " + (meta.width || "?") + "x" + (meta.height || "?"));
		this.renderTabs();
		const { result } = await this.session.call("events");
		this.queues = result.queues;
		this.events.load(result);
		this.timeline.load(this.events, result.queues);
		this.renderBottom();
		// Start where PIX does: the frame's last draw, so the viewer shows the finished image.
		const lastDraw = [...this.events.events].reverse().find((e) => e.kind === "draw") || this.events.events[this.events.events.length - 1];
		if (lastDraw) this.events.select(lastDraw.eid);
		if (this.view !== "pipeline") this.renderView();
	}

	// --- Selection -----------------------------------------------------------------------------------------------

	async select(eid) {
		const generation = ++this.generation;
		this.eid = eid;
		this.events.select(eid, true);
		this.timeline.select(eid);
		// A marker shows its last action's state and output.
		const action = this.events.actionAt(eid);
		const stateEid = action ? action.eid : eid;
		this.outputEid = stateEid;
		this.meshButton.disabled = !(action && action.kind === "draw");
		this.renderCalls(null);
		try {
			const [state, calls] = await Promise.all([this.session.call("state", { eid: stateEid }), this.session.call("apiCalls", { eid })]);
			if (generation !== this.generation) return;
			this.state = state.result;
			this.tree.show(this.state);
			this.renderCalls(calls.result.calls);
			const output = StateTree.outputOf(this.state);
			if (!this.resource || !this.resource.pinned) this.resource = output ? { id: output.resource, name: output.value, pinned: false } : this.resource;
			if (this.panel.firstChild !== this.viewer.el) this.showPanel(this.viewer.el);
			if (this.resource) this.reloadTexture();
			else this.viewer.message("This event writes no texture.");
		}
		catch (e) {
			if (generation === this.generation) this.tree.message(e.message || String(e), true);
		}
	}

	pinResource(id, item) {
		this.resource = { id, name: item.value, pinned: true };
		this.showPanel(this.viewer.el);
		this.reloadTexture({ mip: 0, slice: 0 });
		if (this.bottomTab === "resources" && this.view === "pipeline") this.renderResources();
	}

	async reloadTexture(change) {
		if (!this.resource) return;
		this.textureParams = Object.assign({ mip: 0, slice: 0 }, this.textureParams && this.textureParams.id === this.resource.id ? this.textureParams : {}, change || {}, { id: this.resource.id });
		const generation = this.generation;
		const view = this.viewer.view;
		const maxSize = Math.min(4096, Math.max(512, Math.round(Math.max(view.clientWidth, view.clientHeight) * (window.devicePixelRatio || 1) * 1.5)));
		try {
			const { result, binary } = await this.session.call("texture", { id: this.resource.id, eid: this.outputEid, mip: this.textureParams.mip, slice: this.textureParams.slice, maxSize });
			if (generation !== this.generation) return;
			result.mip = this.textureParams.mip;
			result.slice = this.textureParams.slice;
			this.viewer.show(result, binary);
		}
		catch (e) {
			if (generation === this.generation) this.viewer.message(e.message || String(e));
		}
	}

	async pick(x, y) {
		if (!this.resource) return;
		try {
			const { result } = await this.session.call("pixel", { id: this.resource.id, eid: this.outputEid, x, y, mip: this.textureParams.mip, slice: this.textureParams.slice });
			this.viewer.setPick("(" + x + ", " + y + ") " + result.text + (result.raw ? "  0x" + result.raw : "") + "  " + result.format);
		}
		catch (e) {
			this.viewer.setPick(e.message || String(e));
		}
	}

	async showShader(stage) {
		this.showPanel(this.shader.el);
		this.shader.message("Reading the shader...");
		try {
			const pipeline = this.state ? this.state.pipelineId : 0;
			const { result } = await this.session.call("shader", { stage, pipeline, eid: this.outputEid });
			this.shader.show(result);
		}
		catch (e) {
			this.shader.message(e.message || String(e));
		}
	}

	async showMesh() {
		const action = this.events.actionAt(this.eid);
		if (!action || action.kind !== "draw") return;
		if (this.view !== "pipeline") this.setView("pipeline");
		this.showPanel(this.mesh.el);
		this.mesh.message("Reading the vertex inputs...");
		try {
			const { result, binary } = await this.session.call("mesh", { eid: action.eid });
			this.mesh.show(result, binary);
		}
		catch (e) {
			this.mesh.message(e.message || String(e));
		}
	}

	// --- Timing ----------------------------------------------------------------------------------------------------

	async collectTiming() {
		this.timingButton.disabled = true;
		try {
			const { result } = await this.session.call("timing", { repeat: this.timingRepeat });
			this.timing = result;
			this.events.setTiming(result);
			this.timeline.setTiming(result);
			this.timingChip.hidden = false;
			replace(this.timingChip, icon("check", "sm"), "timing: " + result.repeat + " replays " + DOT + " " + gpuMs(result.frameMs) + " ms");
			if (this.view === "performance") this.renderView();
		}
		catch (e) {
			this.ied.report("Timing failed", e);
		}
		finally {
			this.timingButton.disabled = false;
			this.timingButton.lastChild.textContent = "Collect Timing Data";
		}
	}

	// --- Bottom grids ------------------------------------------------------------------------------------------------

	renderBottom() {
		if (this.bottomTab === "calls") replace(this.bottomBody, this.calls);
		else if (this.bottomTab === "messages") this.renderMessages();
		else if (this.bottomTab === "resources") this.renderResources();
		else this.renderWarnings();
	}

	renderCalls(calls) {
		if (!calls) {
			replace(this.calls);
			return;
		}
		replace(this.calls, calls.map((call) => h("div.gpu-call", null,
			h("div", null, h("span.eid", null, String(call.eid)), h("span.fn", null, call.name)),
			h("div.args", null, this.argsText(call.args)))));
	}

	// A call's arguments as name: value lines, objects by name (a click shows a resource).
	argsText(args) {
		const out = [];
		const value = (v, indent) => {
			if (v === null) return [h("span.num", null, "null")];
			if (typeof v === "number" || typeof v === "boolean") return [h("span.num", null, String(v))];
			if (typeof v === "string") return [h("span.str", null, v)];
			if (Array.isArray(v)) {
				if (v.every((x) => typeof x !== "object" || x === null)) return [h("span.num", null, "[" + v.join(", ") + "]")];
				return v.flatMap((x, i) => ["\n" + indent + "[" + i + "] ", ...value(x, indent + "  ")]);
			}
			if (v.$ref !== undefined) return [h("span.ref", { on: { click: () => this.pinResource(v.$ref, { value: v.name }) } }, (v.name || "#" + v.$ref) + (v.offset !== undefined ? " +" + v.offset : ""))];
			if (v.$heap !== undefined) return [h("span.str", null, (v.name || "heap #" + v.$heap) + "[" + v.index + "]")];
			return Object.entries(v).flatMap(([k, x]) => ["\n" + indent, h("span.key", null, k), ": ", ...value(x, indent + "  ")]);
		};
		for (const [key, v] of Object.entries(args || {})) out.push(h("span.key", null, key), ": ", ...value(v, "  "), "\n");
		return out;
	}

	async renderMessages() {
		const grid = h("div.gpu-grid.gpu-wgrid", null, h("div.gpu-head", null, h("span"), h("span.n", null, "EID"), h("span", null, "Message"), h("span", null, "Severity")), h("div.viewport", null, h("div.gpu-state-msg", null, "Reading...")));
		replace(this.bottomBody, grid);
		try {
			const { result } = await this.session.call("messages");
			this.messages = result.messages;
			const rows = this.messages.map((m) => h("div.gpu-r", { on: { click: () => this.select(m.eid) } },
				h("span", null, icon(m.severity === "error" || m.severity === "corruption" ? "error" : m.severity === "warning" ? "warning" : "info", m.severity === "error" ? "c-error" : m.severity === "warning" ? "c-warn" : "c-info")),
				h("span.n.d", null, String(m.eid)), h("span", { title: m.text }, m.text), h("span.d", null, m.severity)));
			replace(grid.lastChild, rows.length ? rows : h("div.gpu-state-msg", null, "No debug layer messages. Turn on Settings > GPU Debugger > Replay with the debug layer to get them."));
			this.renderTabs();
		}
		catch (e) {
			replace(grid.lastChild, h("div.gpu-state-msg.error", null, e.message || String(e)));
		}
	}

	async renderResources() {
		const grid = h("div.gpu-grid.gpu-resgrid", null,
			h("div.gpu-head", null, h("span.n", null, "First"), h("span", null, "Name"), h("span", null, "Format"), h("span", null, "Size"), h("span", null, "Heap"), h("span.n", null, "Uses"), h("span.n", null, "Initial")),
			h("div.viewport", null, h("div.gpu-state-msg", null, "Reading...")));
		replace(this.bottomBody, grid);
		try {
			if (!this.resources) this.resources = (await this.session.call("resources")).result.resources;
			const rows = this.resources.map((r) => h("div.gpu-r" + (this.resource && this.resource.id === r.id ? ".sel" : ""), { title: r.flags, on: { click: () => this.pinResource(r.id, { value: r.name }) } },
				h("span.n.d", null, r.firstUse ? String(r.firstUse) : ""), h("span", null, r.name),
				h("span.m", null, r.dimension === "BUFFER" ? "buffer" : r.format),
				h("span.m", null, r.dimension === "BUFFER" ? bytes(r.width) : r.width + "x" + r.height + (r.depthOrArraySize > 1 ? "x" + r.depthOrArraySize : "") + (r.mipLevels > 1 ? " " + r.mipLevels + " mips" : "")),
				h("span.d", null, r.heap), h("span.n", null, r.uses ? String(r.uses) : ""), h("span.n.d", null, r.initialBytes ? bytes(r.initialBytes) : "")));
			replace(grid.lastChild, rows);
		}
		catch (e) {
			replace(grid.lastChild, h("div.gpu-state-msg.error", null, e.message || String(e)));
		}
	}

	warnings() {
		const summary = this.summary || {};
		const meta = summary.meta || {};
		return [...new Set([...(meta.warnings || []), ...(summary.warnings || [])])];
	}

	renderWarnings() {
		const list = this.warnings();
		replace(this.bottomBody, h("div.gpu-grid.gpu-wgrid", null,
			h("div.gpu-head", null, h("span"), h("span.n", null, "EID"), h("span", null, "Warning"), h("span", null, "Source")),
			h("div.viewport", null, list.length ? list.map((w) => h("div.gpu-r", null, h("span", null, icon("warning", "c-warn")), h("span"), h("span", { title: w }, w), h("span.d", null, "capture"))) : h("div.gpu-state-msg", null, "No warnings: everything the frame did was captured."))));
	}

	// --- Overview and Performance ----------------------------------------------------------------------------------

	overview() {
		const summary = this.summary || {};
		const meta = summary.meta || {};
		const capture = summary.captureDevice || {};
		const replay = summary.replayDevice || {};
		const counts = summary.counts || {};
		const facts = [
			["Program", meta.program], ["Path", meta.programPath], ["API", summary.api], ["Frame", meta.frame], ["Captured", captureTime(meta.time)],
			["Size", bytes(summary.bytes)], ["Resolution", meta.width ? meta.width + " x " + meta.height : ""],
			["Captured on", [capture.adapter, capture.driver && "driver " + capture.driver, capture.featureLevel && "FL " + capture.featureLevel].filter(Boolean).join(" " + DOT + " ")],
			["Replayed on", replay.adapter ? replay.adapter + " " + DOT + " driver " + replay.driver : replay.problem],
			["Events", counts.events !== undefined ? count(counts.events) + " (" + count(counts.actions) + " actions, " + counts.submissions + " submissions)" : ""],
			["Objects", counts.objects !== undefined ? counts.objects + " (" + counts.resources + " resources)" : ""],
			["Comment", meta.comment],
		].filter(([, v]) => v !== undefined && v !== null && v !== "");
		const thumbnail = "/w/" + encodeURIComponent(this.ctx.wsId) + "/files/.ied/local/gpu-captures/" + encodeURIComponent(this.captureId) + ".png";
		const warnings = this.warnings();
		return h("div.gpu-overview", null,
			h("img", { src: thumbnail, alt: "The presented frame", on: { error: (e) => e.target.remove() } }),
			h("div.gpu-facts", null, facts.flatMap(([k, v]) => [h("span.k", null, k), h("span.v", null, String(v))]),
				warnings.length ? [h("span.k", null, "Warnings"), h("span.v.warn", null, warnings.join("; "))] : null));
	}

	performance() {
		if (!this.timing) return h("div.gpu-state-msg", null, h("p", null, "No timing data yet."), h("button.btn", { on: { click: () => this.collectTiming() } }, icon("watch"), "Collect Timing Data"));
		const rows = [];
		const markerTime = new Map(this.timing.markers.map((m) => [m[0], m[1]]));
		const walk = (list, depth) => {
			for (const e of list) {
				if (e.kind !== "marker" || e.hidden) continue;
				const draws = this.events.events.filter((x) => x.eid > e.eid && x.eid <= e.last && x.kind === "draw").length;
				const stats = this.events.stats && this.events.stats.get(e.eid);
				rows.push(h("div.gpu-r", { on: { click: () => { this.setView("pipeline"); this.select(e.eid); } } },
					h("span.nm", { style: { "--d": depth } }, e.name), h("span.n", null, gpuMs(markerTime.get(e.eid))), h("span.n", null, String(draws)),
					h("span.n", null, stats ? count(stats.prims) : ""), h("span.n", null, stats ? count(stats.ps) : ""), h("span.n.d", null, String(e.eid))));
				walk(e.children, depth + 1);
			}
		};
		walk(this.events.roots || [], 0);
		return h("div.gpu-col.grow", null,
			h("div.gpu-title", null, h("b", null, "Passes"), h("span.muted", null, "GPU " + gpuMs(this.timing.frameMs) + " ms " + DOT + " median of " + this.timing.repeat + " replays")),
			h("div.gpu-grid.gpu-pgrid", null,
				h("div.gpu-head", null, h("span", null, "Pass"), h("span.n", null, "GPU ms"), h("span.n", null, "Draws"), h("span.n", null, "Prims"), h("span.n", null, "PS inv."), h("span.n", null, "EID")),
				h("div.viewport", null, rows.length ? rows : h("div.gpu-state-msg", null, "The frame has no markers."))));
	}

	dispose() {
		this.stopSettings();
		this.session.dispose();
	}
}
