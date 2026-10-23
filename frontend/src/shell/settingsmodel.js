// settingsmodel.js - what the settings window shows (docs/07 "The settings window"): the pages built from the settings
// plugins declare (each under its contribution's page) and the pages plugins draw themselves (settingsPages), a
// setting's title and control from its JSON Schema, the tiers it may be set in, and search.

export const TIERS = [["app", "User"], ["solution", "Solution"], ["project", "Project"]];

// "cpp.clangd.path" -> "Clangd Path"; "ai.maxSteps" -> "Max Steps".
export function settingTitle(key) {
	const parts = key.split(".");
	const rest = parts.length > 1 ? parts.slice(1) : parts;
	return rest.map((part) => part.replace(/([a-z])([A-Z])/g, "$1 $2")).map((part) => part.charAt(0).toUpperCase() + part.slice(1)).join(" ");
}

function pageOf(key, entry) {
	if (entry.page) return entry.page;
	const head = key.split(".")[0];
	return head.charAt(0).toUpperCase() + head.slice(1);
}

// The tree: [{id, title, keys, children: [{id, title, keys, view?, plugin?}]}], pages by title, each page's own keys
// without those a contributed page edits. A page named "Text Editor/C/C++" is "C/C++" under "Text Editor" (the
// first slash nests).
export function settingsPages(settings, contributed) {
	const pages = new Map();
	const top = (title) => {
		if (!pages.has(title)) pages.set(title, { id: "page:" + title, title, keys: [], children: [] });
		return pages.get(title);
	};
	const page = (path) => {
		const slash = path.indexOf("/");
		if (slash < 0) return top(path);
		const parent = top(path.slice(0, slash));
		let child = parent.children.find((c) => c.id === "page:" + path);
		if (!child) {
			child = { id: "page:" + path, title: path.slice(slash + 1), keys: [], children: [] };
			parent.children.push(child);
		}
		return child;
	};
	const claimed = new Set();
	for (const { plugin, value } of contributed) {
		page(value.page).children.push({ id: "custom:" + value.id, title: value.title, keys: value.keys.slice(), view: value.view, plugin });
		for (const key of value.keys) claimed.add(key);
	}
	for (const key of Object.keys(settings).sort()) if (!claimed.has(key)) page(pageOf(key, settings[key])).keys.push(key);
	const sorted = [...pages.values()].sort((a, b) => a.title.localeCompare(b.title));
	for (const p of sorted) p.children.sort((a, b) => Number(Boolean(b.view)) - Number(Boolean(a.view)) || (a.view ? 0 : a.title.localeCompare(b.title)));
	return sorted;
}

// The plugin's own page that edits `key` (it decides how and whether to show it), or null.
export function pageOwning(pages, key) {
	for (const p of pages) {
		const child = p.children.find((c) => c.view && c.keys.includes(key));
		if (child) return child;
	}
	return null;
}

// The node with `id` in the tree, or null.
export function findPage(pages, id) {
	for (const p of pages) {
		if (p.id === id) return p;
		const child = p.children.find((c) => c.id === id);
		if (child) return child;
	}
	return null;
}

// How a value is edited: "boolean", "enum", "integer", "number", "string" or "json" (anything else).
export function controlKind(schema) {
	if (!schema) return "json";
	if (Array.isArray(schema.enum)) return "enum";
	const type = schema.type;
	if (type === "boolean" || type === "integer" || type === "number" || type === "string") return type;
	return "json";
}

// A control's text as the value it means; throws an Error saying what is wrong.
export function parseInput(kind, text) {
	if (kind === "integer" || kind === "number") {
		const value = Number(text.trim());
		if (text.trim() === "" || !Number.isFinite(value)) throw new Error("not a number");
		if (kind === "integer" && !Number.isInteger(value)) throw new Error("not a whole number");
		return value;
	}
	if (kind === "json") {
		try {
			return JSON.parse(text);
		}
		catch (e) {
			throw new Error("not JSON: " + e.message);
		}
	}
	return text;
}

// A value as a control shows it.
export function inputText(kind, value) {
	if (kind === "json") return JSON.stringify(value === undefined ? null : value, null, 2);
	return value === undefined || value === null ? "" : String(value);
}

// Whether `key` matches a search: every word in its key, title or description.
export function matches(key, entry, query) {
	const words = query.toLowerCase().split(/\s+/).filter(Boolean);
	const text = (key + " " + settingTitle(key) + " " + ((entry.schema && entry.schema.description) || "")).toLowerCase();
	return words.every((word) => text.includes(word));
}

// "User" for app, "Solution", "Project", "Default".
export function sourceLabel(source) {
	const tier = TIERS.find(([id]) => id === source);
	return tier ? tier[1] : "Default";
}

// The problems a tier file has for `key`, with their lines.
export function problemsFor(tiers, key) {
	const out = [];
	for (const [tier, state] of Object.entries(tiers || {})) for (const problem of (state && state.problems) || []) if (problem.key === key) out.push(Object.assign({ tier }, problem));
	return out;
}
