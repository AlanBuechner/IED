// index.js - the GPU debugger's UI module (docs/22, mockup 07): the GPU activity with its Capture Library, each capture
// a document (gpu:<captureId>), and the commands - Launch for Capture (Alt+F5) and Capture Next Frame. A capture taken
// from this window opens by itself when its file lands.

import { h } from "/ui-kit/dom.js";
import { CaptureView } from "./capture.js";
import { CaptureLibraryView } from "./library.js";
import { DOT } from "./format.js";

const names = new Map();
let expecting = null; // the capture ids known when Capture Next Frame was asked for

function remember(capture) {
	const meta = capture.meta || {};
	names.set(capture.id, (meta.program || capture.id) + (meta.frame !== undefined ? " " + DOT + " f" + meta.frame : ""));
}

async function openCapture(ied, captureId) {
	await ied.commands.run("shell.showActivity", { id: "ied.gpu" });
	ied.tabs.open("gpu:" + captureId, { preview: false });
}

function failed(ied, what) {
	return (e) => {
		ied.toast(what + ": " + (e.message || e), { level: "error" });
		return null;
	};
}

async function launch(ied, args) {
	const params = Object.assign({ wsId: ied.workspace.wsId }, args || {});
	const started = await ied.client.request("gpu.launch", params).catch(failed(ied, "Cannot launch for capture"));
	if (started) await ied.commands.run("shell.showActivity", { id: "ied.gpu" });
}

async function captureNext(ied, args) {
	const wsId = ied.workspace.wsId;
	const status = await ied.client.request("gpu.status", { wsId }).catch(failed(ied, "Cannot ask the GPU debugger"));
	if (!status) return;
	const running = (status.targets || []).filter((t) => t.state === "running" && t.connected);
	const target = args && args.targetId ? running.find((t) => t.id === args.targetId) : running[0];
	if (!target) {
		ied.toast("No program is running with the capture library. Launch for Capture (Alt+F5) starts the startup project with it.", { level: "info" });
		return;
	}
	expecting = new Set((status.captures || []).map((c) => c.id));
	const asked = await ied.client.request("gpu.capture", { wsId, targetId: target.id, frames: (args && args.frames) || 1 }).catch(failed(ied, "Cannot capture"));
	if (!asked) expecting = null;
}

// The first capture that appears after Capture Next Frame opens.
function followNewCaptures(ied) {
	ied.client.subscribe("gpu:" + ied.workspace.wsId, {
		snap: (data) => (data.captures || []).forEach(remember),
		evt: (kind, data) => {
			if (kind !== "captures") return;
			const captures = data.captures || [];
			captures.forEach(remember);
			if (!expecting) return;
			const added = captures.find((c) => !expecting.has(c.id));
			if (!added) return;
			expecting = null;
			openCapture(ied, added.id);
		},
	});
}

export function activate(ied) {
	if (!document.querySelector("link[href='" + ied.plugin.base + "ui/gpu.css']")) document.head.append(h("link", { rel: "stylesheet", href: ied.plugin.base + "ui/gpu.css" }));
	ied.tabs.kind("gpu", { view: "CaptureDocument", title: (id) => names.get(id) || "Capture", icon: () => "$(device-camera)" });
	ied.commands.handle("ied.gpu.show", () => ied.commands.run("shell.showActivity", { id: "ied.gpu" }));
	ied.commands.handle("ied.gpu.library", () => ied.commands.run("shell.showView", { id: "ied.gpu.library" }));
	ied.commands.handle("ied.gpu.launch", (args) => launch(ied, args));
	ied.commands.handle("ied.gpu.captureNext", (args) => captureNext(ied, args));
	if (ied.workspace.wsId) followNewCaptures(ied);
}

export function CaptureLibrary(ctx) {
	const view = new CaptureLibraryView(ctx, { openCapture: (id) => openCapture(ctx.ied, id), remember });
	return { dispose: () => view.dispose() };
}

export function CaptureDocument(ctx) {
	const view = new CaptureView(ctx, ctx.ref.slice("gpu:".length));
	return { dispose: () => view.dispose(), focus: () => view.events.el.focus({ preventScroll: true }) };
}
