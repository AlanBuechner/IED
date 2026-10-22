// phone.js - the phone chrome (< 720 px, docs/15 "Phone"): a header (back, pane title, workspace, bell), one pane at a
// time, the plugins' toolbar views in a strip colored like the status bar (building, failed), and a bottom nav whose
// four slots take the activities (those asking for phoneSlot first) and then tool windows; More lists the rest.
// Opening a tab pushes the pane it leaves on the back stack (stored in the layout).

import { h, icon, iconFrom, replace } from "/ui-kit/dom.js";
import { wellsOf, groupsOf } from "./layouttree.js";
import { initials, startPageView } from "./workspaces.js";
import { banners } from "./banners.js";
import { ToolbarItems } from "./toolbaritems.js";
import { PanelDock } from "./paneldock.js";

const SLOTS = 4;

export class PhoneChrome {
	constructor(shell, root) {
		this.shell = shell;
		this.head = h("header.phone-head");
		this.banners = h("div.banners");
		this.area = h("main.activity-area.phone-pane");
		this.nav = h("nav.phone-nav", { "aria-label": "Panes" });
		this.strip = h("div.phone-toolbar", { role: "toolbar" });
		this.toolbarItems = new ToolbarItems(shell);
		this.panel = new PanelDock(shell, true, () => {});
		this.app = h("div.app.phone", null, this.head, this.banners, this.area, this.strip, this.nav, this.panel.el);
		this.state = "connecting";
		this.shown = null;
		replace(root, this.app);
		this.stopContext = shell.context.onChange(() => this.renderStrip());
	}

	dispose() {
		this.stopContext();
		this.toolbarItems.release();
		this.panel.release();
		this.app.remove();
	}

	render() {
		const pane = this.paneRef();
		this.renderPane(pane);
		this.renderHead(pane);
		this.renderStrip();
		this.renderNav(pane);
		this.renderBanners();
		this.panel.mount();
		this.panel.render();
	}

	renderStrip() {
		const items = this.toolbarItems.shown();
		this.strip.dataset.state = this.shell.context.get("statusState") || "idle";
		const drawn = ToolbarItems.signature([], items);
		if (drawn === this.stripDrawn) return;
		this.stripDrawn = drawn;
		replace(this.strip, ...items.map((item) => this.toolbarItems.element(item)));
		this.strip.hidden = items.length === 0;
	}

	connection(state) {
		this.state = state;
		this.renderBanners();
	}

	serverChanged() {
		this.renderHead(this.paneRef());
	}

	workspaceChanged() {
		this.renderHead(this.paneRef());
		this.renderBanners();
	}

	contributionsChanged() {
		this.toolbarItems.release();
		this.stripDrawn = null;
		this.renderStrip();
		this.panel.release();
		this.panel.mount();
		this.panel.render();
	}

	chord() {}

	tabChanged(entry) {
		if (entry.ref === this.shown) this.renderHead(this.paneRef());
	}

	startPage() {
		replace(this.area, startPageView(this.shell));
	}

