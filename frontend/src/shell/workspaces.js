// workspaces.js - the ways to a project (F-SHL-1, docs/15 "Start page"): the start page and the workspace switcher list
// the workspaces open on the server, the owner's favorites and the projects opened lately (the server's recent list,
// docs/10 "Workspaces"). A project of the list opens again from any client (ws.openRecent); a new one opens on the
// machine: `ied <path>`, or File > Open Folder in the desktop app - the menu `shell/openWorkspace` holds those ways.

import { h, icon, iconFrom } from "/ui-kit/dom.js";
import { FILE, FOLDER, SOLUTION } from "/ui-kit/fileglyphs.js";

const ON_THE_SERVER = "ied <folder or .iedsln>";
const SWITCHER_RECENT = 10;

function folded(path) {
	return (path || "").replace(/\\/g, "/").replace(/\/+$/, "").toLowerCase();
}

// Whether a recent project is the open workspace `ws`: a solution by its file's folder, anything else by its folder.
export function isProjectOf(project, ws) {
	if (project.kind === "solution") return ws.kind === "solution" && folded(project.path).replace(/\/[^/]*$/, "") === folded(ws.root);
	return ws.kind !== "solution" && folded(project.path) === folded(ws.root);
}

// The lists both views show: the open workspaces (each with its recent entry, for its star), then the favorites and the
// other recent projects that are not open, newest first.
export function projectGroups(server) {
	const recent = server.recent || [];
	const open = (server.workspaces || []).map((ws) => ({ ws, project: recent.find((p) => isProjectOf(p, ws)) || null }));
	const closed = recent.filter((p) => !open.some((o) => o.project === p));
	return { open, favorites: closed.filter((p) => p.favorite), recent: closed.filter((p) => !p.favorite) };
}

// "just now", "5 min ago", "3 h ago", "2 days ago", then the date.
export function whenOpened(ms, now) {
	const minutes = Math.floor((now - ms) / 60000);
	if (minutes < 1) return "just now";
	if (minutes < 60) return minutes + " min ago";
	if (minutes < 24 * 60) return Math.floor(minutes / 60) + " h ago";
	const days = Math.floor(minutes / (24 * 60));
	if (days < 7) return days === 1 ? "yesterday" : days + " days ago";
	return new Date(ms).toLocaleDateString(undefined, { year: "numeric", month: "short", day: "numeric" });
}

export function showWorkspaceSwitcher(shell, at) {
	const groups = projectGroups(shell.server);
	const rows = groups.open.map(({ ws, project }) => ({
		kind: "item", label: ws.name + (ws.wsId === shell.wsId ? "  (shown)" : ""), icon: ws.wsId === shell.wsId ? "$(check)" : project && project.favorite ? "$(star-full)" : kindIcon(ws.kind), base: "/",
		enabled: true, keys: ws.root, wsId: ws.wsId,
	}));
	if (!rows.length) rows.push({ kind: "item", label: "No workspace is open", enabled: false });
	const closedRow = (p, glyph) => ({ kind: "item", label: p.name, icon: glyph, base: "/", enabled: p.exists !== false, keys: p.path, path: p.path });
	if (groups.favorites.length) rows.push({ kind: "separator" }, ...groups.favorites.map((p) => closedRow(p, "$(star-full)")));
	if (groups.recent.length) rows.push({ kind: "separator" }, ...groups.recent.slice(0, SWITCHER_RECENT).map((p) => closedRow(p, kindIcon(p.kind))));
	rows.push({ kind: "separator" }, ...shellRows(shell, groups));
	shell.menuHost.show({ rows, at: at || document.querySelector(".sb-ws") || { x: 48, y: 8 }, label: "Workspaces", minWidth: 320, pick: (row) => pickRow(shell, row) });
}

// The shown workspace's star, the start page and the ways to open a new project.
function shellRows(shell, groups) {
	const shown = groups.open.find((o) => o.ws.wsId === shell.wsId);
	const rows = [];
	if (shown && shown.project) {
		const favorite = shown.project.favorite;
		rows.push({ kind: "item", label: favorite ? "Remove from Favorites" : "Add to Favorites", icon: favorite ? "$(star-full)" : "$(star-empty)", base: "/", enabled: true, favorite: { path: shown.project.path, value: !favorite } });
	}
	if (shell.wsId) rows.push({ kind: "item", label: "Start Page", icon: "$(home)", base: "/", enabled: true, command: "shell.startPage" });
	return [...rows, ...shell.menus.resolve("shell/openWorkspace")];
}

