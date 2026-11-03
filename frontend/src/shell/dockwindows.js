// dockwindows.js - this window among the device's windows (docs/15 "Windows"): an activity is docked to one window, each
// window has its own sidebar and shows its own activity, so the chat can sit on one monitor and the code on another. Which
// window holds what is layout state (windowdoc.js); this is what a window does about it: opening a window for an activity,
// taking one in, showing what another window holds, closing when it has given away its last, and, in the main window,
// taking back what a closed window held and opening again what the last session left in other windows.
// The windows of a device find each other on a BroadcastChannel: it answers for a live page, whatever its socket does.

import { parseOpenFragment } from "./launch.js";
import { activitiesOf, holderOf, newWindowId, windowOf, windowsOf, withMoved, withWindow, withWindowChanged, withoutWindow } from "./windowdoc.js";

const CHANNEL = "ied.windows";
const ASK_MS = 250;
const CLOSED_AFTER_MS = 3000;     // a window whose socket went may be reloading
const REOPEN_AFTER_MS = 3000;     // windows reloading with this one have answered by then
const OPENING_MS = 30000;         // a window just opened is left alone until it has answered, or this long
const REVEAL_KEEP_MS = 3000;      // how long a tab asked for may wait for the document that holds it
const GESTURE_MS = 1500;          // a person's act this recent is what asks to go to another window
const RECT_KEY = "ied.window.";
const DOCKED_KEY = "ied.windows.docked.";

function askedWindow(location) {
	const params = new URLSearchParams(location.search);
	const open = parseOpenFragment(location.hash);
	const win = params.get("win") || (open && open.win) || null;
	return win ? { id: win, ws: params.get("ws") || (open && open.wsId) || null } : { id: null, ws: null };
}

function bearingKey(event) {
	return event.ctrlKey || event.altKey || event.metaKey || event.key === "Enter" || /^F\d+$/.test(event.key);
}

function wait(ms) {
	return new Promise((resolve) => setTimeout(resolve, ms));
}

export class DockWindows {
	constructor(shell, page = window) {
		this.shell = shell;
		this.page = page;
		const asked = askedWindow(page.location);
		this.id = asked.id;               // null: this is the device's main window
		this.ws = asked.ws;               // the workspace this window belongs to
		this.dockedElsewhere = false;     // a main window: this device has windows of this workspace (the last document said)
		this.channel = null;
		this.asks = new Map();            // token -> what has answered
		this.nextToken = 1;
		this.openedAt = new Map();        // id -> when this window opened it
		this.handles = new Map();         // id -> the browser's handle of a window opened here
		this.heldCount = 0;               // how many activities this window held at the last look
		this.reclaimTimer = null;
		this.reveal = null;               // {ref, until}: a tab another window asked this one to show
		this.lastGesture = 0;
	}

	get device() {
		return this.shell.identity ? this.shell.identity.deviceId : null;
	}

	// The windows of a device share one layout document, which says who holds what, so while it has any they all stay in the
	// desktop's at any width: a narrow main window on the tablet's would not know, and show them all again.
	get pinnedClass() {
		return this.id || this.dockedElsewhere ? "desktop" : null;
	}

	start() {
		if (typeof BroadcastChannel === "function") {
			this.channel = new BroadcastChannel(CHANNEL);
			this.channel.onmessage = (event) => this.heard(event.data || {});
		}
		const page = this.page;
		page.addEventListener("pointerdown", () => { this.lastGesture = Date.now(); }, true);
		page.addEventListener("keydown", (event) => { if (bearingKey(event)) this.lastGesture = Date.now(); }, true);
		if (!this.id || page.iedDesktop) return;
		const keep = () => this.rememberRect();
		page.addEventListener("resize", keep);
		page.addEventListener("blur", keep);
		page.addEventListener("pagehide", keep);
	}

	// -----------------------------------------------------------------------------------------------------------------
	// Asking the other windows

	post(message) {
		if (this.channel) this.channel.postMessage(Object.assign({ ws: this.shell.wsId }, message));
	}

