// shell.js - the web client's shell (docs/15 "The web client as built"): one protocol client, the registries (context
// keys, commands, keybindings, menus, views), the shown workspace with its plugins' contributions and its layout, and
// the chrome for the device class (desktop and tablet, or phone). The server owns all state; this is a view of it.

import { ProtocolClient } from "../ws/client.js";
import { parseOpenFragment } from "./launch.js";
import { resolveIdentity, deviceClassOf } from "../ws/identity.js";
import { MenuHost } from "/ui-kit/menu.js";
import { isTextInput } from "/ui-kit/dom.js";
import { ContextKeys } from "./context.js";
import { Commands } from "./commands.js";
import { Bindings } from "./bindings.js";
import { Keybindings } from "./keybindings.js";
import { Menus } from "./menus.js";
import { ViewHost } from "./views.js";
import { Contributions } from "./contributions.js";
import { LayoutStore } from "./layoutstore.js";
import { DEFAULT_MODE, treeOf, withTree, withoutTree, withActivity, withPhonePane, withoutPhoneBack, phoneBack, emptyDocument, withTether, withoutTethers, partnersOf, followersOf } from "./layoutdoc.js";
import * as T from "./layouttree.js";
import { Toasts } from "./toasts.js";
import { registerShellCommands } from "./hostcommands.js";
import { registerShellViews } from "./hostviews.js";
import { DesktopChrome } from "./desktop.js";
import { PhoneChrome } from "./phone.js";

const AUTH_PROBE_AFTER_MS = 1500;
// How long after its plugins load a window waits before it switches the layout back to the default mode: their context
// keys come with their topics' snapshots, so until then "no mode holds" may only mean "not heard yet" (docs/15
// "Window layouts").
const MODE_SETTLE_MS = 2000;
const WORKSPACE_KEY = "ied.wsId";

export class Shell {
	constructor(root) {
		this.root = root;
		this.context = new ContextKeys();
		this.toasts = new Toasts(document.body);
		this.commands = new Commands(this.context, (message, error) => this.report(message, error));
		this.keybindings = new Keybindings(this.commands, this.context, {
			report: (message) => this.report(message),
			onChord: (text, transient) => this.chrome && this.chrome.chord(text, transient),
		});
		this.menus = new Menus(this.commands, this.context, this.keybindings);
		this.menuHost = new MenuHost(document.body);
		this.views = new ViewHost(this);
		this.tabCloseHandlers = new Map();
		this.tabListeners = new Set();    // ied.tabs.onChange
		this.tabsShown = null;            // the tabs state they heard last, as JSON
		this.contributions = new Contributions(this);
		this.contributions.on(() => this.views.mountWaiting());
		this.modules = new Map();
		this.apis = new Map();
		this.settingsViews = new Set();   // the open settings windows (settingsview.js)
		this.nextSettingsPage = null;     // the page the next settings window opens on
		this.bindings = new Bindings((id) => {
			const plugin = this.contributions.plugins.find((p) => p.id === id);
			return plugin ? plugin.contributes.bindings || {} : null;
		}, {
			provides: (name) => this.contributions.plugins.some((p) => ((p.contributes.bindings || {}).provides || []).some((b) => b.name === name && (b.side || "server") === "server")),
			call: (name, args) => this.client.request("plugins.call", this.wsId ? { wsId: this.wsId, name, args } : { name, args }).then((answer) => answer.result),
		});
		this.commands.bindings = this.bindings;
		this.client = null;
		this.server = { workspaces: [], clients: [], notifications: [] };
		this.wsId = null;
		this.showingStartPage = false;   // this window shows the start page (not merely no workspace chosen yet)
		this.startPageAsked = location.hash === "#start";   // the start page even while workspaces are open (docs/15)
		this.pendingOpen = parseOpenFragment(location.hash);
		this.workspace = workspaceInfo();
		this.layout = null;
		this.doc = emptyDocument();
		this.wantedMode = DEFAULT_MODE;  // the layout mode the context keys ask for (docs/15 "Window layouts")
		this.modeTimer = null;
		this.modeSettled = false;
		this.deviceClass = deviceClassOf(window.innerWidth);
		this.chrome = null;
		this.focus = null;
		this.authProbe = null;
		this.shellApi = this.makeApi({ id: "shell", base: "/" });
		this.wsTopic = null;
		this.serverListeners = new Set();
		this.views.onChange((entry) => this.chrome && this.chrome.tabChanged(entry));
		this.context.onChange(() => this.updateMode());
	}

