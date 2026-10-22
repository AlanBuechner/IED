// layouttree.test.js - the layout tree: normal form, the default, tabs, preview tabs, docking, resizing and auto-hide.

import { test, assertEqual, assertTrue } from "./harness.js";
import * as L from "../src/shell/layouttree.js";
import * as D from "../src/shell/layoutdoc.js";

const TOOLS = [
	{ id: "explorer", dock: "left" },
	{ id: "output", dock: "bottom" },
	{ id: "errors", dock: "bottom" },
	{ id: "props", dock: "right" },
	{ id: "welcome", dock: "document" },
];

function shape(node) {
	if (L.isGroup(node)) return (node.well ? "W" : "G") + "[" + node.tabs.join(",") + (node.active ? "|" + node.active : "") + "]" + (node.autoHidden ? "^" + node.autoHide + "(" + node.autoHidden.join(",") + ")" : "");
	return node.dir + "(" + node.children.map(shape).join(" ") + ")";
}

function sum(sizes) {
	return sizes.reduce((a, b) => a + b, 0);
}

function sizesHold(node) {
	if (!L.isSplit(node)) return true;
	return node.children.length >= 2 && Math.abs(sum(node.sizes) - 1) < 1e-9 && node.sizes.every((s) => s > 0) && node.children.every(sizesHold);
}

test("layout: the default places tool windows around the documents", () => {
	const tree = L.defaultTree(TOOLS);
	assertEqual(shape(tree), "row(G[explorer|explorer] column(W[welcome|welcome] G[output,errors|output]) G[props|props])");
	assertTrue(sizesHold(tree), "sizes");
	assertEqual(tree.sizes.map((s) => Math.round(s * 100)), [16, 62, 22], "left 20%, then right 22% of the rest");
	assertEqual(L.groupOf(tree, "output").dock, "bottom", "groups remember their dock");
});

test("layout: an empty tree is one empty document group", () => {
	assertEqual(shape(L.normalize(null)), "W[]");
	assertEqual(shape(L.defaultTree([])), "W[]");
});

test("layout: normal form", () => {
	const messy = {
		kind: "split", dir: "row", sizes: [2, -1, 1], children: [
			{ kind: "split", dir: "row", sizes: [1, 1], children: [L.group(["a"]), L.group(["b", "a"], { active: "zz" })] },
			{ kind: "tabs", tabs: [], active: null },
			{ kind: "split", dir: "column", sizes: [1], children: [L.group(["c"], { well: true })] },
		],
	};
	const tree = L.normalize(messy);
	assertEqual(shape(tree), "row(G[a|a] G[b|b] W[c|c])", "flattened, deduplicated, empty group and one-child split gone");
	assertTrue(sizesHold(tree));
	const noWell = L.normalize(L.group(["x"]));
	assertEqual(shape(noWell), "row(G[x|x] W[])", "a document group is always there");
	assertEqual(shape(L.normalize({ kind: "split", dir: "diagonal", children: "no" })), "W[]", "garbage");
});

test("layout: open, activate and close tabs", () => {
	let tree = L.defaultTree(TOOLS);
	tree = L.openTab(tree, "doc:a.cf");
	tree = L.openTab(tree, "doc:b.cf");
	assertEqual(shape(L.wellsOf(tree)[0]), "W[welcome,doc:a.cf,doc:b.cf|doc:b.cf]", "opened after the active tab");
	tree = L.activateTab(tree, "doc:a.cf");
	tree = L.openTab(tree, "doc:c.cf");
	assertEqual(L.wellsOf(tree)[0].tabs, ["welcome", "doc:a.cf", "doc:c.cf", "doc:b.cf"]);
	tree = L.openTab(tree, "doc:b.cf");
	assertEqual(L.wellsOf(tree)[0].active, "doc:b.cf", "opening an open tab activates it");
	tree = L.closeTab(tree, "doc:b.cf");
	assertEqual(L.wellsOf(tree)[0].active, "doc:c.cf", "closing the active tab activates its neighbor");
	tree = L.closeGroupTabs(tree, "doc:a.cf", false);
	assertEqual(shape(L.wellsOf(tree)[0]), "W[doc:a.cf|doc:a.cf]", "close all but this");
	tree = L.closeGroupTabs(tree, "doc:a.cf", true);
	assertEqual(shape(L.wellsOf(tree)[0]), "W[]", "the last document group stays when emptied");
	tree = L.closeTab(tree, "explorer");
	assertEqual(L.refsOf(tree).includes("explorer"), false);
	assertEqual(shape(tree), "row(column(W[] G[output,errors|output]) G[props|props])", "an emptied tool group goes");
});

