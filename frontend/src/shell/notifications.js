// notifications.js - the notification center (F-SHL-31): the server's notifications (shared by every device), newest
// first, each dismissable, with Clear All. A popup by the bell on a desktop, a bottom sheet on a phone.

import { h, icon, replace } from "/ui-kit/dom.js";

const LEVEL_ICONS = { info: "info", warning: "warning", error: "error", attention: "bell-dot" };

function time(ts) {
	const date = new Date(ts);
	return date.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
}

export function showNotificationCenter(shell, at) {
	shell.menuHost.close();
	const existing = document.querySelector(".notification-center");
	if (existing) {
		existing.remove();
		return;
	}
	const list = h("div.nc-list");
	const phone = window.matchMedia("(max-width: 719px)").matches;
	const el = h("div.popup.notification-center" + (phone ? ".sheet" : ""), { role: "dialog", "aria-label": "Notifications", tabindex: "-1" },
		h("div.panel-title", null, h("span.grow", null, "Notifications"),
			h("button.btn.flat", { on: { click: () => shell.commands.run("shell.clearNotifications") } }, "Clear All"),
			h("button.ib", { title: "Close", "aria-label": "Close", on: { click: () => close() } }, icon("close"))),
		list);

	function render() {
		const notes = [...shell.server.notifications].reverse();
		replace(list, ...notes.map((note) => h("div.nc-item." + (LEVEL_ICONS[note.level] ? note.level : "info"), null,
			icon(LEVEL_ICONS[note.level] || "info", "lvl"),
			h("div.nc-body", null, h("div.nc-text", null, note.text), h("div.nc-time", null, time(note.ts))),
			h("button.ib", { title: "Dismiss", "aria-label": "Dismiss", on: { click: () => shell.client.request("server.dismiss", { id: note.id }).catch((e) => shell.report(e.message, e)) } }, icon("close")))));
		if (!notes.length) list.append(h("div.nc-empty", null, "No notifications"));
	}

	function close() {
		el.remove();
		stop();
		document.removeEventListener("pointerdown", outside, true);
	}

	function outside(event) {
		if (!el.contains(event.target) && !(at instanceof Element && at.contains(event.target))) close();
	}

	const stop = shell.onServerChange(render);
	el.addEventListener("keydown", (event) => {
		if (event.key === "Escape") close();
	});
	render();
	document.body.append(el);
	if (!phone && at instanceof Element) {
		const rect = at.getBoundingClientRect();
		el.style.left = Math.min(rect.right + 4, window.innerWidth - el.offsetWidth - 4) + "px";
		el.style.top = Math.max(4, Math.min(rect.top, window.innerHeight - el.offsetHeight - 4)) + "px";
	}
	document.addEventListener("pointerdown", outside, true);
	el.focus();
}
