// index.js - the profiler's UI module (docs/16, mockup 06): the Profiler activity with its Captures tool window, each
// capture a document (prof:<captureId>), the status bar's "Capturing" while a program records, and the commands -
// Start Profiling (the Profile task, Alt+F2), Stop, open and import a trace from the Solution Explorer, and how to
// connect a program started elsewhere.

import { h, icon, replace } from "/ui-kit/dom.js";
import { promptDialog } from "/ui-kit/dialog.js";
import { CaptureView } from "./capture.js";
import { CaptureLibraryView } from "./library.js";

const names = new Map();
// How long Start Profiling waits for its program to connect (a build may come first).
const FOLLOW_FOR_MS = 10 * 60 * 1000;

function remember(capture) {
	const meta = capture.metadata || {};
	names.set(capture.captureId, meta.app || capture.app || capture.name.replace(/\.iedtrace$/, ""));
}

async function openCapture(ied, captureId) {
	await ied.commands.run("shell.showActivity", { id: "ied.profiler" });
	ied.tabs.open("prof:" + captureId, { preview: false });
}

function failed(ied, what) {
	return (e) => {
		ied.toast(what + ": " + (e.message || e), { level: "error" });
		return null;
	};
}

async function startProfiling(ied) {
	const wsId = ied.workspace.wsId;
	const library = await ied.client.request("prof.captures", { wsId }).catch(failed(ied, "Cannot ask the profiler"));
	if (!library) return;
	const stopFollowing = openNextCapture(ied, wsId, new Set(library.captures.map((c) => c.captureId)));
	const run = await ied.client.request("task.run", { wsId, slot: "profile" }).catch((e) => {
		const missing = e.code === "not_found";
		ied.toast(missing ? "The startup project has no Profile task. Add a task in the \"profile\" slot that starts the program with IED_PROFILER set to ${profiler.connect}." : "Cannot start profiling: " + e.message, { level: missing ? "warning" : "error" });
		return null;
	});
	if (!run) {
		stopFollowing();
		return;
	}
	await ied.commands.run("shell.showActivity", { id: "ied.profiler" });
}

// Opens the first live capture of workspace `wsId` that is not among `known`: the program the Profile task started,
// once it connects. Returns what stops waiting.
export function openNextCapture(ied, wsId, known) {
	let following = true;
	const look = (data) => {
		const live = following && (data.captures || []).find((c) => c.live && !known.has(c.captureId));
		if (!live) return;
		end();
		remember(live);
		openCapture(ied, live.captureId);
	};
	const stop = ied.client.subscribe("profs:" + wsId, { snap: look, evt: (kind, data) => look(data) });
	const timer = setTimeout(() => end(), FOLLOW_FOR_MS);
	function end() {
		if (!following) return;
		following = false;
		clearTimeout(timer);
		stop();
	}
	return end;
}

async function stopProfiling(ied) {
	const library = await ied.client.request("prof.captures", { wsId: ied.workspace.wsId }).catch(failed(ied, "Cannot list the captures"));
	const live = library ? library.captures.filter((c) => c.live) : [];
	if (!live.length) ied.toast("Nothing is being recorded.", { level: "info" });
	for (const capture of live) await ied.client.request("prof.stop", { captureId: capture.captureId }).catch(failed(ied, "Cannot stop the capture"));
}

async function showConnect(ied) {
	const library = await ied.client.request("prof.captures", { wsId: ied.workspace.wsId }).catch(failed(ied, "Cannot ask the profiler"));
	if (!library) return;
	await promptDialog({
		title: "Connect a program to the profiler",
		message: "Start the program with this environment variable; its ITP client (Bedrock Profile::, the C/C++, Go or Rust client) connects and records into this workspace. IED_PROFILER=file:<path> writes a trace file instead.",
		value: "IED_PROFILER=" + library.connect.env,
		confirm: "Done",
	});
}

export function activate(ied) {
	if (!document.querySelector("link[href='" + ied.plugin.base + "ui/profiler.css']")) document.head.append(h("link", { rel: "stylesheet", href: ied.plugin.base + "ui/profiler.css" }));
	ied.tabs.kind("prof", { view: "CaptureDocument", title: (id) => names.get(id) || "Capture", icon: () => "$(pulse)" });
	ied.commands.handle("ied.profiler.show", () => ied.commands.run("shell.showActivity", { id: "ied.profiler" }));
	ied.commands.handle("ied.profiler.profile", () => startProfiling(ied));
	ied.commands.handle("ied.profiler.stop", () => stopProfiling(ied));
	ied.commands.handle("ied.profiler.connect", () => showConnect(ied));
	ied.commands.handle("ied.profiler.open", async (args) => {
		const path = args && args.path ? args.path : ied.context.get("resourcePath");
		if (!path) return;
		const opened = await ied.client.request("prof.open", { wsId: ied.workspace.wsId, path }).catch(failed(ied, "Cannot open the trace"));
		if (opened) openCapture(ied, opened.captureId);
	});
	ied.commands.handle("ied.profiler.import", async (args) => {
		const path = args && args.path ? args.path : ied.context.get("resourcePath");
		if (!path) return;
		const imported = await ied.client.request("prof.import", { wsId: ied.workspace.wsId, path }).catch(failed(ied, "Cannot import " + path));
		if (imported) openCapture(ied, imported.captureId);
	});
}

export function CaptureLibrary(ctx) {
	const view = new CaptureLibraryView(ctx, { openCapture: (id) => openCapture(ctx.ied, id), remember });
	return { dispose: () => view.dispose() };
}

export function CaptureDocument(ctx) {
	const view = new CaptureView(ctx, ctx.ref.slice("prof:".length));
	return { dispose: () => view.dispose(), focus: () => view.timeline.canvas.focus({ preventScroll: true }) };
}

// The status bar's "Capturing <app>" while a program of the workspace records; a click opens it.
export function ProfilerStatus(ctx) {
	let live = null;
	const render = (data) => {
		live = (data.captures || []).find((c) => c.live) || null;
		ctx.el.hidden = !live;
		if (live) {
			remember(live);
			replace(ctx.el, icon("record", "c-error"), "Capturing \u00b7 " + (names.get(live.captureId) || "program"));
		}
	};
	ctx.el.hidden = true;
	ctx.el.addEventListener("click", () => live && openCapture(ctx.ied, live.captureId));
	const stop = ctx.ied.client.subscribe("profs:" + ctx.wsId, { snap: render, evt: (kind, data) => render(data) });
	return { dispose: () => stop() };
}