	renderBanners() {
		replace(this.banners, ...banners(this.shell, this.state));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Panes

	// The bottom nav (docs/15): the activities that ask for a phone slot, then the tool windows, then the other
	// activities - one that did not ask never pushes a tool window off the bar.
	slots() {
		const shell = this.shell;
		const activities = [...shell.contributions.activities].sort((a, b) => a.order - b.order);
		const activity = (a) => ({ pane: "activity:" + a.id, title: a.title, icon: a.icon, base: a.plugin.base, run: () => shell.showActivity(a.id) });
		const all = [
			...activities.filter((a) => a.phoneSlot).map(activity),
			...shell.contributions.toolWindows.filter((t) => t.phoneNav !== false && shell.context.holds(t.when)).map((t) => ({ pane: t.id, title: t.title, icon: t.icon, base: t.plugin.base, run: () => shell.showView(t.id) })),
			...activities.filter((a) => !a.phoneSlot).map(activity),
		];
		return { shown: all.slice(0, SLOTS), more: all.slice(SLOTS) };
	}

	// The pane to show: the stored one while something renders it, else the first slot.
	paneRef() {
		const shell = this.shell;
		const stored = shell.doc.phone ? shell.doc.phone.pane : null;
		if (stored && (stored.startsWith("activity:") ? shell.contributions.activity(stored.slice(9)) : shell.views.has(stored))) return stored;
		const first = this.slots().shown[0];
		return first ? first.pane : null;
	}

	// What shows for a pane: an activity's own view; for a dock host the active document, else its first tool window.
	contentRef(pane) {
		const shell = this.shell;
		if (!pane || !pane.startsWith("activity:")) return pane;
		const id = pane.slice(9);
		if (!shell.isDockHost(id)) return pane;
		const tree = shell.treeOf(id);
		const well = wellsOf(tree)[0];
		if (well && well.active && shell.views.has(well.active)) return well.active;
		const tool = groupsOf(tree).flatMap((g) => g.tabs).find((ref) => shell.views.has(ref));
		return tool || null;
	}

	renderPane(pane) {
		const ref = this.contentRef(pane);
		const entry = ref ? this.shell.views.get(ref) : null;
		if (this.shown && this.shown !== ref) this.shell.views.setVisible(this.shown, false);
		this.shown = ref;
		if (!entry) {
			replace(this.area, h("div.empty-area", null, icon("window", "lg"), h("p", null, "Nothing to show here yet.")));
			return;
		}
		entry.el.hidden = false;
		if (this.area.firstElementChild !== entry.el || this.area.children.length !== 1) replace(this.area, entry.el);
		this.shell.views.setVisible(ref, true);
		this.shell.focused(this.shell.currentActivity(), ref, null, false);
	}

	renderHead(pane) {
		const shell = this.shell;
		const ref = this.contentRef(pane);
		const entry = ref ? shell.views.get(ref) : null;
		const back = shell.doc.phone && shell.doc.phone.back.length > 0;
		const unread = shell.server.notifications.length;
		replace(this.head,
			back ? h("button.ib", { title: "Back", "aria-label": "Back", on: { click: () => shell.phoneBack() } }, icon("arrow-left")) : h("button.sb-ws", { title: "Switch workspace", "aria-label": "Switch workspace", on: { click: (event) => shell.commands.run("shell.switchWorkspace", { at: event.currentTarget }) } }, initials(shell.workspace.name)),
			h("div.t", null, entry ? iconFrom(entry.icon, entry.base) : null, h("b", null, entry ? entry.title : shell.workspace.name || "IED"), entry && entry.dirty ? h("span.dirty") : null),
			ref && ref.includes(":") && !ref.startsWith("activity:") ? h("button.ib", { title: "More", "aria-label": "Tab actions", on: { click: (event) => shell.showMenu("tab/context", event.currentTarget, { ref }) } }, icon("ellipsis")) : null,
			h("button.ib", { title: "Notifications", "aria-label": "Notifications (" + unread + ")", on: { click: (event) => shell.commands.run("shell.showNotifications", { at: event.currentTarget }) } }, icon(unread ? "bell-dot" : "bell"), unread ? h("span.badge.info", null, String(unread)) : null));
	}

	renderNav(pane) {
		const shell = this.shell;
		const { shown, more } = this.slots();
		const activityPane = pane && !pane.startsWith("activity:") && !shell.contributions.toolWindow(pane) ? "activity:" + shell.currentActivity() : pane;
		const buttons = shown.map((slot) => h("button.pn" + (slot.pane === activityPane ? ".on" : ""), { "aria-label": slot.title, dataset: { pane: slot.pane }, on: { click: () => slot.run() } },
			iconFrom(slot.icon, slot.base) || icon("window"), h("span", null, slot.title)));
		while (buttons.length < SLOTS) buttons.push(h("span.pn.empty"));
		buttons.push(h("button.pn", { "aria-label": "More", on: { click: (event) => this.showMore(event.currentTarget, more) } }, icon("menu"), h("span", null, "More")));
		replace(this.nav, ...buttons);
	}

	showMore(at, more) {
		const shell = this.shell;
		const rows = more.map((slot) => ({ kind: "item", label: slot.title, icon: slot.icon, base: slot.base, enabled: true, run: slot.run }));
		if (this.panel.mounted) rows.push({ kind: "item", label: this.panel.title(), icon: "$(terminal)", base: "/", enabled: true, run: () => this.panel.setOpen(true) });
		if (rows.length) rows.push({ kind: "separator" });
		for (const [command, title] of [["shell.commandPalette", "Command Palette"], ["shell.showNotifications", "Notifications"], ["shell.switchWorkspace", "Switch Workspace"], ["shell.plugins", "Plugins"], ["shell.settings", "Settings"], ["shell.keyboardShortcuts", "Keyboard Shortcuts"], ["shell.resetLayout", "Reset Layout"], ["shell.about", "About IED"]]) {
			const definition = shell.commands.get(command);
			rows.push({ kind: "item", label: title, icon: definition ? definition.icon : null, base: "/", enabled: true, run: () => shell.commands.run(command) });
		}
		shell.menuHost.show({ rows, at, label: "More", pick: (row) => row.run() });
	}
}
