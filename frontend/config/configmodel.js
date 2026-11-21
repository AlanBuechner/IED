// configmodel.js - the config server page's model (docs/35 "The web page"): where each served setting comes from, the
// tokens as the page lists them and which IEDs follow, how a server plugin's code looks, and times. No DOM.

// How recently a reader token must have been used for its IED to count as following (it asks every 25 s).
export const FOLLOWING_MS = 90000;

// "just now", "12 s ago", "4 min ago", "3 h ago", "2 d ago", or the date.
export function ago(ms, now) {
	if (!ms) return "never";
	const passed = Math.max(0, now - ms);
	if (passed < 5000) return "just now";
	if (passed < 60000) return Math.floor(passed / 1000) + " s ago";
	if (passed < 3600000) return Math.floor(passed / 60000) + " min ago";
	if (passed < 86400000) return Math.floor(passed / 3600000) + " h ago";
	if (passed < 7 * 86400000) return Math.floor(passed / 86400000) + " d ago";
	return new Date(ms).toLocaleDateString(undefined, { year: "numeric", month: "short", day: "numeric" });
}

// The settings file's text as an object, or null when it does not read (comments are allowed in it).
export function readSettingsText(text) {
	try {
		const stripped = String(text || "").replace(/("(?:[^"\\]|\\.)*")|\/\/[^\n]*|\/\*[\s\S]*?\*\//g, (m, quoted) => quoted || "");
		const value = JSON.parse(stripped.trim() || "{}");
		return value && typeof value === "object" && !Array.isArray(value) ? value : null;
	}
	catch {
		return null;
	}
}

// Each served setting with where it comes from: settings.json, and each server plugin that published it.
// [{key, value, sources: ["settings.json", "spark.models"], members: [names] | null}], by key.
export function settingRows(served, settingsText, serverPlugins) {
	const own = readSettingsText(settingsText) || {};
	return Object.keys(served || {}).sort().map((key) => {
		const sources = [];
		if (Object.prototype.hasOwnProperty.call(own, key)) sources.push("settings.json");
		for (const plugin of serverPlugins || []) if (plugin.published && Object.prototype.hasOwnProperty.call(plugin.published, key)) sources.push(plugin.id);
		const value = served[key];
		const members = value && typeof value === "object" && !Array.isArray(value) ? Object.keys(value) : null;
		return { key, value, sources, members };
	});
}

// The tokens as the page lists them: admins first, then readers, each by name; `you` marked, a browser's own token
// (web-...) and whether a reader's IED follows now.
export function tokenRows(tokens, you, now) {
	return (tokens || []).map((t) => ({
		name: t.name,
		role: t.role,
		createdMs: t.createdMs || 0,
		lastUsedMs: t.lastUsedMs || 0,
		you: t.name === you,
		browser: t.name.startsWith("web-"),
		following: t.role === "reader" && Boolean(t.lastUsedMs) && now - t.lastUsedMs < FOLLOWING_MS,
	})).sort((a, b) => (a.role === b.role ? a.name.localeCompare(b.name) : a.role === "admin" ? -1 : 1));
}

// The reader tokens whose IEDs follow now.
export function following(tokens, now) {
	return tokenRows(tokens, "", now).filter((t) => t.following);
}

// A server plugin's code as a word and an accent: running (saved), building (info), failed (error), or its state.
export function codeLook(plugin) {
	const code = (plugin && plugin.code) || null;
	if (!code) return { label: plugin && plugin.state === "loaded" ? "no code" : (plugin && plugin.state) || "unknown", accent: plugin && plugin.state === "failed" ? "error" : "" };
	if (code.building) return { label: "building", accent: "info" };
	if (code.state === "active") return { label: "running", accent: "saved" };
	if (code.state === "failed" || code.problem) return { label: "failed", accent: "error" };
	return { label: code.state || "stopped", accent: "" };
}

// A token name a person typed: its characters checked as the server checks them; an error message, or "".
export function tokenNameProblem(name) {
	if (!name) return "Give the token a name: the machine it is for (laptop).";
	if (name.length > 64) return "A token's name is at most 64 characters.";
	if (!/^[A-Za-z0-9._-]+$/.test(name)) return "A token's name is letters, digits, '.', '_' and '-'.";
	return "";
}

// The first digits of a hash or version.
export function short(hash, digits) {
	return hash ? String(hash).slice(0, digits || 10) : "";
}

// A value as the page shows it in a line: a string as it is, anything else as compact JSON, cut at `most` characters.
export function compact(value, most) {
	const text = typeof value === "string" ? JSON.stringify(value) : JSON.stringify(value);
	const limit = most || 120;
	return text.length > limit ? text.slice(0, limit - 1) + "\u2026" : text;
}