	heard(message) {
		if (!this.shell.wsId || message.ws !== this.shell.wsId) return;
		if (message.type === "who") this.post({ type: "here", token: message.token, win: this.id });
		else if (message.type === "here") this.answered(message);
		else if (message.type === "raise" && message.win === this.id) this.revealed(null);
		else if (message.type === "reveal" && this.shell.isHere(message.activity)) {
			this.post({ type: "here", token: message.token, win: this.id });
			this.revealed(message.ref);
		}
	}

	answered(message) {
		this.openedAt.delete(message.win);
		if (this.asks.has(message.token)) this.asks.get(message.token).add(message.win);
	}

	// What answers `message` within ASK_MS: a set of window ids (null: a main window).
	async ask(message) {
		const token = this.nextToken++ + "." + Math.random();
		const answers = new Set();
		this.asks.set(token, answers);
		this.post(Object.assign({ token }, message));
		await wait(ASK_MS);
		this.asks.delete(token);
		return answers;
	}

	// Another page already being this window (a bookmark, a window opened twice) leaves this one to be a main window - the
	// window that was opened again only to be this one closes. True when it gave up being docked.
	async claim() {
		if (!this.id || !this.channel) return false;
		const answers = await this.ask({ type: "who" });
		if (answers.has(this.id)) {
			this.closeThisWindow();
			this.forget();
			return true;
		}
		this.post({ type: "here", win: this.id });
		return false;
	}

	forget() {
		this.id = null;
		this.ws = null;
	}

	// This window goes to workspace `wsId`. It was docked to another workspace's layout: it leaves that one, its activities
	// going back to that workspace's main window, and is a main window itself. A main window hears whether this device has
	// windows of that workspace (the document, read next, says for sure).
	showing(wsId) {
		if (this.id && wsId !== this.ws) {
			const shell = this.shell;
			const id = this.id;
			if (shell.layout) shell.layout.update((doc) => withoutWindow(doc, this.device, id));
			this.forget();
		}
		this.dockedElsewhere = !this.id && Boolean(wsId) && this.recalled(wsId);
	}

	recalled(wsId) {
		try {
			return this.page.localStorage.getItem(DOCKED_KEY + wsId) === "1";
		}
		catch {
			return false;
		}
	}

	// A main window noting what the document says, for its next start at any width. Windows gone, it goes back to the layout
	// of its width.
	noteDocked(docked) {
		if (docked === this.dockedElsewhere) return;
		this.dockedElsewhere = docked;
		try {
			if (docked) this.page.localStorage.setItem(DOCKED_KEY + this.shell.wsId, "1");
			else this.page.localStorage.removeItem(DOCKED_KEY + this.shell.wsId);
		}
		catch {
			// a window without storage finds out when its document comes
		}
		if (!docked) setTimeout(() => this.shell.resized(), 0);
	}

	// A window is about to be docked: a main window at a tablet's width moves to the desktop's layout, where windows are kept.
	beforeDocking() {
		if (this.id) return;
		this.noteDocked(true);
		this.shell.resized();
	}

	// The address's part that makes a reload this same window.
	addressPart() {
		return this.id ? "&win=" + encodeURIComponent(this.id) : "";
	}

	urlOf(id) {
		const page = this.page;
		return page.location.origin + page.location.pathname + "?ws=" + encodeURIComponent(this.shell.wsId) + "&win=" + encodeURIComponent(id);
	}

	// -----------------------------------------------------------------------------------------------------------------
	// Moving activities

	// `activityId` into a window of its own, at `at` ({x, y} on the screen, a drop) when given; false when it could not open.
	moveToNewWindow(activityId, at) {
		const shell = this.shell;
		if (!shell.layout || !activityId) return false;
		const id = newWindowId(shell.doc, this.device, activityId);
		if (!this.openWindow(id, at)) {
			shell.report("The browser blocked the new window: allow pop-ups for this page and try again.");
			return false;
		}
		this.beforeDocking();
		shell.layout.update((doc) => withMoved(withWindow(doc, this.device, id), this.device, activityId, id));
		shell.layout.flush();
		return true;
	}

	// A window of its own with nothing docked yet, where `Window > New Window` starts: it offers what it could take.
	newWindow() {
		const shell = this.shell;
		if (!shell.layout) return false;
		const id = newWindowId(shell.doc, this.device, null);
		if (!this.openWindow(id)) {
			shell.report("The browser blocked the new window: allow pop-ups for this page and try again.");
			return false;
		}
		this.beforeDocking();
		shell.layout.update((doc) => withWindow(doc, this.device, id));
		shell.layout.flush();
		return true;
	}

