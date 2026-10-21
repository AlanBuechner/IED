// tree.js - a virtualized tree view (Solution Explorer, git refs, file pickers): nodes from a provider, children loaded
// when a node first opens, only the rows in sight rendered, VS keys (Up/Down, Left/Right, Home/End, PageUp/PageDown,
// Enter, Shift+F10 / the menu key) and type-ahead.
//     const tree = new TreeView(el, { roots, children, render, open, menu, label });
//     tree.refresh();
// A node is any object with a unique `id`; `expandable` says it has children.

import { h, replace } from "./dom.js";

const OVERSCAN = 8;
const TYPE_AHEAD_MS = 900;

export class TreeView {
	// provider: {roots() -> nodes | Promise, children(node) -> nodes | Promise, render(node, row) fills the row's label,
	// label(node) for type-ahead, open(node, {preview}), menu(node, event), select(node)}
	constructor(el, provider) {
		this.el = el;
		this.provider = provider;
		this.expanded = new Set();
		this.cache = new Map();
		this.loading = new Set();
		this.rows = [];
		this.selected = null;
		this.typed = "";
		this.typedAt = 0;
		this.rowHeight = 22;
		this.spacer = h("div.tree-spacer");
		this.window = h("div.tree-rows");
		this.scroller = h("div.tree", { role: "tree", tabindex: "0", on: { scroll: () => this.draw(), keydown: (event) => this.key(event) } }, this.spacer, this.window);
		el.append(this.scroller);
		this.resizeObserver = new ResizeObserver(() => this.draw());
		this.resizeObserver.observe(this.scroller);
		this.roots = [];
	}

	dispose() {
		this.resizeObserver.disconnect();
	}

	focus() {
		this.scroller.focus({ preventScroll: true });
	}

	// Reads the roots again, and the children of every open node (what the server says now).
	async refresh() {
		this.cache.clear();
		this.roots = await this.provider.roots();
		await Promise.all([...this.expanded].map((id) => this.loadChildren(this.findNode(id))));
		this.rebuild();
	}

	// Forgets a node's children and reads them again when it is open.
	async invalidate(node) {
		if (!node) return;
		this.cache.delete(node.id);
		if (this.expanded.has(node.id)) await this.loadChildren(node);
		this.rebuild();
	}

	findNode(id) {
		const queue = [...this.roots];
		while (queue.length) {
			const node = queue.shift();
			if (node.id === id) return node;
			const kids = this.cache.get(node.id);
			if (kids) queue.push(...kids);
		}
		return null;
	}

	async loadChildren(node) {
		if (!node || this.cache.has(node.id)) return;
		this.loading.add(node.id);
		try {
			this.cache.set(node.id, await this.provider.children(node));
		}
		catch (e) {
			this.cache.set(node.id, [{ id: node.id + "\u0000error", error: e.message, depthOnly: true }]);
		}
		finally {
			this.loading.delete(node.id);
		}
	}

	async toggle(node, open) {
		if (!node || !node.expandable) return;
		const wantOpen = open === undefined ? !this.expanded.has(node.id) : open;
		if (wantOpen) {
			this.expanded.add(node.id);
			this.rebuild();
			await this.loadChildren(node);
		}
		else this.expanded.delete(node.id);
		this.rebuild();
	}

	collapseAll() {
		this.expanded.clear();
		this.rebuild();
	}

	// The visible rows, flattened: [{node, depth}].
	rebuild() {
		const rows = [];
		const visit = (nodes, depth) => {
			for (const node of nodes) {
				rows.push({ node, depth });
				if (!this.expanded.has(node.id)) continue;
				const kids = this.cache.get(node.id);
				if (kids) visit(kids, depth + 1);
				else if (this.loading.has(node.id)) rows.push({ node: { id: node.id + "\u0000loading", loading: true }, depth: depth + 1 });
			}
		};
		visit(this.roots, 0);
		this.rows = rows;
		if (this.selected && !rows.some((r) => r.node.id === this.selected)) this.selected = null;
		this.draw();
	}

	draw() {
		const probe = getComputedStyle(this.scroller).getPropertyValue("--row-h");
		this.rowHeight = parseInt(probe, 10) || 22;
		const height = this.scroller.clientHeight || 400;
		const first = Math.max(0, Math.floor(this.scroller.scrollTop / this.rowHeight) - OVERSCAN);
		const last = Math.min(this.rows.length, Math.ceil((this.scroller.scrollTop + height) / this.rowHeight) + OVERSCAN);
		this.spacer.style.height = this.rows.length * this.rowHeight + "px";
		this.window.style.transform = "translateY(" + first * this.rowHeight + "px)";
		replace(this.window, ...this.rows.slice(first, last).map((row, i) => this.rowElement(row, first + i)));
		const active = this.selected ? "tree-row-" + cssId(this.selected) : null;
		if (active) this.scroller.setAttribute("aria-activedescendant", active);
		else this.scroller.removeAttribute("aria-activedescendant");
	}

