// hovercard.test.js - a hover card shows after the pointer rests on its anchor, follows its content, hides on leaving,
// a click or Escape, stays hidden after a click until the pointer leaves, and never shows for a touch.

import { test, assertEqual } from "./harness.js";
import { hoverCard } from "/ui-kit/hovercard.js";

const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const pointer = (el, type, kind, x, y) => el.dispatchEvent(new PointerEvent(type, { pointerType: kind || "mouse", clientX: x || 0, clientY: y || 0 }));
const shown = () => document.querySelector(".hover-card");

test("hovercard: shows after a rest, follows its content, hides on leaving", async () => {
	const anchor = document.body.appendChild(Object.assign(document.createElement("button"), { textContent: "item" }));
	let text = "first";
	const card = hoverCard(anchor, () => (text ? Object.assign(document.createElement("span"), { textContent: text }) : null));
	try {
		pointer(anchor, "pointerenter");
		assertEqual(shown(), null, "not at once");
		await wait(450);
		assertEqual([shown() && shown().textContent, anchor.getAttribute("aria-describedby") === shown().id, card.open], ["first", true, true]);
		text = "second";
		card.refresh();
		assertEqual(shown().textContent, "second", "drawn again");
		pointer(anchor, "pointerleave", "mouse", -50, -50);
		await wait(250);
		assertEqual([shown(), anchor.hasAttribute("aria-describedby")], [null, false]);
		pointer(anchor, "pointerenter");
		await wait(450);
		document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape" }));
		assertEqual(shown(), null, "Escape");
		text = "";
		pointer(anchor, "pointerleave", "mouse", -50, -50);
		pointer(anchor, "pointerenter");
		await wait(450);
		assertEqual(shown(), null, "no content, no card");
	}
	finally {
		card.dispose();
		anchor.remove();
	}
});

test("hovercard: toggle() pins the click's card - a dialog no hover card shows over - until Escape, a click outside or toggle()", async () => {
	const anchor = document.body.appendChild(Object.assign(document.createElement("button"), { textContent: "item" }));
	let detail = "details";
	const card = hoverCard(anchor, () => Object.assign(document.createElement("span"), { textContent: "brief" }), {
		pinned: () => (detail ? Object.assign(document.createElement("button"), { textContent: detail }) : null), label: "Usage" });
	const pinned = () => document.querySelector(".hover-card.pinned");
	try {
		assertEqual(anchor.getAttribute("aria-expanded"), "false");
		card.toggle();
		assertEqual([pinned() && pinned().textContent, pinned().getAttribute("role"), pinned().getAttribute("aria-label"), anchor.getAttribute("aria-expanded"), card.pinned, document.activeElement.textContent],
			["details", "dialog", "Usage", "true", true, "details"], "shown at once, focus inside");
		pointer(anchor, "pointerenter");
		await wait(450);
		assertEqual(document.querySelectorAll(".hover-card").length, 1, "no hover card while it is pinned");
		detail = "more";
		card.refresh();
		assertEqual(pinned().textContent, "more", "drawn again");
		pinned().dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
		assertEqual([pinned(), document.activeElement, anchor.getAttribute("aria-expanded")], [null, anchor, "false"], "Escape, focus back");
		card.toggle();
		document.body.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true }));
		assertEqual(pinned(), null, "a click outside");
		card.toggle();
		anchor.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true }));
		assertEqual(pinned() !== null, true, "a press on the anchor itself leaves it to the click");
		card.toggle();
		assertEqual(pinned(), null, "toggle() again");
		detail = "";
		card.toggle();
		assertEqual([pinned(), card.pinned], [null, false], "nothing to show: no card");
	}
	finally {
		card.dispose();
		anchor.remove();
	}
});

test("hovercard: a click hides it until the pointer leaves; a touch shows nothing", async () => {
	const anchor = document.body.appendChild(Object.assign(document.createElement("button"), { textContent: "item" }));
	const card = hoverCard(anchor, () => Object.assign(document.createElement("span"), { textContent: "usage" }));
	try {
		pointer(anchor, "pointerenter");
		await wait(450);
		pointer(anchor, "pointerdown");
		assertEqual(shown(), null, "the click hides it");
		pointer(anchor, "pointerenter");
		await wait(450);
		assertEqual(shown(), null, "drawn again under the pointer: still hidden");
		pointer(anchor, "pointerleave", "mouse", -50, -50);
		pointer(anchor, "pointerenter");
		await wait(450);
		assertEqual(shown() !== null, true, "entered again after leaving");
		pointer(anchor, "pointerleave", "mouse", -50, -50);
		await wait(250);
		pointer(anchor, "pointerenter", "touch");
		await wait(450);
		assertEqual(shown(), null, "a touch");
	}
	finally {
		card.dispose();
		anchor.remove();
	}
});
