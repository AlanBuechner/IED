// toolsmodel.js - what the language tools snapshot (tools:<wsId>, docs/26 "Language tools") reads as: a tool's state in
// words, the banner of a workspace that lacks tools, the tools an Install installs (with what they need first) and the
// question asked before an install that writes outside IED's tools folder. Pure: the banner and Settings > Languages
// (languagetools.js) draw from it.

const LACKING = new Set(["missing", "wrongVersion", "failed"]);

export function lacking(tool) {
	return LACKING.has(tool.state);
}

// "1.22 or newer (go.mod)", "1.75.0 (rust-toolchain.toml)"; "" without a pin.
export function pinText(pin) {
	if (!pin) return "";
	return pin.version + (pin.match === "minimum" ? " or newer" : "") + " (" + pin.file + ")";
}

const KINDS = { compiler: "Compiler", languageServer: "Language server", debugAdapter: "Debugger", buildTool: "Build tool", other: "Tool" };

export function kindText(kind) {
	return KINDS[kind] || KINDS.other;
}

// {text, tone}: the tool's state for its row - tone ok, warn, error, busy or idle.
export function stateText(tool) {
	switch (tool.state) {
		case "found": return { text: (tool.version ? tool.version : "Found") + (tool.source === "bundled" ? " (comes with IED)" : tool.source === "installed" ? " (installed by IED)" : ""), tone: "ok" };
		case "wrongVersion": return { text: tool.version + " - the workspace needs " + pinText(tool.pin), tone: "warn" };
		case "missing": return { text: "Not installed", tone: "error" };
		case "failed": return { text: "Install failed", tone: "error" };
		case "installing": return { text: "Installing " + (tool.installs && tool.installs !== "latest" ? tool.installs : "") + "...", tone: "busy" };
		case "queued": return { text: "Waiting to install", tone: "busy" };
		default: return { text: "Not checked", tone: "idle" };
	}
}

// The ids an Install of `ids` (empty: every lacking tool that can be installed) runs, with the tools they require that
// are not found, each after what it requires; tools that cannot be installed are left out.
export function installPlan(snapshot, ids) {
	const tools = (snapshot && snapshot.tools) || [];
	const byId = new Map(tools.map((t) => [t.id, t]));
	const wanted = ids && ids.length ? ids.map((id) => byId.get(id)).filter(Boolean) : tools.filter((t) => lacking(t));
	const order = [];
	const visit = (tool, seen) => {
		if (!tool || !tool.canInstall || order.includes(tool.id) || seen.has(tool.id)) return;
		seen.add(tool.id);
		for (const id of tool.requires || []) {
			const required = byId.get(id);
			if (required && required.state !== "found") visit(required, seen);
		}
		order.push(tool.id);
	};
	for (const tool of wanted) visit(tool, new Set());
	return order;
}

// The question to ask before installing `ids`, or null when every step stays in IED's tools folder.
export function userScopeQuestion(snapshot, ids) {
	const forUser = ((snapshot && snapshot.tools) || []).filter((t) => ids.includes(t.id) && t.installsForUser);
	if (!forUser.length) return null;
	return {
		title: "Install for your user",
		message: forUser.map((t) => t.title).join(", ") + (forUser.length === 1 ? " installs" : " install") + " the way " + (forUser.length === 1 ? "its" : "their") +
			" makers do, for your user and outside IED's tools folder (rustup's own folders under your profile). PATH is not changed. Install?",
		confirm: "Install",
	};
}

function names(tools) {
	const titles = tools.map((t) => t.title);
	return titles.length <= 1 ? titles.join("") : titles.slice(0, -1).join(", ") + " and " + titles[titles.length - 1];
}

// The banner of a workspace that lacks tools (null: none to show): {tone, text, install (ids an Install runs), busy}.
export function toolsBanner(snapshot) {
	if (!snapshot || !snapshot.trusted || !snapshot.detected) return null;
	if (snapshot.install) {
		const current = (snapshot.tools || []).find((t) => t.id === snapshot.install.current);
		const at = snapshot.install.tools.indexOf(snapshot.install.current) + 1;
		return { tone: "info", busy: true, install: [], text: "Installing " + (current ? current.title : "language tools") + (snapshot.install.tools.length > 1 ? " (" + at + " of " + snapshot.install.tools.length + ")" : "") + "..." };
	}
	const missing = (snapshot.tools || []).filter(lacking);
	if (!missing.length || snapshot.dismissed) return null;
	const wrong = missing.filter((t) => t.state === "wrongVersion");
	const absent = missing.filter((t) => t.state !== "wrongVersion");
	const parts = [];
	if (absent.length) parts.push(names(absent) + (absent.length === 1 ? " is not installed" : " are not installed"));
	for (const tool of wrong) parts.push(tool.title + " " + tool.version + " is not the " + pinText(tool.pin) + " the workspace needs");
	const install = installPlan(snapshot, []);
	return { tone: "warn", busy: false, install, text: parts.join("; ") + "." + (install.length ? "" : " Settings > Languages says how to get " + (missing.length === 1 ? "it" : "them") + ".") };
}

// The tools grouped by the plugin (the language) that names them, in the snapshot's order: [{plugin, title, tools}].
export function toolGroups(snapshot) {
	const groups = [];
	for (const tool of (snapshot && snapshot.tools) || []) {
		let group = groups.find((g) => g.plugin === tool.plugin);
		if (!group) groups.push(group = { plugin: tool.plugin, title: tool.pluginName || tool.plugin, tools: [] });
		group.tools.push(tool);
	}
	return groups;
}
