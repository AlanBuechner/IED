// dock.js - a dock host's layout tree on screen (F-SHL-10, F-SHL-11, F-SHL-12, docs/15 "Docking"): splits with
// splitters, document groups with their tab strip on top, tool-window groups with a title and their tabs below (VS);
// a tab dragged along its strip slides into a new place (tabslide.js), as in a web browser; dragged off it, onto a group
// (center joins it, edges split it), another strip or an activity edge, with a preview of where it lands. A document
// strip ends in "+", which opens a new tab of its activity (its `newTab` command), as does a double click on its free end.
// Auto-hidden tool windows are tabs on strips along the edges; a click slides one out over the documents until the focus
// or a click goes elsewhere, or Escape.

import { h, icon, iconFrom, replace, append } from "/ui-kit/dom.js";
import { autoHiddenOf, hiddenIn, isGroup, isSplit, nodeAt, pathOf, refsOf } from "./layouttree.js";
import { TabSlide, alongStrip } from "./tabslide.js";

const MIN_PANE_PX = 80;
const DRAG_START_PX = 5;
const EDGE_SHARE = 0.25;
const ACTIVITY_EDGE_PX = 20;
const STRIP_REACH_PX = 20;   // how far off a tab strip a dropped tab still joins it
const EDGES = ["left", "right", "top", "bottom"];
// What a click or the focus may reach without the slid-out window going back: menus and popups, Monaco's widgets.
const NOT_AWAY = ".ah-tab, .popup, .dock-ghost, .shadow-root-host";

export class DockView {
	// `host`: {views, menus, emptyWell(), activate(ref), close(ref), pin(ref), move(ref, path, zone, index),
	// resize(path, sizes), autoHide(ref, hidden), resizeAutoHidden(ref, share), hold(on), focused(ref, path, well),
	// newTab(path, active) and newTabTitle when the activity opens new tabs (null: no "+"), titleActions(path, active):
	// the strip's buttons for its group's active tab, [{title, icon, base, command, run}]}
	constructor(host, container) {
		this.host = host;
		this.container = container;
		this.tree = null;
		this.flyout = null;   // the slid-out window: {ref, el, returnFocus, stop}
		this.root = h("div.dock");
		this.strips = Object.fromEntries(EDGES.map((edge) => [edge, h("div.ah-strip.ah-" + edge, { hidden: true, role: "tablist", "aria-label": "Auto-hidden tool windows" })]));
		this.center = h("div.dock-center", null, this.root);
		this.park = h("div.ah-park", { hidden: true });
		this.frame = h("div.dock-frame", { on: { focusin: (event) => this.focusIn(event) } }, this.strips.left, this.strips.top, this.center, this.strips.bottom, this.strips.right, this.park);
		replace(container, this.frame);
	}

	dispose() {
		if (this.flyout) this.dropFlyout();
	}

	// Builds the tree again; views keep their elements, the one with the focus keeps it, and what was scrolled keeps
	// its place (an element moved to a new parent loses it).
	render(tree) {
		this.tree = tree;
		const focused = document.activeElement;
		const scrolled = [...this.frame.querySelectorAll(".dock-body *")].filter((e) => e.scrollTop || e.scrollLeft).map((e) => [e, e.scrollTop, e.scrollLeft]);
		const el = this.node(tree, []) || this.emptyWellGroup();
		replace(this.root, el);
		this.renderAutoHidden();
		for (const [element, top, left] of scrolled) {
			if (!element.isConnected) continue;
			element.scrollTop = top;
			element.scrollLeft = left;
		}
		if (focused && focused !== document.activeElement && this.frame.contains(focused)) focused.focus({ preventScroll: true });
	}

	// Its activity is not shown, so neither is any of its views; the next render shows them again.
	hide() {
		for (const ref of this.tree ? refsOf(this.tree) : []) this.host.views.setVisible(ref, false);
	}

