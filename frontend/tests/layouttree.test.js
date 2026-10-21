// layouttree.test.js - the layout tree: normal form, the default, tabs, preview tabs, docking and resizing.

import { test, assertEqual, assertTrue } from "./harness.js";
import * as L from "../src/shell/layouttree.js";

const TOOLS = [
	{ id: "explorer", dock: "left" },
	{ id: "output", dock: "bottom" },
	{ id: "errors", dock: "bottom" },
	{ id: "props", dock: "right" },
	{ id: "welcome", dock: "document" },
];

function shape(node) {
	if (L.isGroup(node)) return (node.well ? "W" : "G") + "[" + node.tabs.join(",") + (node.active ? "|" + node.active : "") + "]";
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
