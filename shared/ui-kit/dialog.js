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

// A question with a text answer (a branch name, a stash message): resolves the text, trimmed, or null when cancelled.
// Enter answers; Escape or a click outside cancels. `check` returns why a text cannot be the answer, or "".
export function promptDialog({ title, message, value = "", placeholder = "", confirm = "OK", cancel = "Cancel", check = () => "" }) {
	return new Promise((resolve) => {
		const previous = document.activeElement;
		const phone = window.matchMedia("(max-width: 719px)").matches;
		const finish = (answer) => {
			veil.remove();
			if (previous && previous.isConnected) previous.focus({ preventScroll: true });
			resolve(answer);
		};
		const problem = h("div.dialog-problem.c-error");
		const field = h("input.input-field", { type: "text", value, placeholder, "aria-label": title, spellcheck: "false" });
		const answer = () => {
			const text = field.value.trim();
			const why = text ? check(text) : "A name is needed";
			problem.textContent = why;
			if (!why) finish(text);
		};
		const yes = h("button.btn.primary", { on: { click: answer } }, confirm);
		const no = h("button.btn", { on: { click: () => finish(null) } }, cancel);
		const box = h("div.popup.dialog" + (phone ? ".sheet" : ""), {
			role: "dialog", "aria-modal": "true", "aria-label": title,
			on: {
				keydown: (event) => {
					event.stopPropagation();
					if (event.key === "Escape") finish(null);
					else if (event.key === "Enter" && document.activeElement !== no) answer();
					else return;
					event.preventDefault();
				},
			},
		}, h("div.dialog-title", null, title), message ? h("div.dialog-message", null, message) : null, field, problem, h("div.dialog-buttons", null, yes, no));
		const veil = h("div.dialog-veil", { on: { mousedown: (event) => event.target === veil && finish(null) } }, box);
		document.body.append(veil);
		field.focus();
		field.select();
	});
}
