// boardsnap.test.js - the planning board's alignment snapping (plugins/board/ui/snap.js, docs/32 "Snapping"): edges
// and centers snap within the tolerance and not past it, the nearest wins, equal gaps between neighbours and after a
// pair, and the guides that hold where the box lands.

import { test, assertEqual, assertTrue } from "./harness.js";
import { snapTargets, snapBox, snapValue, spacing, guidesFor } from "/plugins/ied.board/ui/snap.js";

const rect = (x, y, w, h) => ({ x0: x, y0: y, x1: x + w, y1: y + h });
const shifted = (b, dx, dy) => ({ x0: b.x0 + dx, y0: b.y0 + dy, x1: b.x1 + dx, y1: b.y1 + dy });

test("board.snap.edges_and_centers", () => {
	const t = snapTargets([rect(100, 100, 160, 60)]);
	// Left edge 4 past the target's left edge: pulled back onto it; the tops 3 apart: lined up too.
	const s = snapBox(rect(104, 303, 80, 40), t, 6, 0.06);
	assertEqual([s.dx, s.dy], [-4, 0], "x snaps to the left edge, y is too far from every line");
	assertTrue(s.guides.some((g) => g.axis === "x" && g.v === 100 && g.from === 100 && g.to === 343), "a vertical guide through x 100 from the target's top to the box's bottom: " + JSON.stringify(s.guides));
	// Centers: the box's center 5 from the target's (180).
	const c = snapBox(rect(145, 300, 80, 40), t, 6, 0.06);
	assertEqual(c.dx, -5, "center to center");
});

test("board.snap.tolerance_and_nearest", () => {
	const t = snapTargets([rect(0, 0, 100, 100), rect(0, 200, 103, 50)]);
	assertEqual(snapBox(rect(107, 400, 40, 40), t, 3, 0.06).dx, 0, "4 and 7 away are past a tolerance of 3");
	// x0 107: 4 from 103, 7 from 100: the nearer wins.
	assertEqual(snapBox(rect(107, 400, 40, 40), t, 8, 0.06).dx, -4, "the nearest line wins");
	assertEqual(snapValue(98, t, "x", 6), 100, "a point onto an edge");
	assertEqual(snapValue(40, t, "x", 6), 40, "nothing near leaves it");
});

test("board.snap.equal_gaps", () => {
	// A row: A [0,100], B [300,400]; a 100-wide box between them is centered at gaps of 50.
	const a = rect(0, 0, 100, 50), b = rect(300, 0, 100, 50);
	const t = snapTargets([a, b]);
	const s = snapBox(rect(147, 10, 100, 30), t, 6, 0.06);
	assertEqual(s.dx, 3, "between two neighbours: equal gaps");
	const gaps = s.guides.filter((g) => g.gaps);
	assertEqual(gaps.length, 1, "one gap guide: " + JSON.stringify(s.guides));
	assertEqual(gaps[0].gaps.map(([f, to]) => to - f), [50, 50], "both gaps 50");
	// After a pair: A [0,100] and B [140,240] keep 40; a box near 280 goes to B.x1 + 40.
	const t2 = snapTargets([rect(0, 0, 100, 50), rect(140, 0, 100, 50)]);
	const s2 = snapBox(rect(284, 5, 60, 30), t2, 6, 0.06);
	assertEqual(s2.dx, -4, "the gap the row already keeps");
	// Not in the row (no overlap across the axis): no gap snapping.
	assertEqual(spacing(rect(284, 100, 60, 30), t2.boxes, "x", 6).length, 0, "a box below the row");
});

test("board.snap.guides_for_given_values", () => {
	const t = snapTargets([rect(100, 100, 100, 100)]);
	// A resized box whose right edge sits on x 200: only that edge is asked about.
	const g = guidesFor(rect(50, 300, 150, 40), t, 0.06, { x: [200], y: [] }, false);
	assertEqual(g.length, 1);
	assertEqual([g[0].axis, g[0].v, g[0].from, g[0].to], ["x", 200, 100, 340]);
	// Its left edge at 50 lines up with nothing; its center at 125 neither.
	assertEqual(guidesFor(rect(50, 300, 150, 40), t, 0.06, { x: [50, 125], y: [] }, false).length, 0);
	const moved = shifted(rect(0, 0, 10, 10), 100, 0);
	assertTrue(guidesFor(moved, t, 0.06, null, false).some((g) => g.axis === "x" && g.v === 100), "a box's own edges by default");
});
