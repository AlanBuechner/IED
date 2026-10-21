// lspconvert.test.js - the editor plugin's conversions between LSP and Monaco (docs/24): ranges, hover contents,
// locations and links, completion items with their edit ranges and defaults, symbols, workspace edits, markers.

import { test, assertEqual, assertTrue } from "./harness.js";
import * as C from "/plugins/ied.editor/ui/lspconvert.js";

// The parts of Monaco the conversions use.
const monaco = {
	Uri: { parse: (text) => ({ text, toString: () => text }) },
	MarkerSeverity: { Hint: 1, Info: 2, Warning: 4, Error: 8 },
	languages: {
		CompletionItemKind: { Method: 0, Function: 1, Variable: 4, Class: 5, Text: 18, Snippet: 27 },
		CompletionItemInsertTextRule: { InsertAsSnippet: 4 },
		CompletionItemTag: { Deprecated: 1 },
		FoldingRangeKind: { fromValue: (value) => ({ value }) },
		InlayHintKind: { Type: 1, Parameter: 2 },
	},
};

const range = (sl, sc, el, ec) => ({ start: { line: sl, character: sc }, end: { line: el, character: ec } });

test("lspconvert: positions and ranges are 0-based for LSP and 1-based for Monaco", () => {
	assertEqual(C.toLspPosition({ lineNumber: 3, column: 5 }), { line: 2, character: 4 });
	const monacoRange = C.fromLspRange(range(0, 0, 1, 3));
	assertEqual(monacoRange, { startLineNumber: 1, startColumn: 1, endLineNumber: 2, endColumn: 4 });
	assertEqual(C.toLspRange(monacoRange), range(0, 0, 1, 3));
});

test("lspconvert: hover contents of every shape", () => {
	assertEqual(C.hoverOf({ contents: { kind: "markdown", value: "**x**" } }).contents, [{ value: "**x**" }]);
	assertEqual(C.hoverOf({ contents: { kind: "plaintext", value: "a*b" } }).contents, [{ value: "a\\*b" }], "plain text is escaped");
	assertEqual(C.hoverOf({ contents: [{ language: "c", value: "int x" }, "doc"] }).contents, [{ value: "```c\nint x\n```" }, { value: "doc" }]);
	assertEqual(C.hoverOf({ contents: [] }), null, "nothing to show");
	assertEqual(C.hoverOf(null), null);
	assertEqual(C.hoverOf({ contents: "x", range: range(0, 2, 0, 5) }).range, { startLineNumber: 1, startColumn: 3, endLineNumber: 1, endColumn: 6 });
});

test("lspconvert: a location, locations and links", () => {
	const one = C.locationsOf(monaco, { uri: "ied://w1/a.c", range: range(1, 0, 1, 2) });
	assertEqual(one.length, 1);
	assertEqual(one[0].uri.text, "ied://w1/a.c");
	const links = C.locationsOf(monaco, [{ targetUri: "ied://w1/b.c", targetRange: range(0, 0, 9, 0), targetSelectionRange: range(2, 4, 2, 7), originSelectionRange: range(5, 1, 5, 3) }]);
	assertEqual(links[0].uri.text, "ied://w1/b.c");
	assertEqual(links[0].targetSelectionRange.startLineNumber, 3);
	assertEqual(links[0].originSelectionRange.startColumn, 2);
	assertEqual(C.locationsOf(monaco, null), []);
});

test("lspconvert: completion items take their edit, the list's defaults, or the word", () => {
	const word = { startLineNumber: 1, startColumn: 5, endLineNumber: 1, endColumn: 7 };
	const list = C.completionListOf(monaco, {
		isIncomplete: true,
		itemDefaults: { editRange: range(0, 2, 0, 6), insertTextFormat: 2, commitCharacters: ["."] },
		items: [
			{ label: "edit", kind: 3, textEdit: { range: range(0, 0, 0, 1), newText: "editText" } },
			{ label: "defaults", kind: 15, insertText: "x($1)" },
			{ label: "replace", textEdit: { insert: range(0, 0, 0, 1), replace: range(0, 0, 0, 3), newText: "r" }, insertTextFormat: 1, tags: [1] },
		],
	}, word);
	assertTrue(list.incomplete, "isIncomplete");
	const [edit, defaults, replace] = list.suggestions;
	assertEqual([edit.insertText, edit.kind, edit.range.endColumn], ["editText", 1, 2]);
	assertEqual([defaults.insertText, defaults.kind, defaults.insertTextRules, defaults.range.startColumn, defaults.commitCharacters], ["x($1)", 27, 4, 3, ["."]]);
	assertEqual([replace.range.insert.endColumn, replace.range.replace.endColumn], [2, 4]);
	assertEqual(replace.insertTextRules, undefined, "an item's own format beats the default");
	assertEqual(replace.tags, [1]);
	const plain = C.completionListOf(monaco, [{ label: "w" }], word);
	assertEqual([plain.suggestions[0].range, plain.suggestions[0].insertText, plain.suggestions[0].kind, plain.incomplete], [word, "w", 18, false]);
});

