// lspconvert.js - between LSP's shapes and Monaco's (docs/24): LSP positions are 0-based with UTF-16 columns (the
// server converted them for its language servers), Monaco's 1-based; URIs of the workspace's files are
// "ied://<wsId>/<path>", as the models' are. `monaco` is passed in for its enums and Uri.

export function toLspPosition(position) {
	return { line: position.lineNumber - 1, character: position.column - 1 };
}

export function toLspRange(range) {
	return { start: { line: range.startLineNumber - 1, character: range.startColumn - 1 }, end: { line: range.endLineNumber - 1, character: range.endColumn - 1 } };
}

export function fromLspRange(range) {
	return { startLineNumber: range.start.line + 1, startColumn: range.start.character + 1, endLineNumber: range.end.line + 1, endColumn: range.end.character + 1 };
}

// MarkupContent, a MarkedString or a list of them, as Monaco's markdown strings.
export function markdownOf(contents) {
	if (contents === null || contents === undefined) return [];
	if (Array.isArray(contents)) return contents.flatMap(markdownOf);
	if (typeof contents === "string") return contents ? [{ value: contents }] : [];
	if (contents.kind === "plaintext") return [{ value: escapeMarkdown(contents.value) }];
	if (contents.kind === "markdown") return [{ value: contents.value }];
	if (contents.language !== undefined) return [{ value: "```" + contents.language + "\n" + contents.value + "\n```" }];
	return [];
}

