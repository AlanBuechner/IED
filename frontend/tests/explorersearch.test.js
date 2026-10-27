// explorersearch.test.js - Solution Explorer's search: which files, folders, projects and groups a query keeps and opens,
// and the parts of a name it marks.

import { test, assertEqual, assertTrue } from "./harness.js";
import { SearchState, holds, markedParts, searchPaths } from "/plugins/ied.editor/ui/explorersearch.js";

const FILES = ["src/engine/core.c", "src/engine/core.h", "src/game/game.c", "src/main.c", "docs/notes.md", "README.md", "tools/coremark/run.py"];

test("explorer search: names that hold the query, and the folders above them", () => {
	const found = searchPaths(FILES, "core");
	assertEqual([...found.matched].sort(), ["src/engine/core.c", "src/engine/core.h", "tools/coremark"], "files and a folder by their own names, any case");
	assertEqual([...found.above].sort(), ["", "src", "src/engine", "tools"], "the folders that lead to them, the workspace's own first");
	assertTrue(!found.matched.has("tools/coremark/run.py"), "a file in a folder that matches does not match by that");
	assertEqual([...searchPaths(FILES, "GAME").matched].sort(), ["src/game", "src/game/game.c"], "a folder and a file of that name");
	assertEqual([...searchPaths(FILES, "engine/core.h").matched], ["src/engine/core.h"], "a query with a '/' is looked for in the whole path");
	assertEqual(searchPaths(FILES, "  ").matched.size, 0, "a blank query keeps nothing");
	assertEqual([holds("Core.C", "x", "core"), holds("main.c", "x", "core")], [true, false]);
});

test("explorer search: projects and groups of a solution", () => {
	const solution = {
		projects: [{ name: "Engine" }, { name: "Game" }, { name: "CoreTools" }],
		groups: [{ name: "Runtime", projects: ["Engine", "Game"], groups: [{ name: "Libs", projects: [] }] }, { name: "Tooling", projects: ["CoreTools"] }],
		items: [],
	};
	const paths = { Engine: "src/engine", Game: "src/game", CoreTools: null };
	const pathOf = (name) => paths[name];
	const state = new SearchState("core", FILES, solution, false);
	assertEqual([...state.projects], ["CoreTools"], "a project by its name");
	assertTrue(state.keepsProject("Engine", "src/engine") && state.opensProject("src/engine"), "a project whose folder holds a match stays, opened");
	assertTrue(!state.keepsProject("Game", "src/game"), "one that holds none goes");
	assertTrue(state.keepsProject("CoreTools", null) && !state.opensProject(null), "a project outside the folder, by its name only");
	assertTrue(state.keepsGroup(solution.groups[0], pathOf) && state.opensGroup(solution.groups[0], pathOf), "a group holding a kept project stays, opened");
	assertTrue(!state.keepsGroup(solution.groups[0].groups[0], pathOf), "an empty group that does not match goes");
	const groups = new SearchState("libs", FILES, solution, false);
	assertTrue(groups.keepsGroup(solution.groups[0].groups[0], pathOf) && !groups.opensGroup(solution.groups[0].groups[0], pathOf), "a group by its name, not opened");
	assertEqual([state.count, groups.count], [4, 1], "what matched: files, folders, projects and groups");
});

test("explorer search: the parts of a name that hold the query are marked", () => {
	assertEqual(markedParts("CoreCore.c", "core"), [{ mark: "Core" }, { mark: "Core" }, ".c"]);
	assertEqual(markedParts("my_core.h", "CORE"), ["my_", { mark: "core" }, ".h"]);
	assertEqual(markedParts("main.c", "core"), ["main.c"], "nothing to mark");
	assertEqual(markedParts("core.h", "engine/core"), ["core.h"], "a path query marks no part of a name");
});
