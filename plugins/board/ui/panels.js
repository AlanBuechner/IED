// panels.js - the board's side panel (docs/32 "The board in the UI"): Layers, Plan, AI and History.

import { h, icon, replace } from "/ui-kit/dom.js";
import { confirmDialog, promptDialog } from "/ui-kit/dialog.js";

export class LayersPanel {
	constructor(view) {
		this.view = view;
		this.model = view.model;
		this.list = h("div.board-layers", { role: "listbox", "aria-label": "Layers" });
		this.el = h("div.board-panel-body", null,
			h("div.panel-tools", null,
				h("button.ib", { title: "New layer", "aria-label": "New layer", on: { click: () => this.add() } }, icon("add")),
				h("button.ib", { title: "Move the layer up", "aria-label": "Move layer up", on: { click: () => this.shift(1) } }, icon("arrow-up")),
				h("button.ib", { title: "Move the layer down", "aria-label": "Move layer down", on: { click: () => this.shift(-1) } }, icon("arrow-down")),
				h("button.ib", { title: "Merge into the layer below", "aria-label": "Merge down", on: { click: () => this.merge() } }, icon("fold-down")),
				h("span.grow"),
				h("button.ib", { title: "Delete the layer and what is on it", "aria-label": "Delete layer", on: { click: () => this.remove() } }, icon("trash"))),
			this.list);
	}

	render() {
		const active = this.view.activeLayer();
		const rows = this.model.layers.slice().reverse().map((layer) => {
			const count = this.model.all().filter((el) => el.layer === layer.id).length;
			const row = h("div.board-layer" + (active && active.id === layer.id ? ".active" : ""), { role: "option", "aria-selected": String(active && active.id === layer.id), on: { click: () => this.view.setActiveLayer(layer.id), dblclick: () => this.rename(layer) } },
				h("button.ib", { title: layer.visible ? "Hide" : "Show", "aria-label": layer.visible ? "Hide layer" : "Show layer", on: { click: (e) => { e.stopPropagation(); this.update(layer, { visible: !layer.visible }); } } }, icon(layer.visible ? "eye" : "eye-closed")),
				h("button.ib", { title: layer.locked ? "Unlock" : "Lock", "aria-label": layer.locked ? "Unlock layer" : "Lock layer", on: { click: (e) => { e.stopPropagation(); this.update(layer, { locked: !layer.locked }); } } }, icon(layer.locked ? "lock" : "unlock")),
				h("span.name.ellip", { title: layer.name }, layer.name),
				h("span.muted.count", null, String(count)),
				h("input.board-opacity", { type: "range", min: "0", max: "100", value: String(Math.round((layer.opacity ?? 1) * 100)), title: "Opacity", "aria-label": "Layer opacity", on: { click: (e) => e.stopPropagation(), change: (e) => this.update(layer, { opacity: Number(e.target.value) / 100 }) } }));
			return row;
		});
		replace(this.list, rows);
	}

	update(layer, fields) {
		this.model.edit([{ op: "update_layer", id: layer.id, ...fields }], "layer");
	}

	async add() {
		const active = this.view.activeLayer();
		const result = await this.model.edit([{ op: "add_layer", name: "Layer " + (this.model.layers.length + 1), above: active ? active.id : undefined }], "add layer");
		const id = result && result.done && /added layer (L\d+)/.exec(result.done[0] || "");
		if (id) this.view.setActiveLayer(id[1]);
	}

	shift(by) {
		const active = this.view.activeLayer();
		if (!active) return;
		const index = this.model.layers.findIndex((l) => l.id === active.id) + by;
		if (index < 0 || index >= this.model.layers.length) return;
		this.update(active, { index });
	}

	merge() {
		const active = this.view.activeLayer();
		if (active) this.model.edit([{ op: "merge_layer", id: active.id }], "merge layer");
	}

	async remove() {
		const active = this.view.activeLayer();
		if (!active || this.model.layers.length < 2) return;
		const ok = await confirmDialog({ title: "Delete layer", message: "Delete \"" + active.name + "\" and everything on it? Undo brings it back.", confirm: "Delete", danger: true });
		if (ok) this.model.edit([{ op: "delete_layer", id: active.id }], "delete layer");
	}

