// model.js - what the Sandbox activity computes from the server's snapshot (topic sandbox:<wsId>, docs/34): each
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

// Who started a run, for its row: {who: "AI" | "You" | "A plugin", ai, chat: the AI's chat's title or ""}.
export function whoOf(run, chatTitles) {
	if (!run) return { who: "", ai: false, chat: "" };
	if (run.by === "ai") return { who: "AI", ai: true, chat: (run.chatId && chatTitles && chatTitles.get(run.chatId)) || "" };
	if (run.by === "plugin") return { who: "A plugin", ai: false, chat: "" };
	return { who: "You", ai: false, chat: "" };
}

// The AI's runs going now.
export function aiRunning(runs) {
	return (runs || []).filter((run) => run.running && run.by === "ai");
}

// The Sandbox activity's badge: how many runs the AI has going, null when none.
export function activityBadge(runs) {
	const ai = aiRunning(runs);
	if (!ai.length) return null;
	const names = [...new Set(ai.map((run) => run.sandbox || "Docker"))];
	return { count: ai.length, tone: "info", title: "The AI is running " + (ai.length === 1 ? "a command" : ai.length + " commands") + " in " + names.join(", ") };
}

// The run the Activity view shows: the one picked (`shown`), else the newest going (the AI's first), else the newest.
export function followedRun(runs, shown) {
	const all = runs || [];
	if (shown) {
		const picked = all.find((run) => run.id === shown);
		if (picked) return picked;
	}
	return all.find((run) => run.running && run.by === "ai") || all.find((run) => run.running) || all[0] || null;
}

// How long ago `ms` was, briefly: "now", "41 s ago", "3 min ago", "2 h ago", or the date.
export function ago(ms, now) {
	if (!(ms > 0)) return "";
	const passed = (now || Date.now()) - ms;
	if (passed < 5000) return "now";
	if (passed < 60000) return Math.floor(passed / 1000) + " s ago";
	if (passed < 3600000) return Math.floor(passed / 60000) + " min ago";
	if (passed < 86400000) return Math.floor(passed / 3600000) + " h ago";
	return new Date(ms).toLocaleDateString();
}

// --- Desktops ---------------------------------------------------------------------------------------------------------

// The screen pixel under a point of the shown picture: (`x`, `y`) in a picture shown `shownW` x `shownH` of a screen
// `screenW` x `screenH`, clamped to the screen.
export function screenPoint(x, y, shownW, shownH, screenW, screenH) {
	const sx = Math.round((x / Math.max(1, shownW)) * screenW);
	const sy = Math.round((y / Math.max(1, shownH)) * screenH);
	return { x: Math.min(Math.max(sx, 0), screenW - 1), y: Math.min(Math.max(sy, 0), screenH - 1) };
}

const KEY_NAMES = {
	Enter: "Return", Backspace: "BackSpace", Tab: "Tab", Escape: "Escape", Delete: "Delete", Insert: "Insert", Home: "Home", End: "End",
	PageUp: "Prior", PageDown: "Next", ArrowLeft: "Left", ArrowRight: "Right", ArrowUp: "Up", ArrowDown: "Down", " ": "space",
	CapsLock: "Caps_Lock", ContextMenu: "Menu", PrintScreen: "Print",
};

// What a browser key event is to the sandbox's desktop: {type: text} for a character typed as it is, {key: "ctrl+s"} for
// a key xdotool names (with its modifiers), or null for a key alone (Shift, Control) or one there is no name for.
export function desktopKey(event) {
	const key = event.key;
	if (!key || key === "Shift" || key === "Control" || key === "Alt" || key === "Meta" || key === "AltGraph" || key === "Dead" || key === "Unidentified") return null;
	const mods = [];
	if (event.ctrlKey) mods.push("ctrl");
	if (event.altKey) mods.push("alt");
	if (event.metaKey) mods.push("super");
	let name = KEY_NAMES[key] || (/^F([1-9]|1[0-9]|2[0-4])$/.test(key) ? key : null);
	if (!name && key.length === 1) {
		if (!mods.length && key !== " ") return { text: key };
		name = /^[A-Za-z0-9]$/.test(key) ? key.toLowerCase() : null;
		if (!name) return mods.length ? null : { text: key };
		if (event.shiftKey) mods.push("shift");
	}
	else if (name && event.shiftKey) mods.push("shift");
	if (!name) return null;
	return { key: mods.length ? mods.join("+") + "+" + name : name };
}

// The desktop's last input when it is the AI's and recent: what to show over the screen ("click (412, 310)"), else null.
export function aiDriving(desktop, now) {
	const action = desktop && desktop.action;
	if (!action || action.by !== "ai") return null;
	return (now || Date.now()) - action.ms < 8000 ? action : null;
}

// How a run ended, in a word or two.
export function runResult(run) {
	if (!run) return "";
	if (run.running) return "running";
	if (run.stopped) return run.stopped;
	if (run.problem) return "failed";
	return "exit " + run.exitCode;
}
