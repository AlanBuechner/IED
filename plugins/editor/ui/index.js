// index.js - the editor plugin's UI module (docs/02 "UI modules"): the views its manifest names (SolutionExplorer,
// Output, EditorStatus), the "doc:<path>" tab kind (Monaco, or a read-only view when Monaco is not installed), and the
// handlers of its commands, which act on the view they came from (menu args) or the last one used.

import { h, replace } from "/ui-kit/dom.js";
import { Explorer } from "./explorer.js";
import { Output as OutputView, remember as rememberOutputSource } from "./output.js";
import { DocumentView } from "./document.js";
import { CodeEditor, editorEnvironment } from "./editorview.js";
import { Editors } from "./editors.js";
import { loadMonaco } from "./monaco.js";
import { colorize, languageOf } from "./colorize.js";
import { EditorSettings } from "./editorsettings.js";
import { Diagnostics } from "./diagnostics.js";
import { LanguageFeatures } from "./language.js";
import { ErrorList as ErrorListView } from "./errorlist.js";
import { FindInFiles as FindInFilesView } from "./findinfiles.js";
import { findCounterpart } from "./headersource.js";
import { showGoToAll } from "./gotoall.js";
import { fileName } from "/ui-kit/fileuris.js";
import { TaskState, TaskToolbar as TaskToolbarView, TaskStatus as TaskStatusView } from "./tasks.js";
import { DebugState, BreakpointState, DebugToolbar as DebugToolbarView, framePath, hoverExpression } from "./debug.js";
import { BreakpointMargin } from "./breakpointmargin.js";
import { CallStack as CallStackView, Locals as LocalsView, Watch as WatchView, Threads as ThreadsView, BreakpointsWindow } from "./debugviews.js";

const explorers = new Set();
const outputs = new Set();
const errorLists = new Set();
const finders = new Set();
const editors = new Editors();
let diagnostics = null;
let features = null;
let tasks = null;
let debug = null;
let breakpoints = null;

function last(set) {
	return [...set].pop() || null;
}

