// profiler.test.js - the profiler's pure parts (plugins/profiler): durations and ruler ticks, the view's zoom and pan,
// whether a fetched answer still serves a view, lanes laid out from a prof.timeline answer, which zone a point hits,
// and the frame bands over the budget.

import { test, assertEqual, assertTrue } from "./harness.js";
import { duration, ms, bytes, tickStep, tickLabel } from "/plugins/ied.profiler/ui/format.js";
import { TimeView, answerServes, fetchRange, layoutLanes, laneAt, zoneAt, seriesOf, frameBands, MIN_SPAN_NS } from "/plugins/ied.profiler/ui/model.js";

const MICRO = String.fromCharCode(0xb5) + "s";

test("profiler: durations take the unit that keeps them short", () => {
	assertEqual([duration(850), duration(12400), duration(3210000), duration(1234000000), duration(-2500000)],
		["850 ns", "12.4 " + MICRO, "3.21 ms", "1.234 s", "-2.50 ms"]);
	assertEqual([ms(1500000), ms(25000000)], ["1.50", "25.0"]);
	assertEqual([bytes(512), bytes(4096), bytes(3 * 1048576), bytes(5 * 1073741824)], ["512 B", "4.0 KiB", "3.0 MiB", "5.00 GiB"]);
});

test("profiler: ruler ticks are 1, 2 or 5 times a power of ten", () => {
	assertEqual(tickStep(1e9, 1000, 80), 1e8, "80 px of a second over 1000 px is 80 ms: the step is 100 ms");
	assertEqual(tickStep(1e6, 1000, 80), 1e5);
	assertEqual(tickStep(3e9, 1000, 80), 5e8);
	assertEqual([tickLabel(74250000000, 5e6), tickLabel(2500000, 5e5), tickLabel(1500, 500)], ["74.250 s", "2.5 ms", "1.5 " + MICRO]);
});

test("profiler: zooming keeps the anchor in place", () => {
	const view = new TimeView(0, 1000).zoomAt(250, 0.5);
	assertEqual([view.from, view.to], [125, 625]);
	assertEqual(new TimeView(0, 1000).zoomAt(500, 1e-9).span, MIN_SPAN_NS, "never narrower than the smallest span");
	assertEqual(new TimeView(0, 1000).zoomAt(500, 100, 2000).span, 2000, "never wider than the longest");
	assertEqual(new TimeView(0, 1000).panBy(300).from, 300);
});

test("profiler: a view is kept inside the capture", () => {
	assertEqual([new TimeView(-100, 900).clamp(5000).from, new TimeView(4800, 5800).clamp(5000).to], [0, 5000]);
	const all = new TimeView(0, 9000).clamp(5000);
	assertEqual([all.from, all.to], [0, 5000], "a view longer than the capture shows all of it");
	assertEqual(new TimeView(0, 100).x(25, 400), 100);
	assertEqual(new TimeView(0, 100).ns(200, 400), 50);
});

test("profiler: an answer serves a view it covers at about its pixel size", () => {
	const answer = { from: 0, to: 3000, pixelNs: 1 };
	assertTrue(answerServes(answer, new TimeView(1000, 2000), 1000));
	assertTrue(!answerServes(answer, new TimeView(2500, 3500), 1000), "the view runs past what was fetched");
	assertTrue(!answerServes(answer, new TimeView(1000, 1100), 1000), "zoomed in tenfold: the merged runs are too coarse");
	assertTrue(!answerServes(null, new TimeView(0, 1), 10));
	assertEqual(fetchRange(new TimeView(1000, 2000), 10000), { from: 0, to: 3000 }, "a screen more on each side");
	assertEqual(fetchRange(new TimeView(9500, 10500), 10000), { from: 8500, to: 10500 }, "not past the end of the data");
});

const ANSWER = {
	lanes: [
		{ id: "thread:0", kind: "thread", label: "Main", track: 0, depth: 2, renamed: 0, zones: [[0, 0, 100, 0, "0:0:0"], [1, 10, 20, 1, "0:1:0"], [1, 30, 31, -1, 5]] },
		{ id: "task:Physics", kind: "task", label: "Physics", runs: 3, threads: 2, depth: 1, rowDepth: 1, slices: [], zones: [] },
	],
	gpu: [{ id: "gpu:1", label: "Direct", depth: 1, zones: [] }],
	messages: [[5, 2, "hello", 0]], messagesInRange: 1,
	plots: [{ id: 1, name: "Entities", kind: 0, points: [] }],
	names: ["Frame", "Update"],
};
const OPTIONS = { rowHeight: 18, pad: 3, messageHeight: 26, plotHeight: 36 };

test("profiler: lanes stack by their depth", () => {
	const layout = layoutLanes(ANSWER, OPTIONS);
	assertEqual(layout.lanes.map((l) => [l.kind, l.y, l.height]), [["thread", 0, 42], ["task", 42, 24], ["gpu", 66, 24], ["messages", 90, 26], ["plot", 116, 36]]);
	assertEqual(layout.height, 152);
	assertEqual(layout.lanes[1].sub, "3 runs on 2 threads");
	const folded = layoutLanes(ANSWER, Object.assign({ collapsed: new Set(["thread:0"]) }, OPTIONS));
	assertEqual(folded.lanes[0].height, 24, "a folded lane is one row");
	assertEqual(laneAt(layout, 50).id, "task:Physics");
	assertEqual(laneAt(layout, 500), null);
});

test("profiler: a point hits the zone under it, the narrowest when they overlap", () => {
	const layout = layoutLanes(ANSWER, OPTIONS);
	const view = new TimeView(0, 100);
	const main = layout.lanes[0];
	assertEqual(zoneAt(main, view, 1000, 500, 3 + 5, OPTIONS)[4], "0:0:0", "depth 0");
	assertEqual(zoneAt(main, view, 1000, 150, 3 + 18 + 5, OPTIONS)[4], "0:1:0", "depth 1");
	assertEqual(zoneAt(main, view, 1000, 305, 3 + 18 + 5, OPTIONS)[4], 5, "a merged run");
	assertEqual(zoneAt(main, view, 1000, 900, 3 + 18 + 5, OPTIONS), null, "nothing there");
	assertEqual(zoneAt(layout.lanes[3], view, 1000, 50, 95, OPTIONS), null, "the messages lane has no zones");
});

test("profiler: thread colors and frames over the budget", () => {
	assertEqual([seriesOf(0), seriesOf(7), seriesOf(8)], [1, 8, 1]);
	const bands = frameBands({ items: [[0, 0, 10], [1, 10, 25], [2, 25, 50]] }, 12);
	assertEqual(bands.map((b) => [b.index, b.over, b.spike]), [[0, false, false], [1, true, false], [2, true, true]]);
	assertEqual(frameBands({ dense: true, items: [] }, 12), [], "too many to draw");
	assertEqual(frameBands(null, 12), []);
});
