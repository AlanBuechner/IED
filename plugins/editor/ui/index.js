// index.js - the editor plugin's UI module (docs/02 "UI modules"): the views its manifest names (SolutionExplorer,
// Output, EditorStatus), the "doc:<path>" tab kind (Monaco, or a read-only view when Monaco is not installed), and the
// handlers of its commands, which act on the view they came from (menu args) or the last one used.

import { h, replace } from "/ui-kit/dom.js";
import { Explorer } from "./explorer.js";
import { Output as OutputView } from "./output.js";
import { DocumentView } from "./document.js";
import { CodeEditor } from "./editorview.js";
import { Editors } from "./editors.js";
import { loadMonaco } from "./monaco.js";
import { fileIconSpec } from "./fileicons.js";

const explorers = new Set();
const outputs = new Set();
const editors = new Editors();

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
	ied.commands.handle("ied.editor.save", () => {
		const editor = editors.byRef(ied.context.get("activeTab")) || editors.last();
		return editor ? editor.save() : undefined;
	});
	ied.commands.handle("ied.editor.saveAll", () => Promise.all(editors.all().filter((e) => e.sync.dirty).map((e) => e.save())));
}

export function SolutionExplorer(ctx) {
	return new Explorer(ctx, explorers);
}

export function Output(ctx) {
	return new OutputView(ctx, outputs);
}

// Monaco when it loads; otherwise the read-only view, saying why.
export async function TextDocument(ctx) {
	const path = ctx.ref.slice("doc:".length);
	try {
		const monaco = await loadMonaco(ctx.ied);
		return new CodeEditor(ctx, path, monaco, editors);
	}
	catch (e) {
		ctx.ied.report("the code editor is not available, so " + path + " opens read-only: " + e.message, e);
		return new DocumentView(ctx, path);
	}
}

// Ln, Col, indentation, line endings and language of the last focused editor (F-SHL-5).
export function EditorStatus(ctx) {
	const render = (editor) => {
		const facts = editor ? editor.describe() : null;
		ctx.el.hidden = !facts;
		if (!facts) return;
		replace(ctx.el,
			facts.position ? h("span.st-part", null, "Ln " + facts.position.line + ", Col " + facts.position.col) : null,
			h("span.st-part", null, facts.indent), h("span.st-part", null, facts.eol), h("span.st-part", null, facts.language),
			facts.readOnly ? h("span.st-part", null, "Read-only") : null);
	};
	render(editors.last());
	return { dispose: editors.onChange(render) };
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
