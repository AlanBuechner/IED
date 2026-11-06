// pdf.js - the PDF viewer ("pdf:<path>" tabs): the browser's own PDF viewer in the tab, on the workspace's file (pages,
// zoom, search, print); reloaded when the file changes on disk. A browser that shows no PDF in a page (Chrome on
// Android) gets the file to open in its own viewer or to download.

import { h, icon, replace } from "/ui-kit/dom.js";
import { fileUrl, fetchRange, watchFile } from "./fileurl.js";
import { downloadButton, formatSize, notice, openWithButton, saveFile } from "./chrome.js";

export class PdfViewer {
	constructor(ctx, path) {
		this.ctx = ctx;
		this.path = path;
		this.info = h("span.ft-info");
		this.stage = h("div.ft-stage.ft-pdf-stage");
		const outside = h("a.ib", { href: fileUrl(ctx.wsId, path), target: "_blank", rel: "noopener", title: "Open in a browser tab of its own", "aria-label": "Open in a browser tab of its own" }, icon("link-external"));
		ctx.el.classList.add("ft-view");
		ctx.el.append(h("div.ft-tools", null, this.info, h("span.grow"), outside, downloadButton(ctx, path)), this.stage);
		this.stopWatch = watchFile(ctx.ied, ctx.wsId, path, (change) => (change === "deleted" ? this.problem("The file was deleted.") : this.load()));
		this.load();
	}

	async load() {
		const url = fileUrl(this.ctx.wsId, this.path);
		try {
			const { size } = await fetchRange(url, 0, 0);
			replace(this.info, h("span.ft-chip", null, "PDF"), h("span.ft-chip", null, formatSize(size)));
		}
		catch (e) {
			return this.problem(e.message);
		}
		if (navigator.pdfViewerEnabled === false) {
			return replace(this.stage, notice("This browser does not show PDFs inside a page.", [
				h("a.btn", { href: url, target: "_blank", rel: "noopener" }, "Open in the browser's viewer"),
				h("button.btn", { on: { click: () => saveFile(this.ctx, this.path) } }, icon("cloud-download"), " Download"),
			]));
		}
		this.frame = h("iframe.ft-pdf", { src: url, title: this.path });
		return replace(this.stage, this.frame);
	}

	problem(message) {
		replace(this.stage, notice(message, [openWithButton(this.ctx, this.path, "hex", "Open in Hex Viewer")]));
	}

	dispose() {
		this.stopWatch();
	}
}
