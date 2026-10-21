// textedits.test.js - the document protocol's edits: inserts, deletes, replacements, several changes, UTF-16 columns,
// line ends, and refused edits.

import { test, assertEqual, assertThrows } from "./harness.js";
import { applyChanges, lineStartsOf, EditError } from "/ui-kit/textedits.js";

function change(line, col, endLine, endCol, text) {
	return { range: { start: { line, col }, end: { line: endLine, col: endCol } }, text };
}

test("textedits: line starts", () => {
	assertEqual(lineStartsOf("ab\ncd\n"), [0, 3, 6]);
	assertEqual(lineStartsOf(""), [0]);
});

test("textedits: insert, delete, replace", () => {
	assertEqual(applyChanges("hello\nworld\n", [change(0, 5, 0, 5, "!")]), "hello!\nworld\n");
	assertEqual(applyChanges("hello\nworld\n", [change(0, 0, 1, 0, "")]), "world\n", "a whole line");
	assertEqual(applyChanges("hello\nworld\n", [change(1, 0, 1, 5, "there")]), "hello\nthere\n");
	assertEqual(applyChanges("", [change(0, 0, 0, 0, "a\nb")]), "a\nb", "into an empty document");
	assertEqual(applyChanges("abc", [change(0, 3, 0, 3, "\n")]), "abc\n", "at the very end");
});

test("textedits: several changes refer to the text before the edit", () => {
	assertEqual(applyChanges("one two three", [change(0, 8, 0, 13, "3"), change(0, 0, 0, 3, "1")]), "1 two 3");
	assertEqual(applyChanges("a\nb\nc", [change(2, 0, 2, 1, "C"), change(0, 0, 0, 1, "A")]), "A\nb\nC");
});

test("textedits: columns count UTF-16 code units", () => {
	assertEqual(applyChanges("a\u{1F3AE}b", [change(0, 3, 0, 4, "B")]), "a\u{1F3AE}B", "the emoji is two units");
	assertEqual(applyChanges("x\r\ny", [change(0, 1, 0, 2, "")]), "x\ny", "a \\r is part of its line");
});

test("textedits: refused edits", () => {
	assertThrows(() => applyChanges("ab", [change(0, 3, 0, 3, "x")]), EditError, "past the end of the line");
	assertThrows(() => applyChanges("ab", [change(1, 0, 1, 0, "x")]), EditError, "a line that is not there");
	assertThrows(() => applyChanges("abc", [change(0, 2, 0, 1, "x")]), EditError, "backwards");
	assertThrows(() => applyChanges("abcdef", [change(0, 0, 0, 3, "x"), change(0, 2, 0, 4, "y")]), EditError, "overlapping");
	assertThrows(() => applyChanges("ab\ncd", [change(0, 3, 0, 3, "x")]), EditError, "the newline is not a column");
});
