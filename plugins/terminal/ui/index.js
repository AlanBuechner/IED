// index.js - the terminal plugin's UI module (docs/15 "Terminal dock"): the panel's tabs - one per server terminal of the
// workspace, from terms:<wsId>, so every window lists the same - with New, Kill and the panel's show/hide; on a phone a
// row of keys (Esc, Tab, Ctrl+C, arrows) under the screen.

import { h, icon, replace } from "/ui-kit/dom.js";
import { TerminalView } from "./terminalview.js";

const panels = new Set();
const PHONE_KEYS = [["Esc", "\x1b"], ["Tab", "\t"], ["Ctrl+C", "\x03"], ["↑", "\x1b[A"], ["↓", "\x1b[B"], ["←", "\x1b[D"], ["→", "\x1b[C"]];

function last() {
	return [...panels].pop() || null;
}

export function activate(ied) {
	addStylesheet(ied.plugin.base + "ui/terminal.css");
	addStylesheet("/vendor/xterm/css/xterm.css");
	ied.commands.handle("ied.terminal.new", (args) => last() && last().create(args && args.cwd));
	ied.commands.handle("ied.terminal.kill", (args) => last() && last().kill(args && args.termId));
	ied.commands.handle("ied.terminal.rename", (args) => last() && last().rename(args && args.termId));
	ied.commands.handle("ied.terminal.show", () => last() && last().reveal());
	ied.commands.handle("ied.terminal.openHere", (args) => last() && last().create(args && args.path));
}

function addStylesheet(href) {
	if (document.querySelector("link[href='" + href + "']")) return;
	document.head.append(h("link", { rel: "stylesheet", href }));
}

export function TerminalPanel(ctx) {
	return new TerminalPanelView(ctx);
}

// The terminal titles a tab shows: "1: pwsh".
export function tabTitle(summary, index) {
	return (index + 1) + ": " + summary.title;
}

