// outputlinks.test.js - the places Output lines name (MSVC, C4, clang/gcc/rustc, rustc's arrow, test failures), lines
// that name none, and the workspace file a printed path means.

import { test, assertEqual } from "./harness.js";
import { findLocation, pickFile, workspaceRelative } from "/plugins/ied.editor/ui/outputlinks.js";

function place(text) {
	const found = findLocation(text);
	return found ? [found.path, found.line, found.col, text.slice(found.start, found.end)] : null;
}

test("output links: the shapes compilers print", () => {
	assertEqual(place("src/engine/render.c(9,8): warning W1001: unused variable 'scale'"), ["src/engine/render.c", 9, 8, "src/engine/render.c(9,8)"]);
	assertEqual(place("game/logic.cf (3:26) error: no variable named missing"), ["game/logic.cf", 3, 26, "game/logic.cf (3:26)"]);
	assertEqual(place("src/main.rs:8:24: error[E0308]: mismatched types"), ["src/main.rs", 8, 24, "src/main.rs:8:24"]);
	assertEqual(place("  --> src/main.rs:2:13"), ["src/main.rs", 2, 13, "src/main.rs:2:13"]);
	assertEqual(place("FAIL: tests/game_tests.cf:7: two twice (expected 5, got 4)"), ["tests/game_tests.cf", 7, 0, "tests/game_tests.cf:7"]);
	assertEqual(place("C:\\Work\\Game\\src\\main.c(12): error C2065"), ["C:\\Work\\Game\\src\\main.c", 12, 0, "C:\\Work\\Game\\src\\main.c(12)"]);
});

test("output links: lines that name no place", () => {
	for (const text of ["Build succeeded: 0 errors (2.31 s)", "https://example.com:443/path", "[12:30:45] opened w1", "frame 600  player.position = (10.00, 0.00)", ""]) assertEqual(place(text), null);
});

test("output links: the workspace file a path means", () => {
	assertEqual(workspaceRelative("src/main.c", "C:/Work/Game"), "src/main.c");
	assertEqual(workspaceRelative("C:\\Work\\Game\\src\\main.c", "c:/work/game"), "src/main.c");
	assertEqual(workspaceRelative("D:/Other/x.c", "C:/Work/Game"), null);
	const files = ["src/game/logic.cf", "other/src/game/logic.cf", "src/main.cf"];
	assertEqual([pickFile("src/main.cf", files), pickFile("game/logic.cf", files), pickFile("nope.cf", files)], ["src/main.cf", "src/game/logic.cf", null]);
});
