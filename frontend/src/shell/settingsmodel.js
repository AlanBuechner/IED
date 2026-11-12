// settingsmodel.js - what the settings window shows (docs/07 "The settings window"): the pages built from the settings
// plugins declare (each under its contribution's page) and the pages plugins draw themselves (settingsPages), a
// setting's title and control from its JSON Schema, the tiers it may be set in, and search.

export const TIERS = [["app", "User"], ["solution", "Solution"], ["project", "Project"]];

// "cpp.clangd.path" -> "Clangd Path"; "ai.maxSteps" -> "Max Steps".
// The words of key parts that read in capitals: board.ai.model is AI Model.
const ACRONYMS = new Set(["ai", "lsp", "mcp", "gpu", "url", "ui"]);

function words(part) {
	return part.replace(/([a-z])([A-Z])/g, "$1 $2").split(" ").map((word) => ACRONYMS.has(word.toLowerCase()) ? word.toUpperCase() : word.charAt(0).toUpperCase() + word.slice(1)).join(" ");
}

export function settingTitle(key) {
	const parts = key.split(".");
	const rest = parts.length > 1 ? parts.slice(1) : parts;
	return rest.map(words).join(" ");
}

function hidden(entry) {
	return Boolean(entry && entry.schema && entry.schema["x-ied-hidden"]);
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
	// A key whose schema says "x-ied-hidden" (one kept only so old settings files still read, ai.groups) is not shown.
	for (const key of Object.keys(settings).sort()) if (!claimed.has(key) && !hidden(settings[key])) page(pageOf(key, settings[key])).keys.push(key);
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

// --- Groups inside a page (owner, 2026-11-11: "we should also be able to colapse the groups") ----------------------

// The group of a page `key` is drawn in: the schema's "x-ied-group" when it names one, else the key's middle part
// ("ai.chats.keepDeletedDays" -> "Chats", "cpp.clangd.path" -> "Clangd"); a key of two parts ("ai.maxSteps") is in the
// page's general group, "".
export function groupOf(key, entry) {
	const named = entry && entry.schema && entry.schema["x-ied-group"];
	if (typeof named === "string" && named.trim()) return named.trim();
	const parts = key.split(".");
	if (parts.length < 3) return "";
	return parts.slice(1, -1).map(words).join(" ");
}

// A page's keys in its groups: [{id, title, keys}], the general group ("General") first, then the others by title; a
// page whose keys are all in one group has that group alone.
export function pageGroups(keys, settings) {
	const groups = new Map();
	for (const key of keys) {
		const name = groupOf(key, settings[key]);
		if (!groups.has(name)) groups.set(name, []);
		groups.get(name).push(key);
	}
	return [...groups.entries()]
		.sort(([a], [b]) => (a === "" ? -1 : b === "" ? 1 : a.localeCompare(b)))
		.map(([name, list]) => ({ id: (name || "general").toLowerCase().replace(/[^a-z0-9]+/g, "-"), title: name || "General", keys: list }));
}

// Whether `key` has a value set in some tier (its value is not the default).
export function isModified(values, key) {
	const current = values && values[key];
	return Boolean(current && current.source && current.source !== "default");
}

// What a search finds, by page: [{page, keys}] in the tree's order, a key under the page that shows it.
export function searchByPage(pages, settings, query) {
	const out = [];
	const words = query.trim().toLowerCase();
	for (const top of pages) {
		for (const node of [top, ...top.children]) {
			const titled = words && (node.title.toLowerCase().includes(words) || (node !== top && top.title.toLowerCase().includes(words)));
			const keys = node.keys.filter((key) => (settings[key] && matches(key, settings[key], query)) || (titled && !node.view));
			if (keys.length || (titled && node.view)) out.push({ page: node, parent: node === top ? null : top, keys });
		}
	}
	return out;
}
