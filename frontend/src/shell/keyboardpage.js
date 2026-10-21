// keyboardpage.js - Settings > Environment > Keyboard (Visual Studio's Tools > Options > Environment > Keyboard; docs/07
// "Host settings"): every command with its shortcuts, searchable; Add records a key or a chord, a key's x takes it off
// (a default stays off until the command is reset), Reset brings the defaults back. What the owner changes is the app
// tier's keyboard.bindings, applied at once in every window.

import { h, icon, replace } from "/ui-kit/dom.js";
import { formatBinding, keyOfEvent } from "./keys.js";
import { changed, commandLabel, commandRows, conflicts, withDefaults, withKey, withoutKey } from "./keyboardmodel.js";

const KEY = "keyboard.bindings";

export class KeyboardPage {
	constructor(shell, ctx) {
		this.shell = shell;
		this.ctx = ctx;
		this.search = h("input.input-field.kb-search", { type: "search", placeholder: "Search commands and keys", "aria-label": "Search commands and keys", on: { input: () => this.render() } });
		this.onlyChanged = h("input", { type: "checkbox", "aria-label": "Only what I changed", on: { change: () => this.render() } });
		this.list = h("div.kb-list", { role: "table", "aria-label": "Keyboard shortcuts" });
		ctx.el.append(h("div.kb-page", null,
			h("div.set-crumbs", null, "Environment \u203a Keyboard"),
			h("h1", null, "Keyboard"),
			h("div.set-desc", null, "Every command and its shortcuts. Yours win over the defaults; a default you take off stays off until you reset its command. Keys the browser keeps (Ctrl+W, Ctrl+T, Ctrl+N, Ctrl+Tab...) work only in an installed app window."),
			h("div.kb-tools", null, this.search, h("label.kb-only", null, this.onlyChanged, "Only what I changed")),
			this.list));
		this.stopSettings = ctx.settings.onChange(() => this.render());
		this.stopCommands = shell.commands.onChange(() => this.render());
		this.render();
	}

	dispose() {
		this.stopSettings();
		this.stopCommands();
		this.shell.keybindings.recording = false;
	}

	entries() {
		const current = this.ctx.settings.value(KEY);
		return Array.isArray(current.value) ? current.value : [];
	}

	rows() {
		return commandRows(this.shell.commands.all(), this.shell.keybindings.all(), (command, args) => commandLabel(this.shell, command, args));
	}

	render() {
		const words = this.search.value.toLowerCase().split(/\s+/).filter(Boolean);
		const rows = this.rows();
		const shown = rows.filter((row) => (!this.onlyChanged.checked || changed(row))
			&& words.every((w) => (row.label + " " + row.command + " " + row.keys.map((k) => k.label).join(" ")).toLowerCase().includes(w)));
		replace(this.list, shown.length ? shown.map((row) => this.row(row, rows)) : h("p.muted", null, "No command matches."));
	}

	row(row, rows) {
		return h("div.kb-row" + (changed(row) ? ".changed" : ""), { role: "row", dataset: { command: row.command } },
			h("div.kb-cmd", { role: "cell" }, h("span.kb-label", null, row.label), h("span.mono.muted", null, row.command)),
			h("div.kb-keys", { role: "cell" }, row.keys.map((key) => this.chip(row, key, rows))),
			h("div.kb-actions", { role: "cell" },
				h("button.btn.flat", { title: "Add a shortcut", "aria-label": "Add a shortcut to " + row.label, on: { click: () => this.record(row, rows) } }, icon("add"), "Add"),
				changed(row) ? h("button.btn.flat", { title: "Back to the default shortcuts", "aria-label": "Reset " + row.label, on: { click: () => this.write(withDefaults(this.entries(), row.command, row.args)) } }, icon("discard"), "Reset") : null));
	}

	chip(row, key, rows) {
		const clash = key.removed ? [] : conflicts(rows, key.chord, key.when, row);
		const title = [key.user ? "Yours" : "Default (" + key.source + ")", key.when ? "when " + key.when : "", key.removed ? "taken off" : "", clash.length ? "also: " + clash.join(", ") : ""].filter(Boolean).join(" \u00b7 ");
		return h("span.kb-key" + (key.user ? ".user" : "") + (key.removed ? ".removed" : "") + (clash.length ? ".clash" : ""), { title, dataset: { chord: key.chord } },
			h("span.mono", null, key.label),
			key.removed ? null : h("button.ib.sm", { title: "Take this shortcut off", "aria-label": "Take " + key.label + " off " + row.label, on: { click: () => this.write(withoutKey(this.entries(), row.command, row.args, key)) } }, icon("close")));
	}

	// The owner's list written to the app tier; a refusal is shown by the settings window.
	write(entries) {
		this.ctx.settings.set(KEY, entries, "app");
	}

	// The dialog that reads a key or a two-key chord for `row`, with the when-clause of its first default.
	record(row, rows) {
		const keys = [];
		const shown = h("div.kb-recorded.mono", { "aria-live": "polite" }, "Press the shortcut");
		const clash = h("div.kb-clash.c-warn");
		const firstDefault = row.keys.find((k) => !k.user);
		const when = h("input.input-field", { type: "text", value: firstDefault ? firstDefault.when : "", placeholder: "When (optional): activeTabKind == 'doc'", "aria-label": "When", spellcheck: "false" });
		const finish = (accept) => {
			this.shell.keybindings.recording = false;
			veil.remove();
			if (accept && keys.length) this.write(withKey(this.entries(), row.command, row.args, keys.join(" "), when.value.trim()));
		};
		const update = () => {
			shown.textContent = keys.length ? formatBinding(keys) : "Press the shortcut";
			const others = keys.length ? conflicts(rows, keys.join(" "), when.value.trim(), row) : [];
			clash.textContent = others.length ? "Also on: " + others.join(", ") : "";
		};
		const add = h("button.btn.primary", { on: { click: () => finish(true) } }, "Add");
		const box = h("div.popup.dialog.kb-dialog", {
			role: "dialog", "aria-modal": "true", "aria-label": "Add a shortcut", tabindex: "-1",
			on: {
				keydown: (event) => {
					if (event.target === when) {
						if (event.key === "Enter") finish(true);
						if (event.key === "Escape") finish(false);
						event.stopPropagation();
						return;
					}
					const key = keyOfEvent(event);
					event.preventDefault();
					event.stopPropagation();
					if (!key) return;
					if (key === "escape") return finish(false);
					if (key === "enter" && keys.length) return finish(true);
					if (keys.length >= 2) keys.length = 0;
					keys.push(key);
					update();
				},
			},
		},
		h("div.dialog-title", null, "Add a shortcut: " + row.label),
		h("div.dialog-message", null, "Press a key, or two for a chord (Ctrl+K, Ctrl+C). Enter adds it; Escape cancels."),
		shown, clash, when,
		h("div.dialog-buttons", null,
			h("button.btn", { on: { click: () => { keys.length = 0; update(); box.focus(); } } }, "Clear"),
			add,
			h("button.btn", { on: { click: () => finish(false) } }, "Cancel")));
		when.addEventListener("input", update);
		const veil = h("div.dialog-veil", { on: { mousedown: (event) => event.target === veil && finish(false) } }, box);
		document.body.append(veil);
		this.shell.keybindings.recording = true;
		box.focus();
	}
}
