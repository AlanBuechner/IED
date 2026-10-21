// picker.js - a quick pick (VS Go To All, the command palette's look): a field and the items `provide` finds for what
// is typed - asked again as the person types, the request before cancelled - chosen with the arrows and Enter or a
// click. Escape or a click outside closes it.
//     showPicker({ placeholder: "Go to file", provide: async (text, signal) => [{ label, detail, icon }], pick: (item) => ... });

import { h, replace } from "./dom.js";

const MOST_ROWS = 200;
const TYPING_PAUSE_MS = 120;

export function showPicker({ placeholder, label, provide, pick, empty = "Nothing matches" }) {
	const existing = document.querySelector(".picker");
	if (existing) existing.remove();
	const returnFocus = document.activeElement;
	const input = h("input.palette-input", { type: "text", placeholder, "aria-label": label || placeholder, spellcheck: "false", autocomplete: "off" });
	const status = h("div.picker-status.muted");
	const list = h("div.palette-list", { role: "listbox" });
	const el = h("div.popup.palette.picker", { role: "dialog", "aria-label": label || placeholder }, input, status, list);
	let items = [];
	let selected = 0;
	let asking = null;
	let timer = null;

	const close = () => {
		el.remove();
		if (asking) asking.abort();
		clearTimeout(timer);
		document.removeEventListener("pointerdown", outside, true);
		if (returnFocus && document.contains(returnFocus)) returnFocus.focus({ preventScroll: true });
	};
	const outside = (event) => {
		if (!el.contains(event.target)) close();
	};
	const choose = (item) => {
		close();
		pick(item);
	};
	const select = (i) => {
		selected = i;
		[...list.children].forEach((row, index) => row.classList.toggle("sel", index === i));
		if (list.children[i]) list.children[i].scrollIntoView({ block: "nearest" });
	};
	const render = () => {
		replace(list, ...items.slice(0, MOST_ROWS).map((item, i) => h("div.menu-item" + (i === selected ? ".sel" : ""), {
			role: "option", "aria-selected": i === selected ? "true" : "false",
			on: { click: () => choose(item), pointerenter: () => select(i) },
		}, item.icon || h("span.ci.blank"), h("span.label", null, item.label), item.detail ? h("span.kbd.picker-detail", null, item.detail) : null)));
		if (!items.length && input.value) list.append(h("div.menu-item.dis", null, h("span.label", null, empty)));
	};
	const ask = async () => {
		if (asking) asking.abort();
		const controller = new AbortController();
		asking = controller;
		replace(status, "Searching...");
		try {
			const found = await provide(input.value, controller.signal);
			if (controller !== asking) return;
			items = found;
			selected = 0;
			replace(status);
			render();
		}
		catch (e) {
			if (controller === asking && e.code !== "cancelled") replace(status, h("span.c-error", null, e.message));
		}
	};

	input.addEventListener("input", () => {
		clearTimeout(timer);
		timer = setTimeout(ask, TYPING_PAUSE_MS);
	});
	input.addEventListener("keydown", (event) => {
		const count = Math.min(items.length, MOST_ROWS);
		if (event.key === "ArrowDown" && count) select((selected + 1) % count);
		else if (event.key === "ArrowUp" && count) select((selected - 1 + count) % count);
		else if (event.key === "Enter" && items[selected]) choose(items[selected]);
		else if (event.key === "Escape") close();
		else return;
		event.preventDefault();
		event.stopPropagation();
	});
	document.body.append(el);
	document.addEventListener("pointerdown", outside, true);
	input.focus();
	ask();
	return { close };
}
