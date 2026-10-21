// dock.js - a dock host's layout tree on screen (F-SHL-10, F-SHL-12, docs/15 "Docking"): splits with splitters, document
// groups with their tab strip on top, tool-window groups with a title and their tabs below (VS); dragging a tab onto a
// group (center joins it, edges split it) or an activity edge, with a preview of where it lands.

import { h, icon, iconFrom, replace, append } from "/ui-kit/dom.js";
import { isGroup, isSplit, nodeAt } from "./layouttree.js";

const MIN_PANE_PX = 80;
const DRAG_START_PX = 5;
const EDGE_SHARE = 0.25;
const ACTIVITY_EDGE_PX = 20;

export class DockView {
	// `host`: {views, menus, emptyWell(), activate(ref), close(ref), pin(ref), change(fn), hold(on), focused(ref)}
	constructor(host, container) {
		this.host = host;
		this.container = container;
		this.tree = null;
		this.root = h("div.dock", { on: { focusin: (event) => this.focusIn(event) } });
		replace(container, this.root);
	}

	// Builds the tree again; views keep their elements, and the one with the focus keeps it.
	render(tree) {
		this.tree = tree;
		const focused = document.activeElement;
		const el = this.node(tree, []) || this.emptyWellGroup();
		replace(this.root, el);
		if (focused && focused !== document.activeElement && this.root.contains(focused)) focused.focus({ preventScroll: true });
	}

	// A tab's title or unsaved state changed: its tabs say so, without building the tree again.
	updateTab(entry) {
		for (const tab of this.root.querySelectorAll(".tab, .tw-tab")) {
			if (tab.dataset.ref !== entry.ref) continue;
			tab.querySelector(".name").textContent = entry.title;
			const dot = tab.querySelector(".dirty");
			if (entry.dirty && !dot && tab.classList.contains("tab")) tab.querySelector(".name").after(h("span.dirty", { title: "Unsaved changes" }));
			else if (!entry.dirty && dot) dot.remove();
		}
	}

