// tabsnew.test.js - the menu of a document strip's "+" right button (docs/15 "Docking"): the activity's own way to open a
// tab comes first, then what the plugins contribute; an item's when-clause is read of the group whose "+" was pressed, so
// a row about a file is offered for the file that group shows.

import { test, assertEqual } from "./harness.js";
import { Commands } from "../src/shell/commands.js";
import { ContextKeys } from "../src/shell/context.js";
import { Keybindings } from "../src/shell/keybindings.js";
import { Menus } from "../src/shell/menus.js";
import { registerShellCommands } from "../src/shell/hostcommands.js";

// The browser plugin's rows: a page of its own everywhere, and the file the group shows as a page.
const BROWSER = [
	{ command: "ied.browser.open", group: "1_page" },
	{ command: "ied.browser.openFile", group: "2_view", when: "tabKind == 'doc' && resourceExtname == '.html'" },
];

const EDITOR = { id: "ied.editor", title: "Editor", newTab: "ied.editor.newFile" };
const CHAT = { id: "ied.chat", title: "Chat", newTab: "ied.chat.new" };

// A shell whose dock hosts are `activities`; `focus` is the group whose "+" was pressed, when one was.
function shellWith(activities, focus) {
	const context = new ContextKeys();
	const commands = new Commands(context, () => {});
	const keybindings = new Keybindings(commands, context, { report: () => {} });
	const shell = { context, commands, keybindings, contributions: { activities, toolWindows: [], activity: (id) => activities.find((a) => a.id === id) }, focus: focus || null };
	shell.menus = new Menus(commands, context, keybindings);
	commands.define({ id: "ied.editor.newFile", title: "New File" }, EDITOR.id, "/");
	commands.define({ id: "ied.chat.new", title: "New Chat" }, CHAT.id, "/");
	commands.define({ id: "ied.browser.open", title: "Web Browser", icon: "$(globe)" }, "ied.browser", "/");
	commands.define({ id: "ied.browser.openFile", title: "Open in Browser" }, "ied.browser", "/");
	for (const item of BROWSER) shell.menus.add("tabs/new", item, "ied.browser");
	registerShellCommands(shell);
	return shell;
}

const EDITOR_WELL = { activity: "ied.editor", ref: null, path: [], well: true };

function labels(shell, keys) {
	return shell.menus.resolve("tabs/new", keys).filter((row) => row.kind === "item").map((row) => row.label);
}

test("tabs new: the activity's own new tab heads the menu", () => {
	assertEqual(labels(shellWith([EDITOR, CHAT], EDITOR_WELL), {}), ["New File", "Web Browser"], "a new file, then the plugins' rows");
	assertEqual(labels(shellWith([EDITOR, CHAT], { activity: "ied.chat", ref: null, path: [], well: true }), {}), ["New Chat", "Web Browser"], "in Chat, a new chat");
});

test("tabs new: a group of an activity that opens no tabs has only the plugins' rows", () => {
	assertEqual(labels(shellWith([EDITOR, CHAT], { activity: "shell.home", ref: null, path: [], well: true }), {}), ["Web Browser"], "the shell's Home well");
});

test("tabs new: an item's when-clause is read of the group whose + was pressed", () => {
	const shell = shellWith([EDITOR, CHAT], EDITOR_WELL);
	assertEqual(labels(shell, { tabKind: "doc", resourceExtname: ".html" }), ["New File", "Web Browser", "Open in Browser"], "the group shows a page");
	assertEqual(labels(shell, { tabKind: "doc", resourceExtname: ".md" }), ["New File", "Web Browser"], "a Markdown file: no page of it");
	assertEqual(labels(shell, { tabKind: "chat" }), ["New File", "Web Browser"], "a chat: no page of it");
});