export function activate(ied) {
	addStylesheet(ied.plugin.base + "ui/editor.css");
	diagnostics = new Diagnostics(ied);
	tasks = new TaskState(ied);
	debug = new DebugState(ied);
	breakpoints = new BreakpointState(ied);
	debug.onStop = (frame) => goTo(ied, { path: framePath(frame), line: frame.line, col: frame.column });
	const status = () => ied.context.set("statusState", statusState(tasks, debug));
	tasks.on(status);
	debug.on(status);
	const settings = new EditorSettings(ied, editorEnvironment);
	editors.attach((editor) => settings.bind(editor));
	editors.attach((editor) => {
		breakpoints.follow(editor.ctx.wsId);
		debug.follow(editor.ctx.wsId);
		const margin = new BreakpointMargin(editor, editor.monaco, breakpoints, debug);
		debugHover(editor.monaco, editor.model.getLanguageId());
		return () => margin.dispose();
	});
	ied.tabs.kind("doc", { view: "TextDocument", title: fileName, icon: (path) => ied.icons.fileSpec(fileName(path)), resource: (path) => path });
	const pathOf = (args) => (args && args.path !== undefined ? args.path : last(explorers) && last(explorers).selected() ? last(explorers).selected().path : null);
	ied.bindings.provide("editor.colorize", (args) => colorize(ied, args));
	ied.commands.handle("ied.editor.toggleWhitespace", () => settings.toggleWhitespace(ied.workspace.wsId));
	ied.commands.handle("ied.editor.toggleWordWrap", () => settings.toggleWordWrap(ied.workspace.wsId));
	for (const [command, action] of EDITOR_ACTIONS) ied.commands.handle(command, () => runEditorAction(ied, { id: action }));
	ied.commands.handle("ied.editor.cutLine", () => cutLine(ied));
	ied.commands.handle("ied.editor.toggleAllOutlining", () => toggleAllOutlining(ied));
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
	ied.commands.handle("ied.editor.open", (args) => openFromOutside(ied, args));
	ied.commands.handle("ied.task.build", () => tasks.run({ slot: "build" }));
	ied.commands.handle("ied.task.rebuild", () => tasks.run({ slot: "rebuild" }));
	ied.commands.handle("ied.task.clean", () => tasks.run({ slot: "clean" }));
	ied.commands.handle("ied.debug.start", () => debug.start());
	ied.commands.handle("ied.debug.stop", () => debug.stop());
	ied.commands.handle("ied.debug.restart", () => debug.restart());
	ied.commands.handle("ied.debug.pause", () => debug.control("pause"));
	ied.commands.handle("ied.debug.stepOver", () => debug.control("next"));
	ied.commands.handle("ied.debug.stepInto", () => debug.control("stepIn"));
	ied.commands.handle("ied.debug.stepOut", () => debug.control("stepOut"));
	ied.commands.handle("ied.debug.toggleBreakpoint", () => toggleBreakpoint());
	ied.commands.handle("ied.debug.windows", () => ied.commands.run("shell.showView", { id: "ied.breakpoints" }));
	ied.commands.handle("ied.task.start", () => tasks.run({ slot: "run" }));
	ied.commands.handle("ied.task.test", () => tasks.run({ slot: "test" }));
	ied.commands.handle("ied.task.cancel", () => tasks.cancelAll());
	ied.commands.handle("ied.task.setStartup", (args) => args && args.name && tasks.setStartup(args.name));
	ied.commands.handle("ied.task.showResult", () => (tasks.ended && tasks.ended.status === "failed" && !tasks.runs.size ? ied.commands.run("shell.showView", { id: "ied.errors" }) : ied.commands.run("shell.showView", { id: "ied.output" })));
	ied.commands.handle("ied.output.show", (args) => showOutput(ied, args && args.sourceId));
	ied.commands.handle("ied.find.inFiles", () => findInFiles(ied));
	ied.commands.handle("ied.editor.goToAll", () => showGoToAll(ied, (target) => goTo(ied, target)));
	ied.commands.handle("ied.editor.toggleHeader", () => toggleHeader(ied));
	ied.commands.handle("ied.editor.openCounterpartBeside", () => openCounterpartBeside(ied));
	ied.commands.handle("ied.editor.action", (args) => runEditorAction(ied, args));
	ied.commands.handle("ied.navigate.back", () => goTo(ied, editors.navigation.back()));
	ied.commands.handle("ied.navigate.forward", () => goTo(ied, editors.navigation.forward()));
}

// The status bar's color (docs/15): blue while a task runs, the debugging color while a session lives, red after a
// failed run, else plain.
function statusState(tasks, debug) {
	if (tasks.runs.size) return "building";
	if (debug.live()) return "debugging";
	return tasks.ended && tasks.ended.status === "failed" ? "failed" : "idle";
}

// While the program is stopped, a hover shows the value of the expression under it (VS DataTips), beside the language
// server's hover. One provider per language.
const hoverLanguages = new Set();
function debugHover(monaco, language) {
	if (hoverLanguages.has(language)) return;
	hoverLanguages.add(language);
	monaco.languages.registerHoverProvider(language, {
		provideHover: async (model, position) => {
			if (!debug.stopped()) return null;
			const expression = hoverExpression(model.getLineContent(position.lineNumber), position.column - 1);
			if (!expression) return null;
			const value = await debug.evaluate(expression.text, "hover");
			if (value.error || value.result === undefined) return null;
			return {
				range: new monaco.Range(position.lineNumber, expression.start + 1, position.lineNumber, expression.end + 1),
				contents: [{ value: "```\n" + expression.text + " = " + value.result + (value.type ? "  (" + value.type + ")" : "") + "\n```" }],
			};
		},
	});
}

// F9: a breakpoint on the current editor's line comes or goes.
function toggleBreakpoint() {
	const editor = editors.last();
	const at = editor ? editor.position() : null;
	if (at && !/^[a-z]+:/i.test(editor.path)) breakpoints.toggle(editor.path, at.line - 1);
}