	// A tab's title or unsaved state changed: its tabs say so, without building the tree again.
	updateTab(entry) {
		for (const tab of this.frame.querySelectorAll(".tab, .tw-tab, .ah-tab")) {
			if (tab.dataset.ref !== entry.ref) continue;
			tab.querySelector(".name").textContent = entry.title;
			const shown = tab.querySelector(".tab-icon");
			const made = shown && tab.dataset.icon !== JSON.stringify(entry.icon) ? iconFrom(entry.icon, entry.base, "tab-icon") : null;
			if (made) {
				shown.replaceWith(made);
				tab.dataset.icon = JSON.stringify(entry.icon);
			}
			const dot = tab.querySelector(".dirty");
			if (entry.dirty && !dot && tab.classList.contains("tab")) tab.querySelector(".name").after(h("span.dirty", { title: "Unsaved changes" }));
			else if (!entry.dirty && dot) dot.remove();
		}
	}

	node(node, path) {
		if (isGroup(node)) return this.group(node, path);
		if (!isSplit(node)) return null;
		// The cells drawn share the room: a group that draws nothing (auto-hidden, or its views gone) leaves none empty.
		const cells = node.children.map((child, i) => ({ i, el: this.node(child, [...path, i]) })).filter((cell) => cell.el);
		if (cells.length === 0) return null;
		if (cells.length === 1) {
			cells[0].el.style.flex = "";
			return cells[0].el;
		}
		const split = h("div.dock-split.split-" + node.dir, { dataset: { path: path.join(",") } });
		const total = cells.reduce((sum, cell) => sum + node.sizes[cell.i], 0);
		cells.forEach((cell, k) => {
			if (k > 0) split.append(this.sash(node, path, cells[k - 1].i, cell.i));
			cell.el.style.flex = node.sizes[cell.i] / total + " 1 0px";
			cell.el.dataset.index = String(cell.i);
			split.append(cell.el);
		});
		return split;
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Groups and tabs

	group(group, path) {
		const views = this.host.views;
		const visible = group.tabs.filter((ref) => views.has(ref) && !hiddenIn(group, ref));
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
		const plus = this.host.newTab ? h("button.ib.tabs-new", { title: this.host.newTabTitle, "aria-label": this.host.newTabTitle, on: { click: () => this.host.newTab([], null) } }, icon("add")) : null;
		return h("div.dock-group.well", { dataset: { path: "" } }, h("div.tabs", null, plus), h("div.dock-body", null, this.host.emptyWell()));
	}

	documentTabs(group, visible, active, path) {
		const newTab = this.host.newTab ? () => this.host.newTab(path, active) : null;
		const strip = h("div.tabs", { role: "tablist", on: { dblclick: (event) => newTab && (event.target === strip || event.target.matches(".tabs-tail")) && newTab() } });
		for (const ref of visible) strip.append(this.tab(ref, ref === active, group.preview === ref, path, "tab"));
		if (newTab) strip.append(h("button.ib.tabs-new", { title: this.host.newTabTitle, "aria-label": this.host.newTabTitle, on: { click: newTab } }, icon("add")));
		const actions = active && this.host.titleActions ? this.host.titleActions(path, active) : [];
		strip.append(h("div.tabs-tail", null,
			...actions.map((action) => h("button.ib.tabs-action", { title: action.title, "aria-label": action.title, dataset: { command: action.command }, on: { click: action.run } }, iconFrom(action.icon, action.base) || h("span", null, action.title))),
			visible.length ? h("button.ib.tabs-menu", { title: "Open documents", "aria-label": "Open documents", on: { click: (event) => this.listTabs(event, visible) } }, icon("chevron-down")) : null));
		return strip;
	}

	// VS's title bar: the pin auto-hides the window shown, or (`hidden`, slid out) docks it back into its group.
	toolTitle(active, hidden) {
		const entry = active ? this.host.views.get(active) : null;
		const pin = hidden
			? { title: "Dock", glyph: "pin", click: () => this.dockFlyout(active) }
			: { title: "Auto Hide", glyph: "pinned", click: () => this.host.autoHide(active, true) };
		return h("div.panel-title", { on: { contextmenu: (event) => entry && this.tabMenu(event, active) } },
			h("span.grow.ellip", null, entry ? entry.title : ""),
			entry ? h("button.ib", { title: pin.title, "aria-label": pin.title + " " + entry.title, dataset: { pin: hidden ? "dock" : "hide" }, on: { click: pin.click } }, icon(pin.glyph)) : null,
			entry ? h("button.ib", { title: "Close", "aria-label": "Close " + entry.title, on: { click: () => this.host.close(active) } }, icon("close")) : null);
	}

	tabMenu(event, ref) {
		event.preventDefault();
		this.host.menus.show("tab/context", event, { ref });
	}

	toolTabs(visible, active, path) {
		const strip = h("div.tw-tabs", { role: "tablist" });
		for (const ref of visible) strip.append(this.tab(ref, ref === active, false, path, "tw-tab"));
		return strip;
	}

	// A link on a tethered tab (docs/15 "Tethered tabs"), naming what it shows with.
	tetherGlyph(ref) {
		const partners = this.host.tetheredWith ? this.host.tetheredWith(ref) : [];
		if (!partners.length) return null;
		const names = partners.map((p) => (this.host.views.get(p) || { title: p }).title).join(", ");
		return h("span.tether", { title: "Tethered with " + names, "aria-label": "Tethered with " + names }, icon("link"));
	}

	tab(ref, on, preview, path, cls) {
		const entry = this.host.views.get(ref);
		const document = cls === "tab";
		const el = h("div." + cls + (on ? ".on" : "") + (preview ? ".preview" : ""), {
			role: "tab", tabindex: on ? "0" : "-1", "aria-selected": on ? "true" : "false", title: ref, dataset: { ref, icon: JSON.stringify(entry.icon) },
			on: {
				pointerdown: (event) => this.press(event, ref, path),
				click: () => this.host.activate(ref),
				dblclick: () => this.host.pin(ref),
				auxclick: (event) => {
					if (event.button === 1) this.host.close(ref);
				},
				contextmenu: (event) => this.tabMenu(event, ref),
				keydown: (event) => this.tabKey(event, ref),
			},
		},
		document ? iconFrom(entry.icon, entry.base, "tab-icon") : null,
		h("span.name", null, entry.title),
		document && entry.dirty ? h("span.dirty", { title: "Unsaved changes" }) : null,
		this.tetherGlyph(ref),
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
		for (const el of this.frame.querySelectorAll(".dock-group.focused")) if (el !== groupEl) el.classList.remove("focused");
		if (!groupEl) return;
		groupEl.classList.add("focused");
		const group = groupEl.dataset.path !== undefined ? nodeAt(this.tree, parsePath(groupEl.dataset.path)) : null;
		const ref = groupEl.classList.contains("ah-flyout") && this.flyout ? this.flyout.ref : group && group.active;
		if (isGroup(group)) this.host.focused(ref, parsePath(groupEl.dataset.path), Boolean(group.well));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Dragging a tab

	// A tab pressed: dragged along its strip it slides into a new place (a browser's tabs); dragged off the strip it is
	// docked where it is dropped, and brought back along it, it slides again. `path`: its group's, for a tab of a strip
	// (an auto-hidden one has none). The layout holds still from the first move to the drop.
	press(event, ref, path) {
		if (event.button !== 0 || event.target.closest("button")) return;
		const start = { x: event.clientX, y: event.clientY };
		const tabEl = event.currentTarget;
		const strip = path && tabEl.parentElement.matches(".tabs, .tw-tabs") ? tabEl.parentElement : null;
		let slide = null;
		let drag = null;
		const move = (e) => {
			if (!drag && !slide && Math.hypot(e.clientX - start.x, e.clientY - start.y) < DRAG_START_PX) return;
			if (!drag && !slide) this.host.hold(true);
			if (strip && alongStrip(strip, e.clientX, e.clientY)) {
				if (drag) this.endDrag(drag, false);
				drag = null;
				if (!slide) slide = new TabSlide(strip, tabEl, start.x);
				slide.move(e.clientX);
				return;
			}
			if (slide) slide.end();
			slide = null;
			if (!drag) drag = this.beginDrag(ref);
			this.dragOver(drag, e.clientX, e.clientY);
		};
		const finish = (dropIt) => {
			window.removeEventListener("pointermove", move);
			window.removeEventListener("pointerup", up);
			window.removeEventListener("keydown", key, true);
			const moved = slide || drag;
			if (slide) this.endSlide(slide, ref, path, dropIt);
			if (drag) this.endDrag(drag, dropIt);
			if (moved) this.host.hold(false);
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

	endSlide(slide, ref, path, dropIt) {
		const before = slide.before();
		slide.end();
		if (dropIt && before !== undefined) this.host.move(ref, path, "center", this.treeIndex(path, before));
	}

	beginDrag(ref) {
		this.hideFlyout(false);
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
		if (dropIt && drag.target) this.host.move(drag.ref, drag.target.path, drag.target.zone === "insert" ? "center" : drag.target.zone, drag.target.index);
	}

	// Where a tab dropped at (x, y) goes: {path, zone, index, rect}, or null outside this dock. A tab strip comes first -
	// over it or just beside it, the tab joins its group between the tabs - then the activity's edges, then the zones of
	// the group's body (its edges split it, its center joins it).
	targetAt(x, y) {
		const box = this.root.getBoundingClientRect();
		if (x < box.left || x > box.right || y < box.top || y > box.bottom) return null;
		const strip = this.stripAt(x, y);
		if (strip) {
			const path = parsePath(strip.parentElement.dataset.path);
			const before = insertionBefore(strip, x);
			return { path, zone: "insert", index: this.treeIndex(path, before), rect: insertionMark(strip, before) };
		}
		const edge = activityEdge(box, x, y);
		if (edge) return { path: [], zone: edge, index: null, rect: edgeRect(box, edge, EDGE_SHARE) };
		const hit = document.elementFromPoint(x, y);
		const groupEl = hit ? hit.closest(".dock-group") : null;
		if (!groupEl || !this.root.contains(groupEl)) return null;
		const path = parsePath(groupEl.dataset.path);
		const groupRect = groupEl.getBoundingClientRect();
		const body = groupEl.querySelector(":scope > .dock-body");
		const zone = zoneIn(body ? body.getBoundingClientRect() : groupRect, x, y);
		return { path, zone, index: null, rect: zone === "center" ? groupRect : edgeRect(groupRect, zone, 0.5) };
	}

	// The tab strip a tab dropped at (x, y) joins: the one it is over, or within STRIP_REACH_PX of one - a drag aimed at
	// a strip often ends a little off it.
	stripAt(x, y) {
		for (const strip of this.root.querySelectorAll(".dock-group > .tabs, .dock-group > .tw-tabs")) {
			const rect = strip.getBoundingClientRect();
			if (rect.width && x >= rect.left && x <= rect.right && y >= rect.top - STRIP_REACH_PX && y <= rect.bottom + STRIP_REACH_PX) return strip;
		}
		return null;
	}

	// The index in the group's tabs (hidden ones included) of the tab `before`, or the end.
	treeIndex(path, before) {
		const group = nodeAt(this.tree, path);
		if (!isGroup(group)) return null;
		const at = before ? group.tabs.indexOf(before) : -1;
		return at < 0 ? group.tabs.length : at;
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Auto-hidden tool windows: strips of tabs on the edges, by group; one window slid out at a time

	renderAutoHidden() {
		const views = this.host.views;
		const hidden = autoHiddenOf(this.tree).map((group) => ({ group, refs: group.autoHidden.filter((ref) => views.has(ref)) })).filter((g) => g.refs.length);
		if (this.flyout && !hidden.some((g) => g.refs.includes(this.flyout.ref))) this.dropFlyout();
		const shown = this.flyout ? this.flyout.ref : null;
		for (const edge of EDGES) {
			const clusters = hidden.filter((g) => g.group.autoHide === edge).map((g) => h("div.ah-group", null, ...g.refs.map((ref) => this.stripTab(ref, ref === shown))));
			replace(this.strips[edge], ...clusters);
			this.strips[edge].hidden = clusters.length === 0;
		}
		for (const { refs } of hidden) for (const ref of refs) if (ref !== shown) this.parkView(ref);
		if (shown) this.drawFlyout(hidden.find((g) => g.refs.includes(shown)).group);
	}

	stripTab(ref, on) {
		const entry = this.host.views.get(ref);
		return h("div.ah-tab" + (on ? ".on" : ""), {
			role: "tab", tabindex: "0", "aria-selected": on ? "true" : "false", title: entry.title, dataset: { ref },
			on: {
				pointerdown: (event) => this.press(event, ref),
				click: () => this.stripClick(ref),
				contextmenu: (event) => this.tabMenu(event, ref),
				keydown: (event) => this.tabKey(event, ref),
			},
		}, h("span.name", null, entry.title));
	}

	stripClick(ref) {
		if (this.flyout && this.flyout.ref === ref) this.hideFlyout(true);
		else this.host.activate(ref);
	}

	// An auto-hidden view that is not slid out waits, hidden, in the frame.
	parkView(ref) {
		const entry = this.host.views.get(ref);
		entry.el.hidden = true;
		if (entry.el.parentElement !== this.park) this.park.append(entry.el);
		this.host.views.setVisible(ref, false);
	}

	// Slides out the auto-hidden window `ref`; the shell asks this for a tab that is activated or shown.
	showFlyout(ref) {
		const owner = this.tree ? autoHiddenOf(this.tree).find((g) => hiddenIn(g, ref)) : null;
		if (!owner || !this.host.views.has(ref)) return;
		if (!this.flyout) {
			const focused = document.activeElement;
			this.flyout = { ref, el: null, returnFocus: focused && !focused.closest(".ah-tab") ? focused : null, stop: this.watchAway() };
		}
		this.flyout.ref = ref;
		this.renderAutoHidden();
	}

	// Slides it back; with `restoreFocus`, the focus goes back where it was before (Escape, its strip tab again).
	hideFlyout(restoreFocus) {
		if (!this.flyout) return;
		const flyout = this.dropFlyout();
		this.renderAutoHidden();
		if (restoreFocus && flyout.returnFocus && flyout.returnFocus.isConnected) flyout.returnFocus.focus({ preventScroll: true });
	}

	dropFlyout() {
		const flyout = this.flyout;
		this.flyout = null;
		flyout.stop();
		if (flyout.el) flyout.el.remove();
		return flyout;
	}

	dockFlyout(ref) {
		this.host.autoHide(ref, false);
		requestAnimationFrame(() => this.host.views.focus(ref));
	}

	drawFlyout(group) {
		const ref = this.flyout.ref;
		const entry = this.host.views.get(ref);
		const edge = group.autoHide;
		let el = this.flyout.el;
		if (!el || el.dataset.edge !== edge) {
			if (el) el.remove();
			el = h("section.dock-group.tool.ah-flyout", { dataset: { edge } }, h("div.panel-title"), h("div.dock-body"), this.flyoutSash());
			this.flyout.el = el;
			this.center.append(el);
		}
		el.dataset.path = pathOf(this.tree, group).join(",");
		el.setAttribute("aria-label", entry.title);
		el.style.setProperty("--ah-size", group.autoHideSize * 100 + "%");
		el.firstElementChild.replaceWith(this.toolTitle(ref, true));
		const body = el.querySelector(":scope > .dock-body");
		entry.el.hidden = false;
		if (entry.el.parentElement !== body) body.replaceChildren(entry.el);
		this.host.views.setVisible(ref, true);
	}

	// A click or the focus anywhere else slides the window back (a frame taking the focus only blurs the window), and so
	// does an Escape no view or binding took.
	watchAway() {
		const away = (event) => {
			const target = event.target;
			if (!this.flyout || !(target instanceof Element)) return;
			if ((this.flyout.el && this.flyout.el.contains(target)) || target.closest(NOT_AWAY)) return;
			this.hideFlyout(false);
		};
		const blurred = () => setTimeout(() => {
			const active = document.activeElement;
			if (this.flyout && active && active.tagName === "IFRAME" && !(this.flyout.el && this.flyout.el.contains(active))) this.hideFlyout(false);
		}, 0);
		const escape = (event) => {
			if (event.key !== "Escape" || event.defaultPrevented) return;
			event.preventDefault();
			this.hideFlyout(true);
		};
		document.addEventListener("pointerdown", away, true);
		document.addEventListener("focusin", away, true);
		document.addEventListener("keydown", escape);
		window.addEventListener("blur", blurred);
		return () => {
			document.removeEventListener("pointerdown", away, true);
			document.removeEventListener("focusin", away, true);
			document.removeEventListener("keydown", escape);
			window.removeEventListener("blur", blurred);
		};
	}

	flyoutSash() {
		const sash = h("div.ah-sash", { role: "separator", "aria-label": "Resize" });
		sash.addEventListener("pointerdown", (event) => this.resizeFlyout(event, sash));
		return sash;
	}

	resizeFlyout(event, sash) {
		if (event.button !== 0 || !this.flyout) return;
		event.preventDefault();
		const { el, ref } = this.flyout;
		const across = el.dataset.edge === "left" || el.dataset.edge === "right";
		const grows = el.dataset.edge === "left" || el.dataset.edge === "top" ? 1 : -1;
		const room = across ? this.center.clientWidth : this.center.clientHeight;
		const start = across ? event.clientX : event.clientY;
		const startSize = across ? el.offsetWidth : el.offsetHeight;
		let size = startSize;
		sash.setPointerCapture(event.pointerId);
		this.host.hold(true);
		const move = (e) => {
			size = Math.max(Math.min(MIN_PANE_PX, room), Math.min(room, startSize + grows * ((across ? e.clientX : e.clientY) - start)));
			el.style.setProperty("--ah-size", size + "px");
		};
		const up = () => {
			sash.removeEventListener("pointermove", move);
			sash.removeEventListener("pointerup", up);
			sash.removeEventListener("pointercancel", up);
			this.host.hold(false);
			if (room > 0) this.host.resizeAutoHidden(ref, size / room);
		};
		sash.addEventListener("pointermove", move);
		sash.addEventListener("pointerup", up);
		sash.addEventListener("pointercancel", up);
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
		const grown = Number(cellA.style.flexGrow) + Number(cellB.style.flexGrow);
		let ratio = sizeA / (sizeA + sizeB);
		sash.setPointerCapture(event.pointerId);
		sash.classList.add("active");
		this.host.hold(true);
		const move = (e) => {
			const delta = (row ? e.clientX : e.clientY) - start;
			const px = Math.max(MIN_PANE_PX, Math.min(sizeA + sizeB - MIN_PANE_PX, sizeA + delta));
			ratio = px / (sizeA + sizeB);
			cellA.style.flexGrow = String(grown * ratio);
			cellB.style.flexGrow = String(grown * (1 - ratio));
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

// Where a tab dropped on `strip` before `before` (null: at the end) lands: a thin mark between the tabs.
function insertionMark(strip, before) {
	const tabs = [...strip.querySelectorAll(".tab, .tw-tab")];
	const box = strip.getBoundingClientRect();
	const at = before ? tabs.find((el) => el.dataset.ref === before) : tabs[tabs.length - 1];
	const x = at ? (before ? at.getBoundingClientRect().left : at.getBoundingClientRect().right) : box.left;
	return { left: x - 1, top: box.top, width: 3, height: box.height };
}

// The ref of the tab a tab dropped at x goes before, or null for the end of the strip.
function insertionBefore(strip, x) {
	const tab = [...strip.querySelectorAll(".tab, .tw-tab")].find((el) => {
		const rect = el.getBoundingClientRect();
		return x < rect.left + rect.width / 2;
	});
	return tab ? tab.dataset.ref : null;
}
