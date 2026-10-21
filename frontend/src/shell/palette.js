// palette.js - the command palette (Ctrl+Shift+P, Ctrl+Q; F-SHL-22 starts here): every enabled command, every tool
// window and activity, filtered by the words typed, run with Enter.

import { h, iconFrom, replace } from "/ui-kit/dom.js";

const MOST_ROWS = 200;

function entries(shell) {
	const out = [];
	for (const definition of shell.commands.all()) {
		if (definition.id === "shell.showView" || definition.id === "shell.showActivity" || !shell.commands.isEnabled(definition.id)) continue;
		out.push({ label: shell.commands.label(definition.id), command: definition.id, args: undefined, icon: definition.icon, base: definition.base });
	}
	for (const tool of shell.contributions.toolWindows) {
		if (shell.context.holds(tool.when)) out.push({ label: "View: " + tool.title, command: "shell.showView", args: { id: tool.id }, icon: tool.icon, base: tool.plugin.base });
	}
	for (const activity of shell.contributions.activities) out.push({ label: "View: " + activity.title + " (activity)", command: "shell.showActivity", args: { id: activity.id }, icon: activity.icon, base: activity.plugin.base });
	for (const entry of out) entry.keys = shell.keybindings.labelFor(entry.command, entry.args);
	return out.sort((a, b) => a.label.localeCompare(b.label));
}

function matches(entry, words) {
	const text = entry.label.toLowerCase();
	return words.every((word) => text.includes(word));
}

export function showPalette(shell) {
	shell.menuHost.close();
	const existing = document.querySelector(".palette");
	if (existing) existing.remove();
	const all = entries(shell);
	let shown = all;
	let selected = 0;
	const input = h("input.palette-input", { type: "text", placeholder: "Type a command or a tool window", "aria-label": "Command palette", spellcheck: "false", autocomplete: "off" });
	const list = h("div.palette-list", { role: "listbox" });
	const el = h("div.popup.palette", { role: "dialog", "aria-label": "Command palette" }, input, list);
	const returnFocus = document.activeElement;

	function close() {
		el.remove();
		document.removeEventListener("pointerdown", outside, true);
		if (returnFocus && document.contains(returnFocus)) returnFocus.focus({ preventScroll: true });
	}

	function outside(event) {
		if (!el.contains(event.target)) close();
	}

	function run(entry) {
		close();
		shell.commands.run(entry.command, entry.args);
	}

	function render() {
		replace(list, ...shown.slice(0, MOST_ROWS).map((entry, i) => h("div.menu-item" + (i === selected ? ".sel" : ""), {
			role: "option", "aria-selected": i === selected ? "true" : "false",
			on: { click: () => run(entry), pointerenter: () => select(i) },
		}, iconFrom(entry.icon, entry.base) || h("span.ci.blank"), h("span.label", null, entry.label), h("span.kbd", null, entry.keys))));
		if (!shown.length) list.append(h("div.menu-item.dis", null, h("span.label", null, "No matching commands")));
	}

	function select(i) {
		selected = i;
		[...list.children].forEach((row, index) => row.classList.toggle("sel", index === i));
		if (list.children[i]) list.children[i].scrollIntoView({ block: "nearest" });
	}

	input.addEventListener("input", () => {
		const words = input.value.toLowerCase().split(/\s+/).filter(Boolean);
		shown = all.filter((entry) => matches(entry, words));
		selected = 0;
		render();
	});
	input.addEventListener("keydown", (event) => {
		const count = Math.min(shown.length, MOST_ROWS);
		if (event.key === "ArrowDown" && count) select((selected + 1) % count);
		else if (event.key === "ArrowUp" && count) select((selected - 1 + count) % count);
		else if (event.key === "Enter" && shown[selected]) run(shown[selected]);
		else if (event.key === "Escape") close();
		else return;
		event.preventDefault();
		event.stopPropagation();
	});
	render();
	document.body.append(el);
	document.addEventListener("pointerdown", outside, true);
	input.focus();
}
