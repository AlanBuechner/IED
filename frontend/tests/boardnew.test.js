// boardnew.test.js - New Board's name and folder (docs/32 "The board in the UI"): the folder typed as a workspace path,
// what each field refuses, the first free name, the folder a group's boards share and the folders suggested.

import { test, assertEqual } from "./harness.js";
import { boardFile, cleanFolder, cleanName, commonFolder, folderProblem, folderSuggestions, freeName, nameProblem } from "/plugins/ied.board/ui/newboard.js";

test("new board: the folder as a workspace path", () => {
	assertEqual(cleanFolder(" design\\flows/ "), "design/flows", "backslashes, spaces and a trailing slash");
	assertEqual(cleanFolder("./design//sub"), "design/sub", "a leading ./ and doubled slashes");
	assertEqual(cleanFolder(""), "", "the workspace's own folder");
	assertEqual(boardFile("design", "checkout"), "design/checkout.iedboard");
	assertEqual(boardFile("", "checkout"), "checkout.iedboard", "at the workspace's root");
});

test("new board: what the folder refuses", () => {
	assertEqual(folderProblem("design/flows"), "");
	assertEqual(folderProblem(""), "", "the workspace's folder");
	assertEqual(folderProblem("C:/elsewhere"), "The folder is inside the workspace: a path like design/checkout");
	assertEqual(folderProblem("/abs"), "The folder is inside the workspace: a path like design/checkout");
	assertEqual(folderProblem("design/../up"), "The folder is inside the workspace: no '.' or '..'");
	assertEqual(folderProblem("a|b"), "A folder's name has none of < > : \" | ? *");
	assertEqual(folderProblem("build/boards"), "The Boards list does not look in build/: choose another folder", "a folder the list skips");
	assertEqual(folderProblem("docs/.hidden"), "The Boards list does not look in .hidden/: choose another folder");
});

test("new board: what the name refuses", () => {
	assertEqual(cleanName(" checkout.IEDBOARD "), "checkout", "the extension typed is dropped");
	assertEqual(nameProblem("checkout flow"), "", "spaces are fine");
	assertEqual(nameProblem(""), "Name the board");
	assertEqual(nameProblem("design/checkout"), "The name is the file's alone: choose its folder below");
	assertEqual(nameProblem("a?b"), "A board's name has none of < > : \" | ? *");
	assertEqual(nameProblem("draft."), "A board's name does not end in '.' or a space");
	assertEqual(nameProblem("x".repeat(121)), "A board's name is at most 120 characters");
});

test("new board: the first free name in the folder", () => {
	const boards = [{ path: "design/board.iedboard" }, { path: "design/Board2.iedboard" }, { path: "notes/board.iedboard" }];
	assertEqual(freeName(boards, "design"), "board3", "taken in any case");
	assertEqual(freeName(boards, "flows"), "board", "another folder");
	assertEqual(freeName([], ""), "board");
});

test("new board: the folder a group's boards share", () => {
	assertEqual(commonFolder(["a/x.iedboard", "b/y.iedboard", "b/z.iedboard"]), "b");
	assertEqual(commonFolder(["a/x.iedboard", "b/y.iedboard"]), "a", "a tie: the first");
	assertEqual(commonFolder(["x.iedboard"]), "", "the workspace's root");
	assertEqual(commonFolder([]), null, "no boards");
});

test("new board: the folders suggested", () => {
	const known = ["design", "design/flows", "docs", "notes/design", "build", "", ".ied"];
	assertEqual(folderSuggestions("", known), ["design", "design/flows", "docs", "notes/design"], "everything known, minus the root and the folders the list skips");
	assertEqual(folderSuggestions("de", known), ["design", "design/flows", "notes/design"], "those beginning with it, then those holding it");
	assertEqual(folderSuggestions("DESIGN", known), ["design", "design/flows", "notes/design"], "any case");
	assertEqual(folderSuggestions("design\\", known), ["design/flows"], "a trailing slash: only the folders under it");
	assertEqual(folderSuggestions("zzz", known), []);
});
