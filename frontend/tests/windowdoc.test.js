// windowdoc.test.js - which window holds which activity (docs/15 "Windows"), two windows changing one layout document, and
// what a window's address says it is.

import { test, assertEqual, assertTrue } from "./harness.js";
import * as W from "../src/shell/windowdoc.js";
import { mergeLayouts } from "../src/shell/layoutmerge.js";
import { emptyDocument } from "../src/shell/layoutdoc.js";
import { DockWindows } from "../src/shell/dockwindows.js";
import { droppedOutside } from "../src/shell/activitydrag.js";

const ALL = ["ied.chat", "ied.editor", "ied.git"];
const ME = "d-me";
const OTHER = "d-laptop";

function chatAlone() {
	return W.withMoved(W.withWindow(emptyDocument(), ME, "w-chat"), ME, "ied.chat", "w-chat");
}

test("windows: a document with none is one window holding everything", () => {
	const doc = emptyDocument();
	assertEqual(W.activitiesOf(doc, ME, null, ALL), ALL);
	assertEqual(W.holderOf(doc, ME, "ied.chat"), null);
	assertEqual(W.shownIn(doc, ME, null, ALL), "ied.chat", "the first");
	assertEqual(W.shownIn(Object.assign({}, doc, { activity: "ied.git" }), ME, null, ALL), "ied.git");
});

test("windows: an activity moved to a window leaves the main window, and shows there", () => {
	const doc = chatAlone();
	assertEqual(W.activitiesOf(doc, ME, null, ALL), ["ied.editor", "ied.git"]);
	assertEqual(W.activitiesOf(doc, ME, "w-chat", ALL), ["ied.chat"]);
	assertEqual(W.holderOf(doc, ME, "ied.chat"), "w-chat");
	assertEqual(W.shownIn(doc, ME, "w-chat", ["ied.chat"]), "ied.chat");
	assertEqual(W.activitiesOf(doc, ME, "w-gone", ALL), [], "a window the document does not know holds nothing");
});

test("windows: another device's main window holds everything", () => {
	const doc = chatAlone();
	assertEqual(W.activitiesOf(doc, OTHER, null, ALL), ALL);
	assertEqual(W.holderOf(doc, OTHER, "ied.chat"), null);
});

test("windows: each window shows its own activity", () => {
	const doc = W.withMoved(chatAlone(), ME, "ied.git", "w-chat");
	assertEqual(W.shownIn(doc, ME, "w-chat", ["ied.chat", "ied.git"]), "ied.git", "the one moved last");
	const shown = W.withShown(doc, ME, "ied.chat");
	assertEqual(W.shownIn(shown, ME, "w-chat", ["ied.chat", "ied.git"]), "ied.chat");
	assertEqual(shown.activity, doc.activity, "the main window's is not touched");
	const main = W.withShown(doc, ME, "ied.editor");
	assertEqual(main.activity, "ied.editor");
	assertEqual(W.shownIn(main, ME, "w-chat", ["ied.chat", "ied.git"]), "ied.git", "nor the other's");
	assertTrue(W.withShown(main, ME, "ied.editor") === main, "nothing changes when it shows already");
});

test("windows: moving to the main window and closing a window bring activities back", () => {
	const moved = W.withMoved(chatAlone(), ME, "ied.chat", null);
	assertEqual(W.activitiesOf(moved, ME, null, ALL), ALL);
	assertEqual(moved.activity, "ied.chat", "shown where it landed");
	assertEqual(W.windowOf(moved, ME, "w-chat").activities, [], "the window stays, empty, until it closes");
	const closed = W.withoutWindow(chatAlone(), ME, "w-chat");
	assertEqual(W.activitiesOf(closed, ME, null, ALL), ALL);
	assertTrue(!("windows" in closed), "no trace left");
});

test("windows: an activity is in one window at a time", () => {
	const doc = W.withMoved(W.withWindow(chatAlone(), ME, "w-git"), ME, "ied.chat", "w-git");
	assertEqual(W.windowOf(doc, ME, "w-chat").activities, []);
	assertEqual(W.windowOf(doc, ME, "w-git").activities, ["ied.chat"]);
});

