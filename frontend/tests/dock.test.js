// dock.test.js - a dock host on screen (docs/15 "Docking"): its views hear when they are shown and hidden, as the tab
// chosen in a group and as the whole dock when its activity goes away and comes back; and each view keeps its scroll
// places while its element is moved or hidden (scrollplaces.js), one at its end staying at its end.

import { test, assertEqual, assertTrue } from "./harness.js";
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

// The views of `refs`; `shown` holds what each was last told, `placeWhenShown` where its scroller was when told,
// `mounted` the refs whose views were asked for (get mounts a view that is not yet).
function viewsOf(refs) {
	const entries = new Map(refs.map((ref) => [ref, { ref, el: scrollingView(), title: ref, icon: "$(file)", base: null, dirty: false }]));
	const shown = {};
	const placeWhenShown = {};
	const mounted = new Set();
	return {
		shown,
		placeWhenShown,
		mounted,
		has: (ref) => entries.has(ref),
		get: (ref) => {
			mounted.add(ref);
			return entries.get(ref);
		},
		titleOf: (ref) => "Title of " + ref,
		setVisible: (ref, visible) => {
			shown[ref] = visible;
			if (visible) placeWhenShown[ref] = entries.get(ref).el.firstElementChild.scrollTop;
		},
	};
}

function hostWith(views, tethers) {
	const none = () => {};
	return {
		views, menus: { show: none, host: null }, emptyWell: () => document.createElement("div"), activate: none, tetheredWith: (ref) => (tethers && tethers[ref]) || [], close: none,
		pin: none, move: none, resize: none, autoHide: none, resizeAutoHidden: none, hold: none, focused: none, newTab: null, newTabTitle: "New Tab",
		titleActions: () => [],
	};
}

// A host whose activity opens new tabs: `menus` collects what the "+" right button asks for, `opened` the new tabs.
function hostWithNewTab(views) {
	const menus = [];
	const opened = [];
	return {
		host: Object.assign(hostWith(views), {
			newTab: (path, active) => opened.push({ path, active }),
			newTabTitle: "New File",
			newTabMenu: (path, active, at) => menus.push({ path, active, at }),
		}),
		menus,
		opened,
	};
}

// A right button press on an element, as the browser would send one.
function rightClick(el) {
	el.dispatchEvent(new MouseEvent("contextmenu", { bubbles: true, cancelable: true, button: 2 }));
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

test("dock: the strip's + opens a new tab of its group with a click, its right button the menu of other ways", () => {
	const views = viewsOf(["a", "b"]);
	const { host, menus, opened } = hostWithNewTab(views);
	const container = document.createElement("div");
	const dock = new DockView(host, container);
	try {
		dock.render(group(["a", "b"], { active: "b", well: true }));
		const plus = container.querySelector(".tabs .tabs-new");
		assertEqual(plus.title, "New File", "the button says what a click does");
		plus.click();
		assertEqual(opened, [{ path: [], active: "b" }], "a click opens the group's new tab");
		rightClick(plus);
		assertEqual(opened.length, 1, "and the right button opens none");
		assertEqual(menus.length, 1, "the right button asks for the menu");
		assertEqual([menus[0].path, menus[0].active], [[], "b"], "for this group");
		assertTrue(menus[0].at === plus, "hung under the button");
	}
	finally {
		dock.dispose();
	}
});

test("dock: an empty well's + has the same menu", () => {
	const views = viewsOf([]);
	const { host, menus, opened } = hostWithNewTab(views);
	const container = document.createElement("div");
	const dock = new DockView(host, container);
	try {
		dock.render(group([], {}));   // nothing of the activity shown: the empty well's own strip
		const plus = container.querySelector(".tabs .tabs-new");
		rightClick(plus);
		assertEqual(menus.length, 1, "right button: the menu");
		assertEqual(menus[0].path, [], "for the well's own group");
		plus.click();
		assertEqual(opened, [{ path: [], active: null }], "a click still opens one");
	}
	finally {
		dock.dispose();
	}
});

test("dock: a tab's tether names its partner without mounting the partner's view", () => {
	const views = viewsOf(["a"]);
	const container = document.createElement("div");
	const dock = new DockView(hostWith(views, { a: ["gone"] }), container);
	try {
		dock.render(group(["a"], { active: "a", well: true }));
		const glyph = container.querySelector(".tether");
		assertEqual(glyph && glyph.title, "Tethered with Title of gone", "the partner named by its title");
		assertEqual([...views.mounted], ["a"], "only the open tab's view: a partner no longer open is not made again");
	}
	finally {
		dock.dispose();
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
