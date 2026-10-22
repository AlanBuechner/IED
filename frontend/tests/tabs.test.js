// tabs.test.js - tabs as in a web browser (docs/15 "Docking"): a tab dragged along its strip slides past the tabs whose
// middle its leading edge crosses and lands where it was let go; new files are named Untitled-N.txt, the first N free.

import { test, assertEqual } from "./harness.js";
import { TabSlide } from "../src/shell/tabslide.js";
import { freeName, isUntitled } from "/plugins/ied.editor/ui/untitled.js";

// A strip of tabs `widths` wide, laid out at the page's left edge.
function strip(widths) {
	const el = document.createElement("div");
	el.className = "tabs";
	el.style.cssText = "position: fixed; left: 0; top: 0; display: flex; height: 26px;";
	for (const [i, width] of widths.entries()) {
		const tab = document.createElement("div");
		tab.className = "tab";
		tab.dataset.ref = "doc:" + i;
		tab.style.cssText = "flex: none; min-width: 0; width: " + width + "px;";
		el.append(tab);
	}
	document.body.append(el);
	return el;
}

test("tabs: a tab dragged along its strip slides past the tabs it reaches", () => {
	const el = strip([200, 80, 80]);
	try {
		const tabs = [...el.children];
		const slide = new TabSlide(el, tabs[0], 100);
		slide.move(100);
		assertEqual(slide.before(), undefined, "not moved: kept its place");
		slide.move(150);
		assertEqual([slide.before(), tabs[1].style.transform], ["doc:2", "translateX(-200px)"], "its right edge passed the second tab's middle");
		slide.move(1000);
		assertEqual([slide.before(), tabs[0].style.transform], [null, "translateX(160px)"], "at the end, however wide it is; kept within the strip");
		assertEqual(slide.holds(50, 40) && !slide.holds(50, 80), true, "the strip's lane");
		slide.end();
		assertEqual([tabs.map((t) => t.style.transform).join(""), el.classList.contains("sliding")], ["", false]);
		const back = new TabSlide(el, tabs[2], 330);
		back.move(250);
		assertEqual(back.before(), "doc:1", "leftwards, before the tab its left edge passed");
		back.move(-500);
		assertEqual(back.before(), "doc:0");
		back.end();
	}
	finally {
		el.remove();
	}
});

test("tabs: a new file is the first Untitled-N.txt not taken", () => {
	assertEqual([freeName([]), freeName(["Untitled-1.txt", "Untitled-3.txt", "notes.txt"]), freeName(["Untitled-1.txt", "Untitled-2.txt"])], ["Untitled-1.txt", "Untitled-2.txt", "Untitled-3.txt"]);
	assertEqual([isUntitled(".ied/local/untitled/Untitled-1.txt"), isUntitled("src/Untitled-1.txt"), isUntitled("")], [true, false, false]);
});
