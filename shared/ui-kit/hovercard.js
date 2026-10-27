// hovercard.js - a card of rich content for an element, where a `title` tooltip would say it in plain text: shown while
// the pointer rests on the element (after a short delay) or it has keyboard focus, above or below it as there is room and
// kept inside the window; the pointer can move into the card. Escape, a click or leaving hides it. A touch has no hover,
// so nothing shows for one.
//     const card = hoverCard(el, () => contentOrNull);   card.refresh() after the content changed;   card.dispose()
// With `pinned`, toggle() - the anchor's click, a tap on a phone - shows a second card that stays: a dialog anchored the
// same way, which Escape, a click outside it or toggle() again closes; focus goes into it and back to the anchor. No hover
// card shows while it is open.
//     const card = hoverCard(el, () => brief, { pinned: () => detailed, label: "Claude usage" });   card.toggle()

import { h } from "./dom.js";

const SHOW_MS = 350;
const HIDE_MS = 150;
const GAP = 6;
const MARGIN = 4;
let cards = 0;

export function hoverCard(anchor, build, options = {}) {
	const id = "hover-card-" + ++cards;
	let el = null;
	let pinnedEl = null;
	let showTimer = 0;
	let hideTimer = 0;
	let quietFocus = false;   // the anchor focused again as the pinned card closed: no hover card for it
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
		const content = pinnedEl ? null : build();
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
	const drawPinned = () => {
		const content = options.pinned ? options.pinned() : null;
		if (!content) {
			unpin(false);
			return false;
		}
		pinnedEl.replaceChildren(content);
		place(pinnedEl, anchor);
		return true;
	};
	const onOutside = (event) => {
		if (pinnedEl && !pinnedEl.contains(event.target) && !anchor.contains(event.target)) unpin(false);
	};
	const onPinnedKey = (event) => {
		if (event.key !== "Escape") return;
		event.preventDefault();
		event.stopPropagation();
		unpin(true);
	};
	const pin = () => {
		hide();
		pinnedEl = h("div.popup.hover-card.pinned", { id: id + "-pinned", role: "dialog", tabindex: "-1", "aria-label": options.label || null, on: { keydown: onPinnedKey } });
		document.body.append(pinnedEl);
		if (!drawPinned()) return;
		document.addEventListener("pointerdown", onOutside, true);
		anchor.setAttribute("aria-expanded", "true");
		const first = pinnedEl.querySelector("button, [href], input, [tabindex]:not([tabindex='-1'])");
		(first || pinnedEl).focus({ preventScroll: true });
	};
	const unpin = (refocus) => {
		if (!pinnedEl) return;
		pinnedEl.remove();
		pinnedEl = null;
		document.removeEventListener("pointerdown", onOutside, true);
		anchor.setAttribute("aria-expanded", "false");
		if (!refocus || !anchor.isConnected) return;
		quietFocus = true;
		anchor.focus({ preventScroll: true });
		quietFocus = false;
	};
	// after a click, until the pointer leaves: what the click opened is shown, and an anchor drawn again under the pointer
	// is entered again without the pointer moving
	let pressed = false;
	const listeners = {
		pointerenter: (event) => {
			if (event.pointerType === "touch" || pressed || pinnedEl) return;
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
		focusin: () => !pressed && !quietFocus && !pinnedEl && anchor.matches(":focus-visible") && show(),
		focusout: hide,
	};
	for (const [name, fn] of Object.entries(listeners)) anchor.addEventListener(name, fn);
	if (options.pinned) anchor.setAttribute("aria-expanded", "false");
	return {
		refresh: () => {
			if (el) show();
			if (pinnedEl) drawPinned();
		},
		toggle: () => {
			if (pinnedEl) unpin(true);
			else if (options.pinned) pin();
		},
		close: () => unpin(false),
		get open() {
			return el !== null;
		},
		get pinned() {
			return pinnedEl !== null;
		},
		dispose: () => {
			hide();
			unpin(false);
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