	// `activityId` to window `to` (null: the main window), which comes forward when it is another.
	moveTo(activityId, to) {
		const shell = this.shell;
		if (!shell.layout) return;
		shell.layout.update((doc) => withMoved(to ? withWindow(doc, this.device, to) : doc, this.device, activityId, to));
		shell.layout.flush();
		if (to !== this.id) this.post({ type: "raise", win: to });
	}

	moveHere(activityId) {
		this.moveTo(activityId, this.id);
	}

	// A tab dragged from here was let go where no window took it: a window of its own, there. Its window's only activity has
	// that window already.
	tearOff(activityId, at) {
		if (this.id && this.shell.activitiesHere().length === 1) return;
		this.moveToNewWindow(activityId, at);
	}

	// The windows of this device other than this one: [{id, activities: [activity id]}], the main window's id null, with
	// what each holds (the main window's when it may not be open: only what no other window names).
	others() {
		const shell = this.shell;
		const all = shell.contributions.activities.map((a) => a.id);
		const docked = windowsOf(shell.doc, this.device).filter((w) => w.id !== this.id).map((w) => ({ id: w.id, activities: activitiesOf(shell.doc, this.device, w.id, all) }));
		return this.id ? [{ id: null, activities: activitiesOf(shell.doc, this.device, null, all) }, ...docked] : docked;
	}

	// -----------------------------------------------------------------------------------------------------------------
	// Opening and closing windows

	openWindow(id, at) {
		const page = this.page;
		const url = this.urlOf(id);
		this.openedAt.set(id, Date.now());
		if (page.iedDesktop) {
			page.iedDesktop.newWindow(url, Boolean(at));
			return true;
		}
		const opened = page.open(url, "ied-" + id, this.featuresFor(id, at));
		if (!opened) return false;
		this.handles.set(id, opened);
		return true;
	}

	// A browser's window: where it was (a window of this browser remembered), or beside this one, as large.
	featuresFor(id, at) {
		const page = this.page;
		const rect = this.rememberedRect(id) || { width: page.outerWidth, height: page.outerHeight };
		const left = at ? at.x - 120 : rect.x !== undefined ? rect.x : page.screenX + 32;
		const top = at ? at.y - 16 : rect.y !== undefined ? rect.y : page.screenY + 32;
		return "popup=yes,width=" + rect.width + ",height=" + rect.height + ",left=" + left + ",top=" + top;
	}

	rememberedRect(id) {
		try {
			return JSON.parse(this.page.localStorage.getItem(RECT_KEY + id));
		}
		catch {
			return null;
		}
	}

	rememberRect() {
		const page = this.page;
		try {
			page.localStorage.setItem(RECT_KEY + this.id, JSON.stringify({ x: page.screenX, y: page.screenY, width: page.outerWidth, height: page.outerHeight }));
		}
		catch {
			// a window without storage opens where the browser puts it
		}
	}

	closeThisWindow() {
		const page = this.page;
		if (page.iedDesktop && page.iedDesktop.closeWindow) page.iedDesktop.closeWindow();
		else page.close();
	}

	// -----------------------------------------------------------------------------------------------------------------
	// The document changed

	// A window opened for its activities closes when it has given away the last of them. A tab asked for by another window
	// is shown once the document holding it has arrived.
	changed() {
		const shell = this.shell;
		this.settleReveal();
		if (!this.id) {
			this.noteDocked(windowsOf(shell.doc, this.device).length > 0);
			return;
		}
		const own = windowOf(shell.doc, this.device, this.id);
		const count = own ? own.activities.length : 0;
		const gaveAway = this.heldCount > 0 && count === 0;
		this.heldCount = count;
		if (!gaveAway) return;
		shell.layout.update((doc) => withoutWindow(doc, this.device, this.id));
		shell.layout.flush().then(() => this.closeThisWindow());
	}

	// A panel's state is its window's (paneldock.js): the holder of `panel`, the window's own entry or the document.
	panelHolder() {
		return (this.id && windowOf(this.shell.doc, this.device, this.id)) || (this.id ? {} : this.shell.doc);
	}