test("lspconvert: document symbols, as a tree or flat", () => {
	const tree = C.documentSymbolsOf([{ name: "A", kind: 5, range: range(0, 0, 9, 1), selectionRange: range(0, 6, 0, 7), children: [{ name: "f", kind: 12, range: range(1, 1, 2, 1), selectionRange: range(1, 5, 1, 6) }] }]);
	assertEqual([tree[0].name, tree[0].kind, tree[0].selectionRange.startColumn, tree[0].children[0].name, tree[0].children[0].kind], ["A", 4, 7, "f", 11]);
	const flat = C.documentSymbolsOf([{ name: "g", kind: 12, location: { uri: "file:///x", range: range(3, 0, 4, 0) }, containerName: "A" }]);
	assertEqual([flat[0].name, flat[0].range.startLineNumber, flat[0].selectionRange.startLineNumber, flat[0].containerName], ["g", 4, 4, "A"]);
});

test("lspconvert: workspace edits by URI, file operations counted", () => {
	const fromChanges = C.workspaceEditOf({ changes: { "ied://w1/a.c": [{ range: range(0, 0, 0, 1), newText: "x" }] } });
	assertEqual([...fromChanges.byUri.keys()], ["ied://w1/a.c"]);
	assertEqual(fromChanges.byUri.get("ied://w1/a.c")[0].text, "x");
	const fromDocuments = C.workspaceEditOf({ documentChanges: [
		{ textDocument: { uri: "ied://w1/a.c", version: 3 }, edits: [{ range: range(0, 0, 0, 0), newText: "1" }] },
		{ kind: "create", uri: "ied://w1/new.c" },
		{ textDocument: { uri: "ied://w1/a.c", version: 3 }, edits: [{ range: range(1, 0, 1, 0), newText: "2" }] },
	] });
	assertEqual([fromDocuments.byUri.get("ied://w1/a.c").length, fromDocuments.unsupported], [2, 1]);
});

test("lspconvert: diagnostics as markers", () => {
	const [error, hint] = C.markersOf(monaco, [
		{ range: range(0, 1, 0, 4), severity: 1, message: "bad", source: "c4", code: 12, codeDescription: { href: "https://x/12" }, relatedInformation: [{ location: { uri: "ied://w1/b.c", range: range(2, 0, 2, 1) }, message: "here" }] },
		{ range: range(1, 0, 1, 1), severity: 4, message: "unused", tags: [1, 7] },
	]);
	assertEqual([error.severity, error.message, error.startColumn, error.endColumn, error.source], [8, "bad", 2, 5, "c4"]);
	assertEqual([error.code.value, error.code.target.text], ["12", "https://x/12"]);
	assertEqual([error.relatedInformation[0].resource.text, error.relatedInformation[0].startLineNumber], ["ied://w1/b.c", 3]);
	assertEqual([hint.severity, hint.tags], [1, [1]]);
	assertEqual(C.markersOf(monaco, [{ range: range(0, 0, 0, 0), message: "no severity" }])[0].severity, 8, "an error when it does not say");
	assertEqual(C.markersOf(monaco, [{ range: range(0, 0, 0, 0), message: "x", code: null }])[0].code, undefined, "a null code is none");
});

test("lspconvert: ranges meet when they share a position", () => {
	assertTrue(C.rangesMeet(range(1, 0, 1, 4), range(1, 4, 1, 4)), "the cursor at the end");
	assertTrue(!C.rangesMeet(range(1, 0, 1, 4), range(1, 5, 1, 6)), "after it");
	assertTrue(C.rangesMeet(range(0, 0, 9, 0), range(4, 2, 4, 3)), "inside");
});

test("lspconvert: signature help, highlights, folding, edits and inlay hints", () => {
	const help = C.signatureHelpOf({ signatures: [{ label: "f(int a)", parameters: [{ label: [2, 7] }], documentation: "doc" }], activeParameter: 0 });
	assertEqual([help.value.signatures[0].label, help.value.signatures[0].parameters[0].label, help.value.signatures[0].documentation.value, help.value.activeSignature], ["f(int a)", [2, 7], "doc", 0]);
	assertEqual(C.signatureHelpOf({ signatures: [] }), null);
	assertEqual(C.highlightsOf([{ range: range(0, 0, 0, 1) }, { range: range(0, 0, 0, 1), kind: 3 }]).map((h) => h.kind), [0, 2]);
	assertEqual(C.foldingRangesOf(monaco, [{ startLine: 2, endLine: 5, kind: "comment" }]), [{ start: 3, end: 6, kind: { value: "comment" } }]);
	assertEqual(C.textEditsOf([{ range: range(0, 0, 0, 1), newText: "y" }]), [{ range: { startLineNumber: 1, startColumn: 1, endLineNumber: 1, endColumn: 2 }, text: "y" }]);
	const [hint] = C.inlayHintsOf(monaco, [{ position: { line: 0, character: 4 }, label: [{ value: "a:" }], kind: 2, paddingRight: true }]);
	assertEqual([hint.label, hint.position.lineNumber, hint.position.column, hint.kind, hint.paddingRight], [[{ label: "a:", tooltip: undefined }], 1, 5, 2, true]);
});
