// index.js - the Markdown plugin's UI module (docs/26 "Language plugins"): the "mdpreview:<path>" tab kind and the Open
// Preview commands, for the file a menu or a tab strip's button names or the active tab's; to the side, the preview is
// tethered to the file (docs/15 "Tethered tabs"): it shows when the file does, closes with it, and follows the Markdown
// file its group shows (follow.js).

import { fileName } from "/ui-kit/fileuris.js";
import { fenceLanguages } from "/ui-kit/markdown.js";
import { MarkdownPreview as Preview } from "./preview.js";
import { followPreviews } from "./follow.js";

export function activate(ied) {
	addStylesheet(ied.plugin.base + "ui/markdown.css");
	followPreviews(ied);
	ied.tabs.kind("mdpreview", { view: "MarkdownPreview", title: (path) => "Preview " + fileName(path), icon: () => "$(open-preview)", resource: (path) => path });
	ied.commands.handle("ied.markdown.preview", (args) => {
		const path = args && args.path ? args.path : ied.context.get("resourcePath");
		if (path) ied.tabs.open("mdpreview:" + path, { preview: false });
	});
	ied.commands.handle("ied.markdown.previewBeside", (args) => {
		const path = args && args.path ? args.path : ied.context.get("resourcePath");
		if (path) ied.tabs.openBeside("doc:" + path, "mdpreview:" + path, { tether: true, closes: true });
	});
}

export function MarkdownPreview(ctx) {
	const languages = ctx.ied.contributions("languages").map((c) => c.value);
	return new Preview(ctx, ctx.ref.slice("mdpreview:".length), fenceLanguages(languages));
}

function addStylesheet(href) {
	if (document.querySelector('link[href="' + href + '"]')) return;
	document.head.append(Object.assign(document.createElement("link"), { rel: "stylesheet", href }));
}