	// `doc` with `change(holder)` made to what panelHolder() names.
	withPanelChanged(doc, change) {
		return this.id ? withWindowChanged(doc, this.device, this.id, change) : change(doc);
	}

	// -----------------------------------------------------------------------------------------------------------------
	// Showing what another window holds

	// `activityId` (and in it `ref`) was asked for here and another window holds it: what it shows is in the document already.
	// When a person asked (not an event every window hears), that window comes forward; with none to answer, they asked for
	// something no open window holds, and it comes here.
	async revealElsewhere(activityId, ref) {
		const shell = this.shell;
		if (shell.isHere(activityId) || !shell.layout || Date.now() - this.lastGesture > GESTURE_MS) return;
		const handle = this.handles.get(holderOf(shell.doc, this.device, activityId));
		if (handle && !handle.closed) handle.focus();   // a browser lets only the opener do this, within the person's act
		shell.layout.flush();
		const answers = await this.ask({ type: "reveal", activity: activityId, ref: ref || null });
		if (answers.size || shell.isHere(activityId)) return;
		this.moveHere(activityId);
		if (ref) this.revealed(ref);
	}

	revealed(ref) {
		const page = this.page;
		if (page.iedDesktop) page.iedDesktop.focus();
		else page.focus();
		if (!ref) return;
		this.reveal = { ref, until: Date.now() + REVEAL_KEEP_MS };
		this.settleReveal();
	}

	settleReveal() {
		if (!this.reveal) return;
		const { ref, until } = this.reveal;
		const activityId = this.shell.activityOf(ref);
		if (activityId && this.shell.isHere(activityId)) {
			this.reveal = null;
			this.shell.activateTab(ref);
		}
		else if (Date.now() > until) this.reveal = null;
	}

	// -----------------------------------------------------------------------------------------------------------------
	// The main window: what closed windows held, and what the last session left

	// A client of this device went: when it was one of the windows, what it held comes back here. A window that closes loses
	// its socket whatever ended it; one reloading answers again by then.
	clientLeft(client) {
		const shell = this.shell;
		if (this.id || client.deviceId !== this.device || !shell.layout || !windowsOf(shell.doc, this.device).length) return;
		clearTimeout(this.reclaimTimer);
		this.reclaimTimer = setTimeout(() => this.reclaim(), CLOSED_AFTER_MS);
	}

	async reclaim() {
		const shell = this.shell;
		const layout = shell.layout;
		if (!layout) return;
		const gone = await this.missing();
		if (shell.layout !== layout) return;
		layout.update((doc) => gone.reduce((next, w) => withoutWindow(next, this.device, w.id), doc));
		// A window closed before it had said a word is looked at again when its time is up.
		if (this.openedAt.size) this.reclaimTimer = setTimeout(() => this.reclaim(), OPENING_MS);
	}

	async missing() {
		if (!this.channel) return [];
		const answers = await this.ask({ type: "who" });
		const now = Date.now();
		for (const [id, at] of [...this.openedAt]) if (now - at > OPENING_MS) this.openedAt.delete(id);
		return windowsOf(this.shell.doc, this.device).filter((w) => !answers.has(w.id) && !this.openedAt.has(w.id));
	}

	// The layout came with windows no page is: the last session's, opened again. A browser refuses what no person asked for:
	// then their activities stay here.
	async reopen() {
		const shell = this.shell;
		const layout = shell.layout;
		if (this.id || !layout) return;
		await layout.ready;
		if (!windowsOf(shell.doc, this.device).length) return;
		await wait(REOPEN_AFTER_MS);
		if (shell.layout !== layout) return;
		const gone = await this.missing();
		if (shell.layout !== layout) return;
		const refused = gone.filter((w) => !w.activities.length || !this.openWindow(w.id));
		layout.update((doc) => refused.reduce((next, w) => withoutWindow(next, this.device, w.id), doc));
		const lost = refused.flatMap((w) => w.activities).map((id) => shell.contributions.activity(id)).filter(Boolean);
		if (lost.length) shell.toasts.show("The browser blocked reopening the window of " + lost.map((a) => a.title).join(", ") + ": it is back in this window.", "info");
	}

	reset() {
		clearTimeout(this.reclaimTimer);
		this.reveal = null;
		this.heldCount = 0;
	}
}