	rowElement(row, index) {
		const node = row.node;
		const indent = { "padding-left": 4 + row.depth * 16 + "px" };
		if (node.loading) return h("div.row.tree-row.muted", { style: indent }, h("span.tw"), "Loading...");
		if (node.error) return h("div.row.tree-row.c-error", { style: indent }, h("span.tw"), node.error);
		const open = this.expanded.has(node.id);
		const el = h("div.row.tree-row" + (node.id === this.selected ? ".sel" : ""), {
			role: "treeitem", id: "tree-row-" + cssId(node.id), "aria-level": String(row.depth + 1), "aria-selected": node.id === this.selected ? "true" : "false",
			"aria-expanded": node.expandable ? String(open) : null, style: indent, dataset: { index: String(index) },
			on: {
				click: (event) => this.click(node, event),
				dblclick: () => this.open(node, false),
				contextmenu: (event) => {
					event.preventDefault();
					this.select(node);
					if (this.provider.menu) this.provider.menu(node, event);
				},
			},
		}, h("span.tw", { on: { click: (event) => { event.stopPropagation(); this.toggle(node); } } }, node.expandable ? chevron(open) : null));
		this.provider.render(node, el);
		return el;
	}

	click(node, event) {
		this.select(node);
		this.scroller.focus({ preventScroll: true });
		if (node.expandable && event.detail === 1) this.toggle(node);
		else if (!node.expandable) this.open(node, true);
	}

	open(node, preview) {
		if (node.expandable) return;
		if (this.provider.open) this.provider.open(node, { preview });
	}

	select(node) {
		this.selected = node ? node.id : null;
		this.draw();
		if (node && this.provider.select) this.provider.select(node);
	}

	selectedNode() {
		const row = this.rows.find((r) => r.node.id === this.selected);
		return row ? row.node : null;
	}

	reveal(index) {
		const top = index * this.rowHeight;
		if (top < this.scroller.scrollTop) this.scroller.scrollTop = top;
		else if (top + this.rowHeight > this.scroller.scrollTop + this.scroller.clientHeight) this.scroller.scrollTop = top + this.rowHeight - this.scroller.clientHeight;
	}

	moveTo(index) {
		const clamped = Math.max(0, Math.min(this.rows.length - 1, index));
		const row = this.rows[clamped];
		if (!row || row.node.loading || row.node.error) return;
		this.select(row.node);
		this.reveal(clamped);
		this.draw();
	}

	key(event) {
		const index = this.rows.findIndex((r) => r.node.id === this.selected);
		const node = index >= 0 ? this.rows[index].node : null;
		const page = Math.max(1, Math.floor(this.scroller.clientHeight / this.rowHeight) - 1);
		const actions = {
			ArrowDown: () => this.moveTo(index + 1),
			ArrowUp: () => this.moveTo(index < 0 ? 0 : index - 1),
			Home: () => this.moveTo(0),
			End: () => this.moveTo(this.rows.length - 1),
			PageDown: () => this.moveTo(index + page),
			PageUp: () => this.moveTo(index - page),
			ArrowRight: () => node && (node.expandable && !this.expanded.has(node.id) ? this.toggle(node, true) : this.moveTo(index + 1)),
			ArrowLeft: () => node && (node.expandable && this.expanded.has(node.id) ? this.toggle(node, false) : this.moveTo(this.parentIndex(index))),
			Enter: () => node && (node.expandable ? this.toggle(node) : this.open(node, false)),
			ContextMenu: () => node && this.provider.menu && this.provider.menu(node, this.rowAnchor(index)),
		};
		let action = actions[event.key];
		if (event.key === "F10" && event.shiftKey) action = actions.ContextMenu;
		if (action && !event.ctrlKey && !event.altKey) {
			event.preventDefault();
			event.stopPropagation();
			action();
			return;
		}
		if (event.key.length === 1 && !event.ctrlKey && !event.altKey && !event.metaKey) this.typeAhead(event.key, index);
	}

	parentIndex(index) {
		const depth = index >= 0 ? this.rows[index].depth : 0;
		for (let i = index - 1; i >= 0; i--) if (this.rows[i].depth < depth) return i;
		return index;
	}

	rowAnchor(index) {
		return this.window.querySelector('[data-index="' + index + '"]') || this.scroller;
	}

	// Letters typed within a moment of each other jump to the next row starting with them.
	typeAhead(char, index) {
		const now = Date.now();
		this.typed = now - this.typedAt < TYPE_AHEAD_MS ? this.typed + char.toLowerCase() : char.toLowerCase();
		this.typedAt = now;
		const label = this.provider.label || ((n) => n.name || "");
		const start = this.typed.length > 1 ? Math.max(index, 0) : index + 1;
		for (let i = 0; i < this.rows.length; i++) {
			const at = (start + i) % this.rows.length;
			const node = this.rows[at].node;
			if (!node.loading && !node.error && label(node).toLowerCase().startsWith(this.typed)) return this.moveTo(at);
		}
		return undefined;
	}
}

function chevron(open) {
	const el = document.createElement("span");
	el.className = "ci";
	el.style.setProperty("--ci", "url(/codicons/" + (open ? "chevron-down" : "chevron-right") + ".svg)");
	return el;
}

function cssId(id) {
	return String(id).replace(/[^A-Za-z0-9_-]/g, (c) => "_" + c.charCodeAt(0).toString(16));
}
