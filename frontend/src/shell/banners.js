// banners.js - the non-blocking bars under the toolbar: the connection to the master while it is not live (F-SHL-34; the
// views stay readable and input waits), the shown workspace's server while it is not there (servers.js), the trust
// of an untrusted workspace (F-SHL-33), and the language tools the workspace lacks (docs/26 "Language tools": Install,
// Details - Settings > Languages - and Dismiss; an install's progress).

import { h, icon } from "/ui-kit/dom.js";
import { installTools } from "./languagetools.js";
import { serverBanner } from "./servers.js";
import { toolsBanner } from "./toolsmodel.js";

const CONNECTION = {
	connecting: ["info", "sync", "Connecting to the server..."],
	reconnecting: ["warn", "sync", "The connection to the server was lost. Reconnecting... what you type is kept and sent once it is back."],
	closed: ["error", "debug-disconnect", "Disconnected from the server."],
};

export function banners(shell, state) {
	const out = [];
	// While the shown workspace's server restarts the master's socket stays up: its banner says so, not "the connection
	// was lost" (docs/01 "Processes"). The connection banner is the master's.
	const masterState = shell.client && shell.client.master ? shell.client.master.state : state;
	const server = masterState === "live" ? serverBanner(shell) : null;
	if (server) out.push(server);
	else if (masterState === "live" && state !== "live" && shell.wsId) {
		out.push(h("div.banner.info", { role: "status", dataset: { banner: "server" } }, icon("sync", "spin"),
			h("span", null, "Connecting to this workspace's server... what you type is kept and sent once it is there.")));
	}
	const connection = CONNECTION[masterState];
	if (connection) {
		const [level, glyph, text] = connection;
		out.push(h("div.banner." + level, { role: "alert", dataset: { banner: "connection" } }, icon(glyph, masterState === "closed" ? "" : "spin"), h("span", null, text),
			masterState === "closed" ? h("button.btn", { on: { click: () => location.reload() } }, "Reload") : null));
	}
	if (shell.wsId && shell.workspace.wsId === shell.wsId && !shell.workspace.trusted) {
		out.push(h("div.banner.warn", { role: "status", dataset: { banner: "trust" } }, icon("shield"),
			h("span", null, "This workspace is not trusted: its project-local plugins and anything that runs its code stay off."),
			h("button.btn", { on: { click: () => shell.commands.run("shell.trustWorkspace") } }, "Trust Workspace")));
	}
	const tools = shell.wsId && shell.tools && shell.tools.wsId === shell.wsId ? toolsBanner(shell.tools.snapshot) : null;
	if (tools) {
		out.push(h("div.banner." + tools.tone, { role: "status", dataset: { banner: "tools" } }, icon(tools.busy ? "sync" : "tools", tools.busy ? "spin" : ""), h("span", null, tools.text),
			tools.install.length ? h("button.btn", { on: { click: () => installTools(shell, shell.wsId, shell.tools.snapshot, tools.install) } }, "Install") : null,
			tools.busy ? h("button.btn", { on: { click: () => shell.commands.run("ied.output.show", { sourceId: "tools" }) } }, "Show Output") : null,
			h("button.btn", { on: { click: () => shell.commands.run("shell.languageTools") } }, "Details"),
			tools.busy ? null : h("button.ib", { title: "Dismiss", "aria-label": "Dismiss", on: { click: () => shell.client.request("tools.dismiss", { wsId: shell.wsId }).catch((e) => shell.report(e.message, e)) } }, icon("close"))));
	}
	return out;
}
