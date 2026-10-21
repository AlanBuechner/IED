// outputlinks.test.js - the places Output lines name through the shapes language plugins contribute (`outputLinks`:
// MSVC, C4, clang/gcc/rustc, a Python traceback), lines that name none, shapes that do not compile, and the workspace
// file a printed path means.

import { test, assertEqual } from "./harness.js";
import { compileShapes, findLocation, pickFile, workspaceRelative } from "/plugins/ied.editor/ui/outputlinks.js";

const PATH = "((?:[A-Za-z]:)?[^\\s()<>\"'|*?]+?\\.[A-Za-z0-9_+-]+)";
const GNU_PATH = "((?:[A-Za-z]:)?[^\\s():<>\"'|*?]+?\\.[A-Za-z0-9_+-]+)";
const CPP = { id: "ied.cpp" };
const C4 = { id: "c4" };
const PYTHON = { id: "ied.python" };
const SHIPPED = [
	{ plugin: CPP, value: { id: "msvc", regexp: "(?:^|\\s)" + PATH + "\\((\\d+)(?:,(\\d+))?\\)", file: 1, line: 2, column: 3 } },
	{ plugin: CPP, value: { id: "gnu", regexp: "(?:^|\\s)" + GNU_PATH + ":(\\d+)(?::(\\d+))?", file: 1, line: 2, column: 3 } },
	{ plugin: C4, value: { id: "c4", regexp: "(?:^|\\s)" + PATH + " \\((\\d+):(\\d+)\\)", file: 1, line: 2, column: 3 } },
	{ plugin: C4, value: { id: "c4-test", regexp: "(?:^|\\s)" + GNU_PATH + ":(\\d+)(?::(\\d+))?", file: 1, line: 2, column: 3 } },
	{ plugin: PYTHON, value: { id: "traceback", regexp: "File \"([^\"]+)\", line (\\d+)", file: 1, line: 2 } },
];
const SHAPES = compileShapes(SHIPPED);

function place(text, shapes) {
	const found = findLocation(text, shapes || SHAPES);
	return found ? [found.path, found.line, found.col, text.slice(found.start, found.end)] : null;
}

test("output links: the shapes the language plugins contribute", () => {
	assertEqual(SHAPES.length, 4, "one shape per distinct expression");
	assertEqual(place("src/engine/render.c(9,8): warning W1001: unused variable 'scale'"), ["src/engine/render.c", 9, 8, "src/engine/render.c(9,8)"]);
	assertEqual(place("game/logic.cf (3:26) error: no variable named missing"), ["game/logic.cf", 3, 26, "game/logic.cf (3:26)"]);
	assertEqual(place("src/main.rs:8:24: error[E0308]: mismatched types"), ["src/main.rs", 8, 24, "src/main.rs:8:24"]);
	assertEqual(place("  --> src/main.rs:2:13"), ["src/main.rs", 2, 13, "src/main.rs:2:13"]);
	assertEqual(place("FAIL: tests/game_tests.cf:7: two twice (expected 5, got 4)"), ["tests/game_tests.cf", 7, 0, "tests/game_tests.cf:7"]);
	assertEqual(place("C:\\Work\\Game\\src\\main.c(12): error C2065"), ["C:\\Work\\Game\\src\\main.c", 12, 0, "C:\\Work\\Game\\src\\main.c(12)"]);
	assertEqual(place("  File \"tools/build.py\", line 41, in main"), ["tools/build.py", 41, 0, "tools/build.py\", line 41"]);
});

test("output links: lines that name no place, and no plugin to say", () => {
	for (const text of ["Build succeeded: 0 errors (2.31 s)", "https://example.com:443/path", "[12:30:45] opened w1", "frame 600  player.position = (10.00, 0.00)", ""]) assertEqual(place(text), null);
	assertEqual(place("src/main.c(12): error", []), null, "without language plugins nothing is a link");
});

test("output links: a shape that does not compile is reported and left out", () => {
	const said = [];
	const shapes = compileShapes([{ plugin: { id: "bad" }, value: { id: "broken", regexp: "(unclosed", file: 1, line: 2 } }, ...SHIPPED], (message) => said.push(message));
	assertEqual([shapes.length, said.length, said[0].includes("'broken' of bad")], [4, 1, true]);
});

test("output links: the workspace file a path means", () => {
	assertEqual(workspaceRelative("src/main.c", "C:/Work/Game"), "src/main.c");
	assertEqual(workspaceRelative("C:\\Work\\Game\\src\\main.c", "c:/work/game"), "src/main.c");
	assertEqual(workspaceRelative("D:/Other/x.c", "C:/Work/Game"), null);
	const files = ["src/game/logic.cf", "other/src/game/logic.cf", "src/main.cf"];
	assertEqual([pickFile("src/main.cf", files), pickFile("game/logic.cf", files), pickFile("nope.cf", files)], ["src/main.cf", "src/game/logic.cf", null]);
});
