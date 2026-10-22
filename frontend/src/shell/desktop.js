// desktop.js - the desktop and tablet chrome (docs/15 "Desktop layout"): activity sidebar, menu bar with the title and
// connection, the toolbar host, banners (connection, trust), the activity area (dock hosts and activity views), status bar.

import { h, icon, iconFrom, replace } from "/ui-kit/dom.js";
import { MENUBAR } from "./menus.js";
import { DockView } from "./dock.js";
import * as T from "./layouttree.js";
import { initials, kindIcon, startPageView } from "./workspaces.js";
import { StatusBar } from "./statusbar.js";
import { ToolbarItems } from "./toolbaritems.js";
import { PanelDock } from "./paneldock.js";
import { banners } from "./banners.js";

export class DesktopChrome {
	constructor(shell, root) {
		this.shell = shell;
		this.sidebar = h("nav.sidebar", { "aria-label": "Activities" });
		this.menubar = h("div.menubar", { role: "menubar" });
		this.toolbar = h("div.toolbar", { role: "toolbar" });
		this.banners = h("div.banners");
		this.area = h("main.activity-area");
		this.status = new StatusBar(shell);
		this.toolbarItems = new ToolbarItems(shell);
		this.panel = new PanelDock(shell, false);
		this.app = h("div.app.desktop", null, this.sidebar, this.menubar, this.toolbar, this.banners, this.area, this.panel.el, this.status.el);
		this.docks = new Map();
		this.state = "connecting";
		replace(root, this.app);
		this.stopContext = shell.context.onChange(() => this.renderToolbar());
		this.renderSidebar();
		this.renderMenubar();
	}

	dispose() {
		this.stopContext();
		this.toolbarItems.release();
		this.panel.release();
		this.status.dispose();
		this.dropDocks();
		this.app.remove();
	}

	render() {
		this.renderSidebar();
		this.renderMenubar();
		this.renderToolbar();
		this.renderBanners();
		this.renderActivity();
		this.panel.mount();
		this.panel.render();
		this.status.render();
	}

	connection(state) {
		this.state = state;
		this.renderBanners();
		this.status.connection(state);
		this.renderTitle();
	}

	serverChanged() {
		this.renderSidebar();
		this.status.render();
	}

	workspaceChanged() {
		this.renderSidebar();
		this.renderTitle();
		this.renderBanners();
	}

	contributionsChanged() {
		this.toolbarItems.release();
		this.toolbarDrawn = null;
		this.renderToolbar();
		this.panel.release();
		this.panel.mount();
		this.panel.render();
		this.status.contributionsChanged();
	}

	chord(text, transient) {
		this.status.chord(text, transient);
	}

	tabChanged(entry) {
		for (const dock of this.docks.values()) dock.updateTab(entry);
	}

