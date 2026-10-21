// views.js - the views behind tab refs (docs/15 "Layout document", docs/02 "UI modules"): a tool window id renders its
// plugin's export; "<kind>:<arg>" renders the export a plugin registered for the kind (ied.tabs.kind), or a shell view
// for "shell:<name>". A mounted view keeps its element while its tab is open, wherever the tab moves.

import { h, icon, replace } from "/ui-kit/dom.js";

export class ViewHost {
	constructor(shell) {
		this.shell = shell;
		this.entries = new Map();
		this.kinds = new Map();
		this.listeners = new Set();
	}

	// spec: {plugin, view (an export name of the plugin's module, or a function for the shell), title(arg), icon(arg)}
	registerKind(kind, spec) {
		this.kinds.set(kind, spec);
	}

	// What renders `ref`, or null when nothing does now (the tab stays in the layout, hidden). "activity:<id>" is an
	// activity that renders itself.
	resolve(ref) {
		if (ref.startsWith("activity:")) return this.resolveActivity(ref.slice("activity:".length));
		const tool = this.shell.contributions.toolWindow(ref);
		if (tool) return { title: tool.title, icon: tool.icon, base: tool.plugin.base, factory: () => exportOf(tool.plugin, tool.view), plugin: tool.plugin.id, kind: "tool" };
		const colon = ref.indexOf(":");
		if (colon <= 0) return null;
		const spec = this.kinds.get(ref.slice(0, colon));
		if (!spec || (spec.plugin && !this.shell.contributions.plugin(spec.plugin))) return null;
		const arg = ref.slice(colon + 1);
		const plugin = spec.plugin ? this.shell.contributions.plugin(spec.plugin) : null;
		return {
			title: spec.title ? spec.title(arg) : arg, icon: spec.icon ? spec.icon(arg) : null, base: plugin ? plugin.base : null,
			factory: () => (typeof spec.view === "function" ? spec.view : exportOf(plugin, spec.view)), plugin: spec.plugin || "shell", kind: "tab",
		};
	}

	resolveActivity(id) {
		const activity = this.shell.contributions.activity(id);
		if (!activity || !activity.view) return null;
		return { title: activity.title, icon: activity.icon, base: activity.plugin.base, factory: () => exportOf(activity.plugin, activity.view), plugin: activity.plugin.id, kind: "activity" };
	}

	has(ref) {
		return this.resolve(ref) !== null;
	}

	// The workspace-relative file a tab shows, when its kind says (spec.resource(arg)); else null.
	resourceOf(ref) {
		const colon = ref.indexOf(":");
		const spec = colon > 0 ? this.kinds.get(ref.slice(0, colon)) : null;
		return spec && spec.resource ? spec.resource(ref.slice(colon + 1)) : null;
	}

	// The mounted view of `ref` (mounting it the first time): {ref, el, title, icon, base, dirty, instance}.
	get(ref) {
		let entry = this.entries.get(ref);
		if (entry) return entry;
		const resolved = this.resolve(ref);
		if (!resolved) return null;
		entry = { ref, el: h("div.view", { tabindex: "-1", dataset: { ref } }), title: resolved.title, icon: resolved.icon, base: resolved.base, dirty: false, instance: null, kind: resolved.kind };
		this.entries.set(ref, entry);
		this.mount(entry, resolved);
		return entry;
	}

	mount(entry, resolved) {
		const factory = resolved.factory();
		if (typeof factory !== "function") {
			this.failed(entry, "Plugin '" + resolved.plugin + "' has no view for this tab (its UI module did not load, or exports no such view).");
			return;
		}
		const ctx = this.contextFor(entry, resolved);
		try {
			const made = factory(ctx);
			if (made && typeof made.then === "function") made.then((instance) => (entry.instance = instance), (e) => this.failed(entry, "This view failed: " + e.message, e));
			else entry.instance = made || null;
		}
		catch (e) {
			this.failed(entry, "This view failed: " + e.message, e);
		}
	}

	contextFor(entry, resolved) {
		const plugin = resolved.plugin === "shell" ? null : this.shell.contributions.plugin(resolved.plugin);
		return {
			el: entry.el,
			ref: entry.ref,
			wsId: this.shell.wsId,
			ied: plugin ? this.shell.apiFor(plugin) : this.shell.shellApi,
			tab: {
				setTitle: (title) => this.changeEntry(entry, { title }),
				setDirty: (dirty) => this.changeEntry(entry, { dirty: Boolean(dirty) }),
				pin: () => this.shell.pinTab(entry.ref),
				close: () => this.shell.closeTab(entry.ref),
			},
		};
	}

	failed(entry, message, error) {
		replace(entry.el, h("div.view-error", null, icon("error", "c-error"), h("span", null, message)));
		this.shell.report(message, error);
	}

	changeEntry(entry, change) {
		if (Object.entries(change).every(([key, value]) => entry[key] === value)) return;
		Object.assign(entry, change);
		for (const fn of [...this.listeners]) fn(entry);
	}

	onChange(fn) {
		this.listeners.add(fn);
		return () => this.listeners.delete(fn);
	}

	// Tells a view it was shown or hidden (it may subscribe only while visible).
	setVisible(ref, visible) {
		const entry = this.entries.get(ref);
		if (!entry || entry.visible === visible) return;
		entry.visible = visible;
		if (entry.instance && typeof entry.instance.visible === "function") entry.instance.visible(visible);
	}

	focus(ref) {
		const entry = this.entries.get(ref);
		if (entry && entry.instance && typeof entry.instance.focus === "function") entry.instance.focus();
		else if (entry) entry.el.focus({ preventScroll: true });
	}

	release(ref) {
		const entry = this.entries.get(ref);
		if (!entry) return;
		this.entries.delete(ref);
		try {
			if (entry.instance && typeof entry.instance.dispose === "function") entry.instance.dispose();
		}
		catch (e) {
			this.shell.report("A view failed to close: " + e.message, e);
		}
		entry.el.remove();
	}

	// Disposes every view whose ref is not in `keep`.
	releaseExcept(keep) {
		for (const ref of [...this.entries.keys()]) if (!keep.has(ref)) this.release(ref);
	}

	releaseAll() {
		this.releaseExcept(new Set());
	}
}

function exportOf(plugin, name) {
	if (!plugin || !plugin.module || !name) return null;
	const value = plugin.module[name];
	return typeof value === "function" ? value : null;
}
