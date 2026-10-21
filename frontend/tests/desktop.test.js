// desktop.test.js - the desktop app in the web client (docs/27): a new window's URL (plugins/desktop), and the shell's
// File > Open Folder, there only inside the desktop app, in the File menu, the workspace switcher's ways and as the
// button of a page without a workspace.

import { test, assertEqual, assertTrue } from "./harness.js";
import { workspaceUrl } from "/plugins/ied.desktop/ui/index.js";
import { Commands } from "../src/shell/commands.js";
import { ContextKeys } from "../src/shell/context.js";
import { Keybindings } from "../src/shell/keybindings.js";
import { Menus } from "../src/shell/menus.js";
import { registerShellCommands } from "../src/shell/hostcommands.js";
import { noWorkspaceView } from "../src/shell/workspaces.js";

function shellWith(desktopApp) {
	const reports = [];
	const context = new ContextKeys();
	const commands = new Commands(context, (message) => reports.push(message));
	const keybindings = new Keybindings(commands, context, { report: (message) => reports.push(message) });
	const shell = { context, commands, keybindings, menus: new Menus(commands, context, keybindings), contributions: { toolWindows: [], activities: [] }, reports };
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
	assertEqual(browser.menus.resolve("shell/openWorkspace"), [], "no way to open one from a browser");
	const desktop = shellWith(true);
	assertEqual(labels(desktop.menus.resolve("menubar/file"))[0], "Open Folder...", "first in File");
	assertEqual(desktop.menus.resolve("shell/openWorkspace")[0].keys, "Ctrl+Shift+Alt+O");
});

test("desktop: a page without a workspace offers Open Folder, which asks the app", async () => {
	const plain = noWorkspaceView(shellWith(false));
	assertEqual(plain.querySelectorAll("button").length, 0);
	assertEqual(plain.textContent, "No workspace is open. Open one on the server: ied <folder or .ied.sln>");
	const shell = shellWith(true);
	const view = noWorkspaceView(shell);
	const button = view.querySelector("button");
	assertEqual(button.textContent, "Open Folder...");
	assertTrue(view.textContent.includes("Or on the server: ied <folder or .ied.sln>"), "the command line too");
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
