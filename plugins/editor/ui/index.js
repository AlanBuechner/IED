// index.js - the editor plugin's UI module (docs/02 "UI modules"): the views its manifest names (SolutionExplorer,
// Output), the "doc:<path>" tab kind, and the handlers of its commands. The commands act on the view they came from
// (menu args) or on the last one used.

import { Explorer } from "./explorer.js";
import { Output as OutputView } from "./output.js";
import { DocumentView } from "./document.js";
import { fileIconSpec } from "./fileicons.js";

const explorers = new Set();
const outputs = new Set();

function last(set) {
	return [...set].pop() || null;
}

function baseName(path) {
	return path.slice(path.lastIndexOf("/") + 1);
}

export function activate(ied) {
	addStylesheet(ied.plugin.base + "ui/editor.css");
	ied.tabs.kind("doc", { view: "TextDocument", title: baseName, icon: fileIconSpec, resource: (path) => path });
	const pathOf = (args) => (args && args.path !== undefined ? args.path : last(explorers) && last(explorers).selected() ? last(explorers).selected().path : null);
	ied.commands.handle("ied.explorer.open", (args) => {
		const path = pathOf(args);
		if (path) ied.tabs.open("doc:" + path, { preview: false });
	});
	ied.commands.handle("ied.explorer.refresh", () => Promise.all([...explorers].map((e) => e.refresh())));
	ied.commands.handle("ied.explorer.collapseAll", () => explorers.forEach((e) => e.tree.collapseAll()));
	ied.commands.handle("ied.explorer.copyPath", (args) => copy(fullPath(ied, pathOf(args))));
	ied.commands.handle("ied.explorer.copyRelativePath", (args) => copy(pathOf(args) || ""));
	ied.commands.handle("ied.output.clear", () => last(outputs) && last(outputs).clear());
	ied.commands.handle("ied.output.toggleWrap", () => last(outputs) && last(outputs).toggleWrap());
}

export function SolutionExplorer(ctx) {
	return new Explorer(ctx, explorers);
}

export function Output(ctx) {
	return new OutputView(ctx, outputs);
}

export function TextDocument(ctx) {
	return new DocumentView(ctx, ctx.ref.slice("doc:".length));
}

function fullPath(ied, path) {
	const root = ied.workspace.root.replace(/[\\/]+$/, "");
	const separator = root.includes("\\") ? "\\" : "/";
	return path ? root + separator + path.split("/").join(separator) : root;
}

async function copy(text) {
	await navigator.clipboard.writeText(text);
}

function addStylesheet(href) {
	if (document.querySelector('link[href="' + href + '"]')) return;
	document.head.append(Object.assign(document.createElement("link"), { rel: "stylesheet", href }));
}
