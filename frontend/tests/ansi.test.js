// ansi.test.js - escape sequences in streamed text: colors, weights, resets, other sequences dropped, split chunks.

import { test, assertEqual } from "./harness.js";
import { AnsiReader, stripAnsi } from "/ui-kit/ansi.js";

function runs(...chunks) {
	const reader = new AnsiReader();
	return chunks.flatMap((chunk) => reader.read(chunk)).map((run) => [run.text, run.cls]);
}

test("ansi: plain text is one plain run", () => {
	assertEqual(runs("hello\nworld"), [["hello\nworld", ""]]);
});

test("ansi: colors, bright colors, weights and resets", () => {
	assertEqual(runs("\x1b[31merror\x1b[0m ok"), [["error", "a-red"], [" ok", ""]]);
	assertEqual(runs("\x1b[1;92mPASS\x1b[22m done\x1b[39m."), [["PASS", "a-bgreen a-bold"], [" done", "a-bgreen"], [".", ""]]);
	assertEqual(runs("\x1b[mx"), [["x", ""]], "ESC [ m resets");
	assertEqual(runs("\x1b[44;33mx\x1b[49my"), [["x", "a-yellow ab-blue"], ["y", "a-yellow"]]);
	assertEqual(runs("\x1b[3;4mx\x1b[23;24my"), [["x", "a-italic a-underline"], ["y", ""]]);
});

test("ansi: 256 colors within the theme's 16 are kept, others and truecolor are plain", () => {
	assertEqual(runs("\x1b[38;5;1ma\x1b[38;5;9mb\x1b[38;5;200mc"), [["a", "a-red"], ["b", "a-bred"], ["c", ""]]);
	assertEqual(runs("\x1b[38;2;10;20;30mx\x1b[1my"), [["x", ""], ["y", "a-bold"]], "the r;g;b is consumed");
});

test("ansi: other sequences are dropped", () => {
	assertEqual(runs("a\x1b[2Kb\x1b[1;1Hc"), [["abc", ""]], "erase and cursor moves");
	assertEqual(runs("\x1b]0;title\x07x"), [["x", ""]], "an OSC ended by BEL");
	assertEqual(runs("\x1b]8;;http://x\x1b\\link"), [["link", ""]], "an OSC ended by ST");
});

test("ansi: a sequence split across chunks", () => {
	assertEqual(runs("a\x1b", "[3", "1mb"), [["a", ""], ["b", "a-red"]]);
	assertEqual(runs("\x1b]0;ti", "tle\x07x"), [["x", ""]]);
	assertEqual(stripAnsi("\x1b[1m\x1b[31mbold red\x1b[0m"), "bold red");
});