	node(node, path) {
		if (isGroup(node)) return this.group(node, path);
		if (!isSplit(node)) return null;
		const split = h("div.dock-split.split-" + node.dir, { dataset: { path: path.join(",") } });
		let previous = -1;
		node.children.forEach((child, i) => {
			const cell = this.node(child, [...path, i]);
			if (!cell) return;
			if (previous >= 0) split.append(this.sash(node, path, previous, i));
			cell.style.flex = node.sizes[i] + " 1 0px";
			cell.dataset.index = String(i);
			split.append(cell);
			previous = i;
		});
		if (split.children.length === 0) return null;
		if (split.children.length === 1) return split.firstElementChild;
		return split;
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Groups and tabs

	group(group, path) {
		const views = this.host.views;
		const visible = group.tabs.filter((ref) => views.has(ref));
		if (visible.length === 0 && !group.well) return null;
		const active = visible.includes(group.active) ? group.active : visible[0] || null;
		const el = h("div.dock-group" + (group.well ? ".well" : ".tool"), { dataset: { path: path.join(",") } });
		const body = h("div.dock-body");
		for (const ref of visible) {
			const entry = views.get(ref);
			entry.el.hidden = ref !== active;
			body.append(entry.el);
			views.setVisible(ref, ref === active);
		}
		if (visible.length === 0) body.append(this.host.emptyWell());
		if (group.well) el.append(this.documentTabs(group, visible, active, path), body);
		else append(el, [this.toolTitle(active), body, visible.length > 1 ? this.toolTabs(visible, active, path) : null]);
		return el;
	}

	emptyWellGroup() {
		return h("div.dock-group.well", { dataset: { path: "" } }, h("div.tabs"), h("div.dock-body", null, this.host.emptyWell()));
	}

	documentTabs(group, visible, active, path) {
		const strip = h("div.tabs", { role: "tablist" });
		for (const ref of visible) strip.append(this.tab(ref, ref === active, group.preview === ref, path, "tab"));
		strip.append(h("div.tabs-tail", null, visible.length ? h("button.ib.tabs-menu", { title: "Open documents", "aria-label": "Open documents", on: { click: (event) => this.listTabs(event, visible) } }, icon("chevron-down")) : null));
		return strip;
	}

	toolTitle(active) {
		const entry = active ? this.host.views.get(active) : null;
		return h("div.panel-title", null,
			h("span.grow.ellip", null, entry ? entry.title : ""),
			entry ? h("button.ib", { title: "Close", "aria-label": "Close " + entry.title, on: { click: () => this.host.close(active) } }, icon("close")) : null);
	}

	toolTabs(visible, active, path) {
		const strip = h("div.tw-tabs", { role: "tablist" });
		for (const ref of visible) strip.append(this.tab(ref, ref === active, false, path, "tw-tab"));
		return strip;
	}

	tab(ref, on, preview, path, cls) {
		const entry = this.host.views.get(ref);
		const document = cls === "tab";
		const el = h("div." + cls + (on ? ".on" : "") + (preview ? ".preview" : ""), {
			role: "tab", tabindex: on ? "0" : "-1", "aria-selected": on ? "true" : "false", title: ref, dataset: { ref },
			on: {
				pointerdown: (event) => this.press(event, ref),
				click: () => this.host.activate(ref),
				dblclick: () => this.host.pin(ref),
				auxclick: (event) => {
					if (event.button === 1) this.host.close(ref);
				},
				contextmenu: (event) => {
					event.preventDefault();
					this.host.menus.show("tab/context", event, { ref });
				},
				keydown: (event) => this.tabKey(event, ref),
			},
		},
		document ? iconFrom(entry.icon, entry.base) : null,
		h("span.name", null, entry.title),
		document && entry.dirty ? h("span.dirty", { title: "Unsaved changes" }) : null,
		document ? h("button.x", { title: "Close", "aria-label": "Close " + entry.title, on: { click: (event) => { event.stopPropagation(); this.host.close(ref); } } }, icon("close")) : null);
		return el;
	}

	tabKey(event, ref) {
		if (event.key === "Enter" || event.key === " ") {
			event.preventDefault();
			this.host.activate(ref);
		}
		else if (event.key === "ContextMenu" || (event.key === "F10" && event.shiftKey)) {
			event.preventDefault();
			this.host.menus.show("tab/context", event.currentTarget, { ref });
		}
	}

	listTabs(event, refs) {
		const rows = refs.map((ref) => {
			const entry = this.host.views.get(ref);
			return { kind: "item", label: entry.title, icon: entry.icon, base: entry.base, enabled: true, ref };
		});
		this.host.menus.host.show({ rows, at: event.currentTarget, pick: (row) => this.host.activate(row.ref), label: "Open documents" });
	}

	focusIn(event) {
		const groupEl = event.target.closest(".dock-group");
		for (const el of this.root.querySelectorAll(".dock-group.focused")) if (el !== groupEl) el.classList.remove("focused");
		if (!groupEl) return;
		groupEl.classList.add("focused");
		const group = groupEl.dataset.path !== undefined ? nodeAt(this.tree, parsePath(groupEl.dataset.path)) : null;
		if (isGroup(group)) this.host.focused(group.active, parsePath(groupEl.dataset.path), Boolean(group.well));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Dragging a tab

	press(event, ref) {
		if (event.button !== 0 || event.target.closest("button")) return;
		const start = { x: event.clientX, y: event.clientY };
		let drag = null;
		const move = (e) => {
			if (!drag && Math.hypot(e.clientX - start.x, e.clientY - start.y) < DRAG_START_PX) return;
			if (!drag) drag = this.beginDrag(ref);
			this.dragOver(drag, e.clientX, e.clientY);
		};
		const finish = (dropIt) => {
			window.removeEventListener("pointermove", move);
			window.removeEventListener("pointerup", up);
			window.removeEventListener("keydown", key, true);
			if (drag) this.endDrag(drag, dropIt);
		};
		const up = () => finish(true);
		const key = (e) => {
			if (e.key !== "Escape") return;
			e.preventDefault();
			e.stopPropagation();
			finish(false);
		};
		window.addEventListener("pointermove", move);
		window.addEventListener("pointerup", up);
		window.addEventListener("keydown", key, true);
	}

	beginDrag(ref) {
		this.host.hold(true);
		const entry = this.host.views.get(ref);
		const overlay = h("div.dock-drop", { hidden: true });
		const ghost = h("div.dock-ghost", null, entry ? entry.title : ref);
		document.body.append(overlay, ghost);
		this.root.classList.add("dragging");
		return { ref, overlay, ghost, target: null };
	}

	dragOver(drag, x, y) {
		drag.ghost.style.left = x + 12 + "px";
		drag.ghost.style.top = y + 8 + "px";
		drag.target = this.targetAt(x, y);
		const rect = drag.target ? drag.target.rect : null;
		drag.overlay.hidden = !rect;
		if (!rect) return;
		Object.assign(drag.overlay.style, { left: rect.left + "px", top: rect.top + "px", width: rect.width + "px", height: rect.height + "px" });
		drag.overlay.dataset.zone = drag.target.zone;
	}

	endDrag(drag, dropIt) {
		drag.overlay.remove();
		drag.ghost.remove();
		this.root.classList.remove("dragging");
		this.host.hold(false);
		if (dropIt && drag.target) this.host.move(drag.ref, drag.target.path, drag.target.zone, drag.target.index);
	}

	// Where a tab dropped at (x, y) goes: {path, zone, index, rect}, or null outside this dock.
	targetAt(x, y) {
		const box = this.root.getBoundingClientRect();
		if (x < box.left || x > box.right || y < box.top || y > box.bottom) return null;
		const edge = activityEdge(box, x, y);
		if (edge) return { path: [], zone: edge, index: null, rect: edgeRect(box, edge, EDGE_SHARE) };
		const hit = document.elementFromPoint(x, y);
		const groupEl = hit ? hit.closest(".dock-group") : null;
		if (!groupEl || !this.root.contains(groupEl)) return null;
		const path = parsePath(groupEl.dataset.path);
		const strip = hit.closest(".tabs, .tw-tabs");
		const groupRect = groupEl.getBoundingClientRect();
		if (strip) return { path, zone: "center", index: this.treeIndex(path, insertionBefore(strip, x)), rect: groupRect };
		const zone = zoneIn(groupRect, x, y);
		return { path, zone, index: null, rect: zone === "center" ? groupRect : edgeRect(groupRect, zone, 0.5) };
	}

	// The index in the group's tabs (hidden ones included) of the tab `before`, or the end.
	treeIndex(path, before) {
		const group = nodeAt(this.tree, path);
		if (!isGroup(group)) return null;
		const at = before ? group.tabs.indexOf(before) : -1;
		return at < 0 ? group.tabs.length : at;
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Splitters

	sash(split, path, a, b) {
		const sash = h("div.dock-sash.split-" + split.dir, { role: "separator", "aria-orientation": split.dir === "row" ? "vertical" : "horizontal" });
		sash.addEventListener("pointerdown", (event) => this.resize(event, sash, split, path, a, b));
		return sash;
	}

	resize(event, sash, split, path, a, b) {
		if (event.button !== 0) return;
		event.preventDefault();
		const row = split.dir === "row";
		const cellA = sash.previousElementSibling;
		const cellB = sash.nextElementSibling;
		const sizeA = row ? cellA.getBoundingClientRect().width : cellA.getBoundingClientRect().height;
		const sizeB = row ? cellB.getBoundingClientRect().width : cellB.getBoundingClientRect().height;
		const start = row ? event.clientX : event.clientY;
		const shared = split.sizes[a] + split.sizes[b];
		let ratio = sizeA / (sizeA + sizeB);
		sash.setPointerCapture(event.pointerId);
		sash.classList.add("active");
		this.host.hold(true);
		const move = (e) => {
			const delta = (row ? e.clientX : e.clientY) - start;
			const px = Math.max(MIN_PANE_PX, Math.min(sizeA + sizeB - MIN_PANE_PX, sizeA + delta));
			ratio = px / (sizeA + sizeB);
			cellA.style.flexGrow = String(shared * ratio);
			cellB.style.flexGrow = String(shared * (1 - ratio));
		};
		const up = () => {
			sash.removeEventListener("pointermove", move);
			sash.removeEventListener("pointerup", up);
			sash.removeEventListener("pointercancel", up);
			sash.classList.remove("active");
			this.host.hold(false);
			const sizes = [...split.sizes];
			sizes[a] = shared * ratio;
			sizes[b] = shared * (1 - ratio);
			this.host.resize(path, sizes);
		};
		sash.addEventListener("pointermove", move);
		sash.addEventListener("pointerup", up);
		sash.addEventListener("pointercancel", up);
	}
}

function parsePath(text) {
	return text ? text.split(",").map(Number) : [];
}

function activityEdge(box, x, y) {
	if (x - box.left < ACTIVITY_EDGE_PX) return "left";
	if (box.right - x < ACTIVITY_EDGE_PX) return "right";
	if (y - box.top < ACTIVITY_EDGE_PX) return "top";
	if (box.bottom - y < ACTIVITY_EDGE_PX) return "bottom";
	return null;
}

// The zone of a group under (x, y): the edge it is nearest to within a quarter of the group, else the center.
function zoneIn(rect, x, y) {
	const distances = [
		["left", (x - rect.left) / rect.width],
		["right", (rect.right - x) / rect.width],
		["top", (y - rect.top) / rect.height],
		["bottom", (rect.bottom - y) / rect.height],
	].filter(([, d]) => d < EDGE_SHARE).sort((p, q) => p[1] - q[1]);
	return distances.length ? distances[0][0] : "center";
}

function edgeRect(rect, zone, share) {
	const w = rect.width * share;
	const hgt = rect.height * share;
	if (zone === "left") return { left: rect.left, top: rect.top, width: w, height: rect.height };
	if (zone === "right") return { left: rect.right - w, top: rect.top, width: w, height: rect.height };
	if (zone === "top") return { left: rect.left, top: rect.top, width: rect.width, height: hgt };
	return { left: rect.left, top: rect.bottom - hgt, width: rect.width, height: hgt };
}

// The ref of the tab a tab dropped at x goes before, or null for the end of the strip.
function insertionBefore(strip, x) {
	const tab = [...strip.querySelectorAll(".tab, .tw-tab")].find((el) => {
		const rect = el.getBoundingClientRect();
		return x < rect.left + rect.width / 2;
	});
	return tab ? tab.dataset.ref : null;
}
