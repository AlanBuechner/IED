// boardtext.test.js - board text at every zoom (docs/32 "Readable boards"): a label grows toward a readable size on the
// screen as far as its box has room, and far out the frames are named on the screen above them.

import { test, assertEqual } from "./harness.js";
import { LABEL_PX, OVERVIEW_ZOOM, READABLE_PX, fittedSize, frameLabels, noteTint } from "/plugins/ied.board/ui/render.js";

test("board notes: a note's color tints it when it has no fill", () => {
	assertEqual(noteTint({ type: "note" }), "yellow", "plain: yellow");
	assertEqual(noteTint({ type: "note", color: "red" }), "red", "its color");
	assertEqual(noteTint({ type: "note", color: "red", fill: "blue" }), "blue", "a fill wins; the color stays its outline");
	assertEqual(noteTint({ type: "note", color: "ink" }), "yellow", "ink tints nothing");
});

const palette = { font: "sans-serif" };

test("board text: a label grows toward a readable size while its box has room", () => {
	const ctx = document.createElement("canvas").getContext("2d");
	assertEqual(fittedSize(ctx, palette, "API", 16, 1, 100, 40), 16, "at 100% its own size");
	const roomy = fittedSize(ctx, palette, "API", 16, 0.5, 200, 100);
	assertEqual(Math.round(roomy * 0.5), READABLE_PX, "zoomed out with room: readable on the screen");
	const tight = fittedSize(ctx, palette, "Renderer", 16, 0.4, 104, 36);
	ctx.font = "600 " + tight + "px sans-serif";
	assertEqual([tight > 16, tight < READABLE_PX / 0.4, ctx.measureText("Renderer").width <= 104, tight * 1.3 <= 36], [true, true, true, true], "as large as its box allows: " + tight);
	assertEqual(fittedSize(ctx, palette, "A label much too long for its box", 16, 0.4, 60, 24), 16, "no room: its own size");
});

test("board text: far out, the frames are named on the screen above them", () => {
	const frame = (id, x, y, w, h) => ({ id, type: "frame", text: id, x, y, w, h });
	const zoom = 0.3;
	const toScreen = (x, y) => [x * zoom, y * zoom];
	const measure = (text) => text.length * 7;
	const frames = [frame("outer", 0, 0, 1000, 800), frame("inner", 40, 200, 400, 300), frame("apart", 1200, 0, 600, 400), frame("tiny", 2000, 0, 80, 60)];
	assertEqual(frameLabels(frames, OVERVIEW_ZOOM + 0.01, toScreen, measure), [], "near enough: every title in its band");
	const labels = frameLabels(frames, zoom, toScreen, measure);
	assertEqual(labels.map((l) => l.id), ["outer", "apart", "inner"], "the largest first; one too small on the screen has none");
	assertEqual([labels[0].x, labels[0].y + labels[0].h + 2, labels[0].h], [0, 0, LABEL_PX + 6], "just above its frame's top left corner, in screen pixels");
	assertEqual(frameLabels([frame("Runtime", 30, 10, 500, 600), frame("Editor", 0, 0, 400, 300)], zoom, toScreen, measure).map((l) => l.id), ["Runtime"],
		"names that would meet: the larger frame's");
	assertEqual(frameLabels([frame("", 0, 0, 1000, 800)], zoom, toScreen, measure), [], "an untitled frame has none");
});
