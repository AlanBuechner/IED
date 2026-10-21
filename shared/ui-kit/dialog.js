// dialog.js - a modal question (VS message box): a title, a message and two buttons; Enter takes the default
// button, Escape or a click outside cancels. A sheet on a phone.
//     if (await confirmDialog({ title: "Delete", message: "'a.c' will be deleted permanently.", confirm: "Delete" })) ...

import { h } from "./dom.js";

// Resolves true when the person confirms, false otherwise. `danger` marks the confirm button as destructive.
export function confirmDialog({ title, message, confirm = "OK", cancel = "Cancel", danger = false }) {
	return new Promise((resolve) => {
		const previous = document.activeElement;
		const phone = window.matchMedia("(max-width: 719px)").matches;
		const finish = (answer) => {
			veil.remove();
			if (previous && previous.isConnected) previous.focus({ preventScroll: true });
			resolve(answer);
		};
		const yes = h("button.btn" + (danger ? ".danger" : ".primary"), { on: { click: () => finish(true) } }, confirm);
		const no = h("button.btn", { on: { click: () => finish(false) } }, cancel);
		const box = h("div.popup.dialog" + (phone ? ".sheet" : ""), {
			role: "alertdialog", "aria-modal": "true", "aria-label": title, tabindex: "-1",
			on: {
				keydown: (event) => {
					event.stopPropagation();
					if (event.key === "Escape") finish(false);
					else if (event.key === "Enter" && document.activeElement !== no) finish(true);
					else if (event.key === "Tab") {
						event.preventDefault();
						(document.activeElement === yes ? no : yes).focus();
					}
					else return;
					event.preventDefault();
				},
			},
		}, h("div.dialog-title", null, title), h("div.dialog-message", null, message), h("div.dialog-buttons", null, yes, no));
		const veil = h("div.dialog-veil", { on: { mousedown: (event) => event.target === veil && finish(false) } }, box);
		document.body.append(veil);
		yes.focus();
	});
}