	async rename(layer) {
		const name = await promptDialog({ title: "Rename layer", value: layer.name, confirm: "Rename" });
		if (name && name !== layer.name) this.update(layer, { name });
	}
}

export class PlanPanel {
	constructor(view) {
		this.view = view;
		this.model = view.model;
		this.current = null;
		this.picker = h("select.board-plan-pick", { "aria-label": "Plan", on: { change: (e) => this.show(e.target.value) } });
		this.text = h("textarea.board-plan-text.mono", { spellcheck: "false", placeholder: "The written plan: Markdown, or a Mermaid flowchart. The AI writes and reads it too.", on: { input: () => this.dirty(), blur: () => this.save() } });
		this.status = h("span.muted.board-plan-status");
		this.el = h("div.board-panel-body", null,
			h("div.panel-tools", null,
				this.picker,
				h("button.ib", { title: "New Markdown plan", "aria-label": "New plan", on: { click: () => this.create("markdown") } }, icon("add")),
				h("span.grow"),
				h("button.ib", { title: "Delete this plan", "aria-label": "Delete plan", on: { click: () => this.remove() } }, icon("trash"))),
			h("div.panel-tools", null,
				h("button.btn.flat", { title: "Write the board's diagram as Mermaid into a plan", on: { click: () => this.fromBoard("mermaid") } }, icon("type-hierarchy-sub"), "From board"),
				h("button.btn.flat", { title: "Draw this Mermaid onto the board (nodes it has keep their places)", on: { click: () => this.apply() } }, icon("arrow-left"), "To board"),
				h("button.btn.flat", { title: "Copy the plan", on: { click: () => navigator.clipboard && navigator.clipboard.writeText(this.text.value) } }, icon("copy"))),
			this.text,
			h("div.board-plan-foot", null, this.status));
		this.timer = 0;
	}

	render() {
		const plans = this.model.plans;
		if (!this.current || !plans.find((p) => p.id === this.current)) this.current = plans.length ? plans[0].id : null;
		replace(this.picker, plans.map((p) => h("option", { value: p.id, selected: p.id === this.current || null }, (p.title || p.id) + " (" + p.kind + ")")));
		this.picker.hidden = !plans.length;
		const plan = plans.find((p) => p.id === this.current);
		if (document.activeElement !== this.text) this.text.value = plan ? plan.text : "";
		this.text.disabled = !plan;
		this.status.textContent = plan ? (plan.by === "ai" ? "Last written by the AI" : "") : "No plan yet: write one, or ask the AI to.";
	}

	show(id) {
		this.save();
		this.current = id;
		this.render();
	}

	dirty() {
		clearTimeout(this.timer);
		this.timer = setTimeout(() => this.save(), 800);
	}

	save() {
		clearTimeout(this.timer);
		const plan = this.model.plans.find((p) => p.id === this.current);
		if (!plan || plan.text === this.text.value) return;
		plan.text = this.text.value;
		this.model.setPlan({ id: plan.id, text: this.text.value });
	}

	async create(kind) {
		const title = await promptDialog({ title: "New plan", value: kind === "mermaid" ? "Diagram" : "Plan", confirm: "Create" });
		if (!title) return;
		const made = await this.model.setPlan({ title, kind, text: "" });
		if (made) this.current = made.id;
	}

	async remove() {
		const plan = this.model.plans.find((p) => p.id === this.current);
		if (!plan) return;
		if (await confirmDialog({ title: "Delete plan", message: "Delete the plan \"" + (plan.title || plan.id) + "\"?", confirm: "Delete", danger: true })) this.model.deletePlan(plan.id);
	}

	async fromBoard(format) {
		const answer = await this.model.tool("export", { format }).catch((e) => this.view.ied.toast("Cannot export: " + e.message, { level: "error" }));
		if (!answer) return;
		const existing = this.model.plans.find((p) => p.kind === "mermaid" && p.title === "Diagram");
		const made = await this.model.setPlan({ id: existing ? existing.id : undefined, title: "Diagram", kind: "mermaid", text: answer.text.trim() + "\n" });
		if (made) this.current = made.id;
	}

