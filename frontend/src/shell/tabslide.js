// tabslide.js - a tab dragged along its own strip, as in a web browser (docs/15 "Docking"): it follows the pointer
// within the strip and the tabs it passes slide aside to make its room; let go, it takes the place it reached. Only the
// elements move here - the dock then moves the tab in the layout. Dragged off the strip, the dock docks it instead.

const LANE_PX = 24;   // how far above or below the strip the pointer may stray and still slide

export class TabSlide {
	// `tab`: the dragged tab's element in `strip`; `x`: where the drag started.
	constructor(strip, tab, x) {
		this.strip = strip;
		this.tab = tab;
		this.tabs = [...strip.querySelectorAll(":scope > [data-ref]")];
		this.from = this.tabs.indexOf(tab);
		this.rects = this.tabs.map((el) => el.getBoundingClientRect());
		this.width = this.rects[this.from].width;
		this.startX = x;
		this.at = this.from;
		strip.classList.add("sliding");
		tab.classList.add("lifted");
	}

	// Whether (x, y) is still along the strip.
	holds(x, y) {
		const rect = this.strip.getBoundingClientRect();
		return y >= rect.top - LANE_PX && y <= rect.bottom + LANE_PX && x >= rect.left - LANE_PX && x <= rect.right + LANE_PX;
	}

	// The tab under the pointer at `x`, kept within the strip's tabs; those whose middle its leading edge passed make room
	// (so at either end it has passed them all, however wide it is).
	move(x) {
		const own = this.rects[this.from];
		const first = this.rects[0];
		const last = this.rects[this.rects.length - 1];
		const dx = Math.max(first.left - own.left, Math.min(last.right - own.right, x - this.startX));
		this.tab.style.transform = "translateX(" + dx + "px)";
		this.at = this.from;
		this.tabs.forEach((el, i) => {
			if (i === this.from) return;
			const middle = this.rects[i].left + this.rects[i].width / 2;
			const passed = i > this.from ? own.right + dx > middle : own.left + dx < middle;
			if (passed) this.at = i > this.from ? Math.max(this.at, i) : Math.min(this.at, i);
			el.style.transform = passed ? "translateX(" + (i > this.from ? -this.width : this.width) + "px)" : "";
		});
	}

	// The ref the tab now goes before (null: the end of the strip), or undefined when it kept its place.
	before() {
		if (this.at === this.from) return undefined;
		const others = this.tabs.filter((el) => el !== this.tab).map((el) => el.dataset.ref);
		return this.at < others.length ? others[this.at] : null;
	}

	end() {
		this.strip.classList.remove("sliding");
		this.tab.classList.remove("lifted");
		for (const el of this.tabs) el.style.transform = "";
	}
}
