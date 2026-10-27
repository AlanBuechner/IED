// index.js - the MCP plugin's UI module (docs/04 "MCP"): Settings > AI > MCP Servers, its settingsPages view.

import { McpPage } from "./mcppage.js";

export function activate(ied) {
	addStylesheet(ied.plugin.base + "ui/mcp.css");
}

export function McpSettings(ctx) {
	addStylesheet(ctx.ied.plugin.base + "ui/mcp.css");
	return new McpPage(ctx);
}

function addStylesheet(href) {
	if (document.querySelector('link[href="' + href + '"]')) return;
	document.head.append(Object.assign(document.createElement("link"), { rel: "stylesheet", href }));
}