	async apply() {
		const plan = this.model.plans.find((p) => p.id === this.current);
		const text = plan && plan.kind === "mermaid" ? plan.text : (/```mermaid\n([\s\S]*?)```/.exec(this.text.value) || [])[1];
		if (!text) return this.view.ied.toast("This plan has no Mermaid flowchart to draw.", { level: "info" });
		const answer = await this.model.tool("import", { text }).catch((e) => this.view.ied.toast("Cannot draw it: " + e.message, { level: "error" }));
		if (answer) this.view.ied.toast(answer.text.split("\n").filter((l) => l.startsWith("Refused") || l.startsWith("Note: nodes")).join(" ") || "Drawn on the board.", { level: answer.text.startsWith("Refused") ? "error" : "info" });
	}
}

// The board's AI: its chat beside the board, and actions that ask it for the common work.
export class AiPanel {
	constructor(view) {
		this.view = view;
		this.el = h("div.board-panel-body.board-ai", null,
			h("div.board-ai-intro.muted", null, "The board's AI sees the board, its layers, your selection and its plans, and changes them live. Its changes are marked in History and undo like yours."),
			h("button.btn.primary.board-ai-open", { on: { click: () => this.view.openChat() } }, icon("comment-discussion"), "Open the board's chat"),
			h("div.board-ai-actions", null,
				this.action("wand", "Clean up the sketch", "Turn the freehand drawing into clean shapes, frames and connectors on their own layer; your strokes stay, hidden", "cleanup"),
				this.action("type-hierarchy", "Convert to Mermaid", "Write the diagram as a Mermaid flowchart and keep it as a plan", "mermaid"),
				this.action("book", "Write the plan", "Write a plan an engineer can build from: components, interfaces, flows, open questions", "plan"),
				this.action("eye", "Review the design", "Point out gaps, risks and open questions, as notes on the board", "review"),
				this.action("tools", "Build it", "Start an agent that implements the plan in this workspace", "build")));
	}

	action(iconName, title, detail, kind) {
		return h("button.board-ai-action", { title: detail, on: { click: () => this.view.askAi(kind) } }, icon(iconName, "c-ai"), h("span.t", null, title), h("span.d.muted", null, detail));
	}

	render() {}
}

export class HistoryPanel {
	constructor(view) {
		this.view = view;
		this.model = view.model;
		this.list = h("div.board-history");
		this.el = h("div.board-panel-body", null,
			h("div.panel-tools", null,
				h("button.btn.flat", { title: "Undo your last change (Ctrl+Z)", on: { click: () => this.model.undo() } }, icon("discard"), "Undo"),
				h("button.btn.flat", { title: "Redo (Ctrl+Y)", on: { click: () => this.model.redo() } }, icon("redo"), "Redo")),
			this.list);
	}

	render() {
		const rows = this.model.history.slice().reverse().map((c) => h("div.board-change" + (c.undone ? ".undone" : ""), null,
			c.by === "ai" ? icon("sparkle", "c-ai") : icon("person"),
			h("span.ellip.grow", { title: c.id + " " + (c.label || "") }, (c.label || "change") + " " + c.elements + (c.elements === 1 ? " element" : " elements")),
			h("span.muted", null, when(c.timeMs)),
			h("button.ib", { title: c.undone ? "Redo this change" : "Revert this change", "aria-label": c.undone ? "Redo" : "Revert", on: { click: () => this.model.revert(c.id, c.undone) } }, icon(c.undone ? "redo" : "discard"))));
		replace(this.list, rows.length ? rows : [h("div.muted.board-empty", null, "No changes yet in this session.")]);
	}
}

function when(ms) {
	if (!ms) return "";
	const d = new Date(ms);
	return d.getHours().toString().padStart(2, "0") + ":" + d.getMinutes().toString().padStart(2, "0");
}