test("windows: a window's id comes from its activity, so its place comes back, and is unique", () => {
	assertEqual(W.newWindowId(emptyDocument(), ME, "ied.chat"), "w-ied.chat");
	assertEqual(W.newWindowId(emptyDocument(), ME, null), "w-window");
	assertEqual(W.newWindowId(emptyDocument(), ME, "a/b c"), "w-a-b-c", "a name a file can have");
	const taken = W.withWindow(emptyDocument(), ME, "w-ied.chat");
	assertEqual(W.newWindowId(taken, ME, "ied.chat"), "w-ied.chat-2");
	assertEqual(W.newWindowId(taken, OTHER, "ied.chat"), "w-ied.chat", "another device has its own");
});

test("layout merge: each window keeps what it changed", () => {
	const base = Object.assign(emptyDocument(), { activity: "ied.editor", activities: { a: { tabs: ["1"] }, b: { tabs: ["2"] } } });
	const mine = Object.assign({}, base, { activities: Object.assign({}, base.activities, { a: { tabs: ["1", "mine"] } }) });
	const theirs = Object.assign({}, base, { activity: "ied.git", activities: Object.assign({}, base.activities, { b: { tabs: ["2", "theirs"] } }) });
	const merged = mergeLayouts(base, mine, theirs);
	assertEqual(merged.activities.a.tabs, ["1", "mine"], "my tree");
	assertEqual(merged.activities.b.tabs, ["2", "theirs"], "their tree");
	assertEqual(merged.activity, "ied.git", "their field");
});

test("layout merge: the same part changed by both goes to the one merging, the last to write", () => {
	const base = Object.assign(emptyDocument(), { activities: { a: { tabs: ["1"] } } });
	const mine = Object.assign({}, base, { activities: { a: { tabs: ["mine"] } } });
	const theirs = Object.assign({}, base, { activities: { a: { tabs: ["theirs"] } } });
	assertEqual(mergeLayouts(base, mine, theirs).activities.a.tabs, ["mine"]);
});

test("layout merge: a tree or a window one side removed stays removed", () => {
	const base = Object.assign(chatAlone(), { activities: { a: { tabs: ["1"] }, b: { tabs: ["2"] } } });
	const mine = Object.assign({}, base, { activities: { a: base.activities.a, b: { tabs: ["2", "mine"] } } });
	const theirs = W.withoutWindow(Object.assign({}, base, { activities: { b: base.activities.b } }), ME, "w-chat");
	const merged = mergeLayouts(base, mine, theirs);
	assertEqual(Object.keys(merged.activities), ["b"], "their closed tree");
	assertEqual(merged.activities.b.tabs, ["2", "mine"]);
	assertTrue(!("windows" in merged), "their closed window");
});

test("layout merge: the fields of one window merge, modes by tree, and an activity stays in one window", () => {
	const base = W.withWindow(chatAlone(), ME, "w-git");
	const mine = W.withWindowChanged(base, ME, "w-chat", (w) => Object.assign({}, w, { panel: { open: true } }));
	const theirs = W.withShown(W.withMoved(base, ME, "ied.git", "w-chat"), ME, "ied.git");
	const merged = mergeLayouts(base, mine, theirs);
	const chat = W.windowOf(merged, ME, "w-chat");
	assertEqual([chat.panel, chat.activities, chat.activity], [{ open: true }, ["ied.chat", "ied.git"], "ied.git"]);
	const racing = mergeLayouts(base, W.withMoved(base, ME, "ied.git", "w-git"), W.withMoved(base, ME, "ied.git", "w-chat"));
	const holding = W.windowsOf(racing, ME).filter((w) => w.activities.includes("ied.git"));
	assertEqual(holding.length, 1, "two moves at once leave it in one window");
	const modal = Object.assign(emptyDocument(), { modes: { debug: { activities: { a: { tabs: ["1"] }, b: { tabs: ["2"] } } } } });
	const modalMine = Object.assign({}, modal, { modes: { debug: { activities: { a: { tabs: ["mine"] }, b: { tabs: ["2"] } } } } });
	const modalTheirs = Object.assign({}, modal, { modes: { debug: { activities: { a: { tabs: ["1"] }, b: { tabs: ["theirs"] } } } } });
	const debug = mergeLayouts(modal, modalMine, modalTheirs).modes.debug.activities;
	assertEqual([debug.a.tabs, debug.b.tabs], [["mine"], ["theirs"]]);
});

