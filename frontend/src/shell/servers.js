// servers.js - the workspace servers as the shell shows them (docs/01 "Processes", docs/10 "Workspaces"): the status
// bar's item for the shown workspace's server and its menu, the banner while that server is down, and the Workspace
// Servers tab (shell:servers) listing the master and every workspace's server with Restart and Close.

import { h, icon, replace } from "/ui-kit/dom.js";

const LOOKS = {
	starting: { icon: "sync", text: "Server starting", tone: "info" },
	ready: { icon: "server-process", text: "Server", tone: "" },
	restarting: { icon: "sync", text: "Server restarting", tone: "info" },
	stopping: { icon: "debug-stop", text: "Server stopping", tone: "" },
	failed: { icon: "error", text: "Server failed", tone: "err" },
};

export function serverLook(state) {
	return LOOKS[state] || { icon: "server-process", text: "Server " + state, tone: "" };
}

// "3 min", "2 h 5 min", "4 days": how long ago `ms` was, at `now`.
export function upFor(ms, now) {
	const minutes = Math.max(0, Math.floor((now - ms) / 60000));
	if (minutes < 1) return "under a minute";
	if (minutes < 60) return minutes + " min";
	if (minutes < 24 * 60) return Math.floor(minutes / 60) + " h " + (minutes % 60) + " min";
	const days = Math.floor(minutes / (24 * 60));
	return days === 1 ? "1 day" : days + " days";
}

// The tooltip of a workspace's server: its process, how long it has run, its restarts and how its last run ended.
export function serverTitle(name, server, now) {
	const lines = ["Workspace server of " + (name || "this workspace") + ": " + server.state];
	if (server.pid && server.state === "ready") lines.push("pid " + server.pid + ", up " + upFor(server.startedMs, now || Date.now()));
	if (server.restarts) lines.push("restarted " + server.restarts + (server.restarts === 1 ? " time" : " times"));
	if (server.problem) lines.push(server.problem);
	if (server.log) lines.push("log: " + server.log);
	return lines.join("\n");
}

export function showServerMenu(shell, at) {
	const rows = [
		{ kind: "item", label: "Restart Workspace Server", icon: "$(debug-restart)", base: "/", enabled: Boolean(shell.wsId), command: "shell.restartWorkspaceServer" },
		{ kind: "item", label: "Workspace Servers", icon: "$(server-process)", base: "/", enabled: true, command: "shell.workspaceServers" },
		{ kind: "separator" },
		{ kind: "item", label: "Close Workspace", icon: "$(close)", base: "/", enabled: Boolean(shell.wsId), command: "shell.closeWorkspace" },
	];
	shell.menuHost.show({ rows, at, label: "Workspace server", minWidth: 240, pick: (row) => row.command && shell.commands.run(row.command) });
}

// The banner while the shown workspace's server is not there: restarting after an exit, or failed (with Restart).
export function serverBanner(shell) {
	const server = shell.wsId ? shell.workspaceServer(shell.wsId) : null;
	if (!server || (server.state !== "restarting" && server.state !== "failed")) return null;
	if (server.state === "restarting") {
		const why = server.problem ? " (" + server.problem + ")" : "";
		return h("div.banner.info", { role: "status", dataset: { banner: "server" } }, icon("sync", "spin"),
			h("span", null, "This workspace's server is restarting" + why + ". Its chats, builds and terminals come back with it; the other workspaces go on."));
	}
	return h("div.banner.error", { role: "alert", dataset: { banner: "server" } }, icon("error"),
		h("span", null, "This workspace's server stopped: " + (server.problem || "it ended three times within a minute") + ". " + (server.log ? "Its log: " + server.log : "")),
		h("button.btn", { on: { click: () => shell.restartWorkspaceServer() } }, "Restart"));
}

const CHIPS = { starting: "info", ready: "ok", restarting: "info", stopping: "", failed: "err" };

// The Workspace Servers tab: the master and each open workspace's server, live from the server topic.
export function serversView(shell, ctx) {
	const body = h("div.servers");
	function row(ws) {
		const server = ws.server || { state: "starting" };
		return h("tr", { dataset: { ws: ws.wsId } },
			h("td", null, h("b", null, ws.name), h("div.muted.mono", null, ws.root)),
			h("td", null, h("span.chip" + (CHIPS[server.state] ? "." + CHIPS[server.state] : ""), null, server.state)),
			h("td.mono", null, server.pid ? String(server.pid) : ""),
			h("td", null, server.startedMs && server.state === "ready" ? upFor(server.startedMs, Date.now()) : ""),
			h("td", null, String(server.restarts || 0)),
			h("td", { title: server.log || "" }, server.problem || (server.exitCode !== undefined ? "code " + server.exitCode : "")),
			h("td.actions", null,
				h("button.btn", { title: "Stop this workspace's server and start it again", on: { click: () => shell.restartWorkspaceServer(ws.wsId) } }, icon("debug-restart"), "Restart"),
				h("button.btn", { title: "Close the workspace: its server stops", on: { click: () => shell.closeWorkspace(ws.wsId) } }, "Close")));
	}
	function render() {
		const master = shell.server.master;
		const workspaces = shell.server.workspaces || [];
		replace(body,
			h("p.muted", null, master ? "Master server: pid " + master.pid + ", up " + upFor(master.startedMs, Date.now()) + ". It holds the sign-in, the list of workspaces and the app's settings, and starts one server per workspace." : "The master server holds the sign-in and the list of workspaces, and starts one server per workspace."),
			workspaces.length ? h("table.servers-table", null,
				h("thead", null, h("tr", null, ...["Workspace", "State", "PID", "Up", "Restarts", "Last exit", ""].map((t) => h("th", null, t)))),
				h("tbody", null, ...workspaces.map(row))) : h("p.muted", null, "No workspace is open."));
	}
	const stop = shell.onServerChange(render);
	ctx.el.append(h("div.page", null, h("div.page-head", null, h("h1", null, "Workspace Servers")), body));
	render();
	return { dispose: stop };
}
