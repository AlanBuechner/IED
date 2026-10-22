// codecolors.js - code in a chat colored as the editor colors its language (the `editor.colorize` binding, when an editor
// plugin provides it; docs/15 "Chat"): a reply's fenced blocks by their fence's language, a change's diff by its file.
// A fenced block is colored once its closing fence has arrived, and colors are kept by language and text, so a reply
// drawn again while it streams shows its finished blocks colored at once instead of flickering back to plain text.

import { h } from "/ui-kit/dom.js";
import { fenceLanguages } from "/ui-kit/markdown.js";

const KEPT = 200;

export class CodeColors {
	constructor(ied) {
		this.ied = ied;
		this.known = new Map(); // language or path + "\n" + text -> {lines: [html] | null, ready: Promise}
	}

	// The <code> of a fenced block: plain text, colored at once when its colors are known, else when they arrive.
	element(code, fence, closed) {
		const element = h("code", null, code);
		const languageId = closed && fence ? this.languageOf(fence) : null;
		if (languageId) this.paint({ languageId }, [code], (lines) => {
			element.innerHTML = lines.join("\n"); // the colorizer escapes the text it wraps
		});
		return element;
	}

	// The code spans of a diff's rows, colored as the editor colors the language of `path`.
	diff(path, spans) {
		if (path && spans.length) this.paint({ path }, spans.map((span) => span.textContent), (lines) => {
			spans.forEach((span, i) => {
				if (lines[i] !== undefined) span.innerHTML = lines[i];
			});
		});
	}

	languageOf(fence) {
		const languages = this.ied.contributions("languages").map((contribution) => contribution.value);
		return fenceLanguages(languages).get(fence.toLowerCase()) || null;
	}

	// Calls show(lines) with the colored lines of `texts` joined: now when they are known, else when they arrive.
	paint(language, texts, show) {
		if (!this.ied.bindings.has("editor.colorize")) return;
		const colors = this.colors(language, texts.join("\n"));
		if (colors.lines) show(colors.lines);
		else colors.ready.then((lines) => lines && show(lines));
	}

	colors(language, text) {
		const key = (language.languageId || "path:" + language.path) + "\n" + text;
		let colors = this.known.get(key);
		if (colors) return colors;
		colors = { lines: null, ready: null };
		colors.ready = this.ied.bindings.call("editor.colorize", Object.assign({ lines: text.split("\n") }, language)).then((lines) => {
			colors.lines = lines;
			return lines;
		}, () => null);
		this.known.set(key, colors);
		if (this.known.size > KEPT) this.known.delete(this.known.keys().next().value);
		return colors;
	}
}