test("layout: a preview tab is replaced by the next preview until pinned", () => {
	let tree = L.normalize(L.group([], { well: true }));
	tree = L.openTab(tree, "doc:a", { preview: true });
	tree = L.openTab(tree, "doc:b", { preview: true });
	assertEqual(shape(tree), "W[doc:b|doc:b]", "b replaced the preview a");
	assertEqual(tree.preview, "doc:b");
	tree = L.pinTab(tree, "doc:b");
	tree = L.openTab(tree, "doc:c", { preview: true });
	assertEqual(shape(tree), "W[doc:b,doc:c|doc:c]", "a pinned tab stays");
	tree = L.openTab(tree, "doc:c");
	assertEqual(tree.preview, undefined, "opening a preview tab for good pins it");
});

test("layout: moving a tab into a group, and reordering", () => {
	let tree = L.defaultTree(TOOLS);
	const explorerPath = L.pathOf(tree, L.groupOf(tree, "explorer"));
	tree = L.moveTab(tree, "props", explorerPath, "center");
	assertEqual(shape(tree), "row(G[explorer,props|props] column(W[welcome|welcome] G[output,errors|output]))", "the right group emptied and went");
	const bottomPath = L.pathOf(tree, L.groupOf(tree, "errors"));
	tree = L.moveTab(tree, "errors", bottomPath, "center", 0);
	assertEqual(L.groupOf(tree, "errors").tabs, ["errors", "output"], "reordered to the front");
	tree = L.moveTab(tree, "errors", L.pathOf(tree, L.groupOf(tree, "errors")), "center", 2);
	assertEqual(L.groupOf(tree, "errors").tabs, ["output", "errors"], "reordered to the end");
});

test("layout: dropping on a group's edge splits it", () => {
	let tree = L.normalize(L.group(["doc:a", "doc:b"], { well: true }));
	tree = L.moveTab(tree, "doc:b", [], "right");
	assertEqual(shape(tree), "row(W[doc:a|doc:a] W[doc:b|doc:b])", "a new document group beside");
	assertEqual(tree.sizes, [0.5, 0.5]);
	tree = L.moveTab(tree, "doc:a", [1], "bottom");
	assertEqual(shape(tree), "column(W[doc:b|doc:b] W[doc:a|doc:a])", "a group of one moved under another; the empty one went");
	tree = L.openTab(tree, "doc:c", { groupPath: [0] });
	tree = L.moveTab(tree, "doc:c", [0], "top");
	assertEqual(shape(tree), "column(W[doc:c|doc:c] W[doc:b|doc:b] W[doc:a|doc:a])", "a split of the same direction takes the new group in place");
	assertEqual(tree.sizes.map((s) => Math.round(s * 100)), [25, 25, 50]);
	const same = L.moveTab(tree, "doc:a", [2], "left");
	assertEqual(same, tree, "splitting a group by its only tab changes nothing");
});

test("layout: dropping on the activity's edge", () => {
	let tree = L.defaultTree(TOOLS);
	tree = L.moveTab(tree, "output", [], "left");
	assertEqual(shape(tree.children[0]), "G[output|output]", "a new first column");
	assertEqual(Math.round(tree.sizes[0] * 100), 20);
	tree = L.moveTab(tree, "errors", [], "bottom");
	assertEqual(tree.dir, "column", "the whole row is now above it");
	assertEqual(shape(tree.children[1]), "G[errors|errors]");
	assertTrue(sizesHold(tree));
});

test("layout: resizing", () => {
	let tree = L.defaultTree(TOOLS);
	tree = L.setSizes(tree, [], [1, 2, 1]);
	assertEqual(tree.sizes, [0.25, 0.5, 0.25]);
	assertEqual(L.setSizes(tree, [], [1, 1]), tree, "the wrong count changes nothing");
	assertEqual(L.setSizes(tree, [0], [1, 1]), tree, "a group is not a split");
});

