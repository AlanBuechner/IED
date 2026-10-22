// profiler.test.js - the profiler's pure parts (plugins/profiler): durations and ruler ticks, the view's zoom and pan,
// whether a fetched answer still serves a view, lanes laid out from a prof.timeline answer, which zone a point hits,
// and the frame bands over the budget.

import { test, assertEqual, assertTrue } from "./harness.js";
import { duration, ms, bytes, tickStep, tickLabel, instant } from "/plugins/ied.profiler/ui/format.js";
import { TimeView, answerServes, fetchRange, layoutLanes, laneAt, zoneAt, lockAt, noteAt, lockLabel, seriesOf, frameBands, MIN_SPAN_NS, LOCK_HELD, LOCK_CONTENDED, LOCK_BLOCKED } from "/plugins/ied.profiler/ui/model.js";
import { openNextCapture } from "/plugins/ied.profiler/ui/index.js";
import { noteRange } from "/plugins/ied.profiler/ui/notes.js";

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

// A plugin API with a client whose topic the test feeds, recording the tabs it opens.
function fakeIed() {
	const ied = { opened: [], subscribed: [], handlers: new Map() };
	ied.client = {
		subscribe: (key, handler) => {
			ied.subscribed.push(key);
			ied.handlers.set(key, handler);
			return () => ied.handlers.delete(key);
		},
	};
	ied.commands = { run: async () => {} };
	ied.tabs = { open: (ref) => ied.opened.push(ref) };
	return ied;
}

test("profiler: Start Profiling opens its workspace's next live capture, once", async () => {
	const ied = fakeIed();
	openNextCapture(ied, "w2", new Set(["old"]));
	assertEqual(ied.subscribed, ["profs:w2"], "the workspace the task runs in, not the first one opened");
	const topic = ied.handlers.get("profs:w2");
	topic.snap({ captures: [{ captureId: "old", live: true, name: "old.iedtrace" }] });
	topic.evt("captures.changed", { captures: [{ captureId: "done", live: false, name: "done.iedtrace" }] });
	await Promise.resolve();
	assertEqual(ied.opened, [], "neither a capture it knew nor one that is not live");
	topic.evt("captures.changed", { captures: [{ captureId: "new", live: true, name: "game.iedtrace" }] });
	await new Promise((resolve) => setTimeout(resolve, 0));
	assertEqual(ied.opened, ["prof:new"], "the program that connected");
	assertTrue(!ied.handlers.has("profs:w2"), "and it stops listening");
});

test("profiler: a Profile task that does not start stops the waiting", () => {
	const ied = fakeIed();
	const stop = openNextCapture(ied, "w1", new Set());
	stop();
	assertTrue(!ied.handlers.has("profs:w1"));
	stop();
});

// A thread lane with two zone rows and two lock rows; a lock row's items are [start, end, state, hold or -count].
const LOCK_ANSWER = {
	lanes: [{ kind: "thread", id: "thread:0", label: "Main", track: 0, depth: 2, zones: [],
		locks: [
			{ lock: 1, name: "Queue", items: [[100, 300, LOCK_HELD, 0], [400, 600, LOCK_BLOCKED, 1], [600, 700, LOCK_CONTENDED, 1]] },
			{ lock: 2, name: "Assets", items: [[0, 900, LOCK_BLOCKED, -40]] }] }],
};
const LOCK_OPTIONS = { rowHeight: 18, lockRowHeight: 12, pad: 3, messageHeight: 26, plotHeight: 36, slop: 2 };

test("profiler: a thread's lock rows come under its zone rows", () => {
	const lane = layoutLanes(LOCK_ANSWER, LOCK_OPTIONS).lanes[0];
	assertEqual([lane.lockRows, lane.lockTop, lane.height], [2, 3 + 2 * 18, 2 * 18 + 2 * 12 + 6]);
	const folded = layoutLanes(LOCK_ANSWER, { ...LOCK_OPTIONS, collapsed: new Set(["thread:0"]) }).lanes[0];
	assertEqual([folded.lockRows, folded.height], [0, 18 + 6], "a folded lane hides its lock rows");
});

test("profiler: a point hits the lock hold under it, the worst state where they meet", () => {
	const lane = layoutLanes(LOCK_ANSWER, LOCK_OPTIONS).lanes[0];
	const view = new TimeView(0, 1000);
	const row0 = lane.lockTop + 6;
	assertEqual(lockAt(lane, view, 1000, 200, row0, LOCK_OPTIONS).item, [100, 300, LOCK_HELD, 0]);
	const edge = lockAt(lane, view, 1000, 600, row0, LOCK_OPTIONS);
	assertEqual([edge.name, edge.item[2]], ["Queue", LOCK_BLOCKED], "at the wait's end and the hold's start, the wait");
	assertEqual(lockAt(lane, view, 1000, 350, row0, LOCK_OPTIONS), null, "between holds");
	assertEqual(lockAt(lane, view, 1000, 500, row0 + 12, LOCK_OPTIONS).lock, 2, "the second row is the second lock");
	assertEqual(lockAt(lane, view, 1000, 500, 10, LOCK_OPTIONS), null, "the zone rows are not lock rows");
	assertEqual(lockLabel("Queue", [600, 700, LOCK_CONTENDED, 1], duration), "Queue: held while others waited, 100 ns");
	assertEqual(lockLabel("Assets", [0, 900, LOCK_BLOCKED, -40], duration), "Assets: 40 holds and waits");
});

test("profiler: a point on the notes row hits the narrowest note there", () => {
	const notes = [{ id: "n1", fromNs: 0, toNs: 800, text: "loading" }, { id: "n2", fromNs: 300, toNs: 400, text: "hitch" }, { id: "n3", fromNs: 900, toNs: 900, text: "moment" }];
	const view = new TimeView(0, 1000);
	assertEqual(noteAt(notes, view, 1000, 350).id, "n2");
	assertEqual(noteAt(notes, view, 1000, 100).id, "n1");
	assertEqual(noteAt(notes, view, 1000, 902).id, "n3", "a moment is hit within the slop");
	assertEqual(noteAt(notes, view, 1000, 860), null);
	assertEqual([noteRange(notes[1]), noteRange(notes[2])], ["0.00030 ms to 0.00040 ms (100 ns)", "at 0.001 ms"]);
});

test("profiler: a point in time is as precise as its range needs", () => {
	assertEqual([instant(1290050000, 400000), instant(1290450000, 400000)], ["1290.05 ms", "1290.45 ms"], "a short range's ends apart");
	assertEqual([instant(2000000000, 5e9), instant(2000000000, 1e11), instant(1290050000, 0)], ["2.0 s", "2 s", "1290.050 ms"]);
});
