// toasts.js - short-lived, non-blocking messages at the bottom right (docs/15 "Interaction rules": no modals): new
// notifications, failures, and a view's own news with an action (Undo). Each stays 6 s (errors and ones with an
// action 10 s), or until dismissed or acted on; at most four show at once.

import { h, icon } from "/ui-kit/dom.js";

const LEVEL_ICONS = { info: "info", warning: "warning", error: "error", attention: "bell-dot" };
const STAY_MS = 6000;
const ERROR_STAY_MS = 10000;
const MOST = 4;

export class Toasts {
	constructor(parent) {
		this.el = h("div.toasts", { role: "status", "aria-live": "polite" });
		parent.append(this.el);
	}

	// `action`: {label, run} - a button that runs once and closes the toast.
	show(text, level, action) {
		const kind = LEVEL_ICONS[level] ? level : "info";
		const button = action ? h("button.btn.flat.toast-action", { on: { click: () => {
			toast.remove();
			action.run();
		} } }, action.label) : null;
		const toast = h("div.toast." + kind, null,
			icon(LEVEL_ICONS[kind], "lvl"),
			h("span.text", null, text),
			button,
			h("button.ib", { title: "Dismiss", "aria-label": "Dismiss", on: { click: () => toast.remove() } }, icon("close")));
		this.el.append(toast);
		while (this.el.children.length > MOST) this.el.firstElementChild.remove();
		setTimeout(() => toast.remove(), kind === "error" || action ? ERROR_STAY_MS : STAY_MS);
	}
}
