// panels.js - the board's side panel (docs/32 "The board in the UI"): Layers, Plan and History, and the frame that
// shows one of them - in the Board tool window under the board list, or in a phone board's sheet.

import { h, icon, replace } from "/ui-kit/dom.js";
import { confirmDialog, promptDialog } from "/ui-kit/dialog.js";
import { MenuHost } from "/ui-kit/menu.js";

const PANELS = [["layers", "Layers", "layers"], ["plan", "Plan", "book"], ["history", "History", "history"]];
const PANEL_KEY = "ied.board.panel";
const menus = new MenuHost();
const CHECK = "$(check)";

// A layer's menu (docs/32 "Layers"): the rows for `layer` of `layers` (bottom to top) holding `count` elements. Pure, for
// the tests.
export function layerMenuRows(layers, layer, count) {
	const at = layers.findIndex((l) => l.id === layer.id);
	const others = layers.filter((l) => l.id !== layer.id);
	return [
		{ kind: "item", label: "New Layer Above", id: "add", enabled: true },
		{ kind: "separator" },
		{ kind: "item", label: "Rename…", id: "rename", enabled: true, keys: "F2" },
		{ kind: "item", label: "Visible", id: "visible", enabled: true, icon: layer.visible ? CHECK : null },
		{ kind: "item", label: "Locked", id: "locked", enabled: true, icon: layer.locked ? CHECK : null },
		{ kind: "item", label: "Show Only This Layer", id: "solo", enabled: !layer.visible || others.some((l) => l.visible) },
		{ kind: "item", label: "Select Everything on It", id: "select", enabled: count > 0 && layer.visible && !layer.locked },
		{ kind: "separator" },
		{ kind: "item", label: "Move Up", id: "up", enabled: at < layers.length - 1 },
		{ kind: "item", label: "Move Down", id: "down", enabled: at > 0 },
		{ kind: "item", label: "Merge Down", id: "merge", enabled: at > 0 },
		{ kind: "separator" },
		{ kind: "item", label: "Delete Layer…", id: "delete", enabled: layers.length > 1 },
	];
}

// The Layers list's own menu, from its empty space. Pure, for the tests.
export function layersMenuRows(layers) {
	return [
		{ kind: "item", label: "New Layer", id: "add", enabled: true },
		{ kind: "separator" },
		{ kind: "item", label: "Show All Layers", id: "showAll", enabled: layers.some((l) => !l.visible) },
		{ kind: "item", label: "Unlock All Layers", id: "unlockAll", enabled: layers.some((l) => l.locked) },
	];
}

function storedPanel() {
	try {
		const name = localStorage.getItem(PANEL_KEY);
		return PANELS.some(([id]) => id === name) ? name : "layers";
	}
	catch {
		return "layers";
	}
}

// The panel's tabs over the one shown, of the board `attach` gives it (each board keeps its own panels); the tab
// chosen is this device's, the same for every board.
export class PanelFrame {
	constructor() {
		this.view = null;
		this.name = storedPanel();
		this.tabs = h("div.board-panel-tabs", { role: "tablist" }, PANELS.map(([id, title, iconName]) => h("button.board-panel-tab", { role: "tab", dataset: { panel: id }, on: { click: () => this.show(id) } }, icon(iconName), h("span", null, title))));
		this.host = h("div.board-panel-host");
		this.el = h("div.board-panel", null, this.tabs, this.host);
	}

	attach(view) {
		if (this.view && this.view !== view) this.view.panels.plan.save();
		this.view = view;
		this.render();
	}

	show(name) {
		if (this.view && this.name === "plan") this.view.panels.plan.save();
		this.name = name;
		try {
			localStorage.setItem(PANEL_KEY, name);
		}
		catch {
			// a private window: the tab lasts as long as the page
		}
		this.render();
	}

	render() {
		for (const b of this.tabs.children) b.setAttribute("aria-selected", String(b.dataset.panel === this.name));
		if (!this.view) {
			replace(this.host, h("div.muted.board-empty", null, "Open a board to see its layers, plan and history here."));
			return;
		}
		const panel = this.view.panels[this.name];
		if (panel.el.parentNode !== this.host) replace(this.host, panel.el);
		panel.render();
	}
}

