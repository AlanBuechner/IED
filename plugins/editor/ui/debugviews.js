// debugviews.js - the debugger's tool windows (VS): Call Stack (the shown thread's frames; a click shows a frame's place
// and makes it the one Locals and Watch use), Locals (the frame's variables as a tree, children asked of the adapter
// when a row opens), Watch (the workspace's expressions, evaluated at each stop; a field adds one, Delete removes it),
// Threads (a click shows that thread's stack) and Breakpoints (every file's, with their state; a click goes there, the
// box enables or disables one). They follow the session the toolbar follows and say so when there is nothing to show.

import { h, icon, replace } from "/ui-kit/dom.js";
import { TreeView } from "/ui-kit/tree.js";
import { framePath } from "./debug.js";

// "main.cf, line 7" for a frame's place; the file name alone, as VS shows it.
export function frameWhere(frame) {
	const path = framePath(frame);
	if (!path) return frame.source && frame.source.name ? frame.source.name : "";
	return decodeURIComponent(path.slice(path.lastIndexOf("/") + 1)) + ", line " + (frame.line + 1);
}

// The tree indents a row this much a level; the name column gives it back, so values line up (VS).
const INDENT_PX = 16;

// How deep a Locals node is: its id is its parents' ids and its own, joined by "/".
function depthOf(node) {
	return Math.max(0, node.id.split("/").length - 2);
}

function emptyText(debug) {
	if (!debug.session) return "Start debugging (F5) to see the call stack.";
	if (debug.session.state === "running") return "The program is running. Break All to see where it is.";
	if (!debug.live()) return "The debug session has ended.";
	return "";
}

export class CallStack {
	constructor(ctx, debug, goTo) {
		this.ctx = ctx;
		this.debug = debug;
		this.goTo = goTo;
		this.list = h("div.cs-rows", { role: "grid", "aria-label": "Call Stack", tabindex: "0" });
		ctx.el.append(h("div.dbg-head.cs-grid", null, h("span.cs-arrow"), h("span.cs-name", null, "Name"), h("span.cs-where", null, "Location")), h("div.ed-fill", null, this.list));
		debug.follow(ctx.wsId);
		this.stop = debug.on(() => this.render());
	}

	render() {
		const frames = this.debug.stopped() ? this.debug.frames : [];
		if (!frames.length) return replace(this.list, h("div.muted.el-empty", null, emptyText(this.debug) || "No frames."));
		replace(this.list, ...frames.map((frame, i) => h("div.row.cs-row.cs-grid" + (frame === this.debug.frame ? ".sel" : "") + (i === 0 ? ".top" : ""), {
			role: "row",
			on: { click: () => this.open(frame) },
		}, h("span.cs-arrow", null, frame === this.debug.frame ? "▶" : ""), h("span.cs-name", null, frame.name), h("span.cs-where.muted", null, frameWhere(frame)))));
	}

	open(frame) {
		this.debug.selectFrame(frame);
		const path = framePath(frame);
		if (path) this.goTo({ path, line: frame.line, col: frame.column });
	}

	dispose() {
		this.stop();
	}
}

export class Locals {
	constructor(ctx, debug) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.debug = debug;
		this.empty = h("div.muted.el-empty");
		this.host = h("div.ed-fill");
		ctx.el.append(h("div.dbg-head", null, h("span.lv-name", null, "Name"), h("span.lv-value", null, "Value"), h("span.lv-type", null, "Type")), this.empty, this.host);
		this.tree = new TreeView(this.host, {
			roots: () => this.roots(),
			children: (node) => this.variables(node.reference, node.id),
			render: (node, row) => row.append(h("span.lv-name.name", { style: { "flex-basis": "calc(34% - " + INDENT_PX * depthOf(node) + "px)" } }, node.name), h("span.lv-value", { title: node.value }, node.value), h("span.lv-type.muted", null, node.type || "")),
			label: (node) => node.name,
			open: () => {},
		});
		this.shownFor = null;
		debug.follow(ctx.wsId);
		this.stop = debug.on(() => this.changed());
	}

	// A new stop or another frame: the tree is read again.
	changed() {
		const frame = this.debug.stopped() ? this.debug.frame : null;
		const key = frame ? this.debug.session.id + " " + this.debug.session.revision + " " + frame.id : null;
		this.empty.textContent = frame ? "" : emptyText(this.debug) || "No frame.";
		this.empty.hidden = !!frame;
		if (key === this.shownFor) return;
		this.shownFor = key;
		this.tree.refresh().catch((e) => this.ied.report("Locals: " + e.message, e));
	}

	// The frame's first scope's variables (Locals), the others as rows to open.
	async roots() {
		const frame = this.debug.stopped() ? this.debug.frame : null;
		if (!frame) return [];
		const answer = await this.ied.client.request("dbg.scopes", { sessionId: this.debug.session.id, frameId: frame.id });
		const scopes = answer.scopes || [];
		if (!scopes.length) return [];
		const first = await this.variables(scopes[0].variablesReference, "s0");
		const others = scopes.slice(1).map((scope, i) => ({ id: "s" + (i + 1), name: scope.name, value: "", type: "", reference: scope.variablesReference, expandable: scope.variablesReference > 0 }));
		return first.concat(others);
	}

	async variables(reference, parentId) {
		const answer = await this.ied.client.request("dbg.variables", { sessionId: this.debug.session.id, variablesReference: reference });
		return (answer.variables || []).map((v, i) => ({ id: parentId + "/" + i + ":" + v.name, name: v.name, value: v.value, type: v.type, reference: v.variablesReference, expandable: v.variablesReference > 0 }));
	}

	focus() {
		this.tree.focus();
	}

	dispose() {
		this.stop();
		this.tree.dispose();
	}
}

