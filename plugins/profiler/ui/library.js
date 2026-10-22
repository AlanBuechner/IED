// library.js - the Captures tool window (F-PRF-6): the workspace's captures, live ones first, each with its app, when
// it started, how long it ran, its zones and size; a click opens it, its menu deletes it. The header says how a
// program connects (the IED_PROFILER value the Profile task passes) and imports other tools' traces.

import { h, icon, replace } from "/ui-kit/dom.js";
import { confirmDialog, promptDialog } from "/ui-kit/dialog.js";
import { MenuHost } from "/ui-kit/menu.js";
import { duration, bytes, count, when } from "./format.js";

export class CaptureLibraryView {
	constructor(ctx, { openCapture, remember }) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.openCapture = openCapture;
		this.remember = remember;
		this.menus = new MenuHost();
		this.list = h("div.prof-library-list", { role: "listbox", "aria-label": "Captures" });
		this.connect = h("div.prof-connect");
		ctx.el.classList.add("prof-library");
		replace(ctx.el,
			h("div.panel-tools", null,
				h("button.btn.flat", { title: "Run the startup project's Profile task (Alt+F2)", on: { click: () => this.ied.commands.run("ied.profiler.profile") } }, icon("pulse"), "Profile"),
				h("button.btn.flat", { title: "Import a Chrome trace, pprof profile or .iedtrace from the workspace", on: { click: () => this.importFile() } }, icon("cloud-upload"), "Import"),
				h("span.grow"),
				h("button.ib", { title: "Refresh", "aria-label": "Refresh", on: { click: () => this.refresh() } }, icon("refresh"))),
			this.connect,
			h("div.scroll", null, this.list));
		this.stop = this.ied.client.subscribe("profs:" + ctx.wsId, {
			snap: (data) => this.render(data),
			evt: (kind, data) => this.render(data),
		});
	}

	dispose() {
		this.stop();
	}

	async refresh() {
		const data = await this.ied.client.request("prof.captures", { wsId: this.ctx.wsId }).catch((e) => this.ied.report("Cannot list the captures", e));
		if (data) this.render(data);
	}

	render(data) {
		const captures = data.captures || [];
		for (const capture of captures) this.remember(capture);
		const env = data.connect ? data.connect.env : "";
		replace(this.connect, env ? [
			h("span.muted", null, "Programs connect with "),
			h("code.mono.prof-env", { title: "IED_PROFILER=" + env }, "IED_PROFILER=" + env),
			h("button.ib", { title: "Copy", "aria-label": "Copy the IED_PROFILER value", on: { click: () => navigator.clipboard && navigator.clipboard.writeText(env) } }, icon("copy")),
		] : h("span.muted", null, "The profiler is not listening."));
		if (!captures.length) {
			replace(this.list, h("div.prof-empty.muted", null, "No captures yet. Run the Profile task (Alt+F2), or start an instrumented program with IED_PROFILER set to the value above."));
			return;
		}
		replace(this.list, captures.map((c) => this.row(c)));
	}

	row(capture) {
		const meta = capture.metadata || {};
		const app = meta.app || capture.app || capture.name.replace(/\.iedtrace$/, "");
		const details = capture.live ? "recording" : [meta.durationNs ? duration(meta.durationNs) : null, meta.zones ? count(meta.zones) + " zones" : null, bytes(capture.sizeBytes)].filter(Boolean).join(" \u00b7 ");
		return h("div.row.prof-capture" + (capture.live ? ".live" : ""), {
			role: "option", tabindex: "0", title: capture.path, dataset: { capture: capture.captureId },
			on: {
				click: () => this.openCapture(capture.captureId),
				keydown: (e) => { if (e.key === "Enter") this.openCapture(capture.captureId); },
				contextmenu: (e) => this.menu(e, capture),
			},
		}, capture.live ? h("i.prof-dot") : icon("pulse"), h("span.grow.ellip", null, h("span", null, app), h("span.meta", null, " " + when(meta.startUnixNs || capture.modifiedNs))), h("span.meta", null, details));
	}

	menu(e, capture) {
		e.preventDefault();
		this.menus.show({
			at: { x: e.clientX, y: e.clientY },
			rows: [
				{ kind: "item", id: "open", label: "Open", icon: "pulse" },
				{ kind: "item", id: "stop", label: "Stop recording", icon: "debug-stop", enabled: capture.live },
				{ kind: "separator" },
				{ kind: "item", id: "delete", label: "Delete", icon: "trash", enabled: !capture.live },
			],
			pick: (row) => this.act(row.id, capture),
		});
	}

	async act(id, capture) {
		if (id === "open") this.openCapture(capture.captureId);
		else if (id === "stop") await this.ied.client.request("prof.stop", { captureId: capture.captureId }).catch((e) => this.ied.report("Cannot stop the capture", e));
		else if (id === "delete" && await confirmDialog({ title: "Delete capture", message: "Move " + capture.path + " to the trash?", confirm: "Delete", danger: true }))
			await this.ied.client.request("prof.delete", { captureId: capture.captureId }).catch((e) => this.ied.report("Cannot delete the capture", e));
	}

	async importFile() {
		const path = await promptDialog({ title: "Import a trace", message: "A Chrome trace (.json), a pprof profile or an .iedtrace, by its path in the workspace.", placeholder: "traces/trace.json", confirm: "Import" });
		if (path) this.ied.commands.run("ied.profiler.import", { path });
	}
}
