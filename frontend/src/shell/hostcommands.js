// hostcommands.js - the shell's own commands, their menu places and keys (docs/15 "Shell commands"). Commands about a
// tab take {ref}; without it they act on the tab with the focus. Open Folder exists only inside IED's desktop app
// (`desktopApp`, docs/27), which shows the system's folder picker and opens the folder on the server; a browser has no
// way to pick a folder of the server's.

import * as T from "./layouttree.js";
import { showPalette } from "./palette.js";
import { showNotificationCenter } from "./notifications.js";
import { showWorkspaceSwitcher } from "./workspaces.js";
import { showNewProject } from "./newproject.js";

function tabOf(shell, args) {
	return (args && args.ref) || (shell.focus && shell.focus.ref) || null;
}

// Tethers `ref` with the tab shown in the nearest other group of its activity.
function tetherBeside(shell, ref) {
	const activityId = ref ? shell.activityOf(ref) : null;
	if (!activityId) return;
	const tree = shell.treeOf(activityId);
	const home = T.groupOf(tree, ref);
	const other = T.groupsOf(tree).find((g) => g !== home && g.active && g.well);
	if (other) shell.tether(ref, other.active, {});
	else shell.toasts.show("There is no tab beside this one to tether it with: open one in another group first.", "info");
}

function stepTab(shell, step) {
	const ref = tabOf(shell, null);
	const activityId = ref ? shell.activityOf(ref) : null;
	if (!activityId) return;
	const group = T.groupOf(shell.treeOf(activityId), ref);
	const visible = group.tabs.filter((r) => shell.views.has(r));
	const next = visible[(visible.indexOf(ref) + step + visible.length) % visible.length];
	if (next) shell.activateTab(next);
}

async function copyText(shell, text) {
	try {
		await navigator.clipboard.writeText(text);
	}
	catch (e) {
		shell.report("The clipboard refused the text: " + e.message, e);
	}
}

