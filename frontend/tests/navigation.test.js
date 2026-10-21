// navigation.test.js - the editor plugin's Navigate Backward / Forward history: jumps add places, small moves update the
// current one, going back and forward walks without adding, and a new jump after going back drops the forward places.

import { test, assertEqual, assertTrue } from "./harness.js";
import { Navigation } from "/plugins/ied.editor/ui/navigation.js";

const at = (path, line) => ({ path, line, col: 0 });

test("navigation: jumps are places, small moves are not", () => {
	const nav = new Navigation();
	nav.visit(at("a.c", 1));
	nav.visit(at("a.c", 5));
	assertEqual(nav.places.length, 1, "five lines away is the same place");
	assertEqual(nav.places[0].line, 5, "which moved with the cursor");
	nav.visit(at("a.c", 40));
	nav.visit(at("b.c", 40));
	assertEqual(nav.places.map((p) => p.path + ":" + p.line), ["a.c:5", "a.c:40", "b.c:40"]);
	assertTrue(!nav.canGoForward(), "nothing ahead");
});

test("navigation: back, forward, and a new jump drops what was ahead", () => {
	const nav = new Navigation();
	for (const place of [at("a.c", 0), at("b.c", 0), at("c.c", 0)]) nav.visit(place);
	assertEqual(nav.back().path, "b.c");
	assertEqual(nav.back().path, "a.c");
	assertEqual(nav.back(), null, "nothing before the first");
	assertEqual(nav.forward().path, "b.c");
	nav.visit(at("b.c", 3));
	assertEqual(nav.places.length, 3, "arriving at a place does not add it");
	nav.visit(at("d.c", 0));
	assertEqual(nav.places.map((p) => p.path), ["a.c", "b.c", "d.c"], "c.c was ahead and is gone");
	assertEqual(nav.forward(), null);
});
