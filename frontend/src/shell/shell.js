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
import { treeOf, withTree, withoutTree, withActivity, withPhonePane, phoneBack, emptyDocument } from "./layoutdoc.js";
import * as T from "./layouttree.js";
import { Toasts } from "./toasts.js";
import { registerShellCommands } from "./hostcommands.js";
import { registerShellViews } from "./hostviews.js";
import { DesktopChrome } from "./desktop.js";
import { PhoneChrome } from "./phone.js";

const AUTH_PROBE_AFTER_MS = 1500;
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
		this.contributions = new Contributions(this);
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
		this.deviceClass = deviceClassOf(window.innerWidth);
		this.chrome = null;
		this.focus = null;
		this.authProbe = null;
		this.shellApi = this.makeApi({ id: "shell", base: "/" });
		this.wsTopic = null;
		this.serverListeners = new Set();
		this.views.onChange((entry) => this.chrome && this.chrome.tabChanged(entry));
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
			return;
		}
		if (this.startPageAsked) {
			this.startPageAsked = false;
			history.replaceState(null, "", location.pathname + location.search);
		}
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
		this.render();
	}

	openLayout() {
		this.layout = new LayoutStore(this.client, this.wsId, this.deviceClass, (doc) => this.adopt(doc));
	}

	adopt(doc) {
		this.doc = doc;
		this.render();
		this.releaseUnused();
		this.openLaunched();
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

	// The dock host a tab opens into: the current activity when it is one, else the first.
	dockActivity() {
		const current = this.currentActivity();
		if (current && this.isDockHost(current)) return current;
		const host = this.contributions.activities.find((a) => !a.view);
		return host ? host.id : null;
	}

	treeOf(activityId) {
		return treeOf(this.doc, activityId, this.contributions.toolsOf(activityId).map((t) => ({ id: t.id, dock: t.dock })));
	}

	changeTree(activityId, fn) {
		if (!this.layout || !activityId) return;
		this.layout.update((doc) => {
			const before = treeOf(doc, activityId, this.contributions.toolsOf(activityId).map((t) => ({ id: t.id, dock: t.dock })));
			const after = fn(before);
			return after === before ? doc : withTree(doc, activityId, after);
		});
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

	// Opens a tab (ied.tabs.open): in the dock host's document group on a desktop, as the pane on a phone.
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
		const activityId = this.dockActivity();
		const groupPath = this.focus && this.focus.activity === activityId && this.focus.well ? this.focus.path : null;
		this.layout.update((doc) => withActivity(doc, activityId));
		this.changeTree(activityId, (tree) => T.openTab(tree, ref, { preview: opts.preview, groupPath }));
		if (!opts.preview || opts.focus) requestAnimationFrame(() => this.views.focus(ref));
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
		this.changeTree(tool.activity, (tree) => T.ensureTool(tree, id, tool.dock, true));
		requestAnimationFrame(() => this.views.focus(id));
	}

	// The activity whose tree holds `ref` (the current one first).
	activityOf(ref) {
		const current = this.currentActivity();
		const candidates = [current, ...Object.keys(this.doc.activities || {})].filter(Boolean);
		return candidates.find((id) => this.isDockHost(id) && T.refsOf(this.treeOf(id)).includes(ref)) || null;
	}

	activateTab(ref) {
		const activityId = this.activityOf(ref);
		if (activityId) this.changeTree(activityId, (tree) => T.activateTab(tree, ref));
		requestAnimationFrame(() => this.views.focus(ref));
	}

	closeTab(ref) {
		if (this.deviceClass === "phone" && this.doc.phone && this.doc.phone.pane === ref) {
			this.layout.update((doc) => phoneBack(doc));
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
		this.changeTree(activityId, fn);
		for (const ref of before) {
			if (after.has(ref)) continue;
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

	resetLayout() {
		if (!this.layout) return;
		const activityId = this.currentActivity();
		this.layout.update((doc) => (this.deviceClass === "phone" ? Object.assign({}, doc, { phone: { pane: null, back: [] } }) : withoutTree(doc, activityId)));
	}

	phoneBack() {
		if (this.layout) this.layout.update((doc) => phoneBack(doc));
	}

	// The tab with the focus moved (from a dock): context keys follow it.
	focused(activity, ref, path, well) {
		this.focus = { activity, ref, path, well };
		this.context.set("activeTab", ref || undefined);
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

	showMenu(menuId, at, args) {
		const rows = this.menus.resolve(menuId);
		return this.menuHost.show({
			rows, at, label: menuId,
			pick: (row) => this.commands.run(row.command, row.args !== undefined ? row.args : args),
			submenu: (id) => this.menus.resolve(id),
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
				has: (name) => shell.bindings.has(name),
			},
			tabs: {
				kind: (kind, spec) => shell.views.registerKind(kind, Object.assign({}, spec, { plugin: plugin.id === "shell" ? null : plugin.id })),
				open: (ref, options) => shell.openTab(ref, options),
				// `fn(ref)` when the person closes a tab of `kind` in this window.
				onClose: (kind, fn) => shell.onTabClose(kind, fn),
			},
			context: { set: (key, value) => shell.context.set(key, value), get: (key) => shell.context.get(key) },
			contributions: (point) => shell.contributions.of(point),
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
