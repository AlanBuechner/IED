// dock.test.js - a dock host on screen (docs/15 "Docking"): its views hear when they are shown and hidden, as the tab
// chosen in a group and as the whole dock when its activity goes away and comes back.

import { test, assertEqual } from "./harness.js";
import { DockView } from "../src/shell/dock.js";
import { group } from "../src/shell/layouttree.js";

// The views of `refs`; `shown` holds what each was last told.
function viewsOf(refs) {
	const entries = new Map(refs.map((ref) => [ref, { ref, el: document.createElement("div"), title: ref, icon: "$(file)", base: null, dirty: false }]));
	const shown = {};
	return {
		shown,
		has: (ref) => entries.has(ref),
		get: (ref) => entries.get(ref),
		setVisible: (ref, visible) => {
			shown[ref] = visible;
		},
	};
}

function hostWith(views) {
	const none = () => {};
	return {
		views, menus: { show: none, host: null }, emptyWell: () => document.createElement("div"), activate: none, tetheredWith: () => [], close: none,
		pin: none, move: none, resize: none, autoHide: none, resizeAutoHidden: none, hold: none, focused: none, newTab: null, newTabTitle: "New Tab",
		titleActions: () => [],
	};
}

test("dock: a hidden activity's views hear they are hidden, and the active one is shown when it is back", () => {
	const views = viewsOf(["a", "b"]);
	const dock = new DockView(hostWith(views), document.createElement("div"));
	const tree = group(["a", "b"], { active: "a" });
	dock.render(tree);
	assertEqual(views.shown, { a: true, b: false }, "the active tab is shown");
	dock.hide();
	assertEqual(views.shown, { a: false, b: false }, "the activity hidden: none is shown");
	dock.render(tree);
	assertEqual(views.shown, { a: true, b: false }, "the activity back: its active tab is shown again");
	dock.dispose();
});
