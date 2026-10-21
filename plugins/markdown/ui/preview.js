// preview.js - the Markdown preview ("mdpreview:<path>" tabs, F-EDT-91/92): the server document rendered as it
// changes - typing in its editor, another writer, the disk - with fenced code colored as the editor colors it (the
// `editor.colorize` binding, when an editor plugin provides it) and the workspace's images (`/w/<wsId>/files/`,
// docs/10). Ticking a task box edits its line through the document store, taking the lease as typing would; a link to a
// workspace file opens it in the editor, a web link a new browser window; a workspace image clicked shows at its size.
// It scrolls with its file's editor (`editor.scrolled`), and a double-click shows that place in the editor.

import { h, replace } from "/ui-kit/dom.js";
import { DocumentSync } from "/ui-kit/docsync.js";
import { applyChanges } from "/ui-kit/textedits.js";
import { renderMarkdown, toggleTask } from "/ui-kit/markdown.js";
import { isFileUri } from "/ui-kit/fileuris.js";
import { showImage } from "/ui-kit/imageview.js";

const RENDER_MS = 60;

// The workspace path `href` names, seen from the folder `from`; null when it leaves the workspace.
export function resolveRelative(from, href) {
	const parts = from ? from.split("/") : [];
	for (const part of href.split("/")) {
		if (part === "" || part === ".") continue;
		if (part === "..") {
			if (!parts.length) return null;
			parts.pop();
		}
		else parts.push(part);
	}
	return parts.join("/");
}

// Fence name -> language id, from the loaded languages' `fences` and their own ids: a "c++" fence and a "cpp" one both
// mean "cpp".
export function fenceLanguages(languages) {
	const map = new Map();
	for (const language of languages) {
		for (const fence of language.fences || []) map.set(fence.toLowerCase(), language.id);
		if (!map.has(language.id.toLowerCase())) map.set(language.id.toLowerCase(), language.id);
	}
	return map;
}

