// capture.js - one capture as a document (F-PRF-62, mockup 06): the toolbar (live state, Follow, Stop, Threads or
// Tasks, Note, the analysis windows, Ask AI), the frame time graph, the timeline with the zone or lock details beside
// it, and the bottom tool windows - Statistics, Find Zone, Messages, Memory, Flame Graph, Locks, Notes - over the
// selection, else the view. A live capture follows its newest data until the timeline is moved. Notes (F-PRF-19) are
// the server's: the list comes with the capture's topic and every client sees a change. On a phone the details are a
// sheet under the timeline and a tool window takes the whole view, with a way back.

import { h, icon, replace } from "/ui-kit/dom.js";
import { confirmDialog, promptDialog } from "/ui-kit/dialog.js";
import { TimelineView } from "./timeline.js";
import { FrameGraph } from "./framegraph.js";
import { StatsPanel, MessagesPanel } from "./panels.js";
import { FindZonePanel } from "./findzone.js";
import { FlamePanel } from "./flame.js";
import { MemoryPanel } from "./memory.js";
import { LocksPanel } from "./locks.js";
import { NotesPanel, noteRange } from "./notes.js";
import { ZoneDetails } from "./details.js";
import { duration, bytes, count, instant } from "./format.js";
import { seriesOf, LOCK_BLOCKED } from "./model.js";

const PHONE = "(max-width: 719px)";
const PANELS = [["stats", "Statistics", "table"], ["find", "Find Zone", "search"], ["messages", "Messages", "output"], ["memory", "Memory", "database"], ["flame", "Flame Graph", "flame"], ["locks", "Locks", "lock"], ["notes", "Notes", "note"]];
const SCOPE_DELAY_MS = 400;
const LIVE_REFRESH_MS = 1000;

function store(key, value) {
	try {
		localStorage.setItem(key, JSON.stringify(value));
	}
	catch (e) {
		return;
	}
}

function stored(key, fallback) {
	try {
		const text = localStorage.getItem(key);
		return text === null ? fallback : JSON.parse(text);
	}
	catch (e) {
		return fallback;
	}
}

