// configservermodel.js - what Settings > Environment > Config Server shows, as plain data (docs/35 "Settings > Environment
// > Config Server"): the mirror's state as a label and an accent, where the reader token comes from, how long ago the
// last change was mirrored, and what the config server shares. No DOM: configserverpage.js draws it.

export const SECRET_NAME = "configServer";

// Each state of the server topic's configServer: its words and its semantic accent (docs/07: color only with meaning).
const LOOKS = {
	off: { label: "Not following", accent: "", trouble: false },
	connecting: { label: "Connecting", accent: "info", trouble: false },
	following: { label: "Following", accent: "saved", trouble: false },
	unreachable: { label: "Cannot be reached", accent: "error", trouble: true },
	refused: { label: "Refused", accent: "error", trouble: true },
	failed: { label: "Could not mirror", accent: "error", trouble: true },
};

export function stateLook(status) {
	return LOOKS[(status && status.state) || "off"] || { label: String(status.state), accent: "", trouble: false };
}

// Where configServer.token says the token is: {kind: "secret", name, stored} (the secret store, and whether it holds
// that name), {kind: "env", variable}, or {kind: "none"}.
export function tokenSource(value, secrets) {
	const listed = (secrets && secrets.secrets) || [];
	if (value && typeof value === "object" && typeof value.secret === "string" && value.secret)
		return { kind: "secret", name: value.secret, stored: listed.some((s) => s.name === value.secret) };
	if (value && typeof value === "object" && typeof value.env === "string" && value.env) return { kind: "env", variable: value.env };
	return { kind: "none" };
}

// A URL as configServer.url keeps it: trimmed, "http://" added to a bare host, ending with "/". Throws for one that is
// not http or https.
export function normalizeUrl(text) {
	let url = (text || "").trim();
	if (!url) return "";
	if (!/^[a-z][a-z0-9+.-]*:\/\//i.test(url)) url = "http://" + url;
	if (!/^https?:\/\//i.test(url)) throw new Error("A config server's URL is http:// or https://");
	let parsed;
	try {
		parsed = new URL(url);
	}
	catch {
		throw new Error("'" + text.trim() + "' is not a URL");
	}
	if (!parsed.host) throw new Error("'" + text.trim() + "' names no server");
	return url.endsWith("/") ? url : url + "/";
}

// "just now", "12 s ago", "4 min ago", "3 h ago", or the date and time.
export function ago(ms, now) {
	const passed = Math.max(0, now - ms);
	if (passed < 5000) return "just now";
	if (passed < 60000) return Math.floor(passed / 1000) + " s ago";
	if (passed < 3600000) return Math.floor(passed / 60000) + " min ago";
	if (passed < 86400000) return Math.floor(passed / 3600000) + " h ago";
	return new Date(ms).toLocaleString(undefined, { month: "short", day: "numeric", hour: "2-digit", minute: "2-digit" });
}

// What the mirror holds, as the page lists it: [{title, items, note}] for the settings (each key with its members:
// ai.models' profiles), the plugins and the skills.
export function servedSections(status) {
	const served = (status && status.served) || {};
	const settings = served.settings || [];
	return [
		{ id: "settings", title: "Settings", empty: "No settings", items: settings.map((s) => ({ name: s.key, members: s.members || null })) },
		{ id: "plugins", title: "Plugins", empty: "No plugins", items: (served.plugins || []).map((name) => ({ name, members: null })) },
		{ id: "skills", title: "Skills", empty: "No skills", items: (served.skills || []).map((name) => ({ name, members: null })) },
	];
}

// The short form of a state version (a SHA-256 hex): its first 10 digits.
export function shortVersion(version) {
	return version ? String(version).slice(0, 10) : "";
}
