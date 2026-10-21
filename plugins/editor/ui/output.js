// output.js - the Output window (docs/10 "out:<wsId>:<sourceId>", VS): "Show output from" a source of the workspace, its
// text with ANSI colors, following the end while scrolled to it, Clear All and word wrap; a place a line names
// (outputlinks.js) is underlined and opens on a click. Lines past MAX_LINES leave from the top (the server keeps the
// last 64 KB for a reload anyway).

import { h, icon, replace } from "/ui-kit/dom.js";
import { AnsiReader } from "/ui-kit/ansi.js";
import { findLocation, pickFile, workspaceRelative } from "./outputlinks.js";

const MAX_LINES = 20000;
const SOURCE_KEY = "ied.output.source";

export class Output {
	constructor(ctx, mounted) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.mounted = mounted;
		this.sourceId = null;
		this.stop = null;
		this.reader = new AnsiReader();
		this.line = null;
		this.count = 0;
		this.select = h("select.input-field", { "aria-label": "Show output from", on: { change: () => this.show(this.select.value) } });
		this.wrapButton = h("button.ib", { title: "Toggle Word Wrap", "aria-label": "Toggle Word Wrap", on: { click: () => this.toggleWrap() } }, icon("word-wrap"));
		this.text = h("div.output-text", { tabindex: "0", role: "log", "aria-label": "Output", on: { contextmenu: (event) => this.menu(event), click: (event) => this.clicked(event) } });
		ctx.el.append(
			h("div.ed-tools", null, h("label", null, "Show output from:"), this.select, h("span.grow"),
				h("button.ib", { title: "Clear All", "aria-label": "Clear All", on: { click: () => this.clear() } }, icon("clear-all")), this.wrapButton),
			h("div.ed-fill", null, this.text));
		mounted.add(this);
		this.loadSources();
	}

	dispose() {
		this.mounted.delete(this);
		if (this.stop) this.stop();
	}

	focus() {
		this.text.focus({ preventScroll: true });
	}

	async loadSources() {
		try {
			const answer = await this.ied.client.request("out.sources", { wsId: this.ctx.wsId });
			replace(this.select, ...answer.sources.map((s) => h("option", { value: s.sourceId }, s.label)));
			const remembered = readRemembered();
			const chosen = answer.sources.find((s) => s.sourceId === remembered) || answer.sources[0];
			if (chosen) this.show(chosen.sourceId);
		}
		catch (e) {
			replace(this.text, h("div.ed-problem", null, "The output sources could not be read: " + e.message));
		}
	}

	show(sourceId) {
		if (this.stop) this.stop();
		this.sourceId = sourceId;
		this.select.value = sourceId;
		remember(sourceId);
		this.stop = this.ied.client.subscribe("out:" + this.ctx.wsId + ":" + sourceId, {
			snap: (data) => this.reset(data.tail),
			evt: (kind, data) => {
				if (kind === "append") this.append(data.text);
				else if (kind === "clear") this.reset("");
			},
			gone: () => replace(this.text, h("div.ed-problem", null, "This output source is gone.")),
		});
	}

	reset(text) {
		this.reader = new AnsiReader();
		this.text.replaceChildren();
		this.line = null;
		this.count = 0;
		this.append(text);
	}

	// Appends streamed text: runs of the current line continue it; a newline starts the next.
	append(text) {
		if (!text) return;
		const follow = this.text.scrollTop + this.text.clientHeight >= this.text.scrollHeight - 4;
		for (const run of this.reader.read(text.replace(/\r\n/g, "\n").replace(/\r/g, ""))) {
			const parts = run.text.split("\n");
			parts.forEach((part, i) => {
				if (i > 0) this.finishLine();
				if (!part && i < parts.length - 1) {
					this.currentLine();
					return;
				}
				if (part) this.currentLine().append(run.cls ? h("span", { class: run.cls }, part) : part);
			});
		}
		while (this.count > MAX_LINES) {
			this.text.firstElementChild.remove();
			this.count -= 1;
		}
		if (follow) this.text.scrollTop = this.text.scrollHeight;
	}

	finishLine() {
		if (this.line) linkify(this.line);
		this.line = null;
	}

	clicked(event) {
		const link = event.target.closest(".out-loc");
		const line = link ? link.closest(".line") : null;
		if (line) this.open({ path: line.dataset.path, line: Number(line.dataset.line), col: Number(line.dataset.col) });
	}

	// The workspace file a line names, found among the workspace's files (a compiler prints names relative to its
	// project's source folder), opened at the place.
	async open(place) {
		const relative = workspaceRelative(place.path, this.ied.workspace.root);
		const name = relative ? relative.slice(relative.lastIndexOf("/") + 1) : null;
		try {
			const found = name ? (await this.ied.client.request("fs.files", { wsId: this.ctx.wsId, query: name, maxResults: 200 })).files.map((f) => f.path) : [];
			const path = relative ? pickFile(relative, found) : null;
			if (!path) {
				this.ied.toast("There is no " + place.path + " in the workspace.", { level: "info" });
				return;
			}
			await this.ied.commands.run("ied.editor.goTo", { path, line: Math.max(0, place.line - 1), col: Math.max(0, place.col - 1) });
		}
		catch (e) {
			this.ied.toast(e.message, { level: "error" });
		}
	}

	currentLine() {
		if (!this.line) {
			this.line = h("div.line");
			this.text.append(this.line);
			this.count += 1;
		}
		return this.line;
	}

	clear() {
		if (!this.sourceId) return;
		this.ied.client.request("out.clear", { wsId: this.ctx.wsId, sourceId: this.sourceId }).catch((e) => this.ied.report("Output: " + e.message, e));
	}

	toggleWrap() {
		const on = this.text.classList.toggle("wrap");
		this.wrapButton.classList.toggle("on", on);
	}

	menu(event) {
		event.preventDefault();
		this.ied.menus.show("output/context", event, { sourceId: this.sourceId });
	}
}

// Underlines the place a finished line names, splitting its text nodes at the place's edges.
function linkify(line) {
	const found = findLocation(line.textContent);
	if (!found) return;
	line.dataset.path = found.path;
	line.dataset.line = String(found.line);
	line.dataset.col = String(found.col);
	const walker = document.createTreeWalker(line, NodeFilter.SHOW_TEXT);
	const nodes = [];
	for (let node = walker.nextNode(); node; node = walker.nextNode()) nodes.push(node);
	let at = 0;
	for (const node of nodes) {
		const from = Math.max(found.start, at);
		const to = Math.min(found.end, at + node.length);
		at += node.length;
		if (from >= to) continue;
		const start = from - (at - node.length);
		const middle = start > 0 ? node.splitText(start) : node;
		if (to - from < middle.length) middle.splitText(to - from);
		const link = h("span.out-loc", { title: "Go to " + found.path + (found.line ? ", line " + found.line : "") });
		middle.replaceWith(link);
		link.append(middle);
	}
}

function readRemembered() {
	try {
		return sessionStorage.getItem(SOURCE_KEY);
	}
	catch {
		return null;
	}
}

export function remember(sourceId) {
	try {
		sessionStorage.setItem(SOURCE_KEY, sourceId);
	}
	catch {
		// a window without storage starts at the first source
	}
}