export class CaptureView {
	constructor(ctx, captureId) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.captureId = captureId;
		this.mode = stored("ied.profiler.mode", "threads");
		this.panel = stored("ied.profiler.panel", "stats");
		this.status = null;
		this.notes = [];
		this.budgetNs = 1e9 / 60;
		this.phone = window.matchMedia(PHONE).matches;
		this.build();
		this.stop = () => {};
		this.loadSettings();
		this.start();
	}

	// The capture is loaded on the server (its summary asks for it), then its status topic is followed.
	async start() {
		const summary = await this.request("prof.summary", { captureId: this.captureId });
		if (!summary) {
			replace(this.recLabel, icon("warning", "c-warn"), "This capture cannot be read.");
			return;
		}
		this.threadNames = {};
		for (const thread of summary.threads) this.threadNames[thread.track] = thread.name;
		if (this.disposed) return;
		this.stop = this.ied.client.subscribe("prof:" + this.captureId, {
			snap: (data) => {
				this.statusChanged(data);
				this.loadNotes();
			},
			evt: (kind, data) => {
				if (kind === "annotations") this.setNotes(data.annotations);
				else this.statusChanged(data);
			},
		});
		this.refreshFrames();
	}

	request(method, params) {
		return this.ied.client.request(method, params).catch((e) => {
			if (e && e.code === "cancelled") return null;
			this.ied.toast(e.message || String(e), { level: "error" });
			return null;
		});
	}

	// --- Building ---------------------------------------------------------------------------------------------------

	build() {
		const request = (m, p) => this.request(m, p);
		this.recLabel = h("span.rec");
		this.followButton = h("button.btn.flat", { hidden: true, title: "Keep the newest data in view", on: { click: () => this.setFollow(!this.timeline.follow) } }, icon("debug-pause"), h("span.lbl", null, "Pause view"));
		this.stopButton = h("button.btn.flat", { hidden: true, title: "Stop recording (the program keeps running)", on: { click: () => this.stopCapture() } }, icon("debug-stop"), h("span.lbl", null, "Stop"));
		this.modeSeg = h("span.seg", null, [["threads", "Threads", "list-flat"], ["tasks", "Tasks", "symbol-event"]].map(([key, title, glyph]) =>
			h("span" + (key === this.mode ? ".on" : ""), { dataset: { mode: key }, title, on: { click: () => this.setMode(key) } }, icon(glyph), h("span.lbl", null, title))));
		this.legend = h("span.legend");
		this.panelButtons = PANELS.map(([key, title, glyph]) => h("button.btn.flat", { dataset: { panel: key }, title, on: { click: () => this.togglePanel(key) } }, icon(glyph), h("span.lbl", null, title)));
		this.fgHost = h("div.fg");
		this.tlHost = h("div.tl");
		this.details = new ZoneDetails({ request, phone: this.phone, openSource: (z) => this.openSource(z), findZone: (n) => this.findZone(n), askAi: (z) => this.askAi(z), showLocks: () => this.showPanel("locks") });
		this.panels = {
			stats: new StatsPanel({ request, onPick: (row) => this.findZone(row.name) }),
			find: new FindZonePanel({ request, onJump: (from, to) => this.reveal(from, to) }),
			messages: new MessagesPanel({ request, onJump: (t) => this.reveal(t - this.timeline.view.span / 2, t + this.timeline.view.span / 2) }),
			memory: new MemoryPanel({ request }),
			flame: new FlamePanel({ request, onPick: (name) => this.findZone(name) }),
			locks: new LocksPanel({ request, onPick: (row) => this.pickLock(row) }),
			notes: new NotesPanel({ onAdd: () => this.addNote(), onJump: (note) => this.jumpToNote(note), onEdit: (note) => this.editNote(note), onDelete: (note) => this.deleteNote(note) }),
		};
		this.tabs = h("div.tw-tabs", null, PANELS.map(([key, title]) => h("span.tw-tab", { dataset: { panel: key }, on: { click: () => this.togglePanel(key) } }, title)));
		this.panelBody = h("div.prof-panel-body");
		this.sash = h("div.prof-sash", { role: "separator", "aria-orientation": "horizontal", title: "Drag to resize" });
		this.bottom = h("section.prof-bottom", null, this.sash, this.panelBody, this.tabs);
		this.bindSash();
		this.phoneBack = h("button.btn.flat.phone-only", { on: { click: () => this.showPanel(null) } }, icon("arrow-left"), "Timeline");
		this.root = h("div.prof-doc",
			{ dataset: { mode: this.mode } },
			h("div.prof-bar", null, this.recLabel, h("span.tb-sep"), this.followButton, this.stopButton, h("span.tb-sep"), this.modeSeg, this.legend, h("span.grow"),
				h("button.btn.flat", { title: "Show the whole capture (Home)", on: { click: () => this.timeline.showAll() } }, icon("screen-full"), h("span.lbl", null, "All")),
				h("button.btn.flat", { title: "Add a note on the selection or the selected zone", on: { click: () => this.addNote() } }, icon("note"), h("span.lbl", null, "Note")),
				this.panelButtons, h("span.tb-sep"),
				h("button.btn.flat", { title: "Ask the AI about the selection", on: { click: () => this.askAi(this.details.zone) } }, icon("sparkle", "c-ai"), h("span.lbl", null, "Ask AI about selection")),
				this.phoneBack),
			this.fgHost,
			h("div.prof-main", null, this.tlHost, this.details.el),
			this.bottom);
		replace(this.ctx.el, this.root);
		this.frameGraph = new FrameGraph(this.fgHost, { onPick: (from, to) => this.timeline.zoomTo(from, to) });
		this.timeline = new TimelineView(this.tlHost, {
			fetch: (from, to, width) => this.request("prof.timeline", { captureId: this.captureId, from: Math.floor(from), to: Math.ceil(to), width, mode: this.mode }).then((a) => this.answered(a)),
			onSelect: (selected) => this.selected(selected),
			onNote: (note) => this.noteClicked(note),
			onRange: () => this.scopeChanged(),
			onView: (view) => {
				this.frameGraph.setView(view);
				this.scopeChanged();
			},
			budgetNs: this.budgetNs,
		});
		this.showPanel(this.phone ? null : this.panel);
	}

	// The bottom tool windows' height follows a drag on their top edge, kept per device.
	bindSash() {
		const saved = stored("ied.profiler.bottomHeight", 0);
		if (saved) this.bottom.style.height = saved + "px";
		this.sash.addEventListener("pointerdown", (e) => {
			this.sash.setPointerCapture(e.pointerId);
			const startY = e.clientY;
			const startHeight = this.bottom.getBoundingClientRect().height;
			const move = (m) => {
				const most = this.root.getBoundingClientRect().height - 160;
				const height = Math.max(90, Math.min(most, startHeight + startY - m.clientY));
				this.bottom.style.height = height + "px";
			};
			const up = () => {
				this.sash.removeEventListener("pointermove", move);
				this.sash.removeEventListener("pointerup", up);
				store("ied.profiler.bottomHeight", Math.round(this.bottom.getBoundingClientRect().height));
			};
			this.sash.addEventListener("pointermove", move);
			this.sash.addEventListener("pointerup", up);
		});
	}

	dispose() {
		this.disposed = true;
		this.stop();
		this.timeline.dispose();
		this.frameGraph.dispose();
		this.panels.flame.dispose();
		if (this.stopSettings) this.stopSettings();
	}

	// --- State ------------------------------------------------------------------------------------------------------

	loadSettings() {
		this.stopSettings = this.ied.client.subscribe("settings:" + this.ctx.wsId, {
			snap: (data) => this.settings(data.values || {}),
			evt: (kind, data) => {
				if (kind === "settings.changed" && data.values) this.settings(data.values);
			},
		});
	}

	settings(values) {
		const fps = values["profiler.targetFps"] ? values["profiler.targetFps"].value : 60;
		this.budgetNs = 1e9 / Math.max(1, fps || 60);
		this.timeline.budgetNs = this.budgetNs;
		const follow = values["profiler.followLive"];
		if (follow && follow.value === false) this.timeline.follow = false;
		this.refreshFrames();
		this.timeline.schedulePaint();
	}

	statusChanged(status) {
		const was = this.status;
		this.status = status;
		if (!status) return;
		this.ctx.tab.setTitle(status.app || "Capture");
		replace(this.recLabel, status.live ? h("i.r") : icon("pulse"), (status.live ? "Capturing " : "") + (status.app || "Capture"),
			h("span.muted", null, " \u00b7 " + duration(status.durationNs) + " \u00b7 " + count(status.zones) + " zones \u00b7 " + bytes(status.bytes)));
		this.followButton.hidden = !status.live;
		this.stopButton.hidden = !status.live;
		this.timeline.setExtent(status.durationNs, status.live);
		if (!was || !was.durationNs) this.timeline.showAll();
		if (status.live || (was && was.live)) this.liveRefresh();
	}

	// While live, the frame graph and the open tool window catch up once a second.
	liveRefresh() {
		if (this.liveTimer) return;
		this.liveTimer = setTimeout(() => {
			this.liveTimer = null;
			this.refreshFrames();
			this.refreshPanel();
		}, LIVE_REFRESH_MS);
	}

	answered(answer) {
		if (answer && this.mode === "tasks") this.renderLegend(answer);
		else replace(this.legend);
		return answer;
	}

	renderLegend(answer) {
		const tracks = new Map();
		for (const lane of answer.lanes) for (const slice of lane.slices || []) tracks.set(slice[2], true);
		const names = this.threadNames || {};
		replace(this.legend, tracks.size ? "Ran on:" : null, [...tracks.keys()].slice(0, 8).map((track) =>
			h("span", null, h("i.sw.s" + seriesOf(track)), names[track] || "Thread " + track)));
	}

	setMode(mode) {
		this.mode = mode;
		store("ied.profiler.mode", mode);
		this.root.dataset.mode = mode;
		for (const span of this.modeSeg.children) span.classList.toggle("on", span.dataset.mode === mode);
		this.timeline.invalidate();
	}

	setFollow(follow) {
		this.timeline.follow = follow;
		replace(this.followButton, icon(follow ? "debug-pause" : "play"), h("span.lbl", null, follow ? "Pause view" : "Follow"));
		if (follow && this.status) this.timeline.setExtent(this.status.durationNs, this.status.live);
	}

	async stopCapture() {
		await this.request("prof.stop", { captureId: this.captureId });
	}

	async refreshFrames() {
		const graph = await this.request("prof.frames", { captureId: this.captureId, width: Math.max(100, this.fgHost.clientWidth || 800) });
		if (graph) this.frameGraph.set(graph, this.budgetNs);
		this.root.classList.toggle("no-frames", !graph || !graph.count);
	}

	// --- Selection, scope and panels --------------------------------------------------------------------------------

	selected(selected) {
		if (!selected) {
			this.details.clear();
			this.root.classList.remove("zone-shown");
			return;
		}
		if (selected.kind === "lock") {
			this.root.classList.add("zone-shown");
			this.details.showLock(this.captureId, selected);
			return;
		}
		const ref = selected.zone[4];
		if (selected.zone[3] < 0) {
			this.timeline.zoomTo(selected.zone[1], selected.zone[2]);
			return;
		}
		if (typeof ref !== "string") return;
		this.root.classList.add("zone-shown");
		this.details.show(this.captureId, ref);
	}

	// What the tool windows look at: the selection, else the timeline's view.
	scope() {
		const range = this.timeline.normalizedRange();
		if (range) return { from: range.from, to: range.to, label: "selection" };
		const view = this.timeline.view;
		return { from: Math.max(0, view.from), to: view.to, label: "view" };
	}

	scopeChanged() {
		clearTimeout(this.scopeTimer);
		this.scopeTimer = setTimeout(() => this.refreshPanel(), SCOPE_DELAY_MS);
	}

	refreshPanel() {
		if (!this.panel || !this.panels[this.panel]) return;
		this.panels[this.panel].update(this.captureId, this.scope());
	}

	// A toolbar button or tab: shows its tool window, or closes it when it is the one shown.
	togglePanel(key) {
		this.showPanel(key === this.panel && !this.phone ? null : key);
	}

	showPanel(key) {
		this.panel = key;
		if (this.panel) store("ied.profiler.panel", this.panel);
		this.root.classList.toggle("panel-closed", !this.panel);
		this.root.classList.toggle("panel-open", !!this.panel);
		for (const tab of this.tabs.children) tab.classList.toggle("on", tab.dataset.panel === this.panel);
		const shown = [...this.tabs.children].find((tab) => tab.dataset.panel === this.panel);
		if (shown) shown.scrollIntoView({ block: "nearest", inline: "nearest" });
		for (const button of this.panelButtons) button.classList.toggle("on", button.dataset.panel === this.panel);
		if (!this.panel) return;
		replace(this.panelBody, this.panels[this.panel].el);
		this.refreshPanel();
	}

	findZone(name) {
		this.showPanel("find");
		this.panels.find.find(name);
	}

	// A tool window's pick zoomed to on the timeline; on a phone, where the window covers the timeline, the window closes.
	reveal(from, to) {
		this.timeline.zoomTo(from, Math.max(to, from + 1));
		if (this.phone) this.showPanel(null);
	}

	// The Locks window's row: its longest wait, shown and selected.
	pickLock(row) {
		if (!row.worst) return;
		const { track, index, fromNs, toNs } = row.worst;
		this.reveal(fromNs, toNs);
		this.timeline.selectLock({ laneId: "thread:" + track, track, lock: row.lock, name: row.name, item: [fromNs, toNs, LOCK_BLOCKED, index] });
	}

	// --- Notes ------------------------------------------------------------------------------------------------------

	async loadNotes() {
		const answer = await this.request("prof.annotations", { captureId: this.captureId });
		if (answer) this.setNotes(answer.annotations);
	}

	setNotes(notes) {
		this.notes = notes || [];
		this.timeline.setNotes(this.notes);
		this.panels.notes.set(this.notes);
	}

	// What a new note covers: the selected range, else the selected zone or lock hold.
	noteTarget() {
		const range = this.timeline.normalizedRange();
		if (range) return range;
		if (this.timeline.selected) return { from: this.timeline.selected.zone[1], to: this.timeline.selected.zone[2] };
		if (this.timeline.selectedLock) return { from: this.timeline.selectedLock.item[0], to: this.timeline.selectedLock.item[1] };
		return null;
	}

	async addNote() {
		const target = this.noteTarget();
		if (!target) {
			this.ied.toast("Select a range (Shift+drag) or a zone first; the note covers it.", { level: "info" });
			return;
		}
		const text = await promptDialog({ title: "Note " + noteRange({ fromNs: target.from, toNs: target.to }), placeholder: "What happens here", confirm: "Add note", check: (v) => (v.trim() ? "" : "Write the note") });
		if (!text) return;
		const added = await this.request("prof.annotate", { captureId: this.captureId, from: Math.round(target.from), to: Math.round(target.to), text });
		if (added) {
			this.timeline.selectNote(added.annotation.id);
			this.panels.notes.select(added.annotation.id);
		}
	}

	noteClicked(note) {
		this.panels.notes.select(note.id);
		this.showPanel("notes");
	}

	jumpToNote(note) {
		this.timeline.selectNote(note.id);
		this.panels.notes.select(note.id);
		this.timeline.range = note.toNs > note.fromNs ? { from: note.fromNs, to: note.toNs } : null;
		this.reveal(note.fromNs, note.toNs);
		this.scopeChanged();
	}

	async editNote(note) {
		const text = await promptDialog({ title: "Note " + noteRange(note), value: note.text, confirm: "Save", check: (v) => (v.trim() ? "" : "Write the note") });
		if (text && text !== note.text) await this.request("prof.updateAnnotation", { captureId: this.captureId, id: note.id, text });
	}

	async deleteNote(note) {
		const sure = await confirmDialog({ title: "Delete this note?", message: note.text, confirm: "Delete", danger: true });
		if (sure) await this.request("prof.deleteAnnotation", { captureId: this.captureId, id: note.id });
	}

	// --- Leaving the profiler ---------------------------------------------------------------------------------------

	// The source file a zone names, found in the workspace: the path itself, else the file whose path ends with it.
	async openSource(zone) {
		const wanted = zone.file.replace(/\\/g, "/");
		const root = (this.ied.workspace.root || "").replace(/\\/g, "/").replace(/\/$/, "");
		let path = wanted.toLowerCase().startsWith(root.toLowerCase() + "/") ? wanted.slice(root.length + 1) : wanted;
		const name = path.split("/").pop();
		const found = await this.request("fs.files", { wsId: this.ctx.wsId, query: name, maxResults: 200 });
		const files = found ? found.files.map((f) => f.path) : [];
		const match = files.find((f) => f === path) || files.find((f) => f.endsWith("/" + path)) || files.find((f) => f.split("/").pop() === name);
		if (!match) {
			this.ied.toast("The source " + zone.file + " is not in this workspace.", { level: "warning" });
			return;
		}
		path = match;
		await this.ied.commands.run("shell.showActivity", { id: "ied.editor" });
		this.ied.commands.run("ied.editor.goTo", { path, line: Math.max(0, (zone.line || 1) - 1) });
	}

	// A chat about the selection: the question and an excerpt that names the capture, so the AI's profile_* tools can
	// look closer.
	async askAi(zone) {
		const scope = this.scope();
		const question = await promptDialog({ title: "Ask AI about this " + (zone ? "zone" : scope.label), value: zone ? "Why is " + zone.name + " slow, and what would make it faster?" : "Where does the time go here?", confirm: "Ask" });
		if (!question) return;
		const lines = [question, "", "Profiler capture " + this.captureId + (this.status ? " (" + this.status.app + ")" : "") + ", " + scope.label + " " + instant(scope.from, scope.to - scope.from) + " to " + instant(scope.to, scope.to - scope.from) + "."];
		const notes = this.notes.filter((n) => n.toNs >= scope.from && n.fromNs <= scope.to);
		if (notes.length) lines.push("Notes here: " + notes.slice(0, 8).map((n) => noteRange(n) + " \"" + n.text + "\"").join("; ") + ".");
		if (zone) {
			lines.push("Zone " + zone.name + " [" + zone.ref + "]: " + duration(zone.durationNs) + ", self " + duration(zone.selfNs) + ", on " + (zone.task || zone.thread) +
				(zone.file ? ", " + zone.file + ":" + zone.line : "") + (zone.frame ? ", frame " + zone.frame.index + " (" + duration(zone.frame.durationNs) + ")" : "") + ".");
			if (zone.children.length) lines.push("Its children: " + zone.children.slice(0, 8).map((c) => c.name + " " + duration(c.totalNs)).join(", ") + ".");
		}
		lines.push("Use the profile_* tools with capture \"" + this.captureId + "\" to look closer.");
		const created = await this.request("chat.create", { wsId: this.ctx.wsId, title: zone ? "Profiler: " + zone.name : "Profiler question" });
		if (!created) return;
		await this.request("chat.send", { chatId: created.chatId, text: lines.join("\n"), as: "message" });
		await this.ied.commands.run("shell.showActivity", { id: "ied.chat" });
	}
}
