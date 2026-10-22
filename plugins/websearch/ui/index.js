// index.js - the web search plugin's UI module (docs/29): Settings > Web > Search, its settingsPages view.

import { SearchPage } from "./searchpage.js";

export function activate(ied) {
	addStylesheet(ied.plugin.base + "ui/search.css");
}

export function SearchSettings(ctx) {
	addStylesheet(ctx.ied.plugin.base + "ui/search.css");
	return new SearchPage(ctx);
}

function addStylesheet(href) {
	if (document.querySelector('link[href="' + href + '"]')) return;
	document.head.append(Object.assign(document.createElement("link"), { rel: "stylesheet", href }));
}
