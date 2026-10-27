// preview.js - the Markdown preview ("mdpreview:<path>" tabs, F-EDT-91/92): the server document rendered as it
// changes - typing in its editor, another writer, the disk - with fenced code colored as the editor colors it (the
// `editor.colorize` binding, when an editor plugin provides it) and the workspace's images (`/w/<wsId>/files/`,
// docs/10). Ticking a task box edits its line through the document store, taking the lease as typing would; a link to a
// workspace file, clicked or middle-clicked, opens it in a tab of the editor, a web link in a Browser tab
// (linkclicks.js); a workspace image clicked shows at its size.
// It scrolls with its file's editor (`editor.scrolled`). A double-click does what it does in any prose: selects the
// text under the pointer. The source of the block clicked is Go to Source on the preview's context menu.

import { h, replace } from "/ui-kit/dom.js";
import { DocumentSync } from "/ui-kit/docsync.js";
import { applyChanges } from "/ui-kit/textedits.js";
import { renderMarkdown, toggleTask } from "/ui-kit/markdown.js";
import { isFileUri } from "/ui-kit/fileuris.js";
import { resolveRelative } from "/ui-kit/paths.js";
import { showImage } from "/ui-kit/imageview.js";
import { answerLinks, openAddress } from "/ui-kit/linkclicks.js";

const RENDER_MS = 60;

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
		answerLinks(this.body, (target) => this.followLink(target));
		this.body.addEventListener("contextmenu", (event) => this.menu(event));
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

	// The preview's context menu (Go to Source for the block under the pointer, as VS Code's preview has it). The context
	// key says what the pointer was on, as Solution Explorer's do for their kinds; a preview of a file outside the
	// workspace has no source to go to, so the line is named only for a workspace file.
	menu(event) {
		const block = isFileUri(this.path) ? null : event.target.closest("[data-line]");
		const line = block ? Number(block.dataset.line) : undefined;
		this.ied.context.set("markdownSourceLine", line === undefined ? undefined : String(line));
		this.ied.menus.show("mdpreview/context", event, { path: this.path, line });
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
		const image = event.target.closest("img[data-path]");
		if (image && !event.target.closest("a[href]")) showImage({ src: image.getAttribute("src"), title: image.dataset.path });
	}

	// A link clicked or middle-clicked (linkclicks.js): a file of the workspace opens in a tab of this window, read from the
	// previewed file's folder - a Markdown file too, which the preview then follows when it opens in the group of the file
	// it previews (follow.js) - and a web address in a Browser tab.
	followLink(target) {
		if (target.kind !== "path") {
			openAddress(this.ied, target);
			return;
		}
		if (this.folder === null) return;
		const path = resolveRelative(this.folder, decodeURIComponent(target.href.split("#")[0]));
		if (path) this.ied.commands.run("ied.editor.goTo", { path });
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
