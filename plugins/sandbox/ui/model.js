// model.js - what the Sandboxes window computes from the server's snapshot (topic sandbox:<wsId>, docs/34): each
// sandbox's state as one word with its tone and icon, platform labels, a name for a new sandbox, the outputs of runs
// kept across "changed" events. No DOM here, so the frontend's unit tests can check it.

// A sandbox's state for its row: {text, tone: "ok" | "info" | "warn" | "error" | "", icon, spin}.
export function sandboxState(sandbox, engine) {
	if (sandbox.problem) return { text: "definition error", tone: "error", icon: "error" };
	if (sandbox.busy) return { text: sandbox.busy + "...", tone: "info", icon: "loading", spin: true };
	if (!engine || engine.state !== "ready") return { text: "Docker " + ((engine && engine.state) || "unknown"), tone: "", icon: "vm-outline" };
	if (sandbox.container === "running") return { text: "running", tone: "ok", icon: "vm-running" };
	if (sandbox.container === "stale") return { text: "definition changed", tone: "warn", icon: "vm-pending" };
	if (!sandbox.imageBuilt) return { text: "not built", tone: "", icon: "vm-outline" };
	if (sandbox.container === "stopped") return { text: "stopped", tone: "", icon: "vm" };
	return { text: "built", tone: "", icon: "vm" };
}

// "linux/arm64" -> "arm64"; "linux/arm/v7" -> "arm/v7"; a Windows platform as it is.
export function shortPlatform(platform) {
	return String(platform || "").replace(/^linux\//, "");
}

// The engine's platforms as {native: [..], emulated: [..]}.
export function enginePlatforms(engine) {
	const out = { native: [], emulated: [] };
	for (const entry of (engine && engine.platforms) || []) (entry.native ? out.native : out.emulated).push(entry.platform);
	return out;
}

// A name for a new sandbox of system `osId` and `version` on `platform`: "ubuntu", "ubuntu-arm64", "debian-12-386";
// one already taken gets "-2", "-3".
export function suggestName(osId, version, platform, isDefault, native, taken) {
	let name = String(osId || "sandbox").toLowerCase().replace(/[^a-z0-9-]+/g, "-");
	if (!isDefault && version && version !== "latest") name += "-" + String(version).toLowerCase().replace(/[^a-z0-9]+/g, "");
	if (platform && platform !== native) name += "-" + shortPlatform(platform).replace(/[^a-z0-9]+/g, "");
	name = name.replace(/-+/g, "-").replace(/^-|-$/g, "").slice(0, 36) || "sandbox";
	const used = new Set(taken || []);
	if (!used.has(name)) return name;
	for (let i = 2; ; i++) if (!used.has(name + "-" + i)) return name + "-" + i;
}

// Whether `name` can name a sandbox (the server's rule): lowercase letters, digits and '-', 1 to 40, not starting with '-'.
export function validName(name) {
	return /^[a-z0-9][a-z0-9-]{0,39}$/.test(String(name || ""));
}

// A duration as the server writes it.
export function duration(ms) {
	if (!(ms > 0)) return "";
	if (ms < 1000) return ms + " ms";
	if (ms < 60000) return Math.floor(ms / 1000) + " s";
	if (ms < 3600000) return Math.floor(ms / 60000) + " m " + (Math.floor(ms / 1000) % 60) + " s";
	return Math.floor(ms / 3600000) + " h " + (Math.floor(ms / 60000) % 60) + " m";
}

// The validation's outcome in a few words: "3 of 4 passed", "running (1 of 4 done)".
export function validationSummary(validation) {
	if (!validation || !validation.rows || !validation.rows.length) return "";
	const rows = validation.rows;
	const passed = rows.filter((row) => row.result === "pass").length;
	if (validation.running) {
		const done = rows.filter((row) => row.result !== "queued" && row.result !== "running").length;
		return "running (" + done + " of " + rows.length + " done)";
	}
	return passed + " of " + rows.length + " passed";
}

// The result of `sandbox` in the last validation, or null.
export function validationOf(validation, sandbox) {
	if (!validation || !validation.rows) return null;
	return validation.rows.find((row) => row.sandbox === sandbox) || null;
}

// Keeps run outputs across snapshots: a pulled snapshot carries them (the newest runs'), "changed" events do not, and
// "output" events add to one. `outputs` is a Map runId -> text, changed in place; runs gone from `runs` are dropped.
export function keepOutputs(outputs, runs, fromSnapshot) {
	const live = new Set();
	for (const run of runs || []) {
		live.add(run.id);
		if (fromSnapshot && typeof run.output === "string") outputs.set(run.id, run.output);
		else if (!outputs.has(run.id)) outputs.set(run.id, "");
	}
	for (const id of [...outputs.keys()]) if (!live.has(id)) outputs.delete(id);
	return outputs;
}

// Appends an "output" event's text, the kept text cut at a line start past `limit` bytes as the server cuts it.
export function appendOutput(outputs, runId, text, limit = 65536) {
	let kept = (outputs.get(runId) || "") + text;
	if (kept.length > limit) {
		const cut = kept.indexOf("\n", kept.length - limit);
		kept = cut < 0 ? kept.slice(kept.length - limit) : kept.slice(cut + 1);
	}
	outputs.set(runId, kept);
	return kept;
}

// The run a sandbox's details show: its running one, else its last.
export function runOf(runs, sandbox) {
	const mine = (runs || []).filter((run) => run.sandbox === sandbox);
	return mine.find((run) => run.running) || mine[0] || null;
}

// How a run ended, in a word or two.
export function runResult(run) {
	if (!run) return "";
	if (run.running) return "running";
	if (run.stopped) return run.stopped;
	if (run.problem) return "failed";
	return "exit " + run.exitCode;
}
