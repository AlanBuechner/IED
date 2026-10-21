// hostcommands.js - the shell's own commands, their menu places and keys (docs/15 "Shell commands"). Commands about a
// tab take {ref}; without it they act on the tab with the focus.

import * as T from "./layouttree.js";
import { showPalette } from "./palette.js";
import { showNotificationCenter } from "./notifications.js";
import { showWorkspaceSwitcher } from "./workspaces.js";

function tabOf(shell, args) {
	return (args && args.ref) || (shell.focus && shell.focus.ref) || null;
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
	{ id: "shell.copyTabRef", title: "Copy Reference", category: "Window", run: (shell, args) => tabOf(shell, args) && copyText(shell, tabOf(shell, args)) },
	{ id: "shell.nextTab", title: "Next Tab", category: "Window", run: (shell) => stepTab(shell, 1) },
	{ id: "shell.previousTab", title: "Previous Tab", category: "Window", run: (shell) => stepTab(shell, -1) },
	{ id: "shell.resetLayout", title: "Reset Window Layout", category: "Window", run: (shell) => shell.resetLayout() },
	{ id: "shell.togglePanel", title: "Show or Hide the Panel", category: "View", icon: "$(layout-panel)", run: (shell) => shell.chrome && shell.chrome.panel && shell.chrome.panel.mounted && shell.chrome.panel.setOpen(!shell.chrome.panel.isOpen()) },
	{ id: "shell.showNotifications", title: "Notifications", category: "View", icon: "$(bell)", run: (shell, args) => showNotificationCenter(shell, args && args.at) },
	{ id: "shell.clearNotifications", title: "Clear All Notifications", category: "View", run: (shell) => Promise.all(shell.server.notifications.map((n) => shell.client.request("server.dismiss", { id: n.id }))) },
	{ id: "shell.switchWorkspace", title: "Switch Workspace...", category: "File", run: (shell, args) => showWorkspaceSwitcher(shell, args && args.at) },
	{ id: "shell.trustWorkspace", title: "Trust Workspace", category: "File", enablement: "!workspaceTrusted", run: (shell) => shell.client.request("ws.setTrust", { wsId: shell.wsId, trusted: true }) },
	{ id: "shell.keyboardShortcuts", title: "Keyboard Shortcuts", category: "Help", icon: "$(record-keys)", run: (shell) => shell.openTab("shell:keys") },
	{ id: "shell.plugins", title: "Manage Plugins", category: "Extensions", icon: "$(extensions)", run: (shell) => shell.openTab("shell:plugins") },
	{ id: "shell.about", title: "About IED", category: "Help", run: (shell) => shell.openTab("shell:about") },
];

const MENUS = {
	"menubar/file": [
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
		{ command: "shell.resetLayout", group: "9_layout" },
	],
	"menubar/extensions": [{ command: "shell.plugins", group: "1_manage" }],
	"menubar/help": [
		{ command: "shell.keyboardShortcuts", group: "1_keys" },
		{ command: "shell.about", group: "9_about" },
	],
	"shell/manage": [
		{ command: "shell.commandPalette", group: "1_palette" },
		{ command: "shell.keyboardShortcuts", group: "2_settings" },
		{ command: "shell.plugins", group: "2_settings", order: 1 },
		{ command: "shell.resetLayout", group: "3_layout" },
		{ command: "shell.about", group: "9_about" },
	],
	"tab/context": [
		{ command: "shell.closeTab", group: "1_close" },
		{ command: "shell.closeOtherTabs", group: "1_close", order: 1 },
		{ command: "shell.closeAllTabs", group: "1_close", order: 2 },
		{ command: "shell.moveTabRight", group: "2_groups" },
		{ command: "shell.moveTabDown", group: "2_groups", order: 1 },
		{ command: "shell.copyTabRef", group: "3_copy" },
	],
};

const KEYS = [
	{ command: "shell.commandPalette", key: "ctrl+shift+p" },
	{ command: "shell.commandPalette", key: "ctrl+q" },
	{ command: "shell.closeTab", key: "ctrl+f4" },
	{ command: "shell.nextTab", key: "ctrl+alt+pagedown" },
	{ command: "shell.previousTab", key: "ctrl+alt+pageup" },
	{ command: "shell.togglePanel", key: "ctrl+`" },
];

export function registerShellCommands(shell) {
	for (const spec of COMMANDS) {
		shell.commands.define({ id: spec.id, title: spec.title, category: spec.category, icon: spec.icon, enablement: spec.enablement }, "shell", "/");
		shell.commands.handle(spec.id, (args) => spec.run(shell, args));
	}
	for (const [menuId, items] of Object.entries(MENUS)) for (const item of items) shell.menus.add(menuId, item, "shell");
	for (const binding of KEYS) shell.keybindings.define(binding, "shell");
	shell.menus.provide("menubar/view", () => toolWindowItems(shell));
}

// View lists every tool window and every activity, VS style.
function toolWindowItems(shell) {
	const tools = shell.contributions.toolWindows.filter((t) => shell.context.holds(t.when)).map((t, i) => ({ command: "shell.showView", args: { id: t.id }, title: t.title, icon: t.icon, base: t.plugin.base, group: "2_windows", order: i }));
	const activities = shell.contributions.activities.map((a, i) => ({ command: "shell.showActivity", args: { id: a.id }, title: a.title, icon: a.icon, base: a.plugin.base, group: "3_activities", order: i }));
	return [...tools, ...activities];
}
