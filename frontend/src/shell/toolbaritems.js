// toolbaritems.js - plugins' toolbar views (docs/02 `toolbarItems`): each mounted once per workspace and kept across
// renders (it updates itself). The desktop toolbar and the phone's toolbar strip show them.

import { h } from "/ui-kit/dom.js";

export class ToolbarItems {
	constructor(shell) {
		this.shell = shell;
		this.mounted = new Map();
	}

	// The items whose `when` holds, highest priority first.
	shown() {
		const shell = this.shell;
		return shell.contributions.toolbarItems.filter((item) => shell.context.holds(item.when)).sort((a, b) => b.priority - a.priority);
	}

	element(item) {
		const key = item.plugin.id + "/" + item.id;
		let mounted = this.mounted.get(key);
		if (!mounted) {
			mounted = { el: h("span.tb-item", { dataset: { item: item.id } }), instance: null };
			this.mounted.set(key, mounted);
			this.mount(item, mounted);
		}
		return mounted.el;
	}

	mount(item, mounted) {
		const view = item.plugin.module ? item.plugin.module[item.view] : null;
		try {
			if (typeof view === "function") mounted.instance = view({ el: mounted.el, ied: this.shell.apiFor(item.plugin), ref: "toolbar:" + item.id, wsId: this.shell.wsId }) || null;
		}
		catch (e) {
			this.shell.report("Plugin '" + item.plugin.id + "': toolbar item '" + item.id + "' failed: " + e.message, e);
		}
	}

	// What a toolbar of these command rows and items looks like: a toolbar drawn the same is left alone, so a view's
	// open dropdown stays open when an unrelated context key changes.
	static signature(rows, items) {
		return JSON.stringify([rows.map((r) => [r.kind, r.command, r.args, r.label, r.enabled, r.icon, r.keys]), items.map((i) => i.plugin.id + "/" + i.id)]);
	}

	// The workspace's toolbar views go with it.
	release() {
		for (const mounted of this.mounted.values()) if (mounted.instance && typeof mounted.instance.dispose === "function") mounted.instance.dispose();
		this.mounted = new Map();
	}
}