// A tree of values (Watch): each root an expression with its value in the shown frame, children asked of the adapter.
export class Watch {
	constructor(ctx, debug, breakpoints) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.debug = debug;
		this.breakpoints = breakpoints;
		this.field = h("input.input-field.watch-add", { placeholder: "Add item to watch", "aria-label": "Add item to watch", spellcheck: "false", on: { keydown: (event) => this.adding(event) } });
		this.host = h("div.ed-fill", { on: { keydown: (event) => this.key(event) } });
		ctx.el.append(h("div.dbg-head", null, h("span.lv-name", null, "Name"), h("span.lv-value", null, "Value"), h("span.lv-type", null, "Type")), this.host, h("div.watch-foot", null, this.field));
		this.tree = new TreeView(this.host, {
			roots: () => this.roots(),
			children: (node) => this.children(node),
			render: (node, row) => row.append(h("span.lv-name.name", { style: { "flex-basis": "calc(34% - " + INDENT_PX * depthOf(node) + "px)" } }, node.name),
				h("span.lv-value" + (node.failed ? ".c-error" : ""), { title: node.value }, node.value), h("span.lv-type.muted", null, node.type || "")),
			label: (node) => node.name,
			open: () => {},
		});
		this.shownFor = null;
		debug.follow(ctx.wsId);
		breakpoints.follow(ctx.wsId);
		this.stopDebug = debug.on(() => this.changed(false));
		this.stopBreakpoints = breakpoints.on(() => this.changed(false));
	}

	// A new stop, another frame or other expressions: the tree is read again.
	changed() {
		const frame = this.debug.stopped() ? this.debug.frame : null;
		const key = (frame ? this.debug.session.id + " " + this.debug.session.revision + " " + frame.id : "-") + " " + this.breakpoints.watches.join("\n");
		if (key === this.shownFor) return;
		this.shownFor = key;
		this.tree.refresh().catch((e) => this.ied.report("Watch: " + e.message, e));
	}

	async roots() {
		const stopped = this.debug.stopped();
		return Promise.all(this.breakpoints.watches.map(async (expression, i) => {
			const value = stopped ? await this.debug.evaluate(expression, "watch") : {};
			return {
				id: "w" + i, index: i, name: expression, expression: true,
				value: value.error ? value.error : value.result || "", failed: !!value.error, type: value.type || "",
				reference: value.variablesReference || 0, expandable: (value.variablesReference || 0) > 0,
			};
		}));
	}

	async children(node) {
		const answer = await this.ied.client.request("dbg.variables", { sessionId: this.debug.session.id, variablesReference: node.reference });
		return (answer.variables || []).map((v, i) => ({ id: node.id + "/" + i + ":" + v.name, name: v.name, value: v.value, type: v.type, reference: v.variablesReference, expandable: v.variablesReference > 0 }));
	}

	adding(event) {
		if (event.key !== "Enter") return;
		const text = this.field.value.trim();
		if (!text) return;
		this.field.value = "";
		this.breakpoints.setWatches(this.breakpoints.watches.concat(text));
	}

	key(event) {
		const node = this.tree.selectedNode();
		if (event.key !== "Delete" || !node || !node.expression) return;
		event.preventDefault();
		this.breakpoints.setWatches(this.breakpoints.watches.filter((_, i) => i !== node.index));
	}

	focus() {
		this.field.focus();
	}

	dispose() {
		this.stopDebug();
		this.stopBreakpoints();
		this.tree.dispose();
	}
}

