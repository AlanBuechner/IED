// desktop.test.js - the desktop app in the web client (docs/27): a new window's URL (plugins/desktop), and the shell's
// File > Open Folder, there only inside the desktop app, in the File menu, the workspace switcher's ways and as the
// start page's button.

import { test, assertEqual, assertTrue } from "./harness.js";
import { workspaceUrl } from "/plugins/ied.desktop/ui/index.js";
import { Commands } from "../src/shell/commands.js";
import { ContextKeys } from "../src/shell/context.js";
import { Keybindings } from "../src/shell/keybindings.js";
import { Menus } from "../src/shell/menus.js";
import { registerShellCommands } from "../src/shell/hostcommands.js";
import { startPageView } from "../src/shell/workspaces.js";

function shellWith(desktopApp) {
	const reports = [];
	const context = new ContextKeys();
	const commands = new Commands(context, (message) => reports.push(message));
	const keybindings = new Keybindings(commands, context, { report: (message) => reports.push(message) });
	const shell = { context, commands, keybindings, menus: new Menus(commands, context, keybindings), contributions: { toolWindows: [], activities: [] }, reports, server: { workspaces: [], recent: [] } };
	registerShellCommands(shell);
	context.set("desktopApp", desktopApp);
	return shell;
}

function labels(rows) {
	return rows.filter((row) => row.kind === "item").map((row) => row.label);
}

test("desktop: a new window opens on this window's workspace", () => {
	assertEqual(workspaceUrl("http://127.0.0.1:8765", "w2"), "http://127.0.0.1:8765/#open=w2/");
	assertEqual(workspaceUrl("http://127.0.0.1:8765", null), "http://127.0.0.1:8765/", "IED's home without one");
});

test("desktop: Open Folder is there only inside the desktop app", () => {
	const browser = shellWith(false);
	assertTrue(!labels(browser.menus.resolve("menubar/file")).includes("Open Folder..."), "not in a browser's File menu");
	assertTrue(labels(browser.menus.resolve("menubar/file")).includes("Start Page"), "the start page is everywhere");
	assertEqual(labels(browser.menus.resolve("shell/openWorkspace")), ["New Project..."], "a browser makes new projects, but opens none");
	const desktop = shellWith(true);
	assertEqual(labels(desktop.menus.resolve("menubar/file")).slice(0, 2), ["New Project...", "Open Folder..."], "first in File, after New Project");
	assertEqual(desktop.menus.resolve("shell/openWorkspace").find((row) => row.command === "shell.openFolder").keys, "Ctrl+Shift+Alt+O");
});

test("desktop: the start page offers Open Folder, which asks the app", async () => {
	const plain = startPageView(shellWith(false));
	assertEqual([...plain.querySelectorAll(".sp-start button")].map((b) => b.textContent), ["New Project..."], "a browser: New Project only");
	assertEqual(plain.querySelector(".sp-start p").textContent, "Or on the server: ied <folder or .iedsln>");
	const shell = shellWith(true);
	const view = startPageView(shell);
	assertEqual([...view.querySelectorAll(".sp-start button")].map((b) => b.textContent), ["New Project...", "Open Folder..."]);
	const button = [...view.querySelectorAll(".sp-start button")].find((b) => b.textContent === "Open Folder...");
	assertEqual(view.querySelector(".sp-start p").textContent, "Or on the server: ied <folder or .iedsln>", "the command line too");
	let asked = 0;
	const before = Object.getOwnPropertyDescriptor(window, "iedDesktop");
	Object.defineProperty(window, "iedDesktop", { value: { openFolder: () => { asked += 1; } }, configurable: true });
	try {
		await shell.commands.run("shell.openFolder");
	}
	finally {
		if (before) Object.defineProperty(window, "iedDesktop", before);
		else delete window.iedDesktop;
	}
	assertEqual(asked, 1, "the desktop app's picker");
	assertEqual(shell.reports, []);
});
