// fileviewers.test.js - the tab a file opens in from the plugins' fileViewers (docs/02 "fileViewers"): patterns by name
// and by path, a default viewer over the text editor, Open With's options, a later plugin's default, a viewer whose
// plugin's UI is not loaded.

import { test, assertEqual } from "./harness.js";
import { fileTabRef, matchesFilePattern, viewersFor } from "/ui-kit/fileviewers.js";

const BOARD = { id: "ied.board" };
const HEX = { id: "hex" };
const OTHER = { id: "other.board" };

const ENTRIES = [
	{ plugin: BOARD, value: { id: "ied.board", title: "Planning Board", patterns: ["*.iedboard"], view: "board", priority: "default" } },
	{ plugin: HEX, value: { id: "hex", title: "Hex Editor", patterns: ["**/*"], view: "hex", priority: "option" } },
];
const ALL = () => true;

test("file viewers: patterns match a name, or a path when they hold a slash", () => {
	assertEqual(matchesFilePattern("*.iedboard", "design/checkout.iedboard"), true);
	assertEqual(matchesFilePattern("*.iedboard", "design/checkout.iedboard.bak"), false);
	assertEqual(matchesFilePattern("*.IEDBOARD", "a.iedboard"), true, "without case");
	assertEqual(matchesFilePattern("design/*.iedboard", "design/a.iedboard"), true);
	assertEqual(matchesFilePattern("design/*.iedboard", "other/design/a.iedboard"), false, "a path pattern is the whole path");
	assertEqual(matchesFilePattern("**/plans/*.md", "docs/plans/x.md"), true);
	assertEqual(matchesFilePattern("**/plans/*.md", "plans/x.md"), true, "** may be no folder");
	assertEqual(matchesFilePattern("?.cf", "a.cf"), true);
	assertEqual(matchesFilePattern("a+b.txt", "a+b.txt"), true, "regular expression characters are plain");
});

test("file viewers: a default viewer opens its files, others open as text", () => {
	assertEqual(fileTabRef("design/checkout.iedboard", ENTRIES, ALL), "board:design/checkout.iedboard");
	assertEqual(fileTabRef("src/main.cf", ENTRIES, ALL), "doc:src/main.cf", "an option alone is not used by a plain open");
});

test("file viewers: Open With lists the options, then the defaults; a later plugin's default wins", () => {
	const both = ENTRIES.concat([{ plugin: OTHER, value: { id: "other", title: "Other Board", patterns: ["*.iedboard"], view: "otherboard" } }]);
	const viewers = viewersFor("a.iedboard", both, ALL);
	assertEqual(viewers.map((v) => v.id).join(","), "hex,ied.board,other");
	assertEqual(viewers[2].priority, "default", "no priority is a default");
	assertEqual(fileTabRef("a.iedboard", both, ALL), "otherboard:a.iedboard");
});

test("file viewers: a viewer whose tab kind nothing shows is left out", () => {
	assertEqual(fileTabRef("a.iedboard", ENTRIES, (kind) => kind !== "board"), "doc:a.iedboard");
	assertEqual(viewersFor("a.iedboard", [{ plugin: BOARD, value: { id: "x" } }], ALL).length, 0, "an entry without a view or patterns");
});