export class Threads {
	constructor(ctx, debug) {
		this.ctx = ctx;
		this.debug = debug;
		this.list = h("div.cs-rows", { role: "grid", "aria-label": "Threads", tabindex: "0" });
		ctx.el.append(h("div.dbg-head.cs-grid", null, h("span.cs-arrow"), h("span.cs-name", null, "Name"), h("span.cs-where", null, "Id")), h("div.ed-fill", null, this.list));
		debug.follow(ctx.wsId);
		this.stop = debug.on(() => this.render());
	}

	render() {
		const threads = this.debug.stopped() ? this.debug.threads : [];
		if (!threads.length) return replace(this.list, h("div.muted.el-empty", null, emptyText(this.debug) || "No threads."));
		const shown = this.debug.threadShown();
		replace(this.list, ...threads.map((thread) => h("div.row.cs-row.cs-grid" + (thread.id === shown ? ".sel" : ""), {
			role: "row", on: { click: () => this.debug.selectThread(thread.id) },
		}, h("span.cs-arrow", null, thread.id === shown ? "▶" : ""), h("span.cs-name", null, thread.name || "Thread " + thread.id), h("span.cs-where.muted", null, String(thread.id)))));
		return null;
	}

	dispose() {
		this.stop();
	}
}

// "main.cf, line 7" for a breakpoint, with its condition when it has one.
export function breakpointLabel(path, breakpoint) {
	const name = decodeURIComponent(path.slice(path.lastIndexOf("/") + 1));
	return name + ", line " + (breakpoint.line + 1) + (breakpoint.condition ? " when " + breakpoint.condition : "");
}

export class BreakpointsWindow {
	constructor(ctx, debug, breakpoints, goTo) {
		this.ctx = ctx;
		this.debug = debug;
		this.breakpoints = breakpoints;
		this.goTo = goTo;
		this.list = h("div.cs-rows", { role: "grid", "aria-label": "Breakpoints", tabindex: "0" });
		ctx.el.append(h("div.dbg-head.bp-grid", null, h("span"), h("span", null, "Name"), h("span", null, "File"), h("span")), h("div.ed-fill", null, this.list));
		breakpoints.follow(ctx.wsId);
		debug.follow(ctx.wsId);
		this.stopBreakpoints = breakpoints.on(() => this.render());
		this.stopDebug = debug.on(() => this.render());
	}

	render() {
		const rows = this.breakpoints.all();
		if (!rows.length) return replace(this.list, h("div.muted.el-empty", null, "No breakpoints. F9 sets one on the editor's line."));
		const session = this.debug.live() ? this.debug.session : null;
		replace(this.list, ...rows.map(({ path, breakpoint }) => this.row(path, breakpoint, session)));
		return null;
	}

	row(path, breakpoint, session) {
		const bound = session ? this.breakpoints.boundIn(session.id, path).find((b) => b.line === breakpoint.line) : null;
		const state = !breakpoint.enabled ? "bp-disabled" : bound && !bound.verified ? "bp-unverified" : "bp-set";
		const box = h("input", { type: "checkbox", checked: breakpoint.enabled ? "" : null, "aria-label": "Enabled", on: { click: (event) => event.stopPropagation(), change: () => this.toggle(path, breakpoint) } });
		const remove = h("button.ib", { title: "Delete", "aria-label": "Delete breakpoint", on: { click: (event) => { event.stopPropagation(); this.remove(path, breakpoint); } } }, icon("close"));
		return h("div.row.cs-row.bp-grid", { role: "row", title: bound && bound.message ? bound.message : "", on: { click: () => this.goTo({ path, line: breakpoint.line, col: 0 }) } },
			h("span.bp-cell", null, box, h("span.bp-dot." + state)), h("span.cs-name", null, breakpointLabel(path, breakpoint)), h("span.cs-where.muted", null, path), remove);
	}

	toggle(path, breakpoint) {
		this.breakpoints.set(path, this.breakpoints.of(path).map((b) => (b.line === breakpoint.line ? Object.assign({}, b, { enabled: !b.enabled }) : b)));
	}

	remove(path, breakpoint) {
		this.breakpoints.set(path, this.breakpoints.of(path).filter((b) => b.line !== breakpoint.line));
	}

	dispose() {
		this.stopBreakpoints();
		this.stopDebug();
	}
}
