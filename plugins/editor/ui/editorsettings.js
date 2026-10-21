// editorsettings.js - the editor's settings (settings.schema.json) as each workspace has them (settings:<wsId>), applied
// to its open editors as they change. View White Space (Ctrl+R, Ctrl+W) and Word Wrap (Ctrl+E, Ctrl+W) flip theirs in
// the User tier.

export const WHITESPACE = ["all", "trailing", "boundary", "selection", "none"];
const LINE_NUMBERS = ["on", "relative", "off"];

function valueOf(values, key, fallback, allowed) {
	const entry = values && values[key];
	if (!entry || entry.value === undefined || entry.value === null) return fallback;
	if (allowed && !allowed.includes(entry.value)) return fallback;
	return entry.value;
}

// The Monaco options the settings `values` (a settings snapshot's) give; a value the schema would refuse is the default.
// env: {fontSize, lineHeight (the theme's), phone}.
export function optionsOf(values, env) {
	const theme = { fontSize: (env && env.fontSize) || 13, lineHeight: (env && env.lineHeight) || 18 };
	const fontSize = valueOf(values, "editor.fontSize", 0);
	const set = Number.isInteger(fontSize) && fontSize > 0;
	return {
		renderWhitespace: valueOf(values, "editor.renderWhitespace", "all", WHITESPACE),
		wordWrap: valueOf(values, "editor.wordWrap", "off", ["off", "on"]),
		lineNumbers: valueOf(values, "editor.lineNumbers", "on", LINE_NUMBERS),
		minimap: { enabled: valueOf(values, "editor.minimap", true) === true && !(env && env.phone), renderCharacters: false, showSlider: "always" },
		stickyScroll: { enabled: valueOf(values, "editor.stickyScroll", true) === true },
		bracketPairColorization: { enabled: valueOf(values, "editor.bracketPairColorization", false) === true },
		fontSize: set ? fontSize : theme.fontSize,
		lineHeight: set ? Math.round(fontSize * theme.lineHeight / theme.fontSize) : theme.lineHeight,
	};
}

// What View White Space sets: none when any white space shows, all when none does.
export function toggledWhitespace(current) {
	return current === "none" ? "all" : "none";
}

export function toggledWordWrap(current) {
	return current === "on" ? "off" : "on";
}

export class EditorSettings {
	// env(): {fontSize, lineHeight, phone} - the theme's code font and whether the page is a phone's.
	constructor(ied, env) {
		this.ied = ied;
		this.env = env;
		this.workspaces = new Map();   // wsId -> {values, editors: Set, stop}
	}

	// Keeps `editor` (a CodeEditor) on its workspace's settings until the returned function is called.
	bind(editor) {
		const wsId = editor.ctx.wsId;
		const workspace = this.follow(wsId);
		workspace.editors.add(editor);
		editor.editor.updateOptions(optionsOf(workspace.values, this.env()));
		return () => {
			workspace.editors.delete(editor);
			if (workspace.editors.size) return;
			workspace.stop();
			this.workspaces.delete(wsId);
		};
	}

	follow(wsId) {
		if (this.workspaces.has(wsId)) return this.workspaces.get(wsId);
		const workspace = { values: null, editors: new Set(), stop: null };
		const apply = (data) => {
			workspace.values = data.values;
			for (const editor of workspace.editors) editor.editor.updateOptions(optionsOf(workspace.values, this.env()));
		};
		workspace.stop = this.ied.client.subscribe("settings:" + wsId, { snap: apply, evt: (kind, data) => kind === "settings.changed" && apply(data) });
		this.workspaces.set(wsId, workspace);
		return workspace;
	}

	current(wsId) {
		const workspace = this.workspaces.get(wsId);
		return optionsOf(workspace ? workspace.values : null, this.env());
	}

	// View White Space for the workspace of `wsId`.
	toggleWhitespace(wsId) {
		return this.ied.client.request("settings.set", { tier: "app", key: "editor.renderWhitespace", value: toggledWhitespace(this.current(wsId).renderWhitespace) });
	}

	// Word Wrap for the workspace of `wsId`.
	toggleWordWrap(wsId) {
		return this.ied.client.request("settings.set", { tier: "app", key: "editor.wordWrap", value: toggledWordWrap(this.current(wsId).wordWrap) });
	}
}