const COMMANDS = [
	{ id: "shell.commandPalette", title: "Command Palette...", category: "View", icon: "$(symbol-event)", run: (shell) => showPalette(shell) },
	{ id: "shell.showView", title: "Show Tool Window", category: "View", run: (shell, args) => args && args.id && shell.showView(args.id) },
	{ id: "shell.showActivity", title: "Show Activity", category: "View", run: (shell, args) => args && args.id && shell.showActivity(args.id) },
	{ id: "shell.closeTab", title: "Close", category: "Window", run: (shell, args) => tabOf(shell, args) && shell.closeTab(tabOf(shell, args)) },
	{ id: "shell.closeOtherTabs", title: "Close All But This", category: "Window", run: (shell, args) => tabOf(shell, args) && shell.closeGroupTabs(tabOf(shell, args), false) },
	{ id: "shell.closeAllTabs", title: "Close All", category: "Window", run: (shell, args) => tabOf(shell, args) && shell.closeGroupTabs(tabOf(shell, args), true) },
	{ id: "shell.moveTabRight", title: "Move to New Group Right", category: "Window", run: (shell, args) => tabOf(shell, args) && shell.splitTab(tabOf(shell, args), "right") },
	{ id: "shell.moveTabDown", title: "Move to New Group Below", category: "Window", run: (shell, args) => tabOf(shell, args) && shell.splitTab(tabOf(shell, args), "bottom") },
	{ id: "shell.moveTabToActivity", title: "Move to Activity", category: "Window", run: (shell, args) => tabOf(shell, args) && args && args.activity && shell.openTab(tabOf(shell, args), { activity: args.activity }) },
	{ id: "shell.copyTabRef", title: "Copy Reference", category: "Window", run: (shell, args) => tabOf(shell, args) && copyText(shell, tabOf(shell, args)) },
	{ id: "shell.tetherBeside", title: "Tether with the Tab Beside", category: "Window", icon: "$(link)", run: (shell, args) => tetherBeside(shell, tabOf(shell, args)) },
	{ id: "shell.untetherTab", title: "Untether", category: "Window", enablement: "tabTethered", run: (shell, args) => tabOf(shell, args) && shell.untether(tabOf(shell, args)) },
	{ id: "shell.autoHideTab", title: "Auto Hide", category: "Window", icon: "$(pinned)", run: (shell, args) => tabOf(shell, args) && shell.autoHideTab(tabOf(shell, args), true) },
	{ id: "shell.dockTab", title: "Dock", category: "Window", icon: "$(pin)", run: (shell, args) => tabOf(shell, args) && shell.autoHideTab(tabOf(shell, args), false) },
	{ id: "shell.autoHideAll", title: "Auto Hide All", category: "Window", run: (shell) => shell.autoHideAll() },
	{ id: "shell.nextTab", title: "Next Tab", category: "Window", run: (shell) => stepTab(shell, 1) },
	{ id: "shell.previousTab", title: "Previous Tab", category: "Window", run: (shell) => stepTab(shell, -1) },
	{ id: "shell.resetLayout", title: "Reset Window Layout", category: "Window", run: (shell) => shell.resetLayout() },
	{ id: "shell.togglePanel", title: "Show or Hide the Panel", category: "View", icon: "$(layout-panel)", run: (shell) => shell.chrome && shell.chrome.panel && shell.chrome.panel.mounted && shell.chrome.panel.setOpen(!shell.chrome.panel.isOpen()) },
	{ id: "shell.showNotifications", title: "Notifications", category: "View", icon: "$(bell)", run: (shell, args) => showNotificationCenter(shell, args && args.at) },
	{ id: "shell.clearNotifications", title: "Clear All Notifications", category: "View", run: (shell) => Promise.all(shell.server.notifications.map((n) => shell.client.request("server.dismiss", { id: n.id }))) },
	{ id: "shell.newProject", title: "New Project...", category: "File", icon: "$(new-folder)", run: (shell, args) => showNewProject(shell, { addTo: Boolean(args && (args.addTo || args.kind === "solution")) }) },
	{ id: "shell.startPage", title: "Start Page", category: "File", icon: "$(home)", run: (shell) => shell.showStartPage() },
	{ id: "shell.openFolder", title: "Open Folder...", category: "File", icon: "$(folder-opened)", run: () => window.iedDesktop && window.iedDesktop.openFolder() },
	{ id: "shell.switchWorkspace", title: "Switch Workspace...", category: "File", run: (shell, args) => showWorkspaceSwitcher(shell, args && args.at) },
	{ id: "shell.trustWorkspace", title: "Trust Workspace", category: "File", enablement: "!workspaceTrusted", run: (shell) => shell.client.request("ws.setTrust", { wsId: shell.wsId, trusted: true }) },
	{ id: "shell.keyboardShortcuts", title: "Keyboard Shortcuts", category: "Help", icon: "$(record-keys)", run: (shell) => shell.openSettings("custom:shell.keyboard") },
	{ id: "shell.settings", title: "Settings", category: "Tools", icon: "$(settings-gear)", run: (shell, args) => shell.openSettings(args && args.page, args && args.tier, args && args.project) },
	{ id: "shell.plugins", title: "Manage Plugins", category: "Extensions", icon: "$(extensions)", run: (shell) => shell.openTab("shell:plugins") },
	{ id: "shell.about", title: "About IED", category: "Help", run: (shell) => shell.openTab("shell:about") },
];

const MENUS = {
	"menubar/file": [
		{ command: "shell.newProject", group: "0_new" },
		{ command: "shell.openFolder", group: "0_open", when: "desktopApp" },
		{ command: "shell.startPage", group: "0_open", order: 1 },
		{ command: "shell.switchWorkspace", group: "1_workspace" },
		{ command: "shell.trustWorkspace", group: "1_workspace", order: 1, when: "!workspaceTrusted" },
	],
	"menubar/view": [
		{ command: "shell.commandPalette", group: "1_palette" },
		{ command: "shell.showNotifications", group: "9_notifications" },
	],
	"menubar/window": [
		{ command: "shell.nextTab", group: "1_tabs" },
		{ command: "shell.previousTab", group: "1_tabs", order: 1 },
		{ command: "shell.closeAllTabs", group: "2_close", title: "Close All Tabs in Group" },
		{ command: "shell.autoHideTab", group: "3_dock", when: "tabInToolGroup && !tabAutoHidden" },
		{ command: "shell.dockTab", group: "3_dock", order: 1, when: "tabAutoHidden" },
		{ command: "shell.autoHideAll", group: "3_dock", order: 2 },
		{ command: "shell.resetLayout", group: "9_layout" },
	],
	"menubar/extensions": [{ command: "shell.plugins", group: "1_manage" }],
	"menubar/help": [
		{ command: "shell.keyboardShortcuts", group: "1_keys" },
		{ command: "shell.about", group: "9_about" },
	],
	"shell/manage": [
		{ command: "shell.commandPalette", group: "1_palette" },
		{ command: "shell.settings", group: "2_settings" },
		{ command: "shell.keyboardShortcuts", group: "2_settings", order: 1 },
		{ command: "shell.plugins", group: "2_settings", order: 2 },
		{ command: "shell.resetLayout", group: "3_layout" },
		{ command: "shell.about", group: "9_about" },
	],
	// The ways to open a workspace: buttons where none is open, rows of the workspace switcher.
	"shell/openWorkspace": [
		{ command: "shell.newProject", group: "0_new" },
		{ command: "shell.openFolder", group: "1_open", when: "desktopApp" },
	],
	"tab/context": [
		{ command: "shell.closeTab", group: "1_close" },
		{ command: "shell.closeOtherTabs", group: "1_close", order: 1 },
		{ command: "shell.closeAllTabs", group: "1_close", order: 2 },
		{ command: "shell.moveTabRight", group: "2_groups" },
		{ command: "shell.moveTabDown", group: "2_groups", order: 1 },
		{ command: "shell.tetherBeside", group: "2_groups", order: 2, when: "!tabTethered" },
		{ command: "shell.untetherTab", group: "2_groups", order: 3, when: "tabTethered" },
		{ command: "shell.autoHideTab", group: "3_dock", when: "tabInToolGroup && !tabAutoHidden" },
		{ command: "shell.dockTab", group: "3_dock", when: "tabAutoHidden" },
		{ command: "shell.copyTabRef", group: "4_copy" },
	],
};