test("layout: tool windows are added where their dock says", () => {
	let tree = L.normalize(L.group(["doc:a"], { well: true }));
	tree = L.ensureTool(tree, "output", "bottom", false);
	assertEqual(shape(tree), "column(W[doc:a|doc:a] G[output|output])");
	tree = L.ensureTool(tree, "errors", "bottom", true);
	assertEqual(shape(tree), "column(W[doc:a|doc:a] G[output,errors|errors])", "into the bottom group, activated");
	tree = L.ensureTool(tree, "explorer", "left", false);
	assertEqual(shape(tree), "row(G[explorer|explorer] column(W[doc:a|doc:a] G[output,errors|errors]))");
	const again = L.ensureTool(tree, "explorer", "right", false);
	assertEqual(again, tree, "already there");
	tree = L.ensureTool(tree, "output", "bottom", true);
	assertEqual(L.groupOf(tree, "output").active, "output", "activated where it is");
	tree = L.ensureTool(tree, "start", "document", false);
	assertEqual(L.wellsOf(tree)[0].tabs, ["doc:a", "start"]);
});

test("layout: a tool window auto-hides by itself to the edge its group lies on, and docks back into its group", () => {
	const tree = L.defaultTree(TOOLS);
	const one = L.autoHideTab(tree, "output");
	assertEqual(shape(one), "row(G[explorer|explorer] column(W[welcome|welcome] G[output,errors|errors]^bottom(output)) G[props|props])", "Error List stays docked, shown");
	assertEqual(Math.round(L.groupOf(one, "output").autoHideSize * 100), 30, "the share of the height its group had");
	assertTrue(L.isAutoHidden(one, "output") && !L.isAutoHidden(one, "errors"));
	assertEqual(L.groupOf(L.activateTab(one, "output"), "output").active, "errors", "a hidden tab slides out; its group keeps showing Error List");
	const both = L.autoHideTab(one, "errors");
	assertEqual(shape(L.groupOf(both, "errors")), "G[output,errors|errors]^bottom(output,errors)", "every tab hidden: the group takes no room");
	assertEqual(L.autoHideTab(one, "output"), one, "already hidden");
	assertEqual(L.autoHideTab(tree, "welcome"), tree, "a document never hides");
	assertEqual(L.autoHideTab(tree, "nothing"), tree);
	const back = L.dockTab(both, "output");
	assertEqual(shape(L.groupOf(back, "output")), "G[output,errors|output]^bottom(errors)", "back in its group where it was, shown");
	assertEqual(shape(L.groupOf(L.dockTab(back, "errors"), "errors")), "G[output,errors|errors]", "nothing hidden: no edge left");
	assertEqual(L.dockTab(tree, "output"), tree, "not hidden");
	const left = L.autoHideTab(tree, "explorer");
	assertEqual(L.groupOf(left, "explorer").autoHide, "left");
	assertEqual(Math.round(L.groupOf(left, "explorer").autoHideSize * 100), 16, "the share of the width it had");
	assertEqual(L.groupOf(L.autoHideTab(tree, "props"), "props").autoHide, "right");
});

test("layout: hiding the shown tab shows the nearest docked one; tabs come and go in a hidden group", () => {
	let tree = L.normalize(L.group(["doc:a"], { well: true }));
	for (const id of ["a", "b", "c"]) tree = L.ensureTool(tree, id, "bottom", false);
	tree = L.activateTab(tree, "b");
	assertEqual(L.groupOf(L.autoHideTab(tree, "b"), "b").active, "c", "the next one");
	assertEqual(L.groupOf(L.autoHideTab(L.activateTab(tree, "c"), "c"), "c").active, "b", "else the one before");
	const partly = L.autoHideTab(tree, "a");
	assertEqual(shape(L.groupOf(L.ensureTool(partly, "d", "bottom", true), "d")), "G[a,b,c,d|d]^bottom(a)", "a new tool window joins the docked part");
	const all = L.autoHideAll(tree);
	assertEqual(shape(L.groupOf(L.ensureTool(all, "d", "bottom", true), "d")), "G[a,b,c,d|b]^bottom(a,b,c,d)", "a region all hidden stays hidden");
	assertEqual(shape(L.groupOf(L.closeTab(partly, "a"), "b")), "G[b,c|b]", "closing the hidden tab");
	const moved = L.moveTab(partly, "a", L.pathOf(partly, L.wellsOf(partly)[0]), "center");
	assertEqual(L.isAutoHidden(moved, "a"), false, "dragged into a group, it is docked there");
	assertEqual(shape(L.wellsOf(moved)[0]), "W[doc:a,a|a]");
});

