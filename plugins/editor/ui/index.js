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
import { Diagnostics } from "./diagnostics.js";
import { LanguageFeatures } from "./language.js";
import { ErrorList as ErrorListView } from "./errorlist.js";
import { FindInFiles as FindInFilesView } from "./findinfiles.js";
import { findCounterpart } from "./headersource.js";
import { showGoToAll } from "./gotoall.js";
import { fileName } from "./fileuris.js";

const explorers = new Set();
const outputs = new Set();
const errorLists = new Set();
const finders = new Set();
const editors = new Editors();
let diagnostics = null;
let features = null;

function last(set) {
	return [...set].pop() || null;
}

export function activate(ied) {
	addStylesheet(ied.plugin.base + "ui/editor.css");
	diagnostics = new Diagnostics(ied);
	ied.tabs.kind("doc", { view: "TextDocument", title: fileName, icon: (path) => fileIconSpec(fileName(path)), resource: (path) => path });
	const pathOf = (args) => (args && args.path !== undefined ? args.path : last(explorers) && last(explorers).selected() ? last(explorers).selected().path : null);
	ied.commands.handle("ied.explorer.open", (args) => {
		const path = pathOf(args);
		if (path) ied.tabs.open("doc:" + path, { preview: false });
	});
	ied.commands.handle("ied.explorer.refresh", () => Promise.all([...explorers].map((e) => e.refresh())));
	ied.commands.handle("ied.explorer.collapseAll", () => explorers.forEach((e) => e.tree.collapseAll()));
	ied.commands.handle("ied.explorer.copyPath", (args) => copy(fullPath(ied, pathOf(args))));
	ied.commands.handle("ied.explorer.copyRelativePath", (args) => copy(pathOf(args) || ""));
	const explorer = () => last(explorers);
	ied.commands.handle("ied.explorer.newFile", () => explorer() && explorer().create("file"));
	ied.commands.handle("ied.explorer.newFolder", () => explorer() && explorer().create("folder"));
	ied.commands.handle("ied.explorer.rename", () => explorer() && explorer().rename(explorer().selected()));
	ied.commands.handle("ied.explorer.delete", () => explorer() && explorer().remove(explorer().selected()));
	ied.commands.handle("ied.output.clear", () => last(outputs) && last(outputs).clear());
	ied.commands.handle("ied.output.toggleWrap", () => last(outputs) && last(outputs).toggleWrap());
	ied.commands.handle("ied.editor.save", () => {
		const editor = editors.byRef(ied.context.get("activeTab")) || editors.last();
		return editor ? editor.save() : undefined;
	});
	ied.commands.handle("ied.editor.saveAll", () => Promise.all(editors.all().filter((e) => e.sync.dirty).map((e) => e.save())));
	ied.commands.handle("ied.editor.goTo", (args) => goTo(ied, args));
	ied.commands.handle("ied.find.inFiles", () => findInFiles(ied));
	ied.commands.handle("ied.editor.goToAll", () => showGoToAll(ied, (target) => goTo(ied, target)));
	ied.commands.handle("ied.editor.toggleHeader", () => toggleHeader(ied));
	ied.commands.handle("ied.editor.action", (args) => runEditorAction(ied, args));
	ied.commands.handle("ied.navigate.back", () => goTo(ied, editors.navigation.back()));
	ied.commands.handle("ied.navigate.forward", () => goTo(ied, editors.navigation.forward()));
}

function currentEditor(ied) {
	return editors.byRef(ied.context.get("activeTab")) || editors.last();
}

// A Monaco action on the current editor ({id}): the shell takes Ctrl+K as a chord's first key, so the editor's own
// Ctrl+K chords come back through here.
function runEditorAction(ied, args) {
	const editor = currentEditor(ied);
	if (!editor || !args || !args.id) return undefined;
	editor.editor.focus();
	return editor.editor.getAction(args.id) ? editor.editor.getAction(args.id).run() : editor.editor.trigger("ied", args.id, null);
}

async function toggleHeader(ied) {
	const editor = currentEditor(ied);
	if (!editor) return;
	const pairs = ied.contributions("headerSourcePairs").map((c) => c.value);
	const found = await findCounterpart(ied.client, editor.ctx.wsId, editor.path, pairs);
	if (found) {
		ied.tabs.open("doc:" + found, { preview: false });
		(await editors.whenReady("doc:" + found)).focus();
	}
	else ied.toast("There is no header or code file for " + editor.path.slice(editor.path.lastIndexOf("/") + 1) + ".");
}

// Shows Find in Files with the editor's selection (one line of it) as the query.
async function findInFiles(ied) {
	const editor = editors.last();
	const selection = editor && editor.model ? editor.editor.getSelection() : null;
	const text = selection && !selection.isEmpty() && selection.startLineNumber === selection.endLineNumber ? editor.model.getValueInRange(selection) : "";
	await ied.commands.run("shell.showView", { id: "ied.find" });
	requestAnimationFrame(() => {
		const finder = last(finders);
		if (finder) finder.start(text);
	});
}

// Opens a document at a place: {path, line, col} (0-based, UTF-16).
async function goTo(ied, args) {
	if (!args || !args.path) return;
	const ref = "doc:" + args.path;
	ied.tabs.open(ref, { preview: false });
	const editor = await editors.whenReady(ref);
	if (args.line !== undefined) editor.reveal({ lineNumber: args.line + 1, column: (args.col || 0) + 1 });
}

export function SolutionExplorer(ctx) {
	return new Explorer(ctx, explorers);
}

export function Output(ctx) {
	return new OutputView(ctx, outputs);
}

export function ErrorList(ctx) {
	return new ErrorListView(ctx, diagnostics, errorLists);
}

export function FindInFiles(ctx) {
	return new FindInFilesView(ctx, finders);
}

// Monaco when it loads; otherwise the read-only view, saying why.
export async function TextDocument(ctx) {
	const path = ctx.ref.slice("doc:".length);
	try {
		const monaco = await loadMonaco(ctx.ied);
		if (!features) features = new LanguageFeatures(ctx.ied, monaco, editors, diagnostics);
		return new CodeEditor(ctx, path, monaco, editors, features);
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
