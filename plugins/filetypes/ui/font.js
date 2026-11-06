// font.js - the font viewer ("font:<path>" tabs): a TTF, OTF, WOFF or WOFF2 loaded into the page (FontFace) and shown -
// its names, version and glyph count; sample text you can type over, at the sizes code and UI use; and the printable
// ASCII and Latin-1 characters. Reloaded when the file changes on disk.

import { h, replace } from "/ui-kit/dom.js";
import { fileUrl, fetchFile, watchFile } from "./fileurl.js";
import { downloadButton, formatSize, notice, openWithButton } from "./chrome.js";
import { fontNames } from "./fontnames.js";

const SIZES = [12, 14, 16, 20, 24, 32, 48, 72];
const SAMPLE = "The quick brown fox jumps over the lazy dog. 0123456789 {}[]()<>=+-*/&|!?;:";

let fonts = 0;

export class FontViewer {
	constructor(ctx, path) {
		this.ctx = ctx;
		this.path = path;
		this.face = null;
		this.family = "ied-font-" + ++fonts;
		this.info = h("span.ft-info");
		this.sample = h("input.ft-font-sample", { type: "text", value: SAMPLE, "aria-label": "Sample text", on: { input: () => this.renderSizes() } });
		this.body = h("div.ft-font-body");
		this.stage = h("div.ft-stage.ft-font-stage", null, this.body);
		ctx.el.classList.add("ft-view");
		ctx.el.append(h("div.ft-tools", null, this.info, h("span.grow"), downloadButton(ctx, path)), this.stage);
		this.stopWatch = watchFile(ctx.ied, ctx.wsId, path, (change) => (change === "deleted" ? this.problem("The file was deleted.") : this.load()));
		this.load();
	}

	async load() {
		try {
			const bytes = await fetchFile(fileUrl(this.ctx.wsId, this.path));
			const face = new FontFace(this.family, bytes.slice().buffer);
			await face.load();
			if (this.face) document.fonts.delete(this.face);
			document.fonts.add(face);
			this.face = face;
			this.names = await fontNames(bytes);
			this.render(bytes.length);
		}
		catch (e) {
			this.problem("The browser cannot read this font: " + (e.message || e) + ".");
		}
	}

	render(size) {
		const n = this.names;
		replace(this.info, [n.family ? n.family + (n.style ? " " + n.style : "") : null, n.kind, n.glyphs ? n.glyphs + " glyphs" : null, formatSize(size)]
			.filter(Boolean).map((p) => h("span.ft-chip", null, p)));
		this.sizes = h("div.ft-font-sizes");
		const characters = [];
		for (let c = 0x21; c <= 0x7e; c++) characters.push(c);
		for (let c = 0xa1; c <= 0xff; c++) if (c !== 0xad) characters.push(c);
		replace(this.body,
			h("div.ft-font-head", { style: { "font-family": this.family } }, n.full || n.family || this.path.split("/").pop()),
			h("div.ft-font-meta.muted", null, [n.version, n.copyright].filter(Boolean).join("   ")),
			this.sample,
			this.sizes,
			h("div.ft-font-grid", { style: { "font-family": this.family } }, characters.map((c) => h("span.ft-glyph", { title: "U+" + c.toString(16).toUpperCase().padStart(4, "0") }, String.fromCharCode(c)))));
		this.renderSizes();
	}

	renderSizes() {
		if (!this.sizes) return;
		replace(this.sizes, SIZES.map((px) => h("div.ft-font-line", null,
			h("span.ft-font-px.muted", null, px + " px"),
			h("span.ft-font-text", { style: { "font-family": this.family, "font-size": px + "px" } }, this.sample.value))));
	}

	problem(message) {
		replace(this.body, notice(message, [openWithButton(this.ctx, this.path, "hex", "Open in Hex Viewer")]));
	}

	dispose() {
		this.stopWatch();
		if (this.face) document.fonts.delete(this.face);
	}
}
