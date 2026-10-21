// terminalview.js - one server terminal in xterm.js (docs/10 "Terminals"): its screen rebuilt from the scrollback when it
// opens, its output as it comes, its keys sent while this window holds the lease (a bar offers "Type Here" otherwise),
// and its size following the panel while this window types. The colors are the theme's --ansi-* tokens.

import { h, replace } from "/ui-kit/dom.js";
import { Terminal } from "/vendor/xterm/lib/xterm.mjs";
import { FitAddon } from "/vendor/xterm-addon-fit/lib/addon-fit.mjs";

const ANSI = ["black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"];

// xterm's theme from the page's tokens.
export function terminalTheme(style) {
	const token = (name) => style.getPropertyValue(name).trim();
	const theme = { background: token("--bg-1"), foreground: token("--text-0"), cursor: token("--text-0"), selectionBackground: token("--selection") };
	for (const name of ANSI) {
		theme[name] = token("--ansi-" + name);
		theme["bright" + name[0].toUpperCase() + name.slice(1)] = token("--ansi-bright-" + name);
	}
	return theme;
}

// What a window shows when a terminal's program ends.
export function exitLine(code) {
	return "\r\n\x1b[90m[the process exited" + (code === null || code === undefined ? "" : " with code " + code) + "]\x1b[0m\r\n";
}

export class TerminalView {
	constructor(ied, summary) {
		this.ied = ied;
		this.termId = summary.termId;
		this.summary = summary;
		this.holder = summary.holder;
		this.bar = h("div.term-bar", { hidden: true }, h("span", null, "Another window is typing in this terminal."), h("button.btn", { on: { click: () => this.takeLease() } }, "Type Here"));
		this.host = h("div.term-host");
		this.el = h("div.term-view", { dataset: { term: summary.termId } }, this.bar, this.host);
		const style = getComputedStyle(document.documentElement);
		this.xterm = new Terminal({
			fontFamily: style.getPropertyValue("--font-mono").trim(),
			fontSize: parseInt(style.getPropertyValue("--font-size-code"), 10) || 13,
			theme: terminalTheme(style),
			scrollback: 5000,
			cursorBlink: true,
			allowProposedApi: false,
		});
		this.fit = new FitAddon();
		this.xterm.loadAddon(this.fit);
		this.opened = false;
		this.typed = this.xterm.onData((data) => this.send(data));
		this.stop = ied.client.subscribe("term:" + summary.termId, {
			snap: (data) => this.restore(data),
			evt: (kind, data) => this.event(kind, data),
		});
	}

	// The panel shows it: xterm needs a laid-out element to measure.
	show() {
		if (!this.opened) {
			this.xterm.open(this.host);
			this.opened = true;
		}
		this.layout();
	}

	restore(data) {
		this.summary = data.terminal;
		this.holder = data.terminal.holder;
		this.xterm.reset();
		this.xterm.write(data.scrollback || "");
		if (!data.terminal.alive) this.xterm.write(exitLine(data.terminal.exitCode));
		this.leaseChanged();
	}

	event(kind, data) {
		if (kind === "data") this.xterm.write(data.data);
		else if (kind === "exit") this.xterm.write(exitLine(data.code));
		else if (kind === "lease") {
			this.holder = data.holder;
			this.leaseChanged();
			this.layout();
		}
		else if (kind === "resize" && !this.holds()) this.xterm.resize(data.cols, data.rows);
	}

	holds() {
		return this.holder === this.ied.client.clientId;
	}

	leaseChanged() {
		this.bar.hidden = this.holds();
	}

	send(data) {
		if (!this.holds()) return;
		this.ied.client.request("term.input", { termId: this.termId, data }).catch((e) => {
			if (e.code === "forbidden") this.leaseChanged();
		});
	}

	async takeLease() {
		try {
			await this.ied.client.request("term.takeLease", { termId: this.termId });
			this.xterm.focus();
		}
		catch (e) {
			this.ied.toast(e.message, { level: "error" });
		}
	}

	// Fits the panel; while this window types, the program gets the size too.
	layout() {
		if (!this.opened || !this.host.clientWidth || !this.host.clientHeight) return;
		this.fit.fit();
		if (this.holds() && this.summary.alive) this.ied.client.request("term.resize", { termId: this.termId, cols: this.xterm.cols, rows: this.xterm.rows }).catch(() => {});
	}

	focus() {
		this.xterm.focus();
	}

	// Keys a phone keyboard lacks.
	sendKey(sequence) {
		this.send(sequence);
		this.xterm.focus();
	}

	dispose() {
		this.stop();
		this.typed.dispose();
		this.xterm.dispose();
		replace(this.el);
	}
}
