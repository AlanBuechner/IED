// diagnostics.test.js - a problem a build's matcher and a language server both report is shown once, with the wider
// range; different problems at one place stay apart.

import { test, assertEqual } from "./harness.js";
import { distinctItems } from "/plugins/ied.editor/ui/diagnostics.js";

function item(line, character, endCharacter, message, severity = 1) {
	return { severity, message, range: { start: { line, character }, end: { line, character: endCharacter } } };
}

test("diagnostics: one problem from two sources is shown once", () => {
	const sources = new Map([
		["c4/Game", [item(6, 14, 15, "narrowing", 2), item(3, 1, 2, "no variable named missing")]],
		["c4-language-server/Game", [item(6, 14, 22, "narrowing", 2), item(9, 0, 4, "only here")]],
	]);
	const shown = distinctItems(sources);
	assertEqual(shown.get("c4/Game").map((i) => i.message), ["no variable named missing"]);
	assertEqual(shown.get("c4-language-server/Game").map((i) => [i.message, i.range.end.character]), [["narrowing", 22], ["only here", 4]]);
});

test("diagnostics: different problems at one place stay apart", () => {
	const shown = distinctItems(new Map([["a", [item(1, 1, 2, "one"), item(1, 1, 2, "two"), item(1, 1, 2, "one", 2)]]]));
	assertEqual(shown.get("a").length, 3);
	assertEqual(distinctItems(new Map()).size, 0);
});
