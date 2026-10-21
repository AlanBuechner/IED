// details.js - the zone details side panel (F-PRF-15, mockup 06): the selected zone's name, duration and self time,
// thread or task, frame, source (a click opens it in the editor), text and value, call stack, the zones around it and
// its children by name; a small histogram of every zone of its name. On a phone it is a sheet under the timeline.

import { h, icon, replace } from "/ui-kit/dom.js";
import { duration, count } from "./format.js";

export class ZoneDetails {
	constructor({ request, openSource, findZone, askAi, phone }) {
		this.request = request;
		this.openSource = openSource;
		this.findZone = findZone;
		this.askAi = askAi;
		this.el = h("aside.prof-side" + (phone ? ".phone-sheet" : ""));
		this.clear();
	}

	clear() {
		this.zone = null;
		replace(this.el, h("div.zd.muted", null, "Select a zone in the timeline to see its details."));
	}

	async show(captureId, ref) {
		const zone = await this.request("prof.zone", { captureId, ref });
		if (!zone) return;
		this.zone = zone;
		const source = zone.file ? zone.file + (zone.line ? ":" + zone.line : "") : "";
		const kv = [
			["Duration", duration(zone.durationNs) + " \u00b7 self " + duration(zone.selfNs) + (zone.open ? " (still open)" : "")],
			[zone.task ? "Task" : "Thread", zone.task ? zone.task + " \u00b7 on " + zone.thread : zone.thread],
			zone.frame ? ["Frame", zone.frame.index + " (" + duration(zone.frame.durationNs) + ")"] : null,
			source ? ["Source", h("a.mono.prof-link", { href: "#", on: { click: (e) => { e.preventDefault(); this.openSource(zone); } } }, source)] : null,
			zone.function && zone.function !== zone.name ? ["Function", h("span.mono", null, zone.function)] : null,
			zone.text ? ["Text", zone.text] : null,
			zone.value !== null && zone.value !== undefined ? ["Value", String(zone.value)] : null,
			["Starts", duration(zone.startNs)],
		].filter(Boolean);
		const most = Math.max(1, ...zone.children.map((c) => c.totalNs));
		replace(this.el,
			h("div.zd", null,
				h("div.zname", null, icon("symbol-event"), zone.name),
				h("div.kv", null, kv.map(([k, v]) => [h("span.k", null, k), h("span", null, v)])),
				h("div.acts", null,
					source ? h("button.btn", { on: { click: () => this.openSource(zone) } }, icon("go-to-file"), "Open source") : null,
					h("button.btn", { on: { click: () => this.findZone(zone.name) } }, icon("search"), "Find zone"),
					h("button.btn", { on: { click: () => this.askAi(zone) } }, icon("sparkle", "c-ai"), "Ask AI"))),
			zone.parents.length ? [h("div.group-head", null, "Inside"), h("div.kids", null, zone.parents.map((p) => h("div.row", null, h("span.name.ellip", null, p.name), h("span.meta", null, duration(p.durationNs)))))] : null,
			h("div.group-head", null, "Children", h("span.meta", null, count(zone.childCount))),
			h("div.kids", null, zone.children.length ? zone.children.map((c) => h("div.row", { title: count(c.count) + "x" },
				h("span.name.ellip", null, c.name), h("span.bar", null, h("i", { style: { width: Math.round(c.totalNs / most * 100) + "%" } })), h("span.meta", null, duration(c.totalNs))))
				: h("div.row.muted", null, "No zones inside")),
			zone.callstack.length ? [h("div.group-head", null, "Call stack"), h("div.mono.prof-stack", null, zone.callstack.map((f) => h("div", null, f)))] : null);
	}
}