test("layout: the edge is the side of the document group where their branches part", () => {
	const above = L.normalize({ kind: "split", dir: "column", sizes: [0.25, 0.75], children: [L.group(["find"]), { kind: "split", dir: "row", sizes: [0.5, 0.5], children: [L.group(["doc:a"], { well: true }), L.group(["props"])] }] });
	assertEqual(L.groupOf(L.autoHideTab(above, "find"), "find").autoHide, "top");
	assertEqual(L.groupOf(L.autoHideTab(above, "find"), "find").autoHideSize, 0.25);
	assertEqual(L.groupOf(L.autoHideTab(above, "props"), "props").autoHide, "right", "inside the lower row");
	assertEqual(L.groupOf(L.autoHideTab(above, "props"), "props").autoHideSize, 0.5);
});

test("layout: auto-hide all, sizes and the normal form of hidden tabs", () => {
	let tree = L.autoHideAll(L.defaultTree(TOOLS));
	assertEqual(L.autoHiddenOf(tree).map((g) => g.autoHide + ":" + g.autoHidden.join(",")), ["left:explorer", "bottom:output,errors", "right:props"], "every tool window");
	assertEqual(L.wellsOf(tree)[0].autoHidden, undefined, "not the documents");
	assertEqual(L.autoHideAll(tree), tree, "nothing left to hide");
	assertEqual(shape(L.defaultTree(TOOLS, true)), shape(tree), "a tablet's default");
	assertEqual(shape(D.treeOf(D.emptyDocument(), "ied.editor", TOOLS, true)), shape(tree), "a tablet's tree without a stored one");
	tree = L.setAutoHideSize(tree, "output", 0.5);
	assertEqual(L.groupOf(tree, "errors").autoHideSize, 0.5, "one size for the group's hidden tabs");
	assertEqual(L.groupOf(L.setAutoHideSize(tree, "output", 4), "output").autoHideSize, 0.9, "at most 90%");
	assertEqual(L.groupOf(L.setAutoHideSize(tree, "output", 0), "output").autoHideSize, 0.05, "at least 5%");
	assertEqual(L.setAutoHideSize(tree, "welcome", 0.5), tree, "only a hidden tab's group has one");
	const odd = L.normalize({ kind: "split", dir: "row", sizes: [1, 1, 1, 1], children: [
		L.group(["a"], { autoHidden: ["a"], autoHide: "sideways" }),
		L.group(["doc:a"], { well: true, autoHidden: ["doc:a"], autoHide: "left" }),
		L.group(["b", "c"], { autoHidden: ["zz", "c", "c"], autoHide: "right", autoHideSize: "big" }),
		L.group(["d"], { autoHide: "left", autoHideSize: 0.5 }),
	] });
	assertEqual(shape(odd), "row(G[a|a] W[doc:a|doc:a] G[b,c|b]^right(c) G[d|d])", "no edge, a document, or nothing hidden: shown");
	assertEqual(odd.children[0].autoHideSize, undefined);
	assertEqual(odd.children[2].autoHideSize, 0.3, "a size that is no number");
	assertEqual(odd.children[3].autoHide, undefined);
});

