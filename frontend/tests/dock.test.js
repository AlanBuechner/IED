// dock.test.js - a dock host on screen (docs/15 "Docking"): its views hear when they are shown and hidden, as the tab
// chosen in a group and as the whole dock when its activity goes away and comes back; and each view keeps its scroll
// places while its element is moved or hidden (scrollplaces.js), one at its end staying at its end.

import { test, assertEqual } from "./harness.js";
import { DockView } from "../src/shell/dock.js";
import { ScrollPlaces } from "../src/shell/scrollplaces.js";
import { group } from "../src/shell/layouttree.js";

// A view's element holding a scroller 50 px tall over 500 px of content.
function scrollingView() {
	const view = document.createElement("div");
	view.className = "view";
	const scroller = document.createElement("div");
	scroller.style.cssText = "height: 50px; overflow: auto;";
	const content = document.createElement("div");
	content.style.height = "500px";
	scroller.append(content);
	view.append(scroller);
	return view;
}

// The views of `refs`; `shown` holds what each was last told, `placeWhenShown` where its scroller was when told.
function viewsOf(refs) {
	const entries = new Map(refs.map((ref) => [ref, { ref, el: scrollingView(), title: ref, icon: "$(file)", base: null, dirty: false }]));
	const shown = {};
	const placeWhenShown = {};
	return {
		shown,
		placeWhenShown,
		has: (ref) => entries.has(ref),
		get: (ref) => entries.get(ref),
		setVisible: (ref, visible) => {
			shown[ref] = visible;
			if (visible) placeWhenShown[ref] = entries.get(ref).el.firstElementChild.scrollTop;
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

test("dock: a tab chosen again in its group is where it was scrolled", () => {
	const views = viewsOf(["a", "b"]);
	const container = document.createElement("div");
	document.body.append(container);
	const dock = new DockView(hostWith(views), container);
	try {
		dock.render(group(["a", "b"], { active: "a" }));
		const scroller = views.get("a").el.firstElementChild;
		scroller.scrollTop = 120;
		dock.render(group(["a", "b"], { active: "a" }));
		assertEqual(scroller.scrollTop, 120, "the tree built again: kept");
		dock.render(group(["a", "b"], { active: "b" }));
		dock.render(group(["a", "b"], { active: "a" }));
		assertEqual(scroller.scrollTop, 120, "another tab chosen, then this one: kept");
		assertEqual(views.placeWhenShown.a, 120, "told it is shown once its place is back");
	}
	finally {
		dock.dispose();
		container.remove();
	}
});

test("scroll places: a view moved while hidden gets its places back once it is shown", () => {
	const places = new ScrollPlaces();
	const view = scrollingView();
	const scroller = view.firstElementChild;
	const parent = document.createElement("div");
	document.body.append(parent);
	try {
		parent.append(view);
		scroller.scrollTop = 120;
		places.note([view]);
		view.hidden = true;
		const moved = document.createElement("div");
		moved.append(view);
		parent.replaceChildren(moved);
		places.note([view]);
		places.restore([view]);
		view.hidden = false;
		assertEqual(scroller.scrollTop, 0, "moved while hidden: the browser kept nothing, and a hidden view is given nothing");
		places.restore([view]);
		assertEqual(scroller.scrollTop, 120, "shown: its place back");
		scroller.scrollTop = 30;
		places.restore([view]);
		assertEqual(scroller.scrollTop, 30, "given once: later scrolls are the owner's");
	}
	finally {
		parent.remove();
	}
});

test("scroll places: an element at its end stays at its end, so a log shows what came while it was away", () => {
	const places = new ScrollPlaces();
	const view = scrollingView();
	const scroller = view.firstElementChild;
	document.body.append(view);
	try {
		scroller.scrollTop = scroller.scrollHeight;
		places.note([view]);
		view.hidden = true;
		scroller.firstElementChild.style.height = "800px";
		view.hidden = false;
		places.restore([view]);
		assertEqual(scroller.scrollTop, 800 - 50, "at the new end");
	}
	finally {
		view.remove();
	}
});