export class LayersPanel {
	constructor(view) {
		this.view = view;
		this.model = view.model;
		this.list = h("div.board-layers", { role: "listbox", "aria-label": "Layers", on: { contextmenu: (e) => this.listMenu(e) } });
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
			const row = h("div.board-layer" + (active && active.id === layer.id ? ".active" : ""), {
				role: "option", tabindex: "0", "aria-selected": String(active && active.id === layer.id), dataset: { layer: layer.id },
				on: {
					click: () => this.view.setActiveLayer(layer.id),
					dblclick: () => this.rename(layer),
					contextmenu: (e) => this.layerMenu(e, layer),
					keydown: (e) => {
						if (e.target !== e.currentTarget) return;
						if (e.key === "Enter") this.view.setActiveLayer(layer.id);
						else if (e.key === "F2") this.rename(layer);
						else if (e.key === "ContextMenu" || (e.key === "F10" && e.shiftKey)) this.layerMenu(e.currentTarget, layer);
						else return;
						e.preventDefault();
					},
				},
			},
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

	// Several layers changed as one undo; nothing when none changes.
	updateAll(changes, label) {
		const ops = changes.map(([layer, fields]) => ({ op: "update_layer", id: layer.id, ...fields }));
		if (ops.length) this.model.edit(ops, label);
	}

	// A layer's menu (right button, Shift+F10): it becomes the active layer, then the rows of layerMenuRows.
	layerMenu(at, layer) {
		if (at.preventDefault) at.preventDefault();
		this.view.setActiveLayer(layer.id);
		const layers = this.model.layers;
		const count = this.model.all().filter((el) => el.layer === layer.id).length;
		menus.show({ at, label: layer.name, rows: layerMenuRows(layers, layer, count), pick: (row) => {
			if (row.id === "add") this.add(layer);
			else if (row.id === "rename") this.rename(layer);
			else if (row.id === "visible") this.update(layer, { visible: !layer.visible });
			else if (row.id === "locked") this.update(layer, { locked: !layer.locked });
			else if (row.id === "solo") this.updateAll(this.model.layers.filter((l) => l.visible !== (l.id === layer.id)).map((l) => [l, { visible: l.id === layer.id }]), "show only " + layer.name);
			else if (row.id === "select") this.view.input.select(this.model.all().filter((el) => el.layer === layer.id).map((el) => el.id), false);
			else if (row.id === "up") this.shift(1, layer);
			else if (row.id === "down") this.shift(-1, layer);
			else if (row.id === "merge") this.merge(layer);
			else if (row.id === "delete") this.remove(layer);
		} });
	}

	// The list's own menu, from the empty space under the layers.
	listMenu(event) {
		if (event.defaultPrevented || !this.model.loaded) return;
		event.preventDefault();
		menus.show({ at: event, label: "Layers", rows: layersMenuRows(this.model.layers), pick: (row) => {
			if (row.id === "add") this.add();
			else if (row.id === "showAll") this.updateAll(this.model.layers.filter((l) => !l.visible).map((l) => [l, { visible: true }]), "show all layers");
			else if (row.id === "unlockAll") this.updateAll(this.model.layers.filter((l) => l.locked).map((l) => [l, { locked: false }]), "unlock all layers");
		} });
	}

	// A new layer above `below` (default: the active one), made the active one.
	async add(below) {
		const under = below || this.view.activeLayer();
		const result = await this.model.edit([{ op: "add_layer", name: "Layer " + (this.model.layers.length + 1), above: under ? under.id : undefined }], "add layer");
		const id = result && result.done && /added layer (L\d+)/.exec(result.done[0] || "");
		if (id) this.view.setActiveLayer(id[1]);
	}

	shift(by, layer) {
		const which = layer || this.view.activeLayer();
		if (!which) return;
		const index = this.model.layers.findIndex((l) => l.id === which.id) + by;
		if (index < 0 || index >= this.model.layers.length) return;
		this.update(which, { index });
	}

	merge(layer) {
		const which = layer || this.view.activeLayer();
		if (which) this.model.edit([{ op: "merge_layer", id: which.id }], "merge layer");
	}

	async remove(layer) {
		const which = layer || this.view.activeLayer();
		if (!which || this.model.layers.length < 2) return;
		const ok = await confirmDialog({ title: "Delete layer", message: "Delete \"" + which.name + "\" and everything on it? Undo brings it back.", confirm: "Delete", danger: true });
		if (ok) this.model.edit([{ op: "delete_layer", id: which.id }], "delete layer");
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

// The board's changes this session, newest first, each revertible.
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
