// editorsettings.js - the editor's settings (settings.schema.json) as each workspace has them (settings:<wsId>), applied
// to its open editors as they change; View White Space (Ctrl+R, Ctrl+W) flips white space between all and none in the
// User tier.

export const WHITESPACE = ["all", "trailing", "boundary", "selection", "none"];

// The Monaco options the settings `values` (a settings snapshot's) give; a value the schema would refuse is the default.
export function optionsOf(values) {
	const entry = values && values["editor.renderWhitespace"];
	const whitespace = entry && WHITESPACE.includes(entry.value) ? entry.value : "all";
	return { renderWhitespace: whitespace };
}

// What View White Space sets: none when any white space shows, all when none does.
export function toggledWhitespace(current) {
	return current === "none" ? "all" : "none";
}

export class EditorSettings {
	constructor(ied) {
		this.ied = ied;
		this.workspaces = new Map();   // wsId -> {values, editors: Set, stop}
	}

	// Keeps `editor` (a CodeEditor) on its workspace's settings until the returned function is called.
	bind(editor) {
		const wsId = editor.ctx.wsId;
		const workspace = this.follow(wsId);
		workspace.editors.add(editor);
		editor.editor.updateOptions(optionsOf(workspace.values));
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
			for (const editor of workspace.editors) editor.editor.updateOptions(optionsOf(workspace.values));
		};
		workspace.stop = this.ied.client.subscribe("settings:" + wsId, { snap: apply, evt: (kind, data) => kind === "settings.changed" && apply(data) });
		this.workspaces.set(wsId, workspace);
		return workspace;
	}

	// View White Space for the workspace of `wsId`.
	async toggleWhitespace(wsId) {
		const workspace = this.workspaces.get(wsId);
		const current = optionsOf(workspace ? workspace.values : null).renderWhitespace;
		await this.ied.client.request("settings.set", { tier: "app", key: "editor.renderWhitespace", value: toggledWhitespace(current) });
	}
}
