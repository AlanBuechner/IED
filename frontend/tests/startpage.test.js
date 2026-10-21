// startpage.test.js - the start page and the workspace switcher (workspaces.js, docs/15 "Start page"): which projects
// are open, favorites and recent, how long ago, and what a click on each part of a row does.

import { test, assertEqual, assertTrue } from "./harness.js";
import { isProjectOf, projectGroups, whenOpened, startPageView, showWorkspaceSwitcher } from "../src/shell/workspaces.js";

const NOW = Date.UTC(2026, 9, 21, 12, 0, 0);

const RECENT = [
	{ path: "C:/work/Game", name: "Game", kind: "folder", opened: NOW - 60000 * 5, favorite: false, exists: true },
	{ path: "C:/work/Tools/Tools.ied.sln", name: "Tools", kind: "solution", opened: NOW - 3600000 * 3, favorite: true, exists: true },
	{ path: "C:/work/Shooter", name: "Shooter", kind: "folder", opened: NOW - 86400000 * 2, favorite: true, exists: true },
	{ path: "C:/old/Prototype", name: "Prototype", kind: "folder", opened: NOW - 86400000 * 40, favorite: false, exists: false },
];
const OPEN = [{ wsId: "w1", name: "Game", kind: "folder", root: "c:\\work\\game\\", trusted: true }, { wsId: "w2", name: "Tools", kind: "solution", root: "C:/work/Tools", trusted: true }];

function fakeShell(wsId) {
	const calls = [];
	const shell = {
		calls, wsId, server: { workspaces: OPEN, recent: RECENT },
		menus: { resolve: () => [] },
		commands: { run: (id) => calls.push(["run", id]) },
		showWorkspace: (id) => calls.push(["show", id]),
		openProject: (path) => calls.push(["open", path]),
		setFavorite: (path, value) => calls.push(["favorite", path, value]),
		forgetProject: (path) => calls.push(["forget", path]),
		menuHost: { show: (options) => { shell.menu = options; } },
	};
	return shell;
}

function rowOf(view, path) {
	return [...view.querySelectorAll(".sp-row")].find((row) => row.dataset.path === path);
}

test("start page: a recent project is an open workspace by its folder, a solution by its file's folder", () => {
	assertTrue(isProjectOf(RECENT[0], OPEN[0]), "case and separators do not matter");
	assertTrue(isProjectOf(RECENT[1], OPEN[1]), "Tools.ied.sln is the solution rooted at C:/work/Tools");
	assertTrue(!isProjectOf(RECENT[1], { kind: "folder", root: "C:/work/Tools" }), "not the same folder opened as a folder");
	assertTrue(!isProjectOf(RECENT[2], OPEN[0]));
});

test("start page: open workspaces first, then the favorites and the recent ones that are not open", () => {
	const groups = projectGroups({ workspaces: OPEN, recent: RECENT });
	assertEqual(groups.open.map((o) => [o.ws.wsId, o.project && o.project.name]), [["w1", "Game"], ["w2", "Tools"]]);
	assertEqual(groups.favorites.map((p) => p.name), ["Shooter"], "Tools is open");
	assertEqual(groups.recent.map((p) => p.name), ["Prototype"]);
	assertEqual(projectGroups({}).open, [], "nothing yet");
});

test("start page: when a project was opened", () => {
	assertEqual([whenOpened(NOW - 20000, NOW), whenOpened(NOW - 60000 * 5, NOW), whenOpened(NOW - 3600000 * 3, NOW)], ["just now", "5 min ago", "3 h ago"]);
	assertEqual([whenOpened(NOW - 86400000, NOW), whenOpened(NOW - 86400000 * 2, NOW)], ["yesterday", "2 days ago"]);
	assertTrue(/2026/.test(whenOpened(NOW - 86400000 * 40, NOW)), "a date after a week");
});

test("start page: rows open, star and forget; a gone one only says so", () => {
	const shell = fakeShell(null);
	const view = startPageView(shell, NOW);
	assertEqual([...view.querySelectorAll(".sp-section h3")].map((e) => e.textContent), ["Open", "Favorites", "Recent"]);
	rowOf(view, "c:\\work\\game\\").click();
	rowOf(view, "C:/work/Shooter").click();
	rowOf(view, "C:/work/Shooter").querySelector("button[aria-label='Remove from Favorites']").click();
	rowOf(view, "C:/work/Shooter").querySelector("button[aria-label='Remove from the list']").click();
	const gone = rowOf(view, "C:/old/Prototype");
	gone.click();
	assertEqual(shell.calls, [["show", "w1"], ["open", "C:/work/Shooter"], ["favorite", "C:/work/Shooter", false], ["forget", "C:/work/Shooter"]], "a star or a removal opens nothing");
	assertTrue(gone.classList.contains("gone") && gone.querySelector(".sp-when").textContent === "not found");
	assertEqual(rowOf(view, "C:/work/Tools").querySelector(".sp-when").textContent, "open");
	assertTrue(!rowOf(view, "C:/work/Tools").querySelector("button[aria-label='Remove from the list']"), "an open workspace stays listed");
	assertEqual(startPageView(Object.assign(fakeShell(null), { server: { workspaces: [], recent: [] } }), NOW).querySelector(".sp-none").textContent, "The folders and solutions you open show up here.");
});

test("start page: the switcher lists the same projects, the shown one's star and the start page", () => {
	const shell = fakeShell("w1");
	showWorkspaceSwitcher(shell, { x: 0, y: 0 });
	const rows = shell.menu.rows;
	assertEqual(rows.map((r) => r.kind === "separator" ? "-" : r.label), ["Game  (shown)", "Tools", "-", "Shooter", "-", "Prototype", "-", "Add to Favorites", "Start Page"]);
	assertTrue(rows.find((r) => r.label === "Prototype").enabled === false, "a gone one is there, disabled");
	shell.menu.pick(rows.find((r) => r.label === "Shooter"));
	shell.menu.pick(rows.find((r) => r.label === "Tools"));
	shell.menu.pick(rows.find((r) => r.label === "Add to Favorites"));
	shell.menu.pick(rows.find((r) => r.label === "Start Page"));
	assertEqual(shell.calls, [["open", "C:/work/Shooter"], ["show", "w2"], ["favorite", "C:/work/Game", true], ["run", "shell.startPage"]]);
});