function fakePage(search, hash) {
	return { location: { search, hash, origin: "http://ied.test", pathname: "/" } };
}

test("windows: a page's address says which window it is", () => {
	const main = new DockWindows({}, fakePage("?ws=w1", ""));
	assertEqual([main.id, main.pinnedClass, main.addressPart()], [null, null, ""]);
	const chat = new DockWindows({}, fakePage("?ws=w1&win=w-ied.chat", ""));
	assertEqual([chat.id, chat.ws, chat.pinnedClass], ["w-ied.chat", "w1", "desktop"]);
	assertEqual(chat.addressPart(), "&win=w-ied.chat");
	const back = new DockWindows({}, fakePage("", "#open=w1/?win=w-ied.chat"));
	assertEqual([back.id, back.ws, back.pinnedClass], ["w-ied.chat", "w1", "desktop"], "the desktop app bringing it back, through sign-in");
});

test("windows: a main window stays in the desktop's layout at any width while the device has windows", () => {
	const stored = new Map();
	const page = Object.assign(fakePage("?ws=w1", ""), { localStorage: { getItem: (k) => stored.get(k) || null, setItem: (k, v) => stored.set(k, v), removeItem: (k) => stored.delete(k) } });
	const shell = { wsId: "w1", identity: { deviceId: ME }, resized: () => {} };
	const main = new DockWindows(shell, page);
	assertEqual(main.pinnedClass, null, "by its width");
	main.noteDocked(true);
	assertEqual(main.pinnedClass, "desktop");
	const later = new DockWindows(shell, page);
	later.showing("w1");
	assertEqual(later.pinnedClass, "desktop", "from its next start, before the document comes");
	later.showing("w2");
	assertEqual(later.pinnedClass, null, "another workspace's windows are another matter");
	later.showing("w1");
	later.noteDocked(false);
	assertEqual(later.pinnedClass, null, "the windows gone");
	const again = new DockWindows(shell, page);
	again.showing("w1");
	assertEqual(again.pinnedClass, null, "and it remembers that");
});

test("windows: a window docked to one workspace leaving for another is a main window", () => {
	const updates = [];
	const shell = { identity: { deviceId: ME }, layout: { update: (fn) => updates.push(fn(chatAlone())) } };
	const chat = new DockWindows(shell, fakePage("?ws=w1&win=w-chat", ""));
	chat.showing("w1");
	assertEqual(chat.id, "w-chat", "its own");
	chat.showing("w2");
	assertEqual([chat.id, chat.pinnedClass], [null, null]);
	assertEqual(W.windowsOf(updates[0], ME, "w-chat"), [], "what it held goes back to that workspace's main window");
});

function dragEnd(dropEffect, clientX, clientY) {
	return { dataTransfer: { dropEffect }, clientX, clientY, screenX: clientX + 1000, screenY: clientY + 10 };
}

test("windows: a drag let go outside every window asks for one there", () => {
	const page = { innerWidth: 800, innerHeight: 600 };
	assertEqual(droppedOutside(dragEnd("none", 900, 40), page), { x: 1900, y: 50 }, "past the right edge");
	assertEqual(droppedOutside(dragEnd("none", 400, -5), page), { x: 1400, y: 5 }, "above");
	assertEqual(droppedOutside(dragEnd("none", 400, 40), page), null, "still over this window");
	assertEqual(droppedOutside(dragEnd("move", 900, 40), page), null, "another window took it");
});
