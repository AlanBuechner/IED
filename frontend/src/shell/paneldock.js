// paneldock.js - the panel dock (docs/15 "Terminal dock"): the first loaded plugin's `panels` view (the terminal), full
// width under every activity. On a desktop it is open at a height or collapsed to the view's header row (the element the
// view marks `.panel-head`), both kept in the layout document (`doc.panel`) so every window of the device class agrees;
// a drag on its top edge resizes it. On a phone the same view is a slide-up drawer over the pane.

import { h } from "/ui-kit/dom.js";

export const PANEL_MIN_HEIGHT = 90;
const DEFAULT_HEIGHT = 260;
const KEEP_FOR_DOCUMENTS = 160;

export function panelState(doc) {
	const stored = doc && doc.panel ? doc.panel : {};
	return { open: stored.open === true, height: Number.isFinite(stored.height) ? stored.height : DEFAULT_HEIGHT };
}

export function withPanel(doc, change) {
	return Object.assign({}, doc, { panel: Object.assign(panelState(doc), change) });
}

// The height a drag leaves the dock at: at least PANEL_MIN_HEIGHT, and room left above it for the documents.
export function clampPanelHeight(height, available) {
	return Math.round(Math.max(PANEL_MIN_HEIGHT, Math.min(height, available - KEEP_FOR_DOCUMENTS)));
}

export class PanelDock {
	// `phone`: the drawer kind; `onChange` runs when it opens or closes (the phone chrome re-renders).
	constructor(shell, phone, onChange) {
		this.shell = shell;
		this.phone = phone;
		this.onChange = onChange || (() => {});
		this.host = h("div.panel-host");
		this.handle = phone ? null : h("div.panel-resize", { role: "separator", "aria-orientation": "horizontal", "aria-label": "Resize the panel", on: { pointerdown: (event) => this.drag(event) } });
		this.el = h("section.panel-dock" + (phone ? ".drawer" : ""), { "aria-label": "Panel", hidden: true }, this.handle, this.host);
		this.mounted = null;
		this.drawerOpen = false;
		this.listeners = new Set();
	}

	// The view of the panel contribution, mounted once per workspace's contributions.
	mount() {
		const panel = this.shell.contributions.panel;
		const key = panel ? panel.plugin.id + "/" + panel.id : null;
		if (this.mounted && this.mounted.key === key) return;
		this.release();
		if (!panel) return;
		const el = h("div.panel-view", { dataset: { panel: panel.id } });
		this.mounted = { key, el, instance: null, title: panel.title };
		this.host.replaceChildren(el);
		const view = panel.plugin.module ? panel.plugin.module[panel.view] : null;
		try {
			if (typeof view === "function") this.mounted.instance = view({ el, ied: this.shell.apiFor(panel.plugin), ref: "panel:" + panel.id, wsId: this.shell.wsId, phone: this.phone, panel: this.api() }) || null;
		}
		catch (e) {
			this.shell.report("Plugin '" + panel.plugin.id + "': the panel '" + panel.id + "' failed: " + e.message, e);
		}
	}

	api() {
		return {
			isOpen: () => this.isOpen(),
			setOpen: (open) => this.setOpen(open),
			toggle: () => this.setOpen(!this.isOpen()),
			onChange: (fn) => {
				this.listeners.add(fn);
				return () => this.listeners.delete(fn);
			},
		};
	}

	isOpen() {
		return this.phone ? this.drawerOpen : panelState(this.shell.doc).open;
	}

	setOpen(open) {
		if (this.phone) {
			this.drawerOpen = open;
			this.render();
			this.onChange();
		}
		else if (this.shell.layout) this.shell.layout.update((doc) => withPanel(doc, { open }));
		for (const fn of [...this.listeners]) fn(open);
	}

	render() {
		this.el.hidden = !this.mounted;
		if (!this.mounted) return;
		const open = this.isOpen();
		this.el.classList.toggle("collapsed", !open);
		if (this.phone) this.el.classList.toggle("shown", open);
		else this.el.style.height = open ? panelState(this.shell.doc).height + "px" : "";
		if (this.mounted.instance && typeof this.mounted.instance.layout === "function") requestAnimationFrame(() => this.mounted && this.mounted.instance && this.mounted.instance.layout());
	}

	drag(event) {
		if (!this.isOpen()) return;
		event.preventDefault();
		const startY = event.clientY;
		const startHeight = this.el.getBoundingClientRect().height;
		const available = this.el.parentElement ? this.el.parentElement.getBoundingClientRect().height : window.innerHeight;
		let height = startHeight;
		const move = (e) => {
			height = clampPanelHeight(startHeight + startY - e.clientY, available);
			this.el.style.height = height + "px";
		};
		const up = () => {
			window.removeEventListener("pointermove", move);
			window.removeEventListener("pointerup", up);
			if (this.shell.layout) this.shell.layout.update((doc) => withPanel(doc, { height }));
		};
		window.addEventListener("pointermove", move);
		window.addEventListener("pointerup", up);
	}

	release() {
		if (this.mounted && this.mounted.instance && typeof this.mounted.instance.dispose === "function") this.mounted.instance.dispose();
		this.mounted = null;
		this.host.replaceChildren();
	}

	title() {
		return this.mounted ? this.mounted.title : null;
	}
}
