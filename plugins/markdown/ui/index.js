// index.js - the Markdown plugin's UI module (docs/26 "Language plugins"): the "mdpreview:<path>" tab kind and the Open
// Preview command, for the file a menu names or the active tab's.

import { fileName } from "/ui-kit/fileuris.js";
import { MarkdownPreview as Preview, fenceLanguages } from "./preview.js";

export function activate(ied) {
	addStylesheet(ied.plugin.base + "ui/markdown.css");
	ied.tabs.kind("mdpreview", { view: "MarkdownPreview", title: (path) => "Preview " + fileName(path), icon: () => "$(open-preview)", resource: (path) => path });
	ied.commands.handle("ied.markdown.preview", (args) => {
		const path = args && args.path ? args.path : ied.context.get("resourcePath");
		if (path) ied.tabs.open("mdpreview:" + path, { preview: false });
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