function currentEditor(ied) {
	return editors.byRef(ied.context.get("activeTab")) || editors.last();
}

// A Monaco action on the current editor ({id}): the shell takes Ctrl+K as a chord's first key, so the editor's own
// Ctrl+K chords come back through here.
// Visual Studio's editor commands (docs/15 "The editor's keys") as Monaco's actions; their keys are the manifest's.
const EDITOR_ACTIONS = [
	["ied.editor.commentSelection", "editor.action.addCommentLine"],
	["ied.editor.uncommentSelection", "editor.action.removeCommentLine"],
	["ied.editor.toggleLineComment", "editor.action.commentLine"],
	["ied.editor.toggleBlockComment", "editor.action.blockComment"],
	["ied.editor.formatDocument", "editor.action.formatDocument"],
	["ied.editor.formatSelection", "editor.action.formatSelection"],
	["ied.editor.trimTrailingWhitespace", "editor.action.trimTrailingWhitespace"],
	["ied.editor.duplicate", "editor.action.copyLinesDownAction"],
	["ied.editor.deleteLine", "editor.action.deleteLines"],
	["ied.editor.moveLinesUp", "editor.action.moveLinesUpAction"],
	["ied.editor.moveLinesDown", "editor.action.moveLinesDownAction"],
	["ied.editor.insertLineAbove", "editor.action.insertLineBefore"],
	["ied.editor.insertLineBelow", "editor.action.insertLineAfter"],
	["ied.editor.makeLowercase", "editor.action.transformToLowercase"],
	["ied.editor.makeUppercase", "editor.action.transformToUppercase"],
	["ied.editor.joinLines", "editor.action.joinLines"],
	["ied.editor.sortLines", "editor.action.sortLinesAscending"],
	["ied.editor.expandSelection", "editor.action.smartSelect.expand"],
	["ied.editor.contractSelection", "editor.action.smartSelect.shrink"],
	["ied.editor.insertNextMatchingCaret", "editor.action.addSelectionToNextFindMatch"],
	["ied.editor.insertAllMatchingCarets", "editor.action.selectHighlights"],
	["ied.editor.insertCaretAbove", "editor.action.insertCursorAbove"],
	["ied.editor.insertCaretBelow", "editor.action.insertCursorBelow"],
	["ied.editor.boxSelectUp", "cursorColumnSelectUp"],
	["ied.editor.boxSelectDown", "cursorColumnSelectDown"],
	["ied.editor.boxSelectLeft", "cursorColumnSelectLeft"],
	["ied.editor.boxSelectRight", "cursorColumnSelectRight"],
	["ied.editor.goToBrace", "editor.action.jumpToBracket"],
	["ied.editor.selectToBrace", "editor.action.selectToBracket"],
	["ied.editor.goToLine", "editor.action.gotoLine"],
	["ied.editor.find", "actions.find"],
	["ied.editor.replace", "editor.action.startFindReplaceAction"],
	["ied.editor.findNext", "editor.action.nextMatchFindAction"],
	["ied.editor.findPrevious", "editor.action.previousMatchFindAction"],
	["ied.editor.findNextSelected", "editor.action.nextSelectionMatchFindAction"],
	["ied.editor.completeWord", "editor.action.triggerSuggest"],
	["ied.editor.parameterInfo", "editor.action.triggerParameterHints"],
	["ied.editor.quickInfo", "editor.action.showHover"],
	["ied.editor.quickActions", "editor.action.quickFix"],
	["ied.editor.rename", "editor.action.rename"],
	["ied.editor.goToDefinition", "editor.action.revealDefinition"],
	["ied.editor.peekDefinition", "editor.action.peekDefinition"],
	["ied.editor.goToImplementation", "editor.action.goToImplementation"],
	["ied.editor.findAllReferences", "editor.action.goToReferences"],
	["ied.editor.toggleOutlining", "editor.toggleFold"],
	["ied.editor.collapseToDefinitions", "editor.foldAll"],
	["ied.editor.stopOutlining", "editor.unfoldAll"],
	["ied.editor.zoomIn", "editor.action.fontZoomIn"],
	["ied.editor.zoomOut", "editor.action.fontZoomOut"],
	["ied.editor.zoomReset", "editor.action.fontZoomReset"],
];

