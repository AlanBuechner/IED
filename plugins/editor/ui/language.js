// language.js - language servers in Monaco (docs/24): a model's providers ask the workspace's language server pool
// (lsp.request) about the document it shows, once its edits are on the server, for what the serving server can do
// (lsp.capabilities, one set of providers per language and server); diagnostics become markers; lsp.refresh and a
// server that starts again fetch tokens, folding and hints anew. Locations in other files open them as tabs (read-only
// for files outside the workspace), and their text is loaded for peeks; edits to other files open them first.

import * as C from "./lspconvert.js";
import { canonicalFileUri } from "./fileuris.js";

const QUIET = new Set(["cancelled", "not_found", "unavailable"]);
const REPORT_EVERY_MS = 30000;
const EXECUTE = "ied.lsp.executeCommand";
const APPLY = "ied.lsp.applyEdit";

export class LanguageFeatures {
	constructor(ied, monaco, editors, diagnostics) {
		this.ied = ied;
		this.monaco = monaco;
		this.editors = editors;
		this.diagnostics = diagnostics;
		this.infos = new Map();      // docId -> {wsId, promise of {profile, state, capabilities}}
		this.bundles = new Map();    // "<languageId> <profile>" -> {text, disposables}
		this.servers = new Map();    // wsId -> {count, stop} of "lsp:<wsId>"
		this.reported = new Map();   // method -> when its last failure was reported
		this.refreshes = { tokens: new monaco.Emitter(), folding: new monaco.Emitter(), hints: new monaco.Emitter() };
		diagnostics.onChange((wsId, path) => this.markersChanged(wsId, path));
		monaco.editor.registerEditorOpener({ openCodeEditor: (source, resource, selection) => this.open(resource, selection) });
		monaco.editor.registerCommand(EXECUTE, (accessor, docId, command) => this.execute(docId, command));
		monaco.editor.registerCommand(APPLY, (accessor, edit) => this.applyEdit(edit));
	}

