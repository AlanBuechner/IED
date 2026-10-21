// library.js - the Capture Library tool window (F-GPU-1): the programs launched for capture, with Capture Next Frame
// and Stop, and the workspace's captures as cards - thumbnail, program, API, frame, GPU, size, when, comment. A click
// opens a capture as a document; its menu comments on it or deletes it.

import { h, icon, replace } from "/ui-kit/dom.js";
import { confirmDialog, promptDialog } from "/ui-kit/dialog.js";
import { MenuHost } from "/ui-kit/menu.js";
import { bytes, captureTime } from "./format.js";

export class CaptureLibraryView {
	constructor(ctx, { openCapture, remember }) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.openCapture = openCapture;
		this.remember = remember;
		this.menus = new MenuHost();
		this.targets = h("div.gpu-targets");
		this.captures = h("div");
		this.captureButton = h("button.btn.flat", { title: "Capture the next frame of the running program", disabled: true, on: { click: () => this.captureNext() } }, icon("record"), "Capture Next Frame");
		ctx.el.classList.add("gpu-library");
		replace(ctx.el,
			h("div.gpu-bar", null,
				h("button.btn.flat", { title: "Build the startup project and start it for capture (Alt+F5)", on: { click: () => this.ied.commands.run("ied.gpu.launch") } }, icon("device-camera"), "Launch for Capture"),
				this.captureButton,
				h("span.grow"),
				h("button.ib", { title: "Refresh", "aria-label": "Refresh", on: { click: () => this.refresh() } }, icon("refresh"))),
			h("div.scroll", null, this.targets, this.captures));
		this.data = { captures: [], targets: [] };
		this.stop = this.ied.client.subscribe("gpu:" + ctx.wsId, {
			snap: (data) => this.render(data),
			evt: (kind, data) => this.render(Object.assign({}, this.data, data)),
		});
	}

	dispose() {
		this.stop();
	}

	async refresh() {
		const data = await this.ied.client.request("gpu.rescan", { wsId: this.ctx.wsId }).catch((e) => this.ied.report("Cannot read the captures", e));
		if (data) this.render(data);
	}

	render(data) {
		this.data = data;
		const live = (data.targets || []).filter((t) => t.state === "running");
		this.captureButton.disabled = !live.some((t) => t.connected);
		replace(this.targets, live.length ? [h("div.gpu-section", null, "Running"), live.map((t) => this.targetRow(t))] : null);
		this.targets.hidden = !live.length;
		const captures = data.captures || [];
		for (const capture of captures) this.remember(capture);
		if (!captures.length) {
			replace(this.captures, h("div.gpu-empty", null,
				h("p", null, "No captures yet. ", h("b", null, "Launch for Capture"), " (Alt+F5) builds the startup project and starts it with the capture library; then ", h("b", null, "Capture Next Frame"), "."),
				h("p", null, "Vulkan programs are captured as they are. A D3D12 program hands its device and DXGI factory to the capture library: ", h("code", null, "Gpu::Capture"), " in C4, ", h("code", null, "flashbang_app.h"), " in C and C++."),
				h("p", null, "Captures are kept in ", h("code", null, data.captureFolder || ".ied/local/gpu-captures"), ".")));
			return;
		}
		replace(this.captures, h("div.gpu-section", null, "Captures"), h("div.gpu-captures", null, captures.map((c) => this.card(c))));
	}

	targetRow(target) {
		const apis = (target.apis || []).join(", ");
		const state = target.capturing ? "capturing" : target.connected ? "frame " + target.frame : "waiting for the capture library";
		return h("div.gpu-target", null,
			h("i.gpu-dot" + (target.capturing ? ".rec" : target.connected ? ".on" : "")),
			h("span.name", null, target.name),
			h("span.meta.grow", null, ["pid " + target.pid, apis, state].filter(Boolean).join(" \u00b7 ")),
			h("button.ib", { title: "Capture the next frame", "aria-label": "Capture the next frame", disabled: !target.connected, on: { click: () => this.capture(target) } }, icon("record")),
			h("button.ib", { title: "Stop the program", "aria-label": "Stop the program", on: { click: () => this.stopTarget(target) } }, icon("debug-stop")));
	}

	card(capture) {
		const meta = capture.meta || {};
		const details = ["frame " + (meta.frame ?? "?"), meta.gpu, bytes(capture.bytes)].filter(Boolean).join(" \u00b7 ");
		const warnings = (meta.warnings || []).length;
		return h("div.gpu-capture", {
			role: "button", tabindex: "0", title: capture.file,
			on: {
				click: () => this.openCapture(capture.id),
				keydown: (e) => { if (e.key === "Enter") this.openCapture(capture.id); },
				contextmenu: (e) => this.menu(e, capture),
			},
		},
			h("div.thumb", null, capture.thumbnail ? h("img", { src: capture.thumbnail, alt: "", loading: "lazy" }) : icon("device-camera", "lg")),
			h("div.info", null,
				h("div.title", null, h("span.ellip", null, meta.program || capture.id), meta.api ? h("span.gpu-api", null, meta.api) : null,
					warnings ? h("span.c-warn", { title: (meta.warnings || []).join("\n") }, icon("warning", "sm")) : null),
				h("div.meta", null, details),
				h("div.meta", null, captureTime(meta.time)),
				meta.comment ? h("div.comment", null, meta.comment) : null));
	}

	menu(e, capture) {
		e.preventDefault();
		this.menus.show({
			at: { x: e.clientX, y: e.clientY },
			rows: [
				{ kind: "item", id: "open", label: "Open", icon: "$(device-camera)", enabled: true },
				{ kind: "item", id: "comment", label: "Comment...", icon: "$(comment)", enabled: true },
				{ kind: "item", id: "copy", label: "Copy Path", icon: "$(copy)", enabled: !!navigator.clipboard },
				{ kind: "separator" },
				{ kind: "item", id: "delete", label: "Delete", icon: "$(trash)", enabled: true },
			],
			pick: (row) => this.act(row.id, capture),
		});
	}

	async act(id, capture) {
		const meta = capture.meta || {};
		if (id === "open") this.openCapture(capture.id);
		else if (id === "copy" && navigator.clipboard) navigator.clipboard.writeText((this.data.captureFolder || "") + "/" + capture.file);
		else if (id === "comment") {
			const text = await promptDialog({ title: "Comment", message: "A note kept with " + capture.file + ".", value: meta.comment || "", confirm: "Save" });
			if (text !== null && text !== undefined) await this.ied.client.request("gpu.comment", { wsId: this.ctx.wsId, captureId: capture.id, text }).catch((e) => this.ied.report("Cannot save the comment", e));
		}
		else if (id === "delete" && await confirmDialog({ title: "Delete capture", message: "Move " + capture.file + " to the trash?", confirm: "Delete", danger: true }))
			await this.ied.client.request("gpu.delete", { wsId: this.ctx.wsId, captureId: capture.id }).catch((e) => this.ied.report("Cannot delete the capture", e));
	}

	capture(target) {
		this.ied.commands.run("ied.gpu.captureNext", { targetId: target.id });
	}

	captureNext() {
		this.ied.commands.run("ied.gpu.captureNext");
	}

	async stopTarget(target) {
		await this.ied.client.request("gpu.stop", { wsId: this.ctx.wsId, targetId: target.id }).catch((e) => this.ied.report("Cannot stop the program", e));
	}
}