	async start() {
		registerShellCommands(this);
		registerShellViews(this);
		this.identity = await resolveIdentity(window);
		this.client = new ProtocolClient({ url: socketUrl(), identity: this.identity, onLeave: (reason) => this.leave(reason) });
		this.client.onState((state) => this.connectionChanged(state));
		// The width again: the window may have been resized while the identity was read, before anything listened.
		this.deviceClass = deviceClassOf(window.innerWidth);
		window.addEventListener("resize", () => this.resized());
		window.addEventListener("hashchange", () => location.hash === "#start" && !this.showingStartPage && this.showStartPage());
		this.context.set("deviceClass", this.deviceClass);
		this.context.set("layoutMode", DEFAULT_MODE);
		this.context.set("connected", false);
		// Inside IED's desktop app (docs/27), which adds window.iedDesktop before the page's scripts.
		this.context.set("desktopApp", Boolean(window.iedDesktop));
		this.mountChrome();
		window.addEventListener("keydown", (event) => this.keybindings.handle(event), true);
		document.addEventListener("focusin", (event) => this.context.set("inputFocus", isTextInput(event.target)));
		this.client.subscribe("server", { snap: (data) => this.serverSnapshot(data), evt: (kind, data) => this.serverEvent(kind, data) });
		this.client.subscribe("plugins", { evt: (kind) => kind === "plugins.changed" && this.reloadContributions() });
		const ownerKeys = (data) => {
			const entry = data && data.values && data.values["keyboard.bindings"];
			this.keybindings.setOwner(entry ? entry.value : []);
			this.commands.changed();
		};
		this.client.subscribe("settings:app", { snap: ownerKeys, evt: (kind, data) => kind === "settings.changed" && ownerKeys(data) });
		this.client.start();
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Connection

	connectionChanged(state) {
		this.context.set("connected", state === "live");
		if (this.chrome) this.chrome.connection(state);
		clearTimeout(this.authProbe);
		if (state === "connecting" || state === "reconnecting") this.authProbe = setTimeout(() => this.probeSignIn(), AUTH_PROBE_AFTER_MS);
	}

	// A socket that keeps failing may mean this device is signed out: then the sign-in page (docs/11).
	async probeSignIn() {
		try {
			const response = await fetch("/api/auth/state", { cache: "no-store" });
			if (!response.ok) return;
			const state = await response.json();
			if (state.authenticated === false) location.replace("/login.html");
		}
		catch {
			// the server is down: the banner says so and the client keeps trying
		}
	}

	leave(reason) {
		if (reason === "auth-revoked") location.replace("/login.html");
		else location.reload();
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The server topic: workspaces, devices, notifications

	serverSnapshot(data) {
		this.server = { serverVersion: data.serverVersion, workspaces: data.workspaces || [], recent: data.recent || [], clients: data.clients || [], notifications: data.notifications || [] };
		this.serverChanged();
		if (!this.wsId || !this.server.workspaces.some((w) => w.wsId === this.wsId)) this.showWorkspace(this.preferredWorkspace());
	}

	serverEvent(kind, data) {
		const s = this.server;
		if (kind === "device.connected") s.clients = [...s.clients.filter((c) => c.clientId !== data.clientId), data];
		else if (kind === "device.disconnected") s.clients = s.clients.filter((c) => c.clientId !== data.clientId);
		else if (kind === "notification") {
			s.notifications = [...s.notifications, data];
			this.toasts.show(data.text, data.level);
		}
		else if (kind === "notification.dismissed") s.notifications = s.notifications.filter((n) => n.id !== data.id);
		else if (kind === "workspace.opened") s.workspaces = [...s.workspaces.filter((w) => w.wsId !== data.wsId), data];
		else if (kind === "recent.changed") s.recent = data.recent || [];
		this.serverChanged();
		if (kind === "workspace.opened" && !this.wsId && !this.startPageAsked) this.showWorkspace(data.wsId);
	}

	serverChanged() {
		if (this.chrome) this.chrome.serverChanged();
		if (this.chrome && this.showingStartPage) this.chrome.startPage();
		for (const fn of [...this.serverListeners]) fn(this.server);
	}

	onServerChange(fn) {
		this.serverListeners.add(fn);
		return () => this.serverListeners.delete(fn);
	}

	preferredWorkspace() {
		if (this.startPageAsked) return null;
		const ids = this.server.workspaces.map((w) => w.wsId);
		const asked = new URLSearchParams(location.search).get("ws");
		let remembered = null;
		try {
			remembered = sessionStorage.getItem(WORKSPACE_KEY);
		}
		catch {
			remembered = null;
		}
		const launched = this.pendingOpen ? this.pendingOpen.wsId : null;
		return [launched, asked, remembered, ids[0]].find((id) => id && ids.includes(id)) || null;
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The shown workspace

	async showWorkspace(wsId) {
		if (wsId === this.wsId && this.layout) return;
		this.leaveWorkspace();
		this.wsId = wsId;
		try {
			sessionStorage.setItem(WORKSPACE_KEY, wsId || "");
		}
		catch {
			// a window without storage forgets the choice
		}
		this.showingStartPage = !wsId;
		if (!wsId) {
			// The workspace shown before is described no more: not in the title, not on the sidebar's button, not by its
			// plugins' activities, toolbars and panel.
			Object.assign(this.workspace, { wsId: null, name: "", kind: null, root: "", trusted: true, solution: null });
			this.contributions.clear();
			this.workspaceChanged();
			this.chrome.contributionsChanged();
			this.chrome.startPage();
			if (location.search) history.replaceState(null, "", location.pathname + location.hash);
			return;
		}
		if (this.startPageAsked) {
			this.startPageAsked = false;
			history.replaceState(null, "", location.pathname + location.search);
		}
		// The address names the workspace shown (`?ws=`, which preferredWorkspace reads), so a reload, a bookmark or the
		// desktop app's relaunch comes back to it.
		const named = "?ws=" + encodeURIComponent(wsId);
		if (location.search !== named) history.replaceState(null, "", location.pathname + named + location.hash);
		this.wsTopic = this.client.subscribe("ws:" + wsId, {
			snap: (data) => this.workspaceSnapshot(data),
			evt: (kind, data) => this.workspaceEvent(kind, data),
			gone: () => this.showWorkspace(this.preferredWorkspace()),
		});
		await this.reloadContributions();
		this.openLayout();
	}

	// The start page in this window (File > Start Page): the workspace stays open on the server.
	showStartPage() {
		this.startPageAsked = true;
		history.replaceState(null, "", location.pathname + location.search + "#start");
		return this.showWorkspace(null);
	}

	// A project of the recent list, opened again on the server and shown here.
	async openProject(path) {
		try {
			const answer = await this.client.request("ws.openRecent", { path });
			if (answer.workspace.wsId !== this.wsId) await this.showWorkspace(answer.workspace.wsId);
		}
		catch (e) {
			this.report("The project did not open: " + e.message, e);
		}
	}

	setFavorite(path, favorite) {
		return this.client.request("ws.setFavorite", { path, favorite }).catch((e) => this.report("ws.setFavorite: " + e.message, e));
	}

	forgetProject(path) {
		return this.client.request("ws.forgetRecent", { path }).catch((e) => this.report("ws.forgetRecent: " + e.message, e));
	}

	leaveWorkspace() {
		if (this.wsTopic) this.wsTopic();
		this.wsTopic = null;
		if (this.layout) this.layout.dispose();
		this.layout = null;
		this.views.releaseAll();
		this.doc = emptyDocument();
	}

	workspaceSnapshot(data) {
		Object.assign(this.workspace, { wsId: data.wsId, name: data.name, kind: data.kind, root: data.root, trusted: data.trusted !== false, solution: data.solution || null });
		this.workspaceChanged();
	}

	// Only what changes the workspace's description or its plugins (its .ied/plugins/ edited): a tree.changed is for the
	// views that show files.
	workspaceEvent(kind, data) {
		if (kind === "plugins.changed") {
			this.reloadContributions();
			return;
		}
		if (kind === "trust.changed") this.workspace.trusted = data.trusted !== false;
		else if (kind === "solution.changed") this.workspace.solution = data.solution || null;
		else return;
		this.workspaceChanged();
		this.reloadContributions();
	}

	workspaceChanged() {
		this.context.set("workspaceTrusted", this.workspace.trusted);
		this.context.set("workspaceKind", this.workspace.kind);
		document.title = (this.workspace.name ? this.workspace.name + " - " : "") + "IED";
		for (const fn of [...this.workspace.listeners]) fn(this.workspace);
		if (this.chrome) this.chrome.workspaceChanged();
	}

	async reloadContributions() {
		if (!this.wsId) return;
		try {
			await this.contributions.load(this.wsId);
		}
		catch (e) {
			this.report("The plugins' contributions could not be read: " + e.message, e);
		}
		if (this.chrome) this.chrome.contributionsChanged();
		this.updateMode();
		clearTimeout(this.modeTimer);
		this.modeSettled = false;
		this.modeTimer = setTimeout(() => {
			this.modeSettled = true;
			this.reconcileMode();
		}, MODE_SETTLE_MS);
		this.render();
	}

	openLayout() {
		this.layout = new LayoutStore(this.client, this.wsId, this.deviceClass, (doc) => this.adopt(doc));
	}

	adopt(doc) {
		this.doc = doc;
		this.render();
		this.releaseUnused();
		this.tabsChanged();
		this.openLaunched();
	}

	// The tabs open in this window's layout - every dock host's, a phone's pane and back stack - and those shown (each
	// group's active one, the phone's pane): what ied.tabs.state() answers.
	tabsState() {
		const phone = (this.doc && this.doc.phone) || {};
		if (this.deviceClass === "phone") return { open: [phone.pane, ...(phone.back || [])].filter(Boolean), shown: phone.pane ? [phone.pane] : [] };
		const open = new Set();
		const shown = new Set();
		for (const activity of this.contributions.activities) {
			if (activity.view) continue;
			const tree = this.treeOf(activity.id);
			for (const ref of T.refsOf(tree)) open.add(ref);
			for (const group of T.groupsOf(tree)) if (group.active) shown.add(group.active);
		}
		return { open: [...open], shown: [...shown] };
	}

	// Tells the ied.tabs.onChange listeners when the tabs open or shown changed.
	tabsChanged() {
		const state = this.tabsState();
		const text = JSON.stringify(state);
		if (text === this.tabsShown) return;
		this.tabsShown = text;
		for (const fn of [...this.tabListeners]) fn(state);
	}

	// The file a launch URL names (`ied open`, docs/10), shown once its workspace's layout is in place by the command of
	// the last loaded plugin that contributes `fileOpener`, with {path, line, col} (1-based, 0 when absent).
	openLaunched() {
		const open = this.pendingOpen;
		if (!open || open.wsId !== this.wsId) return;
		this.pendingOpen = null;
		history.replaceState(null, "", location.pathname + location.search);
		if (!open.path) return;
		const opener = this.contributions.plugins.flatMap((p) => (p.contributes.fileOpener ? [p.contributes.fileOpener] : [])).pop();
		if (opener) this.commands.run(opener.command, { path: open.path, line: open.line, col: open.col });
		else this.toasts.show("No plugin opens files, so " + open.path + " is not shown.", "warn");
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Device class and chrome

	mountChrome() {
		if (this.chrome) this.chrome.dispose();
		this.chrome = this.deviceClass === "phone" ? new PhoneChrome(this, this.root) : new DesktopChrome(this, this.root);
		this.chrome.connection(this.client ? this.client.state : "connecting");
		// A chrome made for a new width shows what the old one did: a workspace is rendered by render(), the start page here.
		if (this.showingStartPage) this.chrome.startPage();
	}

	resized() {
		const next = deviceClassOf(window.innerWidth);
		if (next === this.deviceClass) return;
		const phoneChanged = (next === "phone") !== (this.deviceClass === "phone");
		this.deviceClass = next;
		this.context.set("deviceClass", next);
		this.menuHost.close();
		if (phoneChanged) this.mountChrome();
		if (this.layout) {
			this.layout.dispose();
			this.openLayout();
		}
		this.render();
	}

	render() {
		if (!this.chrome || !this.wsId) return;
		this.context.set("activeActivity", this.currentActivity());
		this.context.set("layoutMode", this.mode);
		this.chrome.render();
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Activities, tabs and views (used by the chrome, the commands and the plugins' `ied`)

	currentActivity() {
		const activities = this.contributions.activities;
		const stored = this.doc.activity;
		if (stored && activities.some((a) => a.id === stored)) return stored;
		return activities.length ? activities[0].id : null;
	}

	isDockHost(activityId) {
		const activity = this.contributions.activity(activityId);
		return Boolean(activity) && !activity.view;
	}

	// Whether dock host `activityId`'s document well takes tabs like `ref`: its `documents` (tab kinds) name the ref's
	// kind, or it names none.
	takesTab(activityId, ref) {
		const activity = this.contributions.activity(activityId);
		if (!activity || activity.view) return false;
		const kind = ref && ref.includes(":") ? ref.slice(0, ref.indexOf(":")) : null;
		return !activity.documents || activity.documents.includes(kind);
	}

	// The dock host a tab opens into: the current activity when it takes the tab, else the first that names its kind,
	// else the first that takes any.
	dockActivity(ref) {
		const current = this.currentActivity();
		if (current && this.takesTab(current, ref)) return current;
		const kind = ref && ref.includes(":") ? ref.slice(0, ref.indexOf(":")) : null;
		const hosts = this.contributions.activities.filter((a) => !a.view);
		const host = hosts.find((a) => a.documents && a.documents.includes(kind)) || hosts.find((a) => !a.documents) || hosts[0];
		return host ? host.id : null;
	}

	// The layout mode shown: the layout document's, which every window of the device class shows.
	get mode() {
		return (this.doc && this.doc.mode) || DEFAULT_MODE;
	}

	treeOf(activityId) {
		return this.treeIn(this.doc, activityId, this.mode);
	}

	// The tree of `activityId` in layout mode `mode`, its default placed for that mode; a tablet starts with its tool
	// windows auto-hidden (docs/15 "Phone layout").
	treeIn(doc, activityId, mode) {
		const tools = this.contributions.toolsOf(activityId).map((t) => Object.assign({ id: t.id }, this.contributions.placementOf(t, mode)));
		return treeOf(doc, activityId, tools, this.deviceClass === "tablet", mode);
	}

	changeTree(activityId, fn) {
		if (!this.layout || !activityId) return;
		this.layout.update((doc) => {
			const mode = doc.mode || DEFAULT_MODE;
			const before = this.treeIn(doc, activityId, mode);
			const after = fn(before);
			return after === before ? doc : withTree(doc, activityId, after, mode);
		});
	}

	// The layout mode the context asks for is the first contributed one whose `when` holds (debugging), else the default;
	// when it changes, the layout document switches to it, unless another window has already. A window that has just
	// loaded switches back to the default only once settled.
	updateMode() {
		const contributed = this.contributions.layoutModes().find((mode) => this.context.holds(mode.when));
		const next = contributed ? contributed.id : DEFAULT_MODE;
		if (next === this.wantedMode) return;
		this.wantedMode = next;
		if (this.modeSettled || next !== DEFAULT_MODE) this.reconcileMode();
	}

	// Switches the layout document to the wanted mode: that mode's trees, with the documents as they were (docs/15
	// "Window layouts"); nothing when it shows that mode already. A phone's has no trees to switch.
	reconcileMode() {
		if (!this.layout || this.deviceClass === "phone") return;
		const to = this.wantedMode;
		this.layout.update((doc) => {
			const from = doc.mode || DEFAULT_MODE;
			return from === to ? doc : Object.assign(this.withDocumentsCarried(doc, from, to), { mode: to });
		});
	}

	withDocumentsCarried(doc, from, to) {
		const isTool = (ref) => Boolean(this.contributions.toolWindow(ref));
		let next = doc;
		for (const activity of this.contributions.activities) {
			if (activity.view) continue;
			const carried = T.withDocumentsOf(this.treeIn(doc, activity.id, to), this.treeIn(doc, activity.id, from), isTool);
			next = withTree(next, activity.id, carried, to);
		}
		return next;
	}

	showActivity(activityId) {
		if (!this.layout || !this.contributions.activity(activityId)) return;
		if (this.deviceClass === "phone") this.layout.update((doc) => withPhonePane(withActivity(doc, activityId), "activity:" + activityId));
		else this.layout.update((doc) => withActivity(doc, activityId));
	}

	// The settings window, on the page `page` ("custom:shell.keyboard", "page:AI") when one is given.
	openSettings(page) {
		const open = [...this.settingsViews].pop();
		if (page && open) open.showPage(page);
		else if (page) this.nextSettingsPage = page;
		this.openTab("shell:settings");
	}

	// Opens a tab (ied.tabs.open): in the dock host's document group on a desktop, as the pane on a phone. A tab open
	// already is shown where it is; with {activity}, it opens in that dock host, leaving another it was in.
	openTab(ref, options) {
		const opts = options || {};
		if (!this.views.has(ref)) {
			this.report("Nothing can show '" + ref + "': no loaded plugin renders tabs of its kind.");
			return;
		}
		if (this.deviceClass === "phone") {
			this.layout.update((doc) => withPhonePane(doc, ref));
			return;
		}
		const asked = opts.activity && this.isDockHost(opts.activity) ? opts.activity : null;
		const where = this.activityOf(ref);
		if (asked && where && where !== asked) this.changeTree(where, (tree) => T.closeTab(tree, ref));
		const activityId = asked || where || this.dockActivity(ref);
		const groupPath = this.focus && this.focus.activity === activityId && this.focus.well ? this.focus.path : null;
		this.layout.update((doc) => withActivity(doc, activityId));
		this.changeTree(activityId, (tree) => T.revealPartners(T.openTab(tree, ref, { preview: opts.preview, groupPath }), ref, partnersOf(this.doc, ref)));
		this.slideOut(activityId, ref);
		if (!opts.preview || opts.focus) requestAnimationFrame(() => this.views.focus(ref));
	}

	// Opens `ref` beside `anchor` (ied.tabs.openBeside): in another document group, a new one to the right when there is
	// none; with `tether`, the two are tethered (`closes`: `ref` closes with `anchor`).
	openBeside(anchor, ref, options) {
		const opts = options || {};
		if (this.deviceClass === "phone" || !this.activityOf(anchor)) {
			this.openTab(ref, { preview: false });
			if (opts.tether) this.tether(anchor, ref, opts);
			return;
		}
		if (!this.views.has(ref)) {
			this.report("Nothing can show '" + ref + "': no loaded plugin renders tabs of its kind.");
			return;
		}
		this.changeTree(this.activityOf(anchor), (tree) => T.openBeside(tree, anchor, ref));
		if (opts.tether) this.tether(anchor, ref, opts);
	}

	// Tethers two tabs (docs/15 "Tethered tabs"): activating either shows the other in its own group; with `closes`,
	// `follower` closes with `lead`.
	tether(lead, follower, options) {
		if (!this.layout || lead === follower) return;
		this.layout.update((doc) => withTether(doc, lead, follower, options && options.closes));
	}

	untether(ref) {
		if (this.layout) this.layout.update((doc) => withoutTethers(doc, [ref]));
	}

	tetheredWith(ref) {
		return partnersOf(this.doc, ref);
	}

	// Shows a tool window (shell.showView): its activity, its tab added where its dock says when the tree lacks it.
	showView(id) {
		const tool = this.contributions.toolWindow(id);
		if (!tool) {
			this.report("There is no tool window '" + id + "'.");
			return;
		}
		if (this.deviceClass === "phone") {
			this.layout.update((doc) => withPhonePane(doc, id));
			return;
		}
		this.layout.update((doc) => withActivity(doc, tool.activity));
		// One that is not open goes where the mode's default puts it, auto-hidden when that hides it (and so slides out).
		const place = this.contributions.placementOf(tool, this.mode);
		this.changeTree(tool.activity, (tree) => {
			const shown = T.ensureTool(tree, id, place.dock, true, place.group, place.size);
			return place.autoHide && !T.refsOf(tree).includes(id) ? T.autoHideTab(shown, id) : shown;
		});
		this.slideOut(tool.activity, id);
		requestAnimationFrame(() => this.views.focus(id));
	}

	// An auto-hidden tab that is asked for slides out over the documents (VS).
	slideOut(activityId, ref) {
		if (T.isAutoHidden(this.treeOf(activityId), ref) && activityId === this.currentActivity() && this.chrome && this.chrome.slideOut) this.chrome.slideOut(activityId, ref);
	}

	// The activity whose tree holds `ref` (the current one first).
	activityOf(ref) {
		const current = this.currentActivity();
		const candidates = [current, ...Object.keys(this.doc.activities || {})].filter(Boolean);
		return candidates.find((id) => this.isDockHost(id) && T.refsOf(this.treeOf(id)).includes(ref)) || null;
	}

	activateTab(ref) {
		const activityId = this.activityOf(ref);
		if (activityId) {
			this.changeTree(activityId, (tree) => T.revealPartners(T.activateTab(tree, ref), ref, partnersOf(this.doc, ref)));
			this.slideOut(activityId, ref);
		}
		requestAnimationFrame(() => this.views.focus(ref));
	}

	closeTab(ref) {
		if (this.deviceClass === "phone" && this.doc.phone && this.doc.phone.pane === ref) {
			this.layout.update((doc) => phoneBack(doc));
			return;
		}
		if (this.deviceClass === "phone" && this.doc.phone && this.doc.phone.back.includes(ref)) {
			this.layout.update((doc) => withoutPhoneBack(doc, ref));
			return;
		}
		const activityId = this.activityOf(ref);
		if (activityId) this.closeTabsBy(activityId, (tree) => T.closeTab(tree, ref));
	}

	closeGroupTabs(ref, including) {
		const activityId = this.activityOf(ref);
		if (activityId) this.closeTabsBy(activityId, (tree) => T.closeGroupTabs(tree, ref, including));
	}

	// A change that closes tabs, then the plugins' onClose handlers for each tab closed here (by this window's person;
	// another window's views just go when its layout loses the tab).
	closeTabsBy(activityId, fn) {
		const before = T.refsOf(this.treeOf(activityId));
		const after = new Set(T.refsOf(fn(this.treeOf(activityId))));
		const closed = before.filter((ref) => !after.has(ref));
		const followers = closed.flatMap((ref) => followersOf(this.doc, ref)).filter((ref) => after.has(ref));
		this.changeTree(activityId, (tree) => followers.reduce((t, ref) => T.closeTab(t, ref), fn(tree)));
		if (this.layout && closed.length) this.layout.update((doc) => withoutTethers(doc, closed.concat(followers)));
		for (const ref of before) {
			if (after.has(ref) && !followers.includes(ref)) continue;
			for (const handler of this.tabCloseHandlers.get(ref.split(":")[0]) || []) {
				try {
					handler(ref);
				}
				catch (e) {
					this.report("A tab's close handler failed: " + e.message, e);
				}
			}
		}
	}

	onTabClose(kind, fn) {
		if (!this.tabCloseHandlers.has(kind)) this.tabCloseHandlers.set(kind, new Set());
		this.tabCloseHandlers.get(kind).add(fn);
		return () => this.tabCloseHandlers.get(kind).delete(fn);
	}

	pinTab(ref) {
		const activityId = this.activityOf(ref);
		if (activityId) this.changeTree(activityId, (tree) => T.pinTab(tree, ref));
	}

	moveTab(ref, path, zone, index) {
		const activityId = this.activityOf(ref);
		if (activityId) this.changeTree(activityId, (tree) => T.moveTab(tree, ref, path, zone, index));
	}

	// Moves a tab into a new group beside its own ("Move to New Group Right/Below").
	splitTab(ref, zone) {
		const activityId = this.activityOf(ref);
		if (!activityId) return;
		this.changeTree(activityId, (tree) => T.moveTab(tree, ref, T.pathOf(tree, T.groupOf(tree, ref)), zone));
	}

	// Auto-hides the tool window `ref` to its group's edge (VS's pin), or docks it back into its group.
	autoHideTab(ref, hidden) {
		const activityId = this.activityOf(ref);
		if (activityId) this.changeTree(activityId, (tree) => (hidden ? T.autoHideTab(tree, ref) : T.dockTab(tree, ref)));
		this.tabKeys(this.focus ? this.focus.ref : null);
	}

	autoHideAll() {
		const activityId = this.currentActivity();
		if (activityId && this.isDockHost(activityId)) this.changeTree(activityId, (tree) => T.autoHideAll(tree));
	}

	resizeAutoHidden(ref, share) {
		const activityId = this.activityOf(ref);
		if (activityId) this.changeTree(activityId, (tree) => T.setAutoHideSize(tree, ref, share));
	}

	resetLayout() {
		if (!this.layout) return;
		const activityId = this.currentActivity();
		this.layout.update((doc) => (this.deviceClass === "phone" ? Object.assign({}, doc, { phone: { pane: null, back: [] } }) : withoutTethers(withoutTree(doc, activityId, this.mode), T.refsOf(this.treeOf(activityId)))));
	}

	phoneBack() {
		if (this.layout) this.layout.update((doc) => phoneBack(doc));
	}

	// The tab with the focus moved (from a dock): context keys follow it.
	focused(activity, ref, path, well) {
		this.focus = { activity, ref, path, well };
		this.context.set("activeTab", ref || undefined);
		this.tabKeys(ref);
		this.context.set("activeTabKind", ref && ref.includes(":") ? ref.slice(0, ref.indexOf(":")) : ref ? "tool" : undefined);
		const resource = ref ? this.views.resourceOf(ref) : null;
		this.context.set("resourcePath", resource || undefined);
		this.context.set("resourceExtname", resource && /\.[^./]+$/.test(resource) ? /\.[^./]+$/.exec(resource)[0].toLowerCase() : undefined);
	}

	// Views whose tabs left every tree (and the phone's pane and back stack) are disposed.
	releaseUnused() {
		const keep = new Set();
		for (const a of this.contributions.activities) {
			if (a.view) keep.add("activity:" + a.id);
			else for (const ref of T.refsOf(this.treeOf(a.id))) keep.add(ref);
		}
		const phone = this.doc.phone || {};
		if (phone.pane) keep.add(phone.pane);
		for (const ref of phone.back || []) keep.add(ref);
		this.views.releaseExcept(keep);
	}

	// What the menus about a tab ask of it: tethered, in a tool-window group, auto-hidden.
	tabKeys(ref) {
		const activityId = ref ? this.activityOf(ref) : null;
		const owner = activityId ? T.groupOf(this.treeOf(activityId), ref) : null;
		this.context.set("tabRef", ref || undefined);
		this.context.set("tabKind", ref && ref.includes(":") ? ref.slice(0, ref.indexOf(":")) : undefined);
		this.context.set("tabTethered", (ref && partnersOf(this.doc, ref).length > 0) || undefined);
		this.context.set("tabInToolGroup", (owner && !owner.well) || undefined);
		this.context.set("tabAutoHidden", (owner && T.hiddenIn(owner, ref)) || undefined);
	}

	showMenu(menuId, at, args) {
		if (args && args.ref) this.tabKeys(args.ref);
		const rows = this.menus.resolve(menuId);
		return this.menuHost.show({
			rows, at, label: menuId,
			pick: (row) => this.commands.run(row.command, row.args !== undefined ? row.args : args),
			submenu: (id) => this.menus.resolve(id),
			// The keys go back to the focused tab's once the pick (which runs after the close) has been checked.
			onClose: args && args.ref ? () => setTimeout(() => this.tabKeys(this.focus ? this.focus.ref : null), 0) : undefined,
		});
	}

	report(message, error) {
		if (error) console.error("IED:", message, error);
		else console.warn("IED:", message);
		this.toasts.show(message, "error");
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The `ied` object a plugin's UI module gets (docs/02 "UI modules")

	apiFor(plugin) {
		if (!this.apis.has(plugin.id)) this.apis.set(plugin.id, this.makeApi(plugin));
		return this.apis.get(plugin.id);
	}

	makeApi(plugin) {
		const shell = this;
		return {
			plugin: { id: plugin.id, base: plugin.base },
			client: {
				request: (method, params, options) => shell.client.request(method, params, options),
				subscribe: (topic, handler) => shell.client.subscribe(topic, handler),
				newOpId: () => shell.client.newOpId(),
				get clientId() {
					return shell.client.clientId;
				},
			},
			commands: { handle: (id, fn) => shell.commands.handle(id, fn), run: (id, args) => shell.commands.run(id, args) },
			bindings: {
				provide: (name, fn) => shell.bindings.provide(plugin.id, name, fn),
				call: (name, args) => shell.bindings.call(plugin.id, name, args),
				emit: (name, data) => shell.bindings.emit(plugin.id, name, data),
				on: (name, fn) => shell.bindings.on(plugin.id, name, fn),
				has: (name) => shell.bindings.has(name),
			},
			tabs: {
				kind: (kind, spec) => shell.views.registerKind(kind, Object.assign({}, spec, { plugin: plugin.id === "shell" ? null : plugin.id })),
				open: (ref, options) => shell.openTab(ref, options),
				// `ref` beside `anchor`; {tether, closes} tethers them (docs/15 "Tethered tabs").
				openBeside: (anchor, ref, options) => shell.openBeside(anchor, ref, options),
				tether: (lead, follower, options) => shell.tether(lead, follower, options),
				untether: (ref) => shell.untether(ref),
				// `fn(ref)` when the person closes a tab of `kind` in this window.
				onClose: (kind, fn) => shell.onTabClose(kind, fn),
				close: (ref) => shell.closeTab(ref),
				// {open: [ref], shown: [ref]}: the tabs open in this window's layout, and those shown.
				state: () => shell.tabsState(),
				// `fn(state)` each time state() would answer differently; returns the function that stops it.
				onChange: (fn) => {
					shell.tabListeners.add(fn);
					return () => shell.tabListeners.delete(fn);
				},
			},
			context: { set: (key, value) => shell.context.set(key, value), get: (key) => shell.context.get(key) },
			contributions: Object.assign((point) => shell.contributions.of(point), { on: (fn) => shell.contributions.on(fn) }),
			icons: {
				file: (name, extraClass) => shell.contributions.icons.file(name, extraClass),
				fileSpec: (name) => shell.contributions.icons.fileSpec(name),
				folder: (open, extraClass) => shell.contributions.icons.folder(open, extraClass),
				project: (project, rootNames, extraClass) => shell.contributions.icons.project(project, rootNames, extraClass),
				needsMarkers: (project) => shell.contributions.icons.needsMarkers(project),
				solution: (extraClass) => shell.contributions.icons.solution(extraClass),
			},
			menus: { show: (menuId, at, args) => shell.showMenu(menuId, at, args) },
			get workspace() {
				return shell.workspace;
			},
			notify: (text, level) => shell.client.request("server.notify", { text, level: level || "info" }),
			toast: (text, options) => shell.toasts.show(text, (options && options.level) || "info", options && options.action),
			report: (message, error) => shell.report("Plugin '" + plugin.id + "': " + message, error),
		};
	}
}

function workspaceInfo() {
	const info = { wsId: null, name: "", kind: null, root: "", trusted: true, solution: null, listeners: new Set() };
	info.on = (fn) => {
		info.listeners.add(fn);
		return () => info.listeners.delete(fn);
	};
	return info;
}

function socketUrl() {
	return (location.protocol === "https:" ? "wss://" : "ws://") + location.host + "/ws";
}
