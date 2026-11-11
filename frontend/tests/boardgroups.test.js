// boardgroups.test.js - the Boards window's sections (docs/32 "Groups"): the owner's groups in order with the boards
// that exist, then the boards in none, and a board moved into a group, before another or at its end, or out of all.

import { test, assertEqual } from "./harness.js";
import { movedTo, sectionsOf } from "/plugins/ied.board/ui/boardlist.js";

const board = (path) => ({ path, boardId: "w1:" + path });

test("board groups: sections in the owner's order, then the ungrouped", () => {
	const list = {
		groups: [{ name: "Sprint", boards: ["b.iedboard", "gone.iedboard"] }, { name: "Later", boards: ["b.iedboard", "c.iedboard"] }, { name: "Empty", boards: [] }],
		boards: [board("a.iedboard"), board("b.iedboard"), board("c.iedboard"), board("d.iedboard")],
	};
	assertEqual(sectionsOf(list).map((s) => [s.name, s.boards.map((b) => b.path)]), [
		["Sprint", ["b.iedboard"]],
		["Later", ["c.iedboard"]],
		["Empty", []],
		["", ["a.iedboard", "d.iedboard"]],
	], "a board shows in its first group only; one that is gone not at all");
	assertEqual(sectionsOf({ boards: [board("a.iedboard")] }).map((s) => s.name), [""], "no groups: one flat list");
});

test("board groups: moving a board", () => {
	const groups = [{ name: "A", boards: ["1", "2"] }, { name: "B", boards: ["3"] }];
	assertEqual(movedTo(groups, "1", "B", null), [{ name: "A", boards: ["2"] }, { name: "B", boards: ["3", "1"] }], "to the end of another group");
	assertEqual(movedTo(groups, "3", "A", "2"), [{ name: "A", boards: ["1", "3", "2"] }, { name: "B", boards: [] }], "before a board of a group");
	assertEqual(movedTo(groups, "2", "A", "1"), [{ name: "A", boards: ["2", "1"] }, { name: "B", boards: ["3"] }], "reordered in its own group");
	assertEqual(movedTo(groups, "1", "", null), [{ name: "A", boards: ["2"] }, { name: "B", boards: ["3"] }], "out of every group");
	assertEqual(groups[0].boards, ["1", "2"], "the groups given are left as they were");
});