function escapeMarkdown(text) {
	return text.replace(/[\\`*_{}[\]()#+\-.!|<>]/g, "\\$&");
}

function documentationOf(documentation) {
	const parts = markdownOf(documentation);
	return parts.length ? { value: parts.map((p) => p.value).join("\n\n") } : undefined;
}

export function hoverOf(hover) {
	if (!hover) return null;
	const contents = markdownOf(hover.contents);
	if (!contents.length) return null;
	return hover.range ? { contents, range: fromLspRange(hover.range) } : { contents };
}

// Location, Location[] or LocationLink[] as Monaco locations (links keep their origin and target selection).
export function locationsOf(monaco, result) {
	if (!result) return [];
	return (Array.isArray(result) ? result : [result]).map((item) => {
		if (item.targetUri === undefined) return { uri: monaco.Uri.parse(item.uri), range: fromLspRange(item.range) };
		const link = { uri: monaco.Uri.parse(item.targetUri), range: fromLspRange(item.targetRange) };
		if (item.targetSelectionRange) link.targetSelectionRange = fromLspRange(item.targetSelectionRange);
		if (item.originSelectionRange) link.originSelectionRange = fromLspRange(item.originSelectionRange);
		return link;
	});
}

const COMPLETION_KINDS = [null, "Text", "Method", "Function", "Constructor", "Field", "Variable", "Class", "Interface", "Module", "Property", "Unit",
	"Value", "Enum", "Keyword", "Snippet", "Color", "File", "Reference", "Folder", "EnumMember", "Constant", "Struct", "Event", "Operator", "TypeParameter"];

function editRange(range) {
	if (range.insert) return { insert: fromLspRange(range.insert), replace: fromLspRange(range.replace) };
	return fromLspRange(range);
}

// One completion item; `defaults` are the list's itemDefaults, `wordRange` what Monaco replaces when nothing says.
export function completionItemOf(monaco, item, defaults, wordRange) {
	const kinds = monaco.languages.CompletionItemKind;
	const edit = item.textEdit;
	const range = edit ? edit.range || edit : defaults && defaults.editRange ? defaults.editRange : null;
	const format = item.insertTextFormat !== undefined ? item.insertTextFormat : defaults && defaults.insertTextFormat;
	const out = {
		label: item.labelDetails ? { label: item.label, detail: item.labelDetails.detail, description: item.labelDetails.description } : item.label,
		kind: kinds[COMPLETION_KINDS[item.kind] || "Text"],
		insertText: edit ? edit.newText : item.textEditText !== undefined ? item.textEditText : item.insertText !== undefined ? item.insertText : item.label,
		range: range ? editRange(range) : wordRange,
		lsp: item,
	};
	if (item.detail) out.detail = item.detail;
	const documentation = documentationOf(item.documentation);
	if (documentation) out.documentation = documentation;
	if (item.sortText) out.sortText = item.sortText;
	if (item.filterText) out.filterText = item.filterText;
	if (item.preselect) out.preselect = true;
	if (format === 2) out.insertTextRules = monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet;
	if (item.additionalTextEdits) out.additionalTextEdits = textEditsOf(item.additionalTextEdits);
	const commit = item.commitCharacters || (defaults && defaults.commitCharacters);
	if (commit) out.commitCharacters = commit;
	if ((item.tags && item.tags.includes(1)) || item.deprecated) out.tags = [monaco.languages.CompletionItemTag.Deprecated];
	return out;
}

export function completionListOf(monaco, result, wordRange) {
	if (!result) return { suggestions: [] };
	const items = Array.isArray(result) ? result : result.items || [];
	const defaults = Array.isArray(result) ? null : result.itemDefaults;
	return { suggestions: items.map((item) => completionItemOf(monaco, item, defaults, wordRange)), incomplete: !Array.isArray(result) && Boolean(result.isIncomplete) };
}

export function signatureHelpOf(help) {
	if (!help || !help.signatures || !help.signatures.length) return null;
	const signatures = help.signatures.map((s) => ({
		label: s.label,
		documentation: documentationOf(s.documentation),
		parameters: (s.parameters || []).map((p) => ({ label: p.label, documentation: documentationOf(p.documentation) })),
		activeParameter: s.activeParameter,
	}));
	return { value: { signatures, activeSignature: help.activeSignature || 0, activeParameter: help.activeParameter || 0 }, dispose() {} };
}

// LSP's highlight kinds (1 text, 2 read, 3 write) are Monaco's plus one.
export function highlightsOf(result) {
	return (result || []).map((h) => ({ range: fromLspRange(h.range), kind: h.kind ? h.kind - 1 : 0 }));
}

// DocumentSymbol[] (a tree) or SymbolInformation[] (flat); LSP's symbol kinds are Monaco's plus one.
export function documentSymbolsOf(result) {
	const one = (s) => {
		const range = fromLspRange(s.range || s.location.range);
		return {
			name: s.name, detail: s.detail || "", kind: s.kind - 1, tags: s.tags || [],
			range, selectionRange: s.selectionRange ? fromLspRange(s.selectionRange) : range,
			containerName: s.containerName, children: (s.children || []).map(one),
		};
	};
	return (result || []).map(one);
}

export function foldingRangesOf(monaco, result) {
	return (result || []).map((f) => {
		const range = { start: f.startLine + 1, end: f.endLine + 1 };
		if (f.kind) range.kind = monaco.languages.FoldingRangeKind.fromValue(f.kind);
		return range;
	});
}

export function textEditsOf(edits) {
	return (edits || []).map((e) => ({ range: fromLspRange(e.range), text: e.newText }));
}

// A WorkspaceEdit's text edits by URI (its `changes`, or the text edits of its `documentChanges`); `unsupported` counts
// the file operations (create, rename, delete) it also asks for.
export function workspaceEditOf(edit) {
	const byUri = new Map();
	let unsupported = 0;
	const add = (uri, edits) => byUri.set(uri, (byUri.get(uri) || []).concat(textEditsOf(edits)));
	if (edit && edit.documentChanges) {
		for (const change of edit.documentChanges) {
			if (change.textDocument) add(change.textDocument.uri, change.edits);
			else unsupported += 1;
		}
	}
	else if (edit && edit.changes) for (const [uri, edits] of Object.entries(edit.changes)) add(uri, edits);
	return { byUri, unsupported };
}

const SEVERITIES = [null, "Error", "Warning", "Info", "Hint"];

export function markersOf(monaco, items) {
	return (items || []).map((d) => {
		const marker = Object.assign({ severity: monaco.MarkerSeverity[SEVERITIES[d.severity] || "Error"], message: d.message }, fromLspRange(d.range));
		if (d.source) marker.source = d.source;
		if (d.code !== undefined && d.code !== null) marker.code = d.codeDescription ? { value: String(d.code), target: monaco.Uri.parse(d.codeDescription.href) } : String(d.code);
		if (d.tags) marker.tags = d.tags.filter((t) => t === 1 || t === 2);
		if (d.relatedInformation) {
			marker.relatedInformation = d.relatedInformation.map((r) => Object.assign({ resource: monaco.Uri.parse(r.location.uri), message: r.message }, fromLspRange(r.location.range)));
		}
		return marker;
	});
}

// Whether two LSP ranges share a position (touching counts).
export function rangesMeet(a, b) {
	const before = (p, q) => p.line < q.line || (p.line === q.line && p.character < q.character);
	return !before(a.end, b.start) && !before(b.end, a.start);
}

// prepareRename's answer as Monaco's: a range (and the text to start from), or why renaming is refused there.
export function renameLocationOf(result, model, position) {
	if (!result) return { range: null, text: "", rejectReason: "There is nothing to rename here." };
	if (result.defaultBehavior) {
		const word = model.getWordAtPosition(position);
		if (!word) return { range: null, text: "", rejectReason: "There is nothing to rename here." };
		return { range: { startLineNumber: position.lineNumber, startColumn: word.startColumn, endLineNumber: position.lineNumber, endColumn: word.endColumn }, text: word.word };
	}
	const range = fromLspRange(result.range || result);
	return { range, text: result.placeholder !== undefined ? result.placeholder : model.getValueInRange(range) };
}

export function inlayHintsOf(monaco, result) {
	return (result || []).map((hint) => {
		const label = typeof hint.label === "string" ? hint.label : hint.label.map((part) => ({ label: part.value, tooltip: documentationOf(part.tooltip) }));
		const out = { label, position: { lineNumber: hint.position.line + 1, column: hint.position.character + 1 }, paddingLeft: Boolean(hint.paddingLeft), paddingRight: Boolean(hint.paddingRight) };
		if (hint.kind) out.kind = hint.kind === 1 ? monaco.languages.InlayHintKind.Type : monaco.languages.InlayHintKind.Parameter;
		const tooltip = documentationOf(hint.tooltip);
		if (tooltip) out.tooltip = tooltip;
		return out;
	});
}
