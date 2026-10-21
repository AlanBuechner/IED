// keys.test.js - key names: events, normal form, chords, labels, browser-reserved keys, text fields.

import { test, assertEqual, assertTrue, assertThrows } from "./harness.js";
import { keyOfCode, keyOfEvent, normalizeKey, parseBinding, formatBinding, isBrowserReserved, belongsToTextField, KeyError } from "../src/shell/keys.js";

function press(code, mods) {
	const m = mods || {};
	return { code, ctrlKey: !!m.ctrl, shiftKey: !!m.shift, altKey: !!m.alt, metaKey: !!m.meta };
}

test("keys: codes name physical keys", () => {
	assertEqual(keyOfCode("KeyP"), "p");
	assertEqual(keyOfCode("Digit7"), "7");
	assertEqual(keyOfCode("F12"), "f12");
	assertEqual(keyOfCode("F24"), "f24");
	assertEqual(keyOfCode("Backquote"), "`");
	assertEqual(keyOfCode("Minus"), "-");
	assertEqual(keyOfCode("Period"), ".");
	assertEqual(keyOfCode("ArrowDown"), "down");
	assertEqual(keyOfCode("PageDown"), "pagedown");
	assertEqual(keyOfCode("NumpadEnter"), "enter");
	assertEqual(keyOfCode("Pause"), "break");
	assertEqual(keyOfCode("ShiftLeft"), null, "a modifier");
	assertEqual(keyOfCode("F25"), null);
});

test("keys: events in normal form", () => {
	assertEqual(keyOfEvent(press("KeyP", { ctrl: true, shift: true })), "ctrl+shift+p");
	assertEqual(keyOfEvent(press("KeyP", { meta: true, alt: true, shift: true, ctrl: true })), "ctrl+shift+alt+meta+p");
	assertEqual(keyOfEvent(press("Minus", { shift: true })), "shift+-", "the key, not the character it types");
	assertEqual(keyOfEvent(press("F5")), "f5");
	assertEqual(keyOfEvent(press("ControlLeft", { ctrl: true })), null, "a modifier alone");
});

test("keys: bindings normalize modifiers in any order and case", () => {
	assertEqual(normalizeKey("Shift+Ctrl+P"), "ctrl+shift+p");
	assertEqual(normalizeKey("alt+shift+."), "shift+alt+.");
	assertEqual(normalizeKey("cmd+k"), "meta+k");
	assertEqual(normalizeKey(" ctrl+` "), "ctrl+`");
	assertEqual(parseBinding("ctrl+k ctrl+d"), ["ctrl+k", "ctrl+d"]);
	assertEqual(parseBinding("f12"), ["f12"]);
});

test("keys: malformed bindings are refused", () => {
	for (const bad of ["", "ctrl+", "ctrl+banana", "hyper+k", "ctrl+ctrl+k", "a b c", "ctrl+k ctrl+"]) assertThrows(() => parseBinding(bad), KeyError, bad);
});

test("keys: labels", () => {
	assertEqual(formatBinding(["ctrl+shift+p"]), "Ctrl+Shift+P");
	assertEqual(formatBinding(["ctrl+k", "ctrl+d"]), "Ctrl+K, Ctrl+D");
	assertEqual(formatBinding(["ctrl+`"]), "Ctrl+`");
	assertEqual(formatBinding(["shift+f12"]), "Shift+F12");
	assertEqual(formatBinding(["ctrl+alt+pagedown"]), "Ctrl+Alt+PgDn");
	assertEqual(formatBinding(["escape"]), "Esc");
});

test("keys: browser-reserved keys", () => {
	assertTrue(isBrowserReserved("ctrl+w") && isBrowserReserved("ctrl+tab") && isBrowserReserved("ctrl+shift+t"));
	assertTrue(!isBrowserReserved("ctrl+s") && !isBrowserReserved("f5") && !isBrowserReserved("ctrl+alt+tab"));
});

test("keys: what a text field keeps", () => {
	assertTrue(belongsToTextField("a") && belongsToTextField("shift+a") && belongsToTextField("enter") && belongsToTextField("backspace"));
	assertTrue(!belongsToTextField("f5") && !belongsToTextField("shift+f12") && !belongsToTextField("escape"));
	assertTrue(!belongsToTextField("ctrl+s") && !belongsToTextField("alt+enter"));
});