function pickRow(shell, row) {
	if (row.command) return shell.commands.run(row.command, row.args);
	if (row.favorite) return shell.setFavorite(row.favorite.path, row.favorite.value);
	if (row.path) return shell.openProject(row.path);
	return shell.showWorkspace(row.wsId);
}

// IED's start page: the projects to go back to, how to open another, and the User settings - or those settings, in its
// place, while they are shown (startsettings.js).
export function startPageView(shell, now) {
	if (shell.startSettings && shell.startSettings.shown) return shell.startSettings.element();
	const groups = projectGroups(shell.server);
	const sections = [
		section("Open", groups.open.map(({ ws, project }) => projectRow(shell, project, ws, now))),
		section("Favorites", groups.favorites.map((p) => projectRow(shell, p, null, now))),
		section("Recent", groups.recent.map((p) => projectRow(shell, p, null, now))),
	].filter(Boolean);
	const projects = h("section.sp-projects", null, h("h2", null, "Open recent"),
		...(sections.length ? sections : [h("p.sp-none", null, "The folders and solutions you open show up here.")]));
	const ways = shell.menus.resolve("shell/openWorkspace").filter((row) => row.kind === "item").map((row) => h("button.btn", {
		title: row.keys ? row.label + " (" + row.keys + ")" : null, disabled: !row.enabled, on: { click: () => shell.commands.run(row.command, row.args) },
	}, row.label));
	const keys = shell.keybindings ? shell.keybindings.labelFor("shell.settings") : "";
	const settings = h("button.btn.sp-settings", { title: "Settings" + (keys ? " (" + keys + ")" : ""), on: { click: () => shell.commands.run("shell.settings") } }, icon("settings-gear"), "Settings");
	const start = h("section.sp-start", null, h("h2", null, "Get started"), ...ways, h("p", null, (ways.length ? "Or on the server: " : "Open a project on the server: ") + ON_THE_SERVER),
		h("h2", null, "Customize"), settings, h("p", null, "Your User settings - AI models, the editor, keyboard shortcuts - apply to every project."));
	return h("div.start-page", null, projects, start);
}

function section(title, rows) {
	return rows.length ? h("div.sp-section", null, h("h3", null, title), ...rows) : null;
}

// One project: open workspaces show; the others open again (unless gone). Its star and, when not open, its removal.
function projectRow(shell, project, ws, now) {
	const gone = !ws && project.exists === false;
	const open = () => ws ? shell.showWorkspace(ws.wsId) : gone ? null : shell.openProject(project.path);
	const star = project ? h("button.ib" + (project.favorite ? ".on" : ""), {
		title: project.favorite ? "Remove from Favorites" : "Add to Favorites", "aria-label": project.favorite ? "Remove from Favorites" : "Add to Favorites",
		on: { click: (event) => { event.stopPropagation(); shell.setFavorite(project.path, !project.favorite); } },
	}, icon(project.favorite ? "star-full" : "star-empty")) : h("span.sp-slot");
	// An open workspace stays listed while it is open; the slot keeps the columns in line.
	const forget = !ws ? h("button.ib", {
		title: "Remove from the list", "aria-label": "Remove from the list", on: { click: (event) => { event.stopPropagation(); shell.forgetProject(project.path); } },
	}, icon("close")) : h("span.sp-slot");
	const name = ws ? ws.name : project.name;
	const path = ws ? ws.root : project.path;
	const when = ws ? openState(ws, shell.wsId) : gone ? "not found" : whenOpened(project.opened, now || Date.now());
	return h("div.sp-row" + (gone ? ".gone" : ""), {
		role: "button", tabindex: gone ? "-1" : "0", title: path, dataset: { path },
		on: { click: open, keydown: (event) => event.key === "Enter" && open() },
	}, iconFrom(kindIcon(ws ? ws.kind : project.kind), "/"), h("div.sp-text", null, h("span.sp-name", null, name), h("span.sp-path", null, path)), h("span.sp-when", null, when), star, forget);
}

// An open workspace's place on the start page: shown or open, and its server's state while it is not ready
// (docs/01 "Processes").
export function openState(ws, shownId) {
	const base = ws.wsId === shownId ? "shown" : "open";
	const state = ws.server ? ws.server.state : "ready";
	return state === "ready" ? base : base + ", server " + state;
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
