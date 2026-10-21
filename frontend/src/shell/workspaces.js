// workspaces.js - the workspace switcher (F-SHL-1): the server's open workspaces as a menu; the chosen one is shown in
// this tab. A workspace opens on the server: `ied <path>` (the control channel), or File > Open Folder in the desktop
// app, which picks the folder and asks the same channel; the menu `shell/openWorkspace` holds those ways.

import { h, icon } from "/ui-kit/dom.js";
import { FILE, FOLDER, SOLUTION } from "/ui-kit/fileglyphs.js";

const ON_THE_SERVER = "ied <folder or .ied.sln>";

export function showWorkspaceSwitcher(shell, at) {
	const rows = shell.server.workspaces.map((ws) => ({
		kind: "item", label: ws.name + (ws.wsId === shell.wsId ? "  (shown)" : ""), icon: ws.wsId === shell.wsId ? "$(check)" : kindIcon(ws.kind), base: "/",
		enabled: true, keys: ws.root, wsId: ws.wsId,
	}));
	if (!rows.length) rows.push({ kind: "item", label: "No workspace is open: run `ied <folder>` on the server", enabled: false });
	const ways = shell.menus.resolve("shell/openWorkspace");
	if (ways.length) rows.push({ kind: "separator" }, ...ways);
	const pick = (row) => row.command ? shell.commands.run(row.command, row.args) : shell.showWorkspace(row.wsId);
	shell.menuHost.show({ rows, at: at || document.querySelector(".sb-ws") || { x: 48, y: 8 }, label: "Workspaces", minWidth: 280, pick });
}

// What a page without a workspace shows: the ways to open one as buttons, and the server's command line.
export function noWorkspaceView(shell) {
	const ways = shell.menus.resolve("shell/openWorkspace").filter((row) => row.kind === "item");
	const buttons = ways.map((row) => h("button.btn", {
		title: row.keys ? row.label + " (" + row.keys + ")" : null, disabled: !row.enabled, on: { click: () => shell.commands.run(row.command, row.args) },
	}, row.label));
	const parts = buttons.length
		? [h("p", null, "No workspace is open."), h("div.empty-actions", null, ...buttons), h("p", null, "Or on the server: " + ON_THE_SERVER)]
		: [h("p", null, "No workspace is open. Open one on the server: " + ON_THE_SERVER)];
	return h("div.empty-area", null, icon("folder-opened", "lg"), ...parts);
}

export function kindIcon(kind) {
	if (kind === "solution") return SOLUTION;
	if (kind === "file") return FILE;
	return FOLDER;
}

// One or two letters for the sidebar's workspace button.
export function initials(name) {
	const words = (name || "?").split(/[^A-Za-z0-9]+/).filter(Boolean);
	if (words.length >= 2) return (words[0][0] + words[1][0]).toUpperCase();
	const word = words[0] || "?";
	const capitals = word.match(/[A-Z]/g);
	return (capitals && capitals.length >= 2 ? capitals[0] + capitals[1] : word.slice(0, 2)).toUpperCase();
}
