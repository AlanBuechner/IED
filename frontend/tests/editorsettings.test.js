// editorsettings.test.js - the editor's settings as Monaco options (white space shown unless a setting says otherwise,
// the theme's font unless one is set, no map on a phone) and what View White Space and Word Wrap set.

import { test, assertEqual } from "./harness.js";
import { optionsOf, toggledWhitespace, toggledWordWrap } from "/plugins/ied.editor/ui/editorsettings.js";

const DESK = { fontSize: 13, lineHeight: 18, phone: false };
const value = (v) => ({ value: v, source: "app" });

test("editor settings: defaults and values", () => {
	const defaults = optionsOf(null, DESK);
	assertEqual([defaults.renderWhitespace, defaults.wordWrap, defaults.lineNumbers, defaults.minimap.enabled, defaults.stickyScroll.enabled, defaults.bracketPairColorization.enabled, defaults.fontSize, defaults.lineHeight],
		["all", "off", "on", true, true, false, 13, 18], "white space on before the settings arrive; the theme's font");
	const set = optionsOf({ "editor.renderWhitespace": value("trailing"), "editor.wordWrap": value("on"), "editor.lineNumbers": value("relative"), "editor.minimap": value(false), "editor.fontSize": value(26) }, DESK);
	assertEqual([set.renderWhitespace, set.wordWrap, set.lineNumbers, set.minimap.enabled, set.fontSize, set.lineHeight], ["trailing", "on", "relative", false, 26, 36], "the line height keeps the theme's proportion");
	assertEqual(optionsOf({ "editor.renderWhitespace": value("sometimes") }, DESK).renderWhitespace, "all", "a value the schema refuses");
	assertEqual(optionsOf(null, { fontSize: 14, lineHeight: 21, phone: true }).minimap.enabled, false, "no map on a phone");
});

test("editor settings: the toggles", () => {
	assertEqual(["all", "trailing", "none"].map(toggledWhitespace), ["none", "none", "all"]);
	assertEqual(["off", "on"].map(toggledWordWrap), ["on", "off"]);
});