const KEYS = [
	{ command: "shell.commandPalette", key: "ctrl+shift+p" },
	{ command: "shell.commandPalette", key: "ctrl+q" },
	{ command: "shell.closeTab", key: "ctrl+f4" },
	{ command: "shell.nextTab", key: "ctrl+alt+pagedown" },
	{ command: "shell.previousTab", key: "ctrl+alt+pageup" },
	{ command: "shell.togglePanel", key: "ctrl+`" },
	{ command: "shell.openFolder", key: "ctrl+shift+alt+o", when: "desktopApp" },
];

export function registerShellCommands(shell) {
	for (const spec of COMMANDS) {
		shell.commands.define({ id: spec.id, title: spec.title, category: spec.category, icon: spec.icon, enablement: spec.enablement }, "shell", "/");
		shell.commands.handle(spec.id, (args) => spec.run(shell, args));
	}
	for (const [menuId, items] of Object.entries(MENUS)) for (const item of items) shell.menus.add(menuId, item, "shell");
	for (const binding of KEYS) shell.keybindings.define(binding, "shell");
	shell.menus.provide("menubar/view", () => toolWindowItems(shell));
	shell.menus.provide("tab/context", () => moveToActivityItems(shell));
	shell.menus.provide("tabs/new", () => newTabHereItems(shell));
}

// The first row of a "+" menu (the menu `tabs/new`, docs/15 "Docking"): the activity's own way to open a tab, the one a
// click on the "+" runs; the plugins add the other ways to that menu - a web view, a file's preview, Go To All. The group
// whose "+" was pressed is the focused one by the time the menu resolves (shell.js `showNewTabMenu`).
function newTabHereItems(shell) {
	const where = shell.focus && shell.focus.well ? shell.focus.activity : null;
	const activity = where ? shell.contributions.activities.find((a) => a.id === where) : null;
	return activity && activity.newTab ? [{ command: activity.newTab, group: "0_new" }] : [];
}

// A document tab moves to another dock host (F-SHL-14: a chat into the Editor's well, a file into the Chat's).
function moveToActivityItems(shell) {
	const ref = shell.context.get("tabRef");
	const where = ref ? shell.activityOf(ref) : null;
	if (!where || shell.context.get("tabInToolGroup")) return [];
	return shell.contributions.activities.filter((a) => !a.view && a.id !== where)
		.map((a, i) => ({ command: "shell.moveTabToActivity", args: { ref, activity: a.id }, title: "Move to " + a.title, group: "2_groups", order: 10 + i }));
}

// View lists every tool window and every activity, VS style.
function toolWindowItems(shell) {
	const tools = shell.contributions.toolWindows.filter((t) => shell.context.holds(t.when)).map((t, i) => ({ command: "shell.showView", args: { id: t.id }, title: t.title, icon: t.icon, base: t.plugin.base, group: "2_windows", order: i }));
	const activities = shell.contributions.activities.map((a, i) => ({ command: "shell.showActivity", args: { id: a.id }, title: a.title, icon: a.icon, base: a.plugin.base, group: "3_activities", order: i }));
	return [...tools, ...activities];
}
