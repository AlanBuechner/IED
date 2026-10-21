// tree.test.js - TreeView (shared/ui-kit/tree.js): a refresh reads the tree again and keeps open what was open, nested
// folders included.

import { test, assertEqual } from "./harness.js";
import { TreeView } from "/ui-kit/tree.js";

// root/a/b/c, each a folder holding the next; every read is counted.
function provider(reads) {
	const node = (path) => ({ id: path, name: path.slice(path.lastIndexOf("/") + 1), expandable: true });
	return {
		roots: async () => [node("root")],
		children: async (parent) => {
			reads.push(parent.id);
			await new Promise((resolve) => setTimeout(resolve, 1));
			return parent.id.split("/").length < 4 ? [node(parent.id + "/" + "abc"[parent.id.split("/").length - 1])] : [];
		},
		render: () => {},
		label: (n) => n.name,
	};
}

test("tree: a refresh keeps nested folders open", async () => {
	const reads = [];
	const el = document.createElement("div");
	const tree = new TreeView(el, provider(reads));
	await tree.refresh();
	for (const id of ["root", "root/a", "root/a/b"]) await tree.toggle(tree.findNode(id), true);
	assertEqual(tree.rows.map((r) => r.node.id), ["root", "root/a", "root/a/b", "root/a/b/c"]);
	reads.length = 0;
	await tree.refresh();
	assertEqual(tree.rows.map((r) => r.node.id), ["root", "root/a", "root/a/b", "root/a/b/c"], "still open after the refresh");
	assertEqual(reads, ["root", "root/a", "root/a/b"], "each open folder read once, parents first");
	tree.dispose();
});
