// details.js - the zone details side panel (F-PRF-15, mockup 06): the selected zone's name, duration and self time,
// thread or task, frame, source (a click opens it in the editor), text and value, what happened in it, call stack, the
// zones around it and its children by name; or a selected lock hold (F-PRF-18): its wait and hold, the threads that held the lock while it
// waited and those that waited while it held. On a phone it is a sheet under the timeline.

import { h, icon, replace } from "/ui-kit/dom.js";
import { bytes, duration, count, instant } from "./format.js";

// What happened in a zone on its thread (prof.zone's `inside`): what it allocated and the native memory it took, its
// children's included, and its own share; its lock waits, the longest first; the collections that stopped it; its
// messages.
function happenedInside(inside) {
	if (!inside) return null;
	const a = inside.allocations;
	const allocated = a && a.count ? count(a.count) + " (" + bytes(a.bytes) + ")"
		+ (a.selfCount !== a.count ? " \u00b7 self " + count(a.selfCount) + " (" + bytes(a.selfBytes) + ")" : "")
		+ (a.freedInside > 0 ? " \u00b7 " + count(a.freedInside) + " freed in it" : "") : null;
	const native = a && a.nativeBytes ? bytes(a.nativeBytes) + (a.selfNativeBytes !== a.nativeBytes ? " \u00b7 self " + bytes(a.selfNativeBytes) : "") : null;
	const kv = [
		allocated ? ["Allocated", allocated] : null,
		native ? ["Native memory", native] : null,
		inside.waitNs ? ["Lock waits", duration(inside.waitNs)] : null,
		inside.gcCount ? ["Collections", count(inside.gcCount) + ", paused " + duration(inside.gcPauseNs)] : null,
		inside.messageCount ? ["Messages", count(inside.messageCount)] : null,
	].filter(Boolean);
	if (!kv.length) return null;
	return [h("div.group-head", null, "In this zone"),
		h("div.zd", null, h("div.kv", null, kv.map(([k, v]) => [h("span.k", null, k), h("span", null, v)]))),
		inside.waits.length ? h("div.kids", null, inside.waits.map((w) => h("div.row", null, h("span.name.ellip", null, w.lock), h("span.meta", null, duration(w.durationNs))))) : null];
}

export class ZoneDetails {
	constructor({ request, openSource, findZone, askAi, showLocks, phone }) {
		this.request = request;
		this.openSource = openSource;
		this.findZone = findZone;
		this.askAi = askAi;
		this.showLocks = showLocks;
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
			["Starts", instant(zone.startNs, zone.durationNs)],
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
			happenedInside(zone.inside),
			zone.parents.length ? [h("div.group-head", null, "Inside of"), h("div.kids", null, zone.parents.map((p) => h("div.row", null, h("span.name.ellip", null, p.name), h("span.meta", null, duration(p.durationNs)))))] : null,
			h("div.group-head", null, "Children", h("span.meta", null, count(zone.childCount))),
			h("div.kids", null, zone.children.length ? zone.children.map((c) => h("div.row", { title: count(c.count) + "x" },
				h("span.name.ellip", null, c.name), h("span.bar", null, h("i", { style: { width: Math.round(c.totalNs / most * 100) + "%" } })), h("span.meta", null, duration(c.totalNs))))
				: h("div.row.muted", null, "No zones inside")),
			zone.callstack.length ? [h("div.group-head", null, "Call stack"), h("div.mono.prof-stack", null, zone.callstack.map((f) => h("div", null, f)))] : null);
	}

	// One hold of a lock (F-PRF-18): how long the thread waited and held it, who held it meanwhile and who waited.
	async showLock(captureId, pick) {
		const hold = await this.request("prof.lockHold", { captureId, lock: pick.lock, track: pick.track, index: pick.item[3] });
		if (!hold) return;
		this.zone = null;
		const source = hold.file ? hold.file + (hold.line ? ":" + hold.line : "") : "";
		const states = { held: "Held alone", contended: "Held while others waited", waiting: "Still waiting at the end" };
		const kv = [
			["State", h("span" + (hold.state === "waiting" ? ".c-error" : hold.state === "contended" ? ".c-warn" : ""), null, states[hold.state] || hold.state)],
			["Thread", hold.thread],
			["Waited", duration(hold.waitNs) + (hold.waitNs ? " from " + instant(hold.waitFromNs, hold.waitNs) : "")],
			hold.obtainNs === null ? null : ["Held", duration(hold.holdNs) + (hold.releaseNs === null ? " (still held)" : "") + (hold.shared ? ", shared" : "")],
			source ? ["Source", h("a.mono.prof-link", { href: "#", on: { click: (e) => { e.preventDefault(); this.openSource(hold); } } }, source)] : null,
		].filter(Boolean);
		const others = (title, list) => list.length ? [h("div.group-head", null, title), h("div.kids", null, list.map((o) => h("div.row", null, h("span.name.ellip", null, o.thread), h("span.meta.when", null, duration(o.toNs - o.fromNs) + " from " + instant(o.fromNs, o.toNs - o.fromNs)))))] : null;
		const t = hold.totals;
		replace(this.el,
			h("div.zd", null,
				h("div.zname", null, icon("lock"), hold.name),
				h("div.kv", null, kv.map(([k, v]) => [h("span.k", null, k), h("span", null, v)])),
				h("div.acts", null,
					source ? h("button.btn", { on: { click: () => this.openSource(hold) } }, icon("go-to-file"), "Open source") : null,
					this.showLocks ? h("button.btn", { on: { click: () => this.showLocks() } }, icon("lock"), "Lock statistics") : null)),
			others("Held by, while it waited", hold.heldBy),
			others("Waited for by, while it held", hold.waitedBy),
			h("div.group-head", null, "This lock in the capture"),
			h("div.zd", null, h("div.kv", null,
				h("span.k", null, "Holds"), h("span", null, count(t.acquisitions) + ", " + count(t.contended) + " contended"),
				h("span.k", null, "Waits"), h("span", null, count(t.blocked) + ", " + duration(t.waitNs) + " in all, the longest " + duration(t.maxWaitNs)),
				h("span.k", null, "Threads"), h("span", null, count(t.threads)))));
	}
}
