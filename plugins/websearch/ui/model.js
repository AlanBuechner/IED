// model.js - what Settings > Web > Search says (docs/29 "Settings"): SearXNG's and Tor's states as a line and a tone,
// the last search, an engine's test result, and the engine list's edits.

// {text, tone} for the websearch:status snapshot (undefined: none came yet; null: the topic is gone); tone is ok, busy,
// bad or idle.
export function searxLine(status) {
	if (status === undefined) return { text: "", tone: "idle" };
	if (!status) return { text: "The web search plugin is not running", tone: "bad" };
	switch (status.state) {
		case "ready": return { text: "Running at " + status.url, tone: "ok" };
		case "external": return { text: "Using your SearXNG at " + status.url, tone: "ok" };
		case "starting": return { text: status.summary ? capitalized(status.summary) : "Starting", tone: "busy" };
		case "failed": return { text: status.error || "Failed", tone: "bad" };
		default: return { text: "Stopped: the next search starts it", tone: "idle" };
	}
}

// {text, tone} for the tor:status snapshot (undefined: none came yet; null: the Tor plugin is not there), given whether
// search uses Tor.
export function torLine(status, useTor, external) {
	if (external) return { text: "Not used: your SearXNG decides how it reaches the engines", tone: "idle" };
	if (!useTor) return { text: "Off: SearXNG asks the engines directly (websearch.useTor)", tone: "idle" };
	if (status === undefined) return { text: "", tone: "idle" };
	if (!status) return { text: "The Tor plugin is not running: searches fail until it is enabled, or Tor is turned off here", tone: "bad" };
	switch (status.state) {
		case "ready": return { text: "Connected (" + status.socks + ")", tone: "ok" };
		case "starting": return { text: "Connecting: " + status.progress + "% " + (status.summary || ""), tone: "busy" };
		case "failed": return { text: status.error || "Failed", tone: "bad" };
		default: return { text: "Stopped: the next search starts it", tone: "idle" };
	}
}

// The last search as one line ("" when there was none).
export function lastSearchLine(last) {
	if (!last) return "";
	const quoted = '"' + last.query + '"';
	if (last.error) return "Last search " + quoted + " failed: " + last.error;
	const parts = ["Last search " + quoted + ": " + last.results + (last.results === 1 ? " result" : " results") + " in " + seconds(last.ms)];
	if (last.retried) parts.push("tried again on new circuits");
	if (last.failed && last.failed.length) parts.push("failed: " + last.failed.map(([engine, reason]) => engine + " (" + reason + ")").join(", "));
	return parts.join("; ");
}

// An engine's row after Test engines: {text, tone}.
export function engineTestLine(result) {
	if (!result) return { text: "", tone: "idle" };
	if (result.error) return { text: result.error, tone: "bad" };
	if (!result.results) return { text: "no results", tone: "warn" };
	return { text: result.results + (result.results === 1 ? " result" : " results"), tone: "ok" };
}

// `engines` with `name` added (lowercased, trimmed; unchanged when empty or already there).
export function withEngine(engines, name) {
	const clean = String(name || "").trim().toLowerCase();
	if (!clean || engines.includes(clean)) return engines.slice();
	return engines.concat([clean]);
}

export function withoutEngine(engines, name) {
	return engines.filter((e) => e !== name);
}

export function seconds(ms) {
	return ms < 1000 ? ms + " ms" : (ms / 1000).toFixed(1) + " s";
}

function capitalized(text) {
	return text.charAt(0).toUpperCase() + text.slice(1);
}