// Cut Line (Ctrl+L): the whole lines the selection touches.
function cutLine(ied) {
	const editor = currentEditor(ied);
	if (!editor) return;
	const ed = editor.editor;
	const selection = ed.getSelection();
	const last = ed.getModel().getLineCount();
	const end = selection.endLineNumber < last ? { line: selection.endLineNumber + 1, column: 1 } : { line: last, column: ed.getModel().getLineMaxColumn(last) };
	ed.setSelection(new editor.monaco.Selection(selection.startLineNumber, 1, end.line, end.column));
	ed.focus();
	ed.trigger("ied", "editor.action.clipboardCutAction", null);
}

// Toggle All Outlining (Ctrl+M, Ctrl+L): everything folded, or nothing, flipping each time.
function toggleAllOutlining(ied) {
	const editor = currentEditor(ied);
	if (!editor) return;
	editor.allFolded = !editor.allFolded;
	runEditorAction(ied, { id: editor.allFolded ? "editor.foldAll" : "editor.unfoldAll" });
}

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

// The header or code file in the group beside, tethered (docs/15 "Tethered tabs"): showing either shows the other.
async function openCounterpartBeside(ied) {
	const editor = currentEditor(ied);
	if (!editor) return;
	const pairs = ied.contributions("headerSourcePairs").map((c) => c.value);
	const found = await findCounterpart(ied.client, editor.ctx.wsId, editor.path, pairs);
	if (!found) {
		ied.toast("There is no header or code file for " + editor.path.slice(editor.path.lastIndexOf("/") + 1) + ".");
		return;
	}
	ied.tabs.openBeside("doc:" + editor.path, "doc:" + found, { tether: true });
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

// The Output window with `sourceId` shown (a run's, as VS shows the build's output when it starts).
async function showOutput(ied, sourceId) {
	if (sourceId) rememberOutputSource(sourceId);
	await ied.commands.run("shell.showView", { id: "ied.output" });
	const output = last(outputs);
	if (output && sourceId && output.sourceId !== sourceId) output.show(sourceId);
}

// A file opened from outside IED (`ied open`, the shell's fileOpener): {path, line, col}, 1-based and 0 when absent. The
// tabs plugins name for its language open beside it, tethered to close with it (`companionTabs`: a Markdown file's
// preview, J10; docs/15 "Tethered tabs").
async function openFromOutside(ied, args) {
	if (!args || !args.path) return;
	await goTo(ied, { path: args.path, line: args.line > 0 ? args.line - 1 : undefined, col: args.col > 0 ? args.col - 1 : 0 });
	const language = languageOf(ied.contributions("languages").map((c) => c.value), args.path);
	for (const { value } of ied.contributions("companionTabs")) {
		if (language && (value.languages || []).includes(language)) ied.tabs.openBeside("doc:" + args.path, value.kind + ":" + args.path, { tether: true, closes: true });
	}
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

export function TaskToolbar(ctx) {
	return TaskToolbarView(ctx, tasks);
}

export function TaskStatus(ctx) {
	return TaskStatusView(ctx, tasks);
}

export function DebugToolbar(ctx) {
	return DebugToolbarView(ctx, debug, tasks);
}

export function CallStack(ctx) {
	return new CallStackView(ctx, debug, (target) => goTo(ctx.ied, target));
}

export function Locals(ctx) {
	return new LocalsView(ctx, debug);
}

export function Watch(ctx) {
	return new WatchView(ctx, debug, breakpoints);
}

export function Threads(ctx) {
	return new ThreadsView(ctx, debug);
}

export function Breakpoints(ctx) {
	return new BreakpointsWindow(ctx, debug, breakpoints, (target) => goTo(ctx.ied, target));
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
