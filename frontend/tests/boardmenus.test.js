// boardmenus.test.js - the planning board's right-click menus (docs/32 "Groups", "Layers"): the Boards list's empty space,
// a layer's row and the Layers list's empty space - what each offers and when a row is off.

import { test, assertEqual } from "./harness.js";
import { listMenuRows } from "/plugins/ied.board/ui/boardlist.js";
import { layerMenuRows, layersMenuRows } from "/plugins/ied.board/ui/panels.js";

const offered = (rows) => rows.filter((r) => r.kind === "item").map((r) => (r.enabled ? "" : "-") + r.id + (r.icon ? "*" : ""));

test("board menus: the Boards list's empty space", () => {
	assertEqual(offered(listMenuRows([], new Set())), ["board", "group"], "no groups: a new board or group");
	const groups = [{ name: "A", boards: [] }, { name: "B", boards: [] }];
	assertEqual(offered(listMenuRows(groups, new Set())), ["board", "group", "-expand", "collapse"], "all open: nothing to expand");
	assertEqual(offered(listMenuRows(groups, new Set(["A", "B", ""]))), ["board", "group", "expand", "-collapse"], "all folded, Ungrouped too: nothing to collapse");
	assertEqual(offered(listMenuRows(groups, new Set(["A"]))), ["board", "group", "expand", "collapse"], "some folded: both");
});

test("board menus: a layer's row", () => {
	const layers = [{ id: "L1", name: "Sketch", visible: true, locked: false }, { id: "L2", name: "Diagram", visible: true, locked: true }, { id: "L3", name: "Notes", visible: false, locked: false }];
	assertEqual(offered(layerMenuRows(layers, layers[0], 4)), ["add", "rename", "visible*", "locked", "solo", "select", "up", "-down", "-merge", "delete"],
		"the bottom layer: visible checked, nothing below to move or merge into");
	assertEqual(offered(layerMenuRows(layers, layers[1], 2)), ["add", "rename", "visible*", "locked*", "solo", "-select", "up", "down", "merge", "delete"],
		"a locked layer's elements cannot be selected");
	assertEqual(offered(layerMenuRows(layers, layers[2], 0)), ["add", "rename", "visible", "locked", "solo", "-select", "-up", "down", "merge", "delete"],
		"the top layer, hidden and empty");
	const alone = [{ id: "L1", name: "Layer 1", visible: true, locked: false }];
	assertEqual(offered(layerMenuRows(alone, alone[0], 0)), ["add", "rename", "visible*", "locked", "-solo", "-select", "-up", "-down", "-merge", "-delete"],
		"the only layer: shown alone already, and never deleted");
});

test("board menus: the Layers list's empty space", () => {
	assertEqual(offered(layersMenuRows([{ id: "L1", visible: true, locked: false }])), ["add", "-showAll", "-unlockAll"]);
	assertEqual(offered(layersMenuRows([{ id: "L1", visible: false, locked: true }, { id: "L2", visible: true, locked: false }])), ["add", "showAll", "unlockAll"]);
});