class TerminalPanelView {
	constructor(ctx) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.terminals = [];
		this.views = new Map();
		this.selected = null;
		this.tabs = h("div.term-tabs", { role: "tablist", "aria-label": "Terminals" });
		this.toggleButton = h("button.ib", { title: "Hide the panel (Ctrl+`)", "aria-label": "Hide the panel", on: { click: () => ctx.panel.toggle() } });
		this.head = h("div.panel-head.term-head", null, this.tabs,
			h("button.ib", { title: "New Terminal (Ctrl+Shift+`)", "aria-label": "New Terminal", on: { click: () => this.create() } }, icon("add")),
			h("button.ib", { title: "Kill Terminal", "aria-label": "Kill Terminal", on: { click: () => this.kill() } }, icon("trash")),
			h("span.grow"), this.toggleButton);
		this.screens = h("div.term-screens");
		this.keys = ctx.phone ? h("div.term-keys", null, ...PHONE_KEYS.map(([label, sequence]) => h("button.btn", { on: { click: () => this.current() && this.current().sendKey(sequence) } }, label))) : null;
		this.empty = h("div.term-empty.muted", null, "No terminal is open. ", h("button.btn.link", { on: { click: () => this.create() } }, "New Terminal"));
		this.body = h("div.panel-body.term-body", null, this.empty, this.screens, this.keys);
		replace(ctx.el, this.head, this.body);
		panels.add(this);
		this.stopList = this.ied.client.subscribe("terms:" + ctx.wsId, {
			snap: (data) => this.listed(data.terminals || []),
			evt: (kind, data) => this.changed(kind, data),
		});
		this.stopPanel = ctx.panel.onChange(() => this.render());
		this.resizer = new ResizeObserver(() => this.layout());
		this.resizer.observe(this.screens);
		this.render();
	}

	listed(terminals) {
		this.terminals = terminals;
		for (const [termId, view] of [...this.views]) if (!terminals.some((t) => t.termId === termId)) this.drop(termId, view);
		if (!this.selected || !terminals.some((t) => t.termId === this.selected)) this.selected = terminals.length ? terminals[terminals.length - 1].termId : null;
		this.render();
	}

	changed(kind, summary) {
		if (kind === "term.removed") this.listed(this.terminals.filter((t) => t.termId !== summary.termId));
		else if (kind === "term.added") this.listed(this.terminals.concat(summary));
		else {
			this.terminals = this.terminals.map((t) => (t.termId === summary.termId ? summary : t));
			this.render();
		}
	}

	drop(termId, view) {
		view.dispose();
		view.el.remove();
		this.views.delete(termId);
	}

	viewOf(summary) {
		let view = this.views.get(summary.termId);
		if (!view) {
			view = new TerminalView(this.ied, summary);
			this.views.set(summary.termId, view);
			this.screens.append(view.el);
		}
		return view;
	}

	current() {
		return this.selected ? this.views.get(this.selected) || null : null;
	}

	render() {
		const open = this.ctx.panel.isOpen();
		replace(this.toggleButton, icon(open ? "chevron-down" : "chevron-up"));
		this.toggleButton.title = open ? "Hide the panel (Ctrl+`)" : "Show the panel (Ctrl+`)";
		replace(this.tabs, ...this.terminals.map((summary, i) => h("button.term-tab" + (summary.termId === this.selected ? ".on" : "") + (summary.alive ? "" : ".exited"), {
			role: "tab", "aria-selected": summary.termId === this.selected ? "true" : "false", title: summary.alive ? summary.title : summary.title + " (exited)", dataset: { term: summary.termId },
			on: {
				click: () => this.select(summary.termId),
				dblclick: () => this.rename(summary.termId),
				contextmenu: (event) => {
					event.preventDefault();
					this.ied.menus.show("terminal/context", event, { termId: summary.termId });
				},
			},
		}, icon("terminal"), h("span", null, tabTitle(summary, i)))));
		this.empty.hidden = this.terminals.length > 0;
		for (const summary of this.terminals) {
			const view = this.viewOf(summary);
			view.el.hidden = summary.termId !== this.selected;
		}
		if (open) requestAnimationFrame(() => this.layout());
	}

	layout() {
		const view = this.current();
		if (view && this.ctx.panel.isOpen()) view.show();
	}

	select(termId) {
		this.selected = termId;
		if (!this.ctx.panel.isOpen()) this.ctx.panel.setOpen(true);
		this.render();
		requestAnimationFrame(() => this.current() && this.current().focus());
	}

	reveal() {
		if (!this.ctx.panel.isOpen()) this.ctx.panel.setOpen(true);
		if (!this.terminals.length) return this.create();
		requestAnimationFrame(() => this.current() && this.current().focus());
		return null;
	}

	async create(cwd) {
		try {
			const summary = await this.ied.client.request("term.create", Object.assign({ wsId: this.ctx.wsId }, cwd ? { cwd } : {}));
			if (!this.terminals.some((t) => t.termId === summary.termId)) this.terminals = this.terminals.concat(summary);
			this.select(summary.termId);
			return summary;
		}
		catch (e) {
			this.ied.toast(e.message, { level: "error" });
			return null;
		}
	}

	async kill(termId) {
		const id = termId || this.selected;
		if (!id) return;
		await this.ied.client.request("term.close", { termId: id }).catch((e) => this.ied.toast(e.message, { level: "error" }));
	}

	// The tab's title becomes a field (VS renames in place): Enter keeps it, Escape or an empty one leaves it as it was.
	rename(termId) {
		const id = termId || this.selected;
		const summary = this.terminals.find((t) => t.termId === id);
		const tab = this.tabs.querySelector(".term-tab[data-term='" + id + "']");
		if (!summary || !tab) return;
		const field = h("input.input-field.term-rename", { value: summary.title, "aria-label": "Terminal name", spellcheck: "false" });
		let done = false;
		const finish = async (keep) => {
			if (done) return;
			done = true;
			const title = field.value.trim();
			if (keep && title && title !== summary.title) await this.ied.client.request("term.rename", { termId: id, title }).catch((e) => this.ied.toast(e.message, { level: "error" }));
			this.render();
		};
		field.addEventListener("keydown", (event) => {
			event.stopPropagation();
			if (event.key === "Enter") finish(true);
			else if (event.key === "Escape") finish(false);
		});
		field.addEventListener("blur", () => finish(true));
		for (const kind of ["click", "dblclick", "mousedown"]) field.addEventListener(kind, (event) => event.stopPropagation());
		replace(tab, icon("terminal"), field);
		field.focus();
		field.select();
	}

	dispose() {
		panels.delete(this);
		this.stopList();
		this.stopPanel();
		this.resizer.disconnect();
		for (const [termId, view] of [...this.views]) this.drop(termId, view);
	}
}
