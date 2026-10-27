// statusbar.js - the status bar (F-SHL-5, F-SHL-6): left the connection, a chord in progress and plugin items; right
// plugin items, the connected devices and the notifications. Its color follows the context key `statusState`
// (building, debugging, failed; idle otherwise), set by the plugins that know.

import { h, icon, replace } from "/ui-kit/dom.js";

const CONNECTION_TEXT = { live: "Ready", connecting: "Connecting...", reconnecting: "Reconnecting...", closed: "Offline" };
const TRANSIENT_MS = 3000;

export class StatusBar {
	constructor(shell) {
		this.shell = shell;
		this.el = h("footer.statusbar", { role: "status" });
		this.state = "connecting";
		this.chordText = null;
		this.chordTimer = null;
		this.items = new Map();
		this.stopContext = shell.context.onChange((key) => {
			if (key === "statusState") this.el.dataset.state = shell.context.get("statusState") || "idle";
		});
		this.render();
	}

	dispose() {
		this.stopContext();
		this.disposeItems();
	}

	connection(state) {
		this.state = state;
		this.render();
	}

	chord(text, transient) {
		clearTimeout(this.chordTimer);
		this.chordText = text;
		if (text && transient) this.chordTimer = setTimeout(() => this.chord(null), TRANSIENT_MS);
		this.render();
	}

	contributionsChanged() {
		this.disposeItems();
		this.render();
	}

	disposeItems() {
		for (const item of this.items.values()) if (item.instance && typeof item.instance.dispose === "function") item.instance.dispose();
		this.items.clear();
	}

	render() {
		const shell = this.shell;
		const left = this.pluginItems("left");
		const right = this.pluginItems("right");
		const devices = new Set(shell.server.clients.map((c) => c.deviceId)).size;
		const unread = shell.server.notifications.length;
		this.el.dataset.state = shell.context.get("statusState") || "idle";
		replace(this.el,
			h("span.st.conn-state", { title: "Server connection" }, icon(this.state === "live" ? "pass" : this.state === "closed" ? "debug-disconnect" : "sync"), CONNECTION_TEXT[this.state] || this.state),
			this.chordText ? h("span.st.chord", null, this.chordText) : null,
			...left,
			h("span.st.grow"),
			...right,
			h("span.st", { title: devices + " device(s) connected" }, icon("vm"), devices + (devices === 1 ? " device" : " devices")),
			h("button.st.st-bell", { title: "Notifications", "aria-label": "Notifications (" + unread + ")", on: { click: (event) => shell.commands.run("shell.showNotifications", { at: event.currentTarget }) } }, icon(unread ? "bell-dot" : "bell"), unread ? String(unread) : ""));
	}

	// A plugin's status item: its view export renders it (once); a command makes it clickable, and a view whose instance has
	// click(event) takes the clicks itself (a popup of its own), leaving the command to the palette.
	pluginItems(alignment) {
		const shell = this.shell;
		return shell.contributions.statusItems
			.filter((item) => item.alignment === alignment && shell.context.holds(item.when))
			.sort((a, b) => b.priority - a.priority)
			.map((item) => this.itemElement(item));
	}

	itemElement(item) {
		let mounted = this.items.get(item.id);
		if (!mounted) {
			const clicked = (event) => {
				if (mounted.instance && typeof mounted.instance.click === "function") mounted.instance.click(event);
				else this.shell.commands.run(item.command);
			};
			const el = h(item.command ? "button.st" : "span.st", { dataset: { item: item.id }, on: item.command ? { click: clicked } : {} });
			mounted = { el, instance: null };
			this.items.set(item.id, mounted);
			const view = item.view && item.plugin.module ? item.plugin.module[item.view] : null;
			if (typeof view === "function") {
				try {
					mounted.instance = view({ el, ied: this.shell.apiFor(item.plugin), ref: "status:" + item.id, wsId: this.shell.wsId }) || null;
				}
				catch (e) {
					this.shell.report("Plugin '" + item.plugin.id + "': status item '" + item.id + "' failed: " + e.message, e);
				}
			}
			else if (item.command) el.textContent = this.shell.commands.label(item.command);
		}
		return mounted.el;
	}
}
