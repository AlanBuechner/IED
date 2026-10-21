// headersource.test.js - Toggle Header / Code File's candidates: the paired extensions beside the file, then in the
// pair's search paths, never outside the workspace; a file in no pair has none.

import { test, assertEqual } from "./harness.js";
import { counterparts, joinFolder } from "/plugins/ied.editor/ui/headersource.js";

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
