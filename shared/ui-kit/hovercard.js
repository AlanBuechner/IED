// hovercard.js - a card of rich content for an element, where a `title` tooltip would say it in plain text: shown while
// the pointer rests on the element (after a short delay) or it has keyboard focus, above or below it as there is room and
// kept inside the window; the pointer can move into the card. Escape, a click or leaving hides it. A touch has no hover,
// so nothing shows for one.
//     const card = hoverCard(el, () => contentOrNull);   card.refresh() after the content changed;   card.dispose()

import { h } from "./dom.js";

const SHOW_MS = 350;
const HIDE_MS = 150;
const GAP = 6;
const MARGIN = 4;
let cards = 0;

export function hoverCard(anchor, build) {
	const id = "hover-card-" + ++cards;
	let el = null;
	let showTimer = 0;
	let hideTimer = 0;
	const clear = () => {
		clearTimeout(showTimer);
		clearTimeout(hideTimer);
	};
	const hide = () => {
		clear();
		if (!el) return;
		el.remove();
		el = null;
		anchor.removeAttribute("aria-describedby");
		document.removeEventListener("keydown", onKey, true);
	};
	const show = () => {
		clear();
		const content = build();
		if (!content) {
			hide();
			return;
		}
		if (!el) {
			el = h("div.popup.hover-card", { id, role: "tooltip", on: { pointerenter: clear, pointerleave: later } });
			document.body.append(el);
			anchor.setAttribute("aria-describedby", id);
			document.addEventListener("keydown", onKey, true);
		}
		el.replaceChildren(content);
		place(el, anchor);
	};
	const later = () => {
		clearTimeout(showTimer);
		hideTimer = setTimeout(hide, HIDE_MS);
	};
	const onKey = (event) => {
		if (event.key === "Escape") hide();
	};
	// after a click, until the pointer leaves: what the click opened is shown, and an anchor drawn again under the pointer
	// is entered again without the pointer moving
	let pressed = false;
	const listeners = {
		pointerenter: (event) => {
			if (event.pointerType === "touch" || pressed) return;
			clearTimeout(hideTimer);
			if (!el) showTimer = setTimeout(show, SHOW_MS);
		},
		pointerleave: (event) => {
			const at = anchor.getBoundingClientRect();
			if (event.clientX < at.left || event.clientX >= at.right || event.clientY < at.top || event.clientY >= at.bottom) pressed = false;
			later();
		},
		pointerdown: () => {
			pressed = true;
			hide();
		},
		focusin: () => !pressed && anchor.matches(":focus-visible") && show(),
		focusout: hide,
	};
	for (const [name, fn] of Object.entries(listeners)) anchor.addEventListener(name, fn);
	return {
		refresh: () => el && show(),
		get open() {
			return el !== null;
		},
		dispose: () => {
			hide();
			for (const [name, fn] of Object.entries(listeners)) anchor.removeEventListener(name, fn);
		},
	};
}

// Above the anchor in the window's lower half, below it otherwise; its right edge on the anchor's in the right half.
function place(el, anchor) {
	const at = anchor.getBoundingClientRect();
	const card = el.getBoundingClientRect();
	const above = at.top + at.height / 2 > window.innerHeight / 2;
	const top = above ? at.top - card.height - GAP : at.bottom + GAP;
	const left = at.left + at.width / 2 > window.innerWidth / 2 ? at.right - card.width : at.left;
	el.style.top = Math.max(MARGIN, Math.min(top, window.innerHeight - card.height - MARGIN)) + "px";
	el.style.left = Math.max(MARGIN, Math.min(left, window.innerWidth - card.width - MARGIN)) + "px";
}
