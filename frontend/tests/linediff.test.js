// linediff.test.js - the track-changes margin's line diff: inserted and changed lines are marked in the newer text, a
// deletion marks the line after it, unchanged texts mark nothing, and a long one stays exact.

import { test, assertEqual } from "./harness.js";
import { changedLines } from "/plugins/ied.editor/ui/linediff.js";

test("linediff: inserts, changes and deletions", () => {
	assertEqual(changedLines("a\nb\nc\n", "a\nb\nc\n"), []);
	assertEqual(changedLines("a\nb\nc\n", "a\nB\nc\n"), [2], "a changed line");
	assertEqual(changedLines("a\nb\nc\n", "a\nx\ny\nb\nc\n"), [2, 3], "two inserted lines");
	assertEqual(changedLines("a\nb\nc\nd\n", "a\nd\n"), [2], "deleted lines mark the one after them");
	assertEqual(changedLines("a\nb\n", "a\n"), [2], "a deletion at the end marks the last line");
	assertEqual(changedLines("", "x\n"), [1]);
	assertEqual(changedLines("a\nb\nc\nd\ne\n", "a\nX\nc\nY\ne\n"), [2, 4], "two separate changes");
	assertEqual(changedLines("a\nb\nc\n", "c\na\nb\n"), [1, 4], "a line moved up: inserted above, deleted below");
});

test("linediff: a long text with a few edits", () => {
	const lines = [];
	for (let i = 0; i < 20000; i++) lines.push("line " + i);
	const before = lines.join("\n");
	const after = lines.slice();
	after[100] = "changed";
	after.splice(15000, 0, "new");
	const started = performance.now();
	assertEqual(changedLines(before, after.join("\n")), [101, 15001]);
	assertEqual(performance.now() - started < 200, true, "fast");
});