test("layout: a mode's default places tool windows by dock and group, hides those that ask, leaves out the closed", () => {
	const editing = [
		{ id: "explorer", dock: "left" },
		{ id: "output", dock: "right", autoHide: true, size: 0.4 },
		{ id: "errors", dock: "bottom", autoHide: true },
		{ id: "locals", dock: "bottom", open: false },
	];
	const tree = L.defaultTree(editing);
	assertEqual(L.groupOf(tree, "output").autoHideSize, 0.4, "it slides out as large as its default size");
	assertEqual(shape(tree), "row(G[explorer|explorer] column(W[] G[errors|errors]^bottom(errors)) G[output|output]^right(output))");
	assertEqual(L.refsOf(tree).includes("locals"), false, "a closed one is left out");
	const debugging = [
		{ id: "explorer", dock: "left" },
		{ id: "output", dock: "bottom", group: "output" },
		{ id: "errors", dock: "bottom", group: "output" },
		{ id: "locals", dock: "bottom", group: "variables" },
		{ id: "watch", dock: "bottom", group: "variables" },
		{ id: "find", dock: "bottom", autoHide: true },
	];
	const debug = L.defaultTree(debugging);
	assertEqual(shape(debug), "row(G[explorer|explorer] column(W[] row(G[output,errors|output] G[locals,watch|locals] G[find|find]^bottom(find))))", "groups of one dock side by side");
	assertEqual(L.groupOf(debug, "watch").dockGroup, "variables");
	const sides = L.ensureTool(L.ensureTool(L.defaultTree([]), "a", "left", false, "one"), "b", "left", false, "two");
	assertEqual(shape(sides), "row(column(G[a|a] G[b|b]) W[])", "on a side, one below the other");
	assertEqual(shape(L.ensureTool(sides, "c", "left", false, "two")), "row(column(G[a|a] G[b,c|b]) W[])", "a name finds its group");
});

test("layout: switching modes carries the documents, and the mode keeps its tool windows", () => {
	const isTool = (ref) => !ref.includes(":");
	let editing = L.defaultTree([{ id: "explorer", dock: "left" }, { id: "output", dock: "right", autoHide: true }]);
	editing = L.openTab(editing, "doc:a");
	editing = L.openTab(editing, "doc:b");
	editing = L.moveTab(editing, "doc:b", L.pathOf(editing, L.wellsOf(editing)[0]), "right");
	editing = L.moveTab(editing, "doc:a", L.pathOf(editing, L.groupOf(editing, "explorer")), "center");
	editing = L.openTab(editing, "doc:c", { groupPath: L.pathOf(editing, L.groupOf(editing, "doc:b")) });
	let debug = L.defaultTree([{ id: "explorer", dock: "left" }, { id: "output", dock: "bottom" }]);
	debug = L.openTab(debug, "doc:stale");
	const carried = L.withDocumentsOf(debug, editing, isTool);
	assertEqual(shape(carried), "row(G[explorer|explorer] column(W[doc:b,doc:c,doc:a|doc:c] G[output|output]))", "the documents as they were, a stray one into the documents, none stale");
	assertTrue(sizesHold(carried));
	const split = L.moveTab(carried, "doc:c", L.pathOf(carried, L.groupOf(carried, "doc:c")), "right");
	const back = L.withDocumentsOf(editing, split, isTool);
	assertEqual(shape(back), "row(G[explorer|explorer] W[doc:b,doc:a|doc:a] W[doc:c|doc:c] G[output|output]^right(output))", "back again: the documents split as they were left, editing's tool windows");
	assertEqual(L.isAutoHidden(back, "output"), true, "Output still hidden in the editing layout");
});

test("layout doc: each layout mode keeps its own trees", () => {
	const tools = [{ id: "explorer", dock: "left" }, { id: "output", dock: "bottom" }];
	let doc = D.emptyDocument();
	const debugTree = L.openTab(D.treeOf(doc, "ed", tools, false, "debug"), "doc:a");
	doc = D.withTree(doc, "ed", debugTree, "debug");
	assertEqual(D.treeOf(doc, "ed", tools, false, "debug"), debugTree, "stored under modes.debug");
	assertEqual(doc.activities, {}, "the default mode's untouched");
	assertEqual(shape(D.treeOf(doc, "ed", tools, false, D.DEFAULT_MODE)), shape(L.defaultTree(tools)), "the default mode starts from its default");
	doc = D.withTree(doc, "ed", L.defaultTree(tools), D.DEFAULT_MODE);
	assertTrue(Boolean(doc.activities.ed) && Boolean(doc.modes.debug.activities.ed), "both kept");
	doc = D.withoutTree(doc, "ed", "debug");
	assertEqual(doc.modes.debug.activities, {}, "reset one mode");
	assertTrue(Boolean(doc.activities.ed), "not the other");
});

