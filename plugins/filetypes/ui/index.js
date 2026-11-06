// index.js - the File Types plugin's UI module (docs/33): the tabs that open files which are not text - "image:<path>",
// "media:<path>", "pdf:<path>", "font:<path>", "hex:<path>" - named by the manifest's fileViewers; an SVG's picture to
// the side of its text, tethered to it; Open in Hex Viewer and Download for the active tab's file, or the Solution
// Explorer's.

import { fileName } from "/ui-kit/fileuris.js";
import { addStylesheet, saveFile } from "./chrome.js";
import { ImageViewer } from "./image/viewer.js";
import { MediaPlayer } from "./media.js";
import { PdfViewer } from "./pdf.js";
import { FontViewer } from "./font.js";
import { HexViewer } from "./hex.js";

const KINDS = { image: "ImageDocument", media: "MediaDocument", pdf: "PdfDocument", font: "FontDocument", hex: "HexDocument" };

const pathOf = (ied, args) => (args && args.path ? args.path : ied.context.get("resourcePath"));
const isSvg = (path) => /\.svg$/i.test(path);

export function activate(ied) {
	addStylesheet(ied.plugin.base + "ui/filetypes.css");
	for (const [kind, view] of Object.entries(KINDS)) {
		ied.tabs.kind(kind, {
			view,
			title: (path) => (kind === "image" && isSvg(path) ? "Preview " + fileName(path) : fileName(path)),
			icon: (path) => (kind === "hex" ? "$(file-binary)" : ied.icons.fileSpec(fileName(path))),
			resource: (path) => path,
		});
	}
	ied.commands.handle("ied.filetypes.previewBeside", (args) => {
		const path = pathOf(ied, args);
		if (path) ied.tabs.openBeside("doc:" + path, "image:" + path, { tether: true, closes: true });
	});
	ied.commands.handle("ied.filetypes.openHex", (args) => {
		const path = pathOf(ied, args);
		if (path) ied.files.open(path, { preview: false, view: "hex" });
	});
	ied.commands.handle("ied.filetypes.download", (args) => {
		const path = pathOf(ied, args);
		if (path) saveFile({ wsId: ied.workspace.wsId }, path);
	});
}

// Each tab's view, for its path; the e2e checks find the viewer on the tab's element.
function mount(ctx, Viewer) {
	const viewer = new Viewer(ctx, ctx.ref.slice(ctx.ref.indexOf(":") + 1));
	ctx.el.fileViewer = viewer;
	return viewer;
}

export const ImageDocument = (ctx) => mount(ctx, ImageViewer);
export const MediaDocument = (ctx) => mount(ctx, MediaPlayer);
export const PdfDocument = (ctx) => mount(ctx, PdfViewer);
export const FontDocument = (ctx) => mount(ctx, FontViewer);
export const HexDocument = (ctx) => mount(ctx, HexViewer);
