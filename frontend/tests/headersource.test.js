// headersource.test.js - Toggle Header / Code File's candidates: the paired extensions beside the file, then in the
// pair's search paths, never outside the workspace; a file in no pair has none. A counterpart tethered beside its file
// follows the file either group shows, for the pairs that say `follow`.

import { test, assertEqual } from "./harness.js";
import { counterparts, joinFolder } from "/plugins/ied.editor/ui/headersource.js";
import { follows, moves } from "/plugins/ied.editor/ui/counterpartfollow.js";

const C = { extensions: [".h", ".c", ".cpp"], searchPaths: ["../include"] };
const C4 = { extensions: [".cf", ".ch"] };

test("headersource: candidates beside the file, then in the search paths", () => {
	assertEqual(counterparts("src/game/player.c", [C, C4]), ["src/game/player.h", "src/game/player.cpp", "src/include/player.h", "src/include/player.cpp"]);
	assertEqual(counterparts("lsp/pool.CH", [C, C4]), ["lsp/pool.cf"], "extensions in any case");
	assertEqual(counterparts("README.md", [C, C4]), [], "no pair");
	assertEqual(counterparts("Makefile", [C]), [], "no extension");
	assertEqual(counterparts("main.c", [C]), ["main.h", "main.cpp"], "at the top, the search path would leave the workspace");
});

test("headersource: folders", () => {
	assertEqual(joinFolder("src/game", "../include"), "src/include");
	assertEqual(joinFolder("src", "./x/../y"), "src/y");
	assertEqual(joinFolder("", ".."), null);
	assertEqual(joinFolder("", "include"), "include");
});

test("headersource: a counterpart beside its file follows the file its partner's group shows", () => {
	const pairs = [Object.assign({ follow: true }, C), C4];
	const state = (left, right) => ({
		open: ["doc:a.c", "doc:b.c", "doc:notes.md", "doc:x.cf", "doc:a.h", "doc:z.h"],
		groups: [{ activity: "ied.editor", well: true, tabs: ["doc:a.c", "doc:b.c", "doc:notes.md", "doc:x.cf"], active: left }, { activity: "ied.editor", well: true, tabs: ["doc:a.h", "doc:z.h"], active: right || "doc:a.h" }],
	});
	const tethered = (ref) => ({ "doc:a.c": ["doc:a.h"], "doc:a.h": ["doc:a.c"] })[ref] || [];
	assertEqual(moves(state("doc:b.c"), tethered, pairs), [{ side: "doc:a.c", partner: "doc:a.h", next: "doc:b.c" }], "another code file: its header takes the partner's place");
	assertEqual(moves(state("doc:a.c", "doc:z.h"), tethered, pairs), [{ side: "doc:a.h", partner: "doc:a.c", next: "doc:z.h" }], "either way");
	assertEqual(moves(state("doc:a.c"), tethered, pairs), [], "the pair shown");
	assertEqual(moves(state("doc:notes.md"), tethered, pairs), [], "a file of no pair: it stays");
	assertEqual(moves(state("doc:x.cf"), tethered, pairs), [], "a pair that does not follow");
	assertEqual(moves(state("doc:b.c"), () => [], pairs), [], "not tethered");
	assertEqual(moves(state("doc:b.c"), (ref) => (ref === "doc:b.c" ? ["doc:b.h"] : tethered(ref)), pairs), [], "the file shown has a partner of its own");
	assertEqual([follows("doc:src/A.CPP", pairs), follows("doc:x.cf", pairs), follows("mdpreview:a.c", pairs)], [true, false, false]);
});
