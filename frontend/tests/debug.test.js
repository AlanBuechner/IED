// debug.test.js - which session the debugger follows, a frame's file and place, and the breakpoint margin's decorations
// (bound breakpoints on the line the adapter moved them to, unverified and disabled ones, the current statement).

import { test, assertEqual } from "./harness.js";
import { currentSession, framePath, isLive } from "/plugins/ied.editor/ui/debug.js";
import { marginDecorations } from "/plugins/ied.editor/ui/breakpointmargin.js";
import { frameWhere } from "/plugins/ied.editor/ui/debugviews.js";

test("debug: the session followed is the newest live one, else the newest", () => {
	const ended = { id: "d1", state: "ended" };
	const stopped = { id: "d2", state: "stopped" };
	const failed = { id: "d3", state: "failed" };
	assertEqual(currentSession([ended, stopped, failed]).id, "d2");
	assertEqual(currentSession([ended, failed]).id, "d3");
	assertEqual(currentSession([]), null);
	assertEqual([isLive(stopped), isLive(ended), isLive(null)], [true, false, false]);
});

test("debug: a frame's file and place", () => {
	const inside = { name: "main", line: 6, source: { name: "main.cf", path: "src/main.cf" } };
	const outside = { name: "Push", line: 41, source: { name: "array.ch", uri: "file:///C:/toolchain/Bedrock/Container/array.ch" } };
	const nowhere = { name: "__scrt_common_main_seh", line: 287, source: { name: "exe_common.inl" } };
	assertEqual([framePath(inside), framePath(outside), framePath(nowhere), framePath({ name: "x" })], ["src/main.cf", "file:///C:/toolchain/Bedrock/Container/array.ch", null, null]);
	assertEqual([frameWhere(inside), frameWhere(outside), frameWhere(nowhere)], ["main.cf, line 7", "array.ch, line 42", "exe_common.inl"]);
});

test("debug: the margin's decorations", () => {
	const breakpoints = [{ line: 2, enabled: true }, { line: 9, enabled: true }, { line: 12, enabled: false }];
	const bound = [{ line: 2, verified: true, boundLine: 3 }, { line: 9, verified: false, boundLine: 9, message: "no code on or after this line" }];
	const frame = { line: 3, source: { path: "src/main.cf" } };
	const top = marginDecorations("src/main.cf", breakpoints, bound, frame, true);
	assertEqual(top.map((d) => [d.line, d.glyph]), [[3, "bp-glyph bp-set"], [9, "bp-glyph bp-unverified"], [12, "bp-glyph bp-disabled"], [3, "dbg-arrow"]]);
	assertEqual(top[1].hover, "no code on or after this line");
	assertEqual(top[3].whole, "dbg-current-line");
	const caller = marginDecorations("src/main.cf", [], [], frame, false);
	assertEqual(caller.map((d) => [d.glyph, d.whole]), [["dbg-arrow dbg-caller", "dbg-caller-line"]]);
	assertEqual(marginDecorations("src/other.cf", [], [], frame, true), []);
});