	// The start page takes the whole area: the activities' docks go with what it replaces, and come back on render.
	startPage() {
		this.dropDocks();
		this.renderMenubar();
		replace(this.area, startPageView(this.shell));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Sidebar

	renderSidebar() {
		const shell = this.shell;
		const current = shell.currentActivity();
		const ws = shell.workspace;
		const unread = shell.server.notifications.length;
		const wsButton = h("button.sb-ws", { title: ws.name ? ws.name + " (" + (ws.kind || "folder") + ") - switch workspace" : "Workspaces", "aria-label": "Switch workspace", on: { click: (event) => shell.commands.run("shell.switchWorkspace", { at: event.currentTarget }) } }, initials(ws.name));
		const activities = shell.contributions.activities.map((a) => h("button.sb-act" + (a.id === current ? ".on" : ""), {
			title: a.title, "aria-label": a.title, "aria-pressed": a.id === current ? "true" : "false", dataset: { activity: a.id },
			on: { click: () => shell.showActivity(a.id) },
		}, iconFrom(a.icon, a.plugin.base) || icon("window")));
		const bell = h("button.sb-act.sb-bell", { title: "Notifications", "aria-label": "Notifications (" + unread + ")", on: { click: (event) => shell.commands.run("shell.showNotifications", { at: event.currentTarget }) } },
			icon(unread ? "bell-dot" : "bell"), unread ? h("span.badge.info", null, String(unread)) : null);
		const gear = h("button.sb-act", { title: "Manage", "aria-label": "Manage", on: { click: (event) => shell.showMenu("shell/manage", event.currentTarget) } }, icon("settings-gear"));
		replace(this.sidebar, wsButton, h("div.sb-sep"), ...activities, h("div.sb-spacer"), bell, gear);
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Menu bar and title

	renderMenubar() {
		const shell = this.shell;
		const menus = h("div.menus");
		for (const [name, title] of MENUBAR) {
			const menuId = "menubar/" + name;
			if (!shell.menus.hasItems(menuId)) continue;
			menus.append(h("button.mi", {
				role: "menuitem", dataset: { menu: menuId },
				on: {
					click: (event) => this.openMenu(menuId, event.currentTarget),
					pointerenter: (event) => {
						if (shell.menuHost.isOpen && this.openMenuId && this.openMenuId !== menuId) this.openMenu(menuId, event.currentTarget);
					},
				},
			}, title));
		}
		this.title = h("div.title");
		replace(this.menubar, menus, this.title);
		this.renderTitle();
		requestAnimationFrame(() => this.foldMenus(menus));
	}

	openMenu(menuId, button) {
		const shell = this.shell;
		for (const el of this.menubar.querySelectorAll(".mi.open")) el.classList.remove("open");
		button.classList.add("open");
		this.openMenuId = menuId;
		const rows = menuId === "menubar/..." ? this.overflowRows : shell.menus.resolve(menuId);
		shell.menuHost.show({
			rows, at: button, label: menuId, minWidth: 220,
			pick: (row) => shell.commands.run(row.command, row.args),
			submenu: (id) => shell.menus.resolve(id),
			onClose: () => {
				button.classList.remove("open");
				this.openMenuId = null;
			},
		});
	}

	// Menus that do not fit move into a trailing "..." menu.
	foldMenus(menus) {
		const folded = [];
		while (menus.scrollWidth > menus.clientWidth + 1 && menus.children.length > 1) {
			const last = menus.lastElementChild;
			folded.unshift({ kind: "submenu", label: last.textContent, menuId: last.dataset.menu, order: 0 });
			last.remove();
		}
		if (!folded.length) return;
		this.overflowRows = folded;
		const more = h("button.mi", { title: "More menus", dataset: { menu: "menubar/..." }, on: { click: (event) => this.openMenu("menubar/...", event.currentTarget) } }, "...");
		menus.append(more);
		while (menus.scrollWidth > menus.clientWidth + 1 && menus.children.length > 2) {
			const last = more.previousElementSibling;
			folded.unshift({ kind: "submenu", label: last.textContent, menuId: last.dataset.menu, order: 0 });
			last.remove();
		}
	}

	renderTitle() {
		if (!this.title) return;
		const ws = this.shell.workspace;
		const live = this.state === "live";
		replace(this.title,
			ws.name ? h("span.ws-title", { title: ws.root }, iconFrom(kindIcon(ws.kind), "/", "sm"), " ", h("b", null, ws.name)) : null,
			h("span.conn" + (live ? ".live" : "." + this.state), { title: "Server connection: " + this.state }, h("span.dot" + (live ? ".saved" : this.state === "closed" ? ".error" : ".warn")), live ? "live" : this.state));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Toolbar and banners

	renderToolbar() {
		const shell = this.shell;
		const rows = shell.menus.resolve("toolbar/main");
		const items = this.toolbarItems.shown();
		const drawn = ToolbarItems.signature(rows, items);
		if (drawn === this.toolbarDrawn) return;
		this.toolbarDrawn = drawn;
		replace(this.toolbar, ...rows.map((row) => {
			if (row.kind === "separator") return h("span.tb-sep");
			const el = h("button.ib" + (row.enabled ? "" : ".dim"), { title: row.label + (row.keys ? " (" + row.keys + ")" : ""), "aria-label": row.label, disabled: !row.enabled, on: { click: () => shell.commands.run(row.command, row.args) } },
				iconFrom(row.icon, row.base) || h("span.tb-label", null, row.label));
			return el;
		}), items.length && rows.length ? h("span.tb-sep") : null, ...items.map((item) => this.toolbarItems.element(item)));
		this.app.classList.toggle("no-toolbar", rows.length === 0 && items.length === 0);
	}

	renderBanners() {
		replace(this.banners, ...banners(this.shell, this.state));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The activity area

	renderActivity() {
		const shell = this.shell;
		const activityId = shell.currentActivity();
		for (const [id, dock] of [...this.docks]) {
			if (!shell.contributions.activity(id)) {
				dock.dispose();
				dock.container.remove();
				this.docks.delete(id);
			}
			else dock.container.hidden = id !== activityId;
		}
		for (const el of this.area.querySelectorAll(":scope > .view")) el.hidden = el.dataset.ref !== "activity:" + activityId;
		for (const empty of this.area.querySelectorAll(":scope > .empty-area, :scope > .start-page")) empty.remove();
		if (!activityId) {
			this.area.append(h("div.empty-area", null, icon("extensions", "lg"), h("p", null, "No plugin contributes an activity. Manage plugins with Extensions > Manage Plugins.")));
			return;
		}
		if (shell.isDockHost(activityId)) this.dockFor(activityId).render(shell.treeOf(activityId));
		else {
			const entry = shell.views.get("activity:" + activityId);
			if (entry && entry.el.parentElement !== this.area) this.area.append(entry.el);
			if (entry) entry.el.hidden = false;
		}
	}

	dropDocks() {
		for (const dock of this.docks.values()) {
			dock.dispose();
			dock.container.remove();
		}
		this.docks.clear();
	}

	// A tab of an auto-hidden group slides out (shell.slideOut).
	slideOut(activityId, ref) {
		const dock = this.docks.get(activityId);
		if (dock) dock.showFlyout(ref);
	}

	dockFor(activityId) {
		let dock = this.docks.get(activityId);
		if (dock) return dock;
		const shell = this.shell;
		const container = h("div.dock-host", { dataset: { activity: activityId } });
		this.area.append(container);
		dock = new DockView({
			views: shell.views,
			menus: { show: (menuId, at, args) => shell.showMenu(menuId, at, args), host: shell.menuHost },
			emptyWell: () => this.emptyWell(activityId),
			activate: (ref) => shell.activateTab(ref),
			tetheredWith: (ref) => shell.tetheredWith(ref),
			close: (ref) => shell.closeTab(ref),
			pin: (ref) => shell.pinTab(ref),
			move: (ref, path, zone, index) => shell.moveTab(ref, path, zone, index),
			resize: (path, sizes) => shell.changeTree(activityId, (tree) => T.setSizes(tree, path, sizes)),
			autoHide: (ref, hidden) => shell.autoHideTab(ref, hidden),
			resizeAutoHidden: (ref, share) => shell.resizeAutoHidden(ref, share),
			hold: (on) => shell.layout && shell.layout.hold(on),
			focused: (ref, path, well) => shell.focused(activityId, ref, path, well),
			// The activity's `newTab` command, run with the group whose "+" was clicked focused, so the tab opens there.
			get newTab() {
				const activity = shell.contributions.activity(activityId);
				if (!activity || !activity.newTab) return null;
				return (path, active) => {
					shell.focused(activityId, active, path, true);
					shell.commands.run(activity.newTab);
				};
			},
			get newTabTitle() {
				const activity = shell.contributions.activity(activityId);
				const command = activity && activity.newTab ? shell.commands.get(activity.newTab) : null;
				return command ? command.title : "New Tab";
			},
		}, container);
		dock.container = container;
		this.docks.set(activityId, dock);
		return dock;
	}

	// VS's empty document well: where to start.
	emptyWell(activityId) {
		const shell = this.shell;
		const hint = (label, keys, command, args) => h("button.hint", { on: { click: () => shell.commands.run(command, args) } }, h("span", null, label), h("span.kbd", null, keys));
		const activity = shell.contributions.activity(activityId);
		const own = ((activity && activity.emptyWell) || []).map((id) => hint((shell.commands.get(id) || { title: id }).title, shell.keybindings.labelFor(id), id));
		const tools = shell.contributions.toolsOf(activityId).map((t) => hint(t.title, shell.keybindings.labelFor("shell.showView", { id: t.id }), "shell.showView", { id: t.id }));
		const plugins = tools.length ? null : hint("Manage Plugins", shell.keybindings.labelFor("shell.plugins"), "shell.plugins");
		return h("div.empty-well", null, h("div.hints", null, ...own, hint("Command Palette", shell.keybindings.labelFor("shell.commandPalette"), "shell.commandPalette"), ...tools, plugins));
	}
}