test("layout: a tab opens beside another, in a new group when there is no other", () => {
	let tree = L.normalize(L.group(["doc:a.md", "doc:b"], { well: true, active: "doc:a.md" }));
	tree = L.openBeside(tree, "doc:a.md", "mdpreview:a.md");
	assertEqual(shape(tree), "row(W[doc:a.md,doc:b|doc:a.md] W[mdpreview:a.md|mdpreview:a.md])", "the anchor stays shown");
	const again = L.openBeside(tree, "doc:b", "doc:c");
	assertEqual(shape(again), "row(W[doc:a.md,doc:b|doc:a.md] W[mdpreview:a.md,doc:c|doc:c])", "into the other group");
	const moved = L.openBeside(L.normalize(L.group(["doc:a", "doc:b"], { well: true, active: "doc:a" })), "doc:a", "doc:b");
	assertEqual(shape(moved), "row(W[doc:a|doc:a] W[doc:b|doc:b])", "one in the anchor's group moves beside it");
	assertEqual(L.openBeside(tree, "doc:a.md", "mdpreview:a.md"), tree, "already beside");
});

test("layout: activating a tab shows its tethered partners in their groups", () => {
	let tree = L.normalize(L.group(["doc:a.h", "doc:x"], { well: true, active: "doc:a.h" }));
	tree = L.openBeside(tree, "doc:a.h", "doc:a.cf");
	tree = L.openTab(tree, "doc:y", { groupPath: L.pathOf(tree, L.groupOf(tree, "doc:a.cf")) });
	tree = L.activateTab(tree, "doc:x");
	const shown = L.revealPartners(L.activateTab(tree, "doc:a.h"), "doc:a.h", ["doc:a.cf"]);
	assertEqual(L.groupOf(shown, "doc:a.cf").active, "doc:a.cf");
	assertEqual(L.groupOf(shown, "doc:a.h").active, "doc:a.h");
	const same = L.normalize(L.group(["doc:a.h", "doc:a.cf"], { well: true, active: "doc:a.h" }));
	assertEqual(L.revealPartners(same, "doc:a.h", ["doc:a.cf"]).tabs.length, 2, "a partner in the same group stays behind");
	assertEqual(L.groupOf(L.revealPartners(same, "doc:a.h", ["doc:a.cf"]), "doc:a.h").active, "doc:a.h");
	assertEqual(L.revealPartners(tree, "doc:a.h", []), tree);
});

test("layout doc: tethers pair tabs, and a follower closes with its lead", () => {
	let doc = D.emptyDocument();
	doc = D.withTether(doc, "doc:a.md", "mdpreview:a.md", true);
	doc = D.withTether(doc, "doc:a.h", "doc:a.cf", false);
	assertEqual(D.partnersOf(doc, "mdpreview:a.md"), ["doc:a.md"]);
	assertEqual(D.followersOf(doc, "doc:a.md"), ["mdpreview:a.md"]);
	assertEqual(D.followersOf(doc, "doc:a.h"), [], "not closing");
	assertEqual(D.partnersOf(doc, "doc:a.cf"), ["doc:a.h"]);
	doc = D.withTether(doc, "doc:a.cf", "doc:a.h", true);
	assertEqual(doc.tethers.length, 2, "tethering a pair again replaces it");
	assertEqual(D.followersOf(doc, "doc:a.cf"), ["doc:a.h"]);
	const untethered = D.withoutTethers(doc, ["doc:a.h"]);
	assertEqual(untethered.tethers.length, 1);
	assertEqual(D.withoutTethers(untethered, ["doc:z"]), untethered, "nothing to drop");
	assertTrue(!D.emptyDocument().tethers, "an old document has none");
	assertEqual(D.partnersOf(D.emptyDocument(), "doc:a"), []);
});

test("layout doc: a phone's tab closed while another shows leaves its back stack", () => {
	let doc = D.withPhonePane(D.emptyDocument(), "chat:new");
	doc = D.withPhonePane(doc, "chat:c4");
	assertEqual(doc.phone, { pane: "chat:c4", back: ["chat:new"] });
	doc = D.withoutPhoneBack(doc, "chat:new");
	assertEqual(doc.phone, { pane: "chat:c4", back: [] }, "back goes where the draft came from, not to the draft");
	assertEqual(D.withoutPhoneBack(doc, "chat:z"), doc, "nothing to drop");
});
