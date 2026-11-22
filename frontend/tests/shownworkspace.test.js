// shownworkspace.test.js - which workspace a window shows when it starts or its server comes back (docs/15 "The
// address"): an id that names another folder now is never taken for the window's own.

import { test, assertEqual, assertTrue } from "./harness.js";
import { namesAnotherFolder, preferredWorkspaceOf, sameRoot } from "../src/shell/shownworkspace.js";

const GAME = { wsId: "w1", root: "C:\\Work\\Game" };
const ENGINE = { wsId: "w2", root: "C:\\Work\\Engine" };

test("shown workspace: one folder in two spellings is one root", () => {
	assertTrue(sameRoot("C:\\Work\\Game", "c:/work/game/"));
	assertTrue(!sameRoot("C:/Work/Game", "C:/Work/Games"));
	assertTrue(!sameRoot("", ""), "no root is no folder");
	assertTrue(sameRoot("\\\\nas\\Share\\Game", "//NAS/share/game"), "a share's names ignore case too");
});

test("shown workspace: a Linux root keeps its case", () => {
	assertTrue(sameRoot("/home/a/Game", "/home/a/Game/"), "a trailing slash does not matter");
	assertTrue(!sameRoot("/home/a/Game", "/home/a/game"), "two folders on Linux");
	const twins = [{ wsId: "w1", root: "/home/a/game" }, { wsId: "w2", root: "/home/a/Game" }];
	assertEqual(preferredWorkspaceOf({ workspaces: twins, asked: "w1", root: "/home/a/Game" }), "w2", "the tab's own folder, not its case twin");
	assertTrue(namesAnotherFolder("/home/a/Game", "/home/a/game"));
});

test("shown workspace: the address's id while it is still the tab's folder", () => {
	const workspaces = [GAME, ENGINE];
	assertEqual(preferredWorkspaceOf({ workspaces, asked: "w2", remembered: "w1", root: "c:/work/engine" }), "w2");
	assertEqual(preferredWorkspaceOf({ workspaces, asked: "w2", remembered: "w1", root: null }), "w2", "a tab that knows no folder yet takes the id");
	assertEqual(preferredWorkspaceOf({ workspaces, asked: null, remembered: "w1", root: null }), "w1");
	assertEqual(preferredWorkspaceOf({ workspaces, launched: "w1", asked: "w2", root: "C:/Work/Engine" }), "w1", "a launch's #open= wins");
});

test("shown workspace: an id another folder has now is passed over for the tab's folder", () => {
	// A master that came back with the ids the other way round: w1 is the engine now.
	const swapped = [{ wsId: "w1", root: "C:\\Work\\Engine" }, { wsId: "w2", root: "C:\\Work\\Game" }];
	assertEqual(preferredWorkspaceOf({ workspaces: swapped, asked: "w1", remembered: "w1", root: "C:\\Work\\Game" }), "w2", "the game, under its new id");
	assertEqual(preferredWorkspaceOf({ workspaces: [{ wsId: "w1", root: "C:\\Work\\Engine" }], asked: "w1", root: "C:\\Work\\Game" }), "w1", "its folder not open: the first, as before");
	assertEqual(preferredWorkspaceOf({ workspaces: [], asked: "w1", root: "C:\\Work\\Game" }), null);
});

test("shown workspace: a snapshot of another folder under the shown id is noticed", () => {
	assertTrue(namesAnotherFolder("C:\\Work\\Game", "C:\\Work\\Engine"));
	assertTrue(!namesAnotherFolder("C:\\Work\\Game", "c:/work/game"));
	assertTrue(!namesAnotherFolder(null, "C:\\Work\\Engine"), "a window that knows no folder yet takes the snapshot's");
});