	// A CodeEditor's model exists: its workspace's servers and diagnostics followed, its server's providers registered,
	// its markers set. Returns the function that detaches it.
	attach(editor) {
		const wsId = editor.ctx.wsId;
		const stops = [this.diagnostics.watch(wsId), this.followServers(wsId)];
		editor.markerOwners = new Set();
		this.setMarkers(editor);
		this.prepare(editor);
		return () => stops.forEach((stop) => stop());
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Servers and their capabilities

	followServers(wsId) {
		let entry = this.servers.get(wsId);
		if (!entry) {
			entry = { count: 0, stop: null };
			this.servers.set(wsId, entry);
			entry.stop = this.ied.client.subscribe("lsp:" + wsId, {
				snap: () => this.serversChanged(wsId),
				evt: (kind, data) => (kind === "lsp.refresh" ? this.refresh(data.kinds) : this.serversChanged(wsId)),
			});
		}
		entry.count += 1;
		let stopped = false;
		return () => {
			if (stopped) return;
			stopped = true;
			entry.count -= 1;
			if (entry.count > 0) return;
			entry.stop();
			this.servers.delete(wsId);
		};
	}

	serversChanged(wsId) {
		for (const [docId, info] of this.infos) if (info.wsId === wsId) this.infos.delete(docId);
		for (const editor of this.editors.all()) if (editor.ctx.wsId === wsId && editor.model) this.prepare(editor);
		this.refresh(["semanticTokens", "foldingRange", "inlayHint"]);
	}

	refresh(kinds) {
		if (kinds.includes("semanticTokens")) this.refreshes.tokens.fire();
		if (kinds.includes("foldingRange")) this.refreshes.folding.fire();
		if (kinds.includes("inlayHint")) this.refreshes.hints.fire();
	}

	async prepare(editor) {
		const info = await this.info(editor);
		if (info && info.profile && info.state === "running" && editor.model && !editor.model.isDisposed()) this.bundle(editor.model.getLanguageId(), info);
	}

	info(editor) {
		const docId = editor.sync.docId;
		const known = this.infos.get(docId);
		if (known) return known.promise;
		const promise = this.ied.client.request("lsp.capabilities", { docId }).catch((e) => {
			this.failed("lsp.capabilities", e);
			return null;
		});
		this.infos.set(docId, { wsId: editor.ctx.wsId, promise });
		return promise;
	}

	// The providers of one server for one language, registered again when its capabilities change.
	bundle(languageId, info) {
		const key = languageId + " " + info.profile;
		const text = JSON.stringify(info.capabilities);
		const known = this.bundles.get(key);
		if (known && known.text === text) return;
		if (known) known.disposables.forEach((d) => d.dispose());
		const selector = [{ language: languageId, scheme: "ied" }, { language: languageId, scheme: "file" }];
		this.bundles.set(key, { text, disposables: this.register(selector, info.capabilities || {}) });
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Asking

	editorOf(model) {
		return this.editors.all().find((editor) => editor.model === model) || null;
	}

	// A request about the document `model` shows, once its edits are on the server; null when it fails (reported unless
	// it is one that happens: cancelled, no server, a server starting).
	async ask(model, method, params, token) {
		const editor = this.editorOf(model);
		if (!editor || !editor.sync.docId) return null;
		await editor.sync.settled();
		if (token && token.isCancellationRequested) return null;
		const controller = new AbortController();
		const listener = token ? token.onCancellationRequested(() => controller.abort()) : null;
		try {
			return await this.ied.client.request("lsp.request", { docId: editor.sync.docId, method, params }, { signal: controller.signal });
		}
		catch (e) {
			this.failed(method, e);
			return null;
		}
		finally {
			if (listener) listener.dispose();
		}
	}

	failed(method, error) {
		if (QUIET.has(error.code)) return;
		const now = Date.now();
		if (now - (this.reported.get(method) || 0) < REPORT_EVERY_MS) return;
		this.reported.set(method, now);
		this.ied.report("the language server's answer to " + method + " failed: " + error.message, error);
	}

	execute(docId, command) {
		return this.ied.client.request("lsp.request", { docId, method: "workspace/executeCommand", params: { command: command.command, arguments: command.arguments || [] } })
			.catch((e) => this.failed("workspace/executeCommand", e));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Providers

	register(selector, caps) {
		const L = this.monaco.languages;
		const M = this.monaco;
		const at = (position) => ({ position: C.toLspPosition(position) });
		const out = [];
		const add = (enabled, make) => {
			if (enabled) out.push(make());
		};
		add(caps.hoverProvider, () => L.registerHoverProvider(selector, {
			provideHover: async (model, position, token) => C.hoverOf(await this.ask(model, "textDocument/hover", at(position), token)),
		}));
		add(caps.completionProvider, () => L.registerCompletionItemProvider(selector, this.completion(caps.completionProvider)));
		add(caps.signatureHelpProvider, () => L.registerSignatureHelpProvider(selector, {
			signatureHelpTriggerCharacters: caps.signatureHelpProvider.triggerCharacters || [],
			signatureHelpRetriggerCharacters: caps.signatureHelpProvider.retriggerCharacters || [],
			provideSignatureHelp: async (model, position, token, context) => C.signatureHelpOf(await this.ask(model, "textDocument/signatureHelp",
				Object.assign(at(position), { context: { triggerKind: context.triggerKind, triggerCharacter: context.triggerCharacter, isRetrigger: context.isRetrigger } }), token)),
		}));
		const locations = (method) => async (model, position, token) => this.located(C.locationsOf(M, await this.ask(model, method, at(position), token)));
		add(caps.definitionProvider, () => L.registerDefinitionProvider(selector, { provideDefinition: locations("textDocument/definition") }));
		add(caps.declarationProvider, () => L.registerDeclarationProvider(selector, { provideDeclaration: locations("textDocument/declaration") }));
		add(caps.typeDefinitionProvider, () => L.registerTypeDefinitionProvider(selector, { provideTypeDefinition: locations("textDocument/typeDefinition") }));
		add(caps.implementationProvider, () => L.registerImplementationProvider(selector, { provideImplementation: locations("textDocument/implementation") }));
		add(caps.referencesProvider, () => L.registerReferenceProvider(selector, {
			provideReferences: async (model, position, context, token) => this.located(C.locationsOf(M, await this.ask(model, "textDocument/references",
				Object.assign(at(position), { context: { includeDeclaration: context.includeDeclaration } }), token))),
		}));
		add(caps.documentHighlightProvider, () => L.registerDocumentHighlightProvider(selector, {
			provideDocumentHighlights: async (model, position, token) => C.highlightsOf(await this.ask(model, "textDocument/documentHighlight", at(position), token)),
		}));
		add(caps.documentSymbolProvider, () => L.registerDocumentSymbolProvider(selector, {
			provideDocumentSymbols: async (model, token) => C.documentSymbolsOf(await this.ask(model, "textDocument/documentSymbol", {}, token)),
		}));
		add(caps.foldingRangeProvider, () => L.registerFoldingRangeProvider(selector, {
			onDidChange: this.refreshes.folding.event,
			provideFoldingRanges: async (model, context, token) => C.foldingRangesOf(M, await this.ask(model, "textDocument/foldingRange", {}, token)),
		}));
		const tokens = caps.semanticTokensProvider;
		add(tokens && tokens.full && tokens.legend, () => L.registerDocumentSemanticTokensProvider(selector, {
			onDidChange: this.refreshes.tokens.event,
			getLegend: () => tokens.legend,
			provideDocumentSemanticTokens: async (model, lastResultId, token) => {
				const result = await this.ask(model, "textDocument/semanticTokens/full", {}, token);
				return result && result.data ? { data: Uint32Array.from(result.data), resultId: result.resultId } : null;
			},
			releaseDocumentSemanticTokens: () => {},
		}));
		add(caps.inlayHintProvider, () => L.registerInlayHintsProvider(selector, {
			onDidChangeInlayHints: this.refreshes.hints.event,
			provideInlayHints: async (model, range, token) => ({ hints: C.inlayHintsOf(M, await this.ask(model, "textDocument/inlayHint", { range: C.toLspRange(range) }, token)), dispose() {} }),
		}));
		const formatting = (options) => ({ tabSize: options.tabSize, insertSpaces: options.insertSpaces });
		add(caps.documentFormattingProvider, () => L.registerDocumentFormattingEditProvider(selector, {
			provideDocumentFormattingEdits: async (model, options, token) => C.textEditsOf(await this.ask(model, "textDocument/formatting", { options: formatting(options) }, token)),
		}));
		add(caps.documentRangeFormattingProvider, () => L.registerDocumentRangeFormattingEditProvider(selector, {
			provideDocumentRangeFormattingEdits: async (model, range, options, token) => C.textEditsOf(await this.ask(model, "textDocument/rangeFormatting", { range: C.toLspRange(range), options: formatting(options) }, token)),
		}));
		add(caps.renameProvider, () => L.registerRenameProvider(selector, this.rename(caps.renameProvider)));
		add(caps.codeActionProvider, () => L.registerCodeActionProvider(selector, {
			provideCodeActions: async (model, range, context, token) => ({ actions: this.codeActions(model, await this.askCodeActions(model, range, context, token)), dispose() {} }),
		}, { providedCodeActionKinds: caps.codeActionProvider.codeActionKinds }));
		return out;
	}

	completion(options) {
		return {
			triggerCharacters: options.triggerCharacters || [],
			provideCompletionItems: async (model, position, context, token) => {
				const word = model.getWordUntilPosition(position);
				const range = { startLineNumber: position.lineNumber, startColumn: word.startColumn, endLineNumber: position.lineNumber, endColumn: word.endColumn };
				// Monaco's trigger kinds (invoke, character, incomplete) are LSP's less one.
				const lspContext = { triggerKind: context.triggerKind + 1 };
				if (context.triggerCharacter) lspContext.triggerCharacter = context.triggerCharacter;
				const list = C.completionListOf(this.monaco, await this.ask(model, "textDocument/completion", { position: C.toLspPosition(position), context: lspContext }, token), range);
				for (const item of list.suggestions) item.model = model;
				return list;
			},
			resolveCompletionItem: options.resolveProvider ? async (item, token) => {
				const resolved = await this.ask(item.model, "completionItem/resolve", item.lsp, token);
				if (!resolved) return item;
				const better = C.completionItemOf(this.monaco, resolved, null, item.range);
				for (const key of ["detail", "documentation", "additionalTextEdits"]) if (better[key] !== undefined) item[key] = better[key];
				return item;
			} : undefined,
		};
	}

	rename(options) {
		return {
			provideRenameEdits: async (model, position, newName, token) => {
				const edit = await this.ask(model, "textDocument/rename", { position: C.toLspPosition(position), newName }, token);
				if (!edit) return { edits: [], rejectReason: "The language server did not rename it." };
				return this.monacoEdit(await this.openFor(edit));
			},
			resolveRenameLocation: options && options.prepareProvider
				? async (model, position, token) => C.renameLocationOf(await this.ask(model, "textDocument/prepareRename", { position: C.toLspPosition(position) }, token), model, position)
				: undefined,
		};
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Code actions

	askCodeActions(model, range, context, token) {
		const editor = this.editorOf(model);
		const lspRange = C.toLspRange(range);
		const diagnostics = [];
		if (editor) {
			for (const items of this.diagnostics.of(editor.ctx.wsId, editor.path).values()) {
				for (const item of items) if (C.rangesMeet(item.range, lspRange)) diagnostics.push(item);
			}
		}
		// Monaco's trigger types are LSP's: invoked 1, automatic 2.
		const lspContext = { diagnostics, triggerKind: context.trigger };
		if (context.only) lspContext.only = [context.only];
		return this.ask(model, "textDocument/codeAction", { range: lspRange, context: lspContext }, token);
	}

	// Each action as Monaco's: an edit of this document is applied as it is; one that reaches other files runs as a
	// command that opens them first; a bare command runs on the server.
	codeActions(model, result) {
		const editor = this.editorOf(model);
		const docId = editor ? editor.sync.docId : null;
		return (result || []).map((action) => {
			if (typeof action.command === "string") return { title: action.title, command: { id: EXECUTE, title: action.title, arguments: [docId, action] } };
			const out = { title: action.title, kind: action.kind, isPreferred: Boolean(action.isPreferred) };
			if (action.disabled) out.disabled = action.disabled.reason;
			if (action.diagnostics) out.diagnostics = C.markersOf(this.monaco, action.diagnostics);
			if (action.edit) {
				const edit = C.workspaceEditOf(action.edit);
				const local = edit.unsupported === 0 && [...edit.byUri.keys()].every((uri) => this.monaco.editor.getModel(this.monaco.Uri.parse(uri)));
				if (local) out.edit = this.monacoEdit(edit);
				else out.command = { id: APPLY, title: action.title, arguments: [action.edit] };
			}
			if (action.command) {
				if (out.command) this.ied.report("the code action '" + action.title + "' has a command after an edit that opens files; the command is not run");
				else out.command = { id: EXECUTE, title: action.title, arguments: [docId, action.command] };
			}
			return out;
		});
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Edits and locations in other files

	// The documents a WorkspaceEdit changes, opened as tabs so their editors take the edits: the converted edit.
	async openFor(lspEdit) {
		const edit = C.workspaceEditOf(lspEdit);
		const outside = [];
		for (const uri of edit.byUri.keys()) {
			const resource = this.monaco.Uri.parse(uri);
			if (resource.scheme !== "ied") outside.push(uri);
			else if (!this.editors.byRef("doc:" + resource.path.slice(1))) await this.openTab(resource.path.slice(1), true);
		}
		for (const uri of outside) edit.byUri.delete(uri);
		if (outside.length) this.ied.report("the edit also changes files outside the workspace, which IED does not change: " + outside.join(", "));
		if (edit.unsupported) this.ied.report("the edit also creates, renames or deletes files, which IED does not do for language servers yet");
		return edit;
	}

	monacoEdit(edit) {
		const edits = [];
		for (const [uri, textEdits] of edit.byUri) {
			const resource = this.monaco.Uri.parse(uri);
			for (const textEdit of textEdits) edits.push({ resource, textEdit, versionId: undefined });
		}
		return { edits };
	}

	async applyEdit(lspEdit) {
		const edit = await this.openFor(lspEdit);
		for (const [uri, textEdits] of edit.byUri) {
			const model = this.monaco.editor.getModel(this.monaco.Uri.parse(uri));
			if (model) model.pushEditOperations([], textEdits.map((e) => ({ range: e.range, text: e.text })), () => null);
		}
	}

	openTab(path, background) {
		const ref = "doc:" + path;
		this.ied.tabs.open(ref, { preview: !background });
		return this.editors.whenReady(ref);
	}

	// Locations in files not open get a model of their text, so a peek can show them: a workspace file's from disk, a
	// file outside the workspace from its read-only document (docs/10).
	async located(locations) {
		const missing = new Set();
		for (const location of locations) {
			if ((location.uri.scheme === "ied" || location.uri.scheme === "file") && !this.monaco.editor.getModel(location.uri)) missing.add(location.uri.toString());
		}
		await Promise.all([...missing].map(async (uri) => {
			const resource = this.monaco.Uri.parse(uri);
			try {
				const text = resource.scheme === "ied" ? (await this.ied.client.request("fs.read", { wsId: resource.authority, path: resource.path.slice(1) })).text
					: await this.externalText(canonicalFileUri(uri));
				if (!this.monaco.editor.getModel(resource)) this.monaco.editor.createModel(text, undefined, resource);
			}
			catch (e) {
				this.failed(resource.scheme === "ied" ? "fs.read" : "doc.open", e);
			}
		}));
		return locations;
	}

	// The text of a file outside the workspace: its document's first snapshot.
	async externalText(uri) {
		const opened = await this.ied.client.request("doc.open", { wsId: this.ied.workspace.wsId, uri });
		return new Promise((resolve, reject) => {
			let off = null;
			off = this.ied.client.subscribe("doc:" + opened.docId, {
				snap: (data) => {
					resolve(data.text);
					setTimeout(() => off && off(), 0);
				},
				evt: () => {},
				gone: (reason) => reject(new Error("the document is gone (" + reason + ")")),
			});
		});
	}

	// Monaco opening another model (go to definition, a peek's file): its tab, at the place. A file outside the workspace
	// opens read-only, named by its canonical file: URI.
	open(resource, selection) {
		const path = resource.scheme === "ied" ? resource.path.slice(1) : resource.scheme === "file" ? canonicalFileUri(resource.toString()) : null;
		if (!path) return false;
		this.openTab(path, false).then((editor) => editor.reveal(selection));
		return true;
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Diagnostics as markers

	markersChanged(wsId, path) {
		for (const editor of this.editors.all()) {
			if (editor.ctx.wsId === wsId && (path === null || editor.path === path) && editor.model && editor.markerOwners) this.setMarkers(editor);
		}
	}

	setMarkers(editor) {
		const sources = this.diagnostics.of(editor.ctx.wsId, editor.path);
		for (const owner of editor.markerOwners) if (!sources.has(owner)) this.monaco.editor.setModelMarkers(editor.model, "lsp:" + owner, []);
		editor.markerOwners = new Set(sources.keys());
		for (const [source, items] of sources) this.monaco.editor.setModelMarkers(editor.model, "lsp:" + source, C.markersOf(this.monaco, items));
	}
}
