// editorsettings.test.js - the editor's settings as Monaco options (white space shown unless the setting says otherwise)
// and what View White Space sets.

import { test, assertEqual } from "./harness.js";
import { optionsOf, toggledWhitespace } from "/plugins/ied.editor/ui/editorsettings.js";

test("editor settings: white space", () => {
	assertEqual(optionsOf(null), { renderWhitespace: "all" }, "on before the settings arrive");
	assertEqual(optionsOf({ "editor.renderWhitespace": { value: "trailing", source: "app" } }), { renderWhitespace: "trailing" });
	assertEqual(optionsOf({ "editor.renderWhitespace": { value: "sometimes", source: "app" } }), { renderWhitespace: "all" }, "a value the schema refuses");
	assertEqual(["all", "trailing", "none"].map(toggledWhitespace), ["none", "none", "all"]);
});