export class MarkdownPreview {
	// `fences`: fenceLanguages() of the loaded languages.
	constructor(ctx, path, fences) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.path = path;
		this.fences = fences;
		this.folder = isFileUri(path) ? null : path.slice(0, Math.max(0, path.lastIndexOf("/")));
		this.text = "";
		this.timer = null;
		this.body = h("div.md-preview", { tabindex: "0" });
		ctx.el.append(this.body);
		this.body.addEventListener("click", (event) => this.clicked(event));
		this.body.addEventListener("dblclick", (event) => this.showSource(event));
		this.unfollow = this.ied.bindings.on("editor.scrolled", (data) => {
			if (data.path === this.path) this.follow(data.line);
		});
		this.sync = new DocumentSync(this.ied.client, {
			setText: (text) => this.changed(text),
			applyRemote: (changes) => this.changed(applyChanges(this.text, changes)),
			setReadOnly: () => {},
		}, { onState: (state) => this.stateChanged(state) });
		this.sync.open(ctx.wsId, path).catch((e) => replace(this.body, h("div.md-problem", null, "The document could not be opened: " + e.message)));
	}

	dispose() {
		this.unfollow();
		clearTimeout(this.timer);
		this.sync.dispose();
	}

	changed(text) {
		this.text = text;
		clearTimeout(this.timer);
		this.timer = setTimeout(() => this.render(), RENDER_MS);
	}

	stateChanged(state) {
		if (state.renamed && state.renamed !== this.path) {
			this.path = state.renamed;
			this.folder = this.path.slice(0, Math.max(0, this.path.lastIndexOf("/")));
		}
		if (state.error) replace(this.body, h("div.md-problem", null, state.error));
	}

	render() {
		const top = this.body.scrollTop;
		replace(this.body, ...renderMarkdown(this.text, { code: (code, language) => this.colored(code, language) }));
		for (const image of this.body.querySelectorAll("img[src]")) this.locate(image);
		for (const box of this.body.querySelectorAll("input[data-task]")) box.disabled = this.sync.readOnly;
		this.body.scrollTop = top;
	}

	// Scrolls to where the editor shows `line` (0-based, fractional): between the blocks that start before and after it.
	// Not while hidden (a tab behind another).
	follow(line) {
		if (!this.body.offsetParent) return;
		let before = null;
		let after = null;
		for (const block of this.body.querySelectorAll("[data-line]")) {
			const at = Number(block.dataset.line);
			if (at <= line) before = { block, at };
			else {
				after = { block, at };
				break;
			}
		}
		const offset = (element) => element.getBoundingClientRect().top - this.body.getBoundingClientRect().top + this.body.scrollTop;
		const from = before ? offset(before.block) : 0;
		const to = after ? offset(after.block) : before ? from + before.block.offsetHeight : 0;
		const fromLine = before ? before.at : 0;
		const toLine = after ? after.at : fromLine + 1;
		const part = Math.min(1, Math.max(0, (line - fromLine) / Math.max(1, toLine - fromLine)));
		this.body.scrollTop = from + (to - from) * part;
	}

	// The source of the block double-clicked, in the editor.
	showSource(event) {
		const block = event.target.closest("[data-line]");
		if (block && !isFileUri(this.path)) this.ied.commands.run("ied.editor.goTo", { path: this.path, line: Number(block.dataset.line) });
	}

	// A fenced block colored as the editor colors its language; plain text until it answers, and without an editor.
	colored(code, language) {
		const element = h("code", null, code);
		const id = this.fences.get(language.toLowerCase());
		if (id && this.ied.bindings.has("editor.colorize")) {
			this.ied.bindings.call("editor.colorize", { languageId: id, lines: code.split("\n") }).then((lines) => {
				element.innerHTML = lines.join("\n"); // the colorizer escapes the text it wraps
			}, () => {});
		}
		return element;
	}

	// A relative image is the workspace's (served as an image only); others stay as written, for the page's policy.
	locate(image) {
		const src = image.getAttribute("src");
		if (/^[a-z][a-z0-9+.-]*:/i.test(src) || this.folder === null) return;
		const path = resolveRelative(this.folder, decodeURIComponent(src.split("#")[0]));
		if (path === null) image.removeAttribute("src");
		else {
			image.setAttribute("src", "/w/" + encodeURIComponent(this.ctx.wsId) + "/files/" + path.split("/").map(encodeURIComponent).join("/"));
			image.dataset.path = path;
		}
	}

	clicked(event) {
		const box = event.target.closest("input[data-task]");
		if (box) {
			event.preventDefault();
			this.tick(parseInt(box.getAttribute("data-task"), 10), !box.hasAttribute("checked")).catch((e) => this.ied.report("ticking a task: " + e.message, e));
			return;
		}
		const link = event.target.closest("a[href]");
		const image = event.target.closest("img[data-path]");
		if (image && !link) {
			showImage({ src: image.getAttribute("src"), title: image.dataset.path });
			return;
		}
		if (!link) return;
		event.preventDefault();
		const href = link.getAttribute("href");
		if (/^(https?|mailto):/i.test(href)) window.open(href, "_blank", "noopener,noreferrer");
		else if (!href.startsWith("#") && this.folder !== null) {
			const path = resolveRelative(this.folder, decodeURIComponent(href.split("#")[0]));
			if (path) this.ied.commands.run("ied.editor.goTo", { path });
		}
	}

	// The task box on `line` ticked or cleared in the source, as an edit of this client's.
	async tick(line, checked) {
		if (this.sync.readOnly) return;
		const toggle = toggleTask(this.text.split("\n")[line] || "", checked);
		if (!toggle) return;
		if (!this.sync.holdsLease) await this.sync.takeLease();
		const change = { range: { start: { line, col: toggle.col }, end: { line, col: toggle.col + 1 } }, text: toggle.text };
		this.text = applyChanges(this.text, [change]);
		this.sync.localEdit([change]);
		this.render();
	}
}
