// gotoall.js - Go To All (VS Ctrl+, / Ctrl+T): files by name (fs.files) and the language servers' workspace symbols
// (lsp.workspaceSymbols) in one quick pick, ranked together by how well their names match. "f " asks for files only,
// "# " for symbols only.

import { icon } from "/ui-kit/dom.js";
import { showPicker } from "/ui-kit/picker.js";
import { canonicalFileUri, displayPath, isFileUri } from "/ui-kit/fileuris.js";

const IN_NAME_SCORE = 1000;
const MOST_FILES = 100;

// LSP SymbolKind 1..26 as the codicons that exist (VS Code's mapping).
const SYMBOL_ICONS = [null, "symbol-file", "symbol-namespace", "symbol-namespace", "package", "symbol-class", "symbol-method",
	"symbol-property", "symbol-field", "symbol-method", "symbol-enum", "symbol-interface", "symbol-method", "symbol-variable",
	"symbol-constant", "symbol-string", "symbol-numeric", "symbol-boolean", "symbol-array", "symbol-namespace", "symbol-key",
	"circle-slash", "symbol-enum-member", "symbol-structure", "symbol-event", "symbol-operator", "symbol-parameter"];

// The codicon name of an LSP SymbolKind.
export function symbolIconName(kind) {
	return SYMBOL_ICONS[kind] || "symbol-misc";
}

// The workspace path of an "ied://<wsId>/<path>" URI, or null for any other.
export function pathOfUri(uri, wsId) {
	const prefix = "ied://" + wsId + "/";
	return typeof uri === "string" && uri.startsWith(prefix) ? decodeURIComponent(uri.slice(prefix.length)) : null;
}

// {files, symbols, query}: which kinds what was typed asks for, and the query without its prefix.
export function parseQuery(text) {
	const prefix = /^([f#])\s+/.exec(text);
	if (!prefix) return { files: true, symbols: true, query: text.trim() };
	return { files: prefix[1] === "f", symbols: prefix[1] === "#", query: text.slice(prefix[0].length).trim() };
}

function folderOf(path) {
	const slash = path.lastIndexOf("/");
	return slash < 0 ? "" : path.slice(0, slash);
}

function fileItem(file, iconOf) {
	return { label: file.path.slice(file.path.lastIndexOf("/") + 1), detail: folderOf(file.path), icon: iconOf ? iconOf(file.path) : null, target: { path: file.path } };
}

// Where a symbol opens: a workspace file, or a file outside the workspace (read-only, docs/10); null for another URI.
export function symbolTarget(symbol, wsId) {
	const location = symbol.location || {};
	const path = pathOfUri(location.uri, wsId) || canonicalFileUri(location.uri);
	if (!path) return null;
	const start = location.range ? location.range.start : null;
	return start ? { path, line: start.line, col: start.character } : { path };
}

function symbolItem(symbol, target) {
	const file = isFileUri(target.path) ? displayPath(target.path) : target.path;
	const where = symbol.containerName ? symbol.containerName + " - " + file : file;
	return { label: symbol.name, detail: where, icon: icon(symbolIconName(symbol.kind)), target };
}

function isWordStart(name, i) {
	if (i === 0) return true;
	const before = name[i - 1];
	return "_-. /:".includes(before) || (before === before.toLowerCase() && name[i] !== name[i].toLowerCase());
}

// The query's letters at the name's word starts, in order: "lp" for LspPool, "fs" for find_symbol.
function matchesInitials(query, name) {
	let at = 0;
	for (const wanted of query) {
		while (at < name.length && !(isWordStart(name, at) && name[at].toLowerCase() === wanted)) at++;
		if (at === name.length) return false;
		at++;
	}
	return true;
}

// How well a name matches (0 best): the name itself (an extension aside), its start, inside it, its word starts,
// its letters scattered.
export function matchTier(query, name) {
	const wanted = query.toLowerCase();
	const lower = name.toLowerCase();
	const dot = lower.lastIndexOf(".");
	if (!wanted || lower === wanted || (dot > 0 && lower.slice(0, dot) === wanted)) return 0;
	if (lower.startsWith(wanted)) return 1;
	if (lower.includes(wanted)) return 2;
	if (matchesInitials(wanted, name)) return 3;
	return 4;
}

const PATH_ONLY_TIER = 5;

// The rows of the picker, files and symbols together by how well their names match; a file found only through its
// folders last. Within a tier the order is the server's (files by score). `iconOf(path)` draws a file's icon.
export function rowsOf(files, symbols, wsId, query, iconOf) {
	const ranked = [];
	for (const file of files) {
		const item = fileItem(file, iconOf);
		ranked.push({ item, tier: file.score >= IN_NAME_SCORE ? matchTier(query, item.label) : PATH_ONLY_TIER });
	}
	for (const symbol of symbols) {
		const target = symbolTarget(symbol, wsId);
		if (target) ranked.push({ item: symbolItem(symbol, target), tier: matchTier(query, symbol.name) });
	}
	return ranked.map((entry, order) => ({ ...entry, order })).sort((a, b) => a.tier - b.tier || a.order - b.order).map((entry) => entry.item);
}

export function showGoToAll(ied, open) {
	const wsId = ied.workspace.wsId;
	if (!wsId) return;
	showPicker({
		placeholder: "Go to file or symbol (f files, # symbols)",
		label: "Go To All",
		empty: "No file or symbol matches",
		provide: async (text, signal) => {
			const asked = parseQuery(text);
			const files = asked.files ? ied.client.request("fs.files", { wsId, query: asked.query, maxResults: MOST_FILES }, { signal }) : null;
			const symbols = asked.symbols && asked.query ? ied.client.request("lsp.workspaceSymbols", { wsId, query: asked.query }, { signal }) : null;
			const [foundFiles, foundSymbols] = await Promise.all([files, symbols]);
			return rowsOf(foundFiles ? foundFiles.files : [], foundSymbols ? foundSymbols.symbols : [], wsId, asked.query, (path) => ied.icons.file(path));
		},
		pick: (item) => open(item.target),
	});
}
