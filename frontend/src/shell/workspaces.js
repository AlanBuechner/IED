// workspaces.js - the workspace switcher (F-SHL-1): the server's open workspaces as a menu; the chosen one is shown in
// this tab. A workspace opens on the server (`ied <path>`, the control channel), never from the browser.

export function showWorkspaceSwitcher(shell, at) {
	const rows = shell.server.workspaces.map((ws) => ({
		kind: "item", label: ws.name + (ws.wsId === shell.wsId ? "  (shown)" : ""), icon: ws.wsId === shell.wsId ? "$(check)" : kindIcon(ws.kind), base: "/",
		enabled: true, keys: ws.root, wsId: ws.wsId,
	}));
	if (!rows.length) rows.push({ kind: "item", label: "No workspace is open: run `ied <folder>` on the server", enabled: false });
	shell.menuHost.show({ rows, at: at || document.querySelector(".sb-ws") || { x: 48, y: 8 }, label: "Workspaces", minWidth: 280, pick: (row) => shell.showWorkspace(row.wsId) });
}

export function kindIcon(kind) {
	if (kind === "solution") return "$(project)";
	if (kind === "file") return "$(file)";
	return "$(folder)";
}

// One or two letters for the sidebar's workspace button.
export function initials(name) {
	const words = (name || "?").split(/[^A-Za-z0-9]+/).filter(Boolean);
	if (words.length >= 2) return (words[0][0] + words[1][0]).toUpperCase();
	const word = words[0] || "?";
	const capitals = word.match(/[A-Z]/g);
	return (capitals && capitals.length >= 2 ? capitals[0] + capitals[1] : word.slice(0, 2)).toUpperCase();
}
