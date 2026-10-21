// debugviews.js - the debugger's tool windows (VS): Call Stack (the stopped thread's frames; a click shows a frame's place
// and makes it the one Locals shows) and Locals (the frame's variables as a tree, children asked of the adapter when a
// row opens). Both follow the session the toolbar follows and say so when there is nothing to show.

import { h, replace } from "/ui-kit/dom.js";
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
