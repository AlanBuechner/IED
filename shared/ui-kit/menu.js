// menu.js - popup menus (context menus, menu bar menus, dropdowns): rows of items, submenus and separators, keyboard
// driven (Up/Down, Home/End, Enter/Space, Right/Left for submenus, Esc), closed by a click outside. On a phone width
// a menu is a bottom sheet. One MenuHost shows one menu at a time.
//     host.show({ rows, at: event, pick: (row) => run(row), submenu: (menuId) => rowsOf(menuId) });

import { h, icon, iconFrom } from "./dom.js";

const PHONE = "(max-width: 719px)";

export class MenuHost {
	constructor(root) {
		this.root = root || document.body;
		this.open = null;
		this.onOutside = (event) => {
			if (this.open && !this.open.contains(event.target)) this.close();
		};
	}

	// options: {rows, at: MouseEvent | Element | {x, y}, pick(row), submenu(menuId) -> rows, onClose(), label, minWidth}
	show(options) {
		this.close();
		const menu = new Menu(this, options, null);
		this.open = menu;
		menu.mount(this.root, options.at);
		document.addEventListener("pointerdown", this.onOutside, true);
		return menu;
	}

	close() {
		const menu = this.open;
		if (!menu) return;
		this.open = null;
		document.removeEventListener("pointerdown", this.onOutside, true);
		menu.destroy();
		if (menu.options.onClose) menu.options.onClose();
	}

	get isOpen() {
		return this.open !== null;
	}
}

class Menu {
	constructor(host, options, parent) {
		this.host = host;
		this.options = options;
		this.parent = parent;
		this.child = null;
		this.selected = -1;
		this.rowEls = [];
		this.el = null;
		this.returnFocus = document.activeElement;
	}

	contains(target) {
		return (this.el && this.el.contains(target)) || (this.child !== null && this.child.contains(target));
	}

	mount(root, at) {
		const sheet = window.matchMedia(PHONE).matches && !this.parent;
		this.el = h("div.popup.menu" + (sheet ? ".sheet" : ""), { role: "menu", tabindex: "-1", "aria-label": this.options.label || "menu" });
		if (this.options.minWidth) this.el.style.minWidth = this.options.minWidth + "px";
		this.options.rows.forEach((row, index) => this.el.append(this.rowElement(row, index)));
		if (this.options.rows.length === 0) this.el.append(h("div.menu-item.dis", null, h("span.label", null, "(empty)")));
		this.el.addEventListener("keydown", (event) => this.key(event));
		root.append(this.el);
		if (!sheet) this.place(at);
		this.el.focus({ preventScroll: true });
	}

	rowElement(row, index) {
		if (row.kind === "separator") {
			this.rowEls.push(null);
			return h("div.menu-sep", { role: "separator" });
		}
		const disabled = row.kind === "item" && !row.enabled;
		const el = h("div.menu-item" + (disabled ? ".dis" : ""), {
			role: "menuitem", "aria-disabled": disabled ? "true" : null, dataset: { index: String(index) },
			on: {
				pointerenter: () => this.select(index, row.kind === "submenu"),
				click: (event) => {
					event.stopPropagation();
					this.activate(index);
				},
			},
		},
		(row.icon && iconFrom(row.icon, row.base)) || h("span.ci.blank"),
		h("span.label", null, row.label),
		row.kind === "submenu" ? icon("chevron-right", "sub") : h("span.kbd", null, row.keys || ""));
		this.rowEls.push(el);
		return el;
	}

	place(at) {
		const rect = this.el.getBoundingClientRect();
		let x;
		let y;
		if (at instanceof Element) {
			const anchor = at.getBoundingClientRect();
			x = this.parent ? anchor.right - 2 : anchor.left;
			y = this.parent ? anchor.top - 4 : anchor.bottom;
		}
		else {
			x = at && at.clientX !== undefined ? at.clientX : at ? at.x : 0;
			y = at && at.clientY !== undefined ? at.clientY : at ? at.y : 0;
		}
		const maxX = window.innerWidth - rect.width - 4;
		const maxY = window.innerHeight - rect.height - 4;
		if (x > maxX) x = this.parent && at instanceof Element ? at.getBoundingClientRect().left - rect.width + 2 : maxX;
		this.el.style.left = Math.max(4, x) + "px";
		this.el.style.top = Math.max(4, Math.min(y, maxY)) + "px";
	}

	selectable(index) {
		const row = this.options.rows[index];
		return row && row.kind !== "separator" && !(row.kind === "item" && !row.enabled);
	}

	select(index, openSubmenu) {
		if (this.selected >= 0 && this.rowEls[this.selected]) this.rowEls[this.selected].classList.remove("sel");
		this.selected = index;
		if (index >= 0 && this.rowEls[index]) {
			this.rowEls[index].classList.add("sel");
			this.rowEls[index].scrollIntoView({ block: "nearest" });
		}
		this.closeChild();
		if (openSubmenu) this.openSubmenu(index);
	}

	move(step) {
		const count = this.options.rows.length;
		for (let i = 1; i <= count; i++) {
			const index = ((this.selected < 0 && step < 0 ? 0 : this.selected) + step * i + count * 2) % count;
			if (this.selectable(index)) return this.select(index, false);
		}
		return undefined;
	}

	// The first selectable row from `start` going `step` (Home: from before the first; End: from the first, backwards).
	moveFrom(start, step) {
		this.selected = start;
		this.move(step);
	}

	activate(index) {
		const row = this.options.rows[index];
		if (!this.selectable(index)) return;
		if (row.kind === "submenu") {
			this.select(index, true);
			if (this.child) this.child.move(1);
			return;
		}
		const pick = this.root().options.pick;
		this.host.close();
		pick(row);
	}

	root() {
		return this.parent ? this.parent.root() : this;
	}

	openSubmenu(index) {
		const row = this.options.rows[index];
		const submenu = this.root().options.submenu;
		if (!row || row.kind !== "submenu" || !submenu) return;
		this.child = new Menu(this.host, { rows: submenu(row.menuId), label: row.label }, this);
		this.child.mount(this.host.root, this.rowEls[index]);
	}

	closeChild() {
		if (!this.child) return;
		this.child.destroy();
		this.child = null;
		this.el.focus({ preventScroll: true });
	}

	key(event) {
		const handled = {
			ArrowDown: () => this.move(1),
			ArrowUp: () => this.move(-1),
			Home: () => this.moveFrom(-1, 1),
			End: () => this.moveFrom(0, -1),
			Enter: () => this.activate(this.selected),
			" ": () => this.activate(this.selected),
			ArrowRight: () => this.activate(this.selected),
			ArrowLeft: () => (this.parent ? this.parent.closeChild() : undefined),
			Escape: () => (this.parent ? this.parent.closeChild() : this.host.close()),
			Tab: () => this.host.close(),
		}[event.key];
		if (!handled) return;
		event.preventDefault();
		event.stopPropagation();
		handled();
	}

	destroy() {
		this.closeChild();
		if (this.el) this.el.remove();
		if (!this.parent && this.returnFocus && this.returnFocus.focus && document.contains(this.returnFocus)) this.returnFocus.focus({ preventScroll: true });
	}
}
