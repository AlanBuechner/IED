// mdpreview.js - the Markdown preview ("mdpreview:<path>" tabs, F-EDT-91/92): the server document rendered as it
// changes - typing in its editor, another writer, the disk - with fenced code colored by Monaco and the workspace's
// images (`/w/<wsId>/files/`, docs/10). Ticking a task box edits its line through the document store, taking the lease
// as typing would; a link to a workspace file opens it in the editor, a web link a new browser window.

import { h, replace } from "/ui-kit/dom.js";
import { DocumentSync } from "/ui-kit/docsync.js";
import { applyChanges } from "/ui-kit/textedits.js";
import { renderMarkdown, toggleTask } from "/ui-kit/markdown.js";
import { isFileUri } from "./fileuris.js";
import { themeName } from "./monaco.js";

const RENDER_MS = 60;
const FENCE_LANGUAGES = { js: "javascript", ts: "typescript", py: "python", sh: "shell", bash: "shell", ps1: "powershell", "c++": "cpp", cs: "csharp", rs: "rust", md: "markdown", yml: "yaml" };

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

export class MarkdownPreview {
	constructor(ctx, path, monaco) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.path = path;
		this.monaco = monaco;
		this.folder = isFileUri(path) ? null : path.slice(0, Math.max(0, path.lastIndexOf("/")));
		this.text = "";
		this.timer = null;
		this.body = h("div.md-preview", { tabindex: "0" });
		ctx.el.append(this.body);
		this.body.addEventListener("click", (event) => this.clicked(event));
		this.sync = new DocumentSync(this.ied.client, {
			setText: (text) => this.changed(text),
			applyRemote: (changes) => this.changed(applyChanges(this.text, changes)),
			setReadOnly: () => {},
		}, { onState: (state) => this.stateChanged(state) });
		this.sync.open(ctx.wsId, path).catch((e) => replace(this.body, h("div.md-problem", null, "The document could not be opened: " + e.message)));
	}

	dispose() {
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

	// A fenced block colored by Monaco when it knows the language, in IED's theme (its colorizer uses the theme set last,
	// Monaco's light one before any editor exists); plain text until it answers.
	colored(code, language) {
		const element = h("code", null, code);
		const id = FENCE_LANGUAGES[language.toLowerCase()] || language.toLowerCase();
		if (this.monaco && id && this.monaco.languages.getLanguages().some((l) => l.id === id)) {
			this.monaco.editor.setTheme(themeName());
			this.monaco.editor.colorize(code, id, {}).then((html) => {
				element.innerHTML = html; // Monaco's colorizer escapes the text it wraps
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
		else image.setAttribute("src", "/w/" + encodeURIComponent(this.ctx.wsId) + "/files/" + path.split("/").map(encodeURIComponent).join("/"));
	}

	clicked(event) {
		const box = event.target.closest("input[data-task]");
		if (box) {
			event.preventDefault();
			this.tick(parseInt(box.getAttribute("data-task"), 10), !box.hasAttribute("checked")).catch((e) => this.ied.report("ticking a task: " + e.message, e));
			return;
		}
		const link = event.target.closest("a[href]");
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
