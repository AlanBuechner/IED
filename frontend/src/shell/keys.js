// keys.js - key names (docs/15 "Commands and keys"): "[ctrl+][shift+][alt+][meta+]<key>", a chord being two of them
// separated by a space. <key> is the physical key (KeyboardEvent.code) named as on a US keyboard, so Shift+Minus is
// "shift+-" whatever character it types.

export class KeyError extends Error {}

const MODIFIERS = ["ctrl", "shift", "alt", "meta"];

const CODE_NAMES = {
	Backquote: "`", Minus: "-", Equal: "=", BracketLeft: "[", BracketRight: "]", Backslash: "\\", Semicolon: ";",
	Quote: "'", Comma: ",", Period: ".", Slash: "/", Enter: "enter", NumpadEnter: "enter", Escape: "escape", Tab: "tab",
	Space: "space", Backspace: "backspace", Delete: "delete", Insert: "insert", Home: "home", End: "end",
	PageUp: "pageup", PageDown: "pagedown", ArrowUp: "up", ArrowDown: "down", ArrowLeft: "left", ArrowRight: "right",
	Pause: "break", Cancel: "break",
};

const LABELS = {
	"`": "`", "-": "-", "=": "=", enter: "Enter", escape: "Esc", tab: "Tab", space: "Space", backspace: "Backspace",
	delete: "Del", insert: "Ins", home: "Home", end: "End", pageup: "PgUp", pagedown: "PgDn", up: "Up", down: "Down",
	left: "Left", right: "Right", break: "Break",
};

const KEY_NAMES = new Set([...Object.values(CODE_NAMES), ..."abcdefghijklmnopqrstuvwxyz0123456789"]);
for (let i = 1; i <= 24; i++) KEY_NAMES.add("f" + i);

// The key name of a KeyboardEvent.code; null for a modifier or a key without a name.
export function keyOfCode(code) {
	if (CODE_NAMES[code]) return CODE_NAMES[code];
	let match = /^Key([A-Z])$/.exec(code);
	if (match) return match[1].toLowerCase();
	match = /^Digit([0-9])$/.exec(code);
	if (match) return match[1];
	match = /^F([0-9]{1,2})$/.exec(code);
	if (match && Number(match[1]) <= 24) return "f" + match[1];
	return null;
}

// A keydown as a normalized key ("ctrl+shift+p"); null for a press of a modifier alone.
export function keyOfEvent(event) {
	const key = keyOfCode(event.code);
	if (!key) return null;
	const parts = [];
	if (event.ctrlKey) parts.push("ctrl");
	if (event.shiftKey) parts.push("shift");
	if (event.altKey) parts.push("alt");
	if (event.metaKey) parts.push("meta");
	parts.push(key);
	return parts.join("+");
}

// One key in normal form: modifiers in the order ctrl, shift, alt, meta. Throws KeyError for anything else.
export function normalizeKey(text) {
	const parts = text.trim().toLowerCase().split("+");
	const key = parts.pop();
	if (!KEY_NAMES.has(key)) throw new KeyError("unknown key '" + key + "' in '" + text + "'");
	const seen = new Set();
	for (const part of parts) {
		const name = part === "cmd" || part === "win" ? "meta" : part === "control" ? "ctrl" : part;
		if (!MODIFIERS.includes(name)) throw new KeyError("unknown modifier '" + part + "' in '" + text + "'");
		if (seen.has(name)) throw new KeyError("'" + name + "' twice in '" + text + "'");
		seen.add(name);
	}
	return [...MODIFIERS.filter((m) => seen.has(m)), key].join("+");
}

// A binding's keys: one key, or the two of a chord. Throws KeyError.
export function parseBinding(text) {
	const keys = (text || "").trim().split(/\s+/).filter(Boolean);
	if (keys.length < 1 || keys.length > 2) throw new KeyError("a binding is one key or a chord of two: '" + text + "'");
	return keys.map(normalizeKey);
}

// "Ctrl+Shift+P", "Ctrl+K, Ctrl+D".
export function formatBinding(keys) {
	return keys.map(formatKey).join(", ");
}

export function formatKey(key) {
	return key.split("+").map((part) => {
		if (MODIFIERS.includes(part)) return part === "meta" ? "Win" : part[0].toUpperCase() + part.slice(1);
		if (LABELS[part]) return LABELS[part];
		return part.toUpperCase();
	}).join("+");
}

// The browser keeps these in a tab (docs/15); a binding on one works only in an installed app window.
const BROWSER_RESERVED = new Set(["ctrl+w", "ctrl+t", "ctrl+n", "ctrl+shift+t", "ctrl+shift+n", "ctrl+shift+w", "ctrl+tab", "ctrl+shift+tab", "ctrl+pageup", "ctrl+pagedown"]);

export function isBrowserReserved(key) {
	return BROWSER_RESERVED.has(key);
}

// A key typed into a text field: no modifier but Shift, and not an F-key or Escape.
export function belongsToTextField(key) {
	const parts = key.split("+");
	const name = parts[parts.length - 1];
	if (/^f[0-9]+$/.test(name) || name === "escape") return false;
	return parts.slice(0, -1).every((m) => m === "shift");
}
