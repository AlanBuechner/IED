// mentions.js - what the composer's @ list offers (docs/04 "Attachments and @-mentions"): the workspace's files and their
// folders (fs.files), symbols after "@#" (lsp.workspaceSymbols), terminals (term.list), output sources (out.sources),
// the other chats (chat.list), and the Error List and the working tree's diff; each a mention {kind, ref, label} the
// message carries, its "@label" in the text.

const MOST_ROWS = 12;
const MOST_FILES = 40;

export const KIND_ICONS = { file: "file", folder: "folder", symbol: "symbol-misc", terminal: "terminal", output: "output", problems: "warning", diff: "diff", chat: "comment-discussion" };

const FIXED = [
	{ kind: "problems", ref: "", label: "problems", detail: "the Error List" },
	{ kind: "diff", ref: "", label: "diff", detail: "the working tree's changes against HEAD" },
];

// The "@word" being typed at `caret`: {start, query}, start the "@"'s index; null when the caret is not in one. An "@"
// starts a mention at the text's start or after a space or an opening bracket, never inside a word (an address).
export function mentionAt(text, caret) {
	let start = caret;
	while (start > 0 && !/[\s([{]/.test(text[start - 1])) start--;
	return text[start] === "@" ? { start, query: text.slice(start + 1, caret) } : null;
}

// How well `label` matches `query` (lower is better): its start, a part's start, inside it, its letters in order;
// null for none.
export function matchRank(query, label) {
	const wanted = query.toLowerCase();
	const lower = label.toLowerCase();
	if (!wanted) return 0;
	if (lower.startsWith(wanted)) return 0;
	const name = lower.slice(lower.lastIndexOf("/", lower.length - 2) + 1);
	if (name.startsWith(wanted)) return 1;
	if (lower.includes(wanted)) return 2;
	let at = 0;
	for (const c of lower) if (c === wanted[at]) at++;
	return at === wanted.length ? 3 : null;
}

// The folders of `paths`, each with its trailing "/".
export function foldersOf(paths) {
	const folders = new Set();
	for (const path of paths) {
		for (let slash = path.indexOf("/"); slash > 0; slash = path.indexOf("/", slash + 1)) folders.add(path.slice(0, slash + 1));
	}
	return [...folders];
}

// The rows for `query`, best first: candidates ranked by their label, ties in the order given.
export function rankRows(query, candidates) {
	const ranked = [];
	candidates.forEach((candidate, order) => {
		const rank = matchRank(query, candidate.label);
		if (rank !== null) ranked.push({ candidate, rank, order });
	});
	return ranked.sort((a, b) => a.rank - b.rank || a.order - b.order).slice(0, MOST_ROWS).map((entry) => entry.candidate);
}

// A workspace symbol as a mention: by the place of its name, "path:line" (1-based), which the server reads it at.
export function symbolMention(symbol, wsId) {
	const location = symbol.location || {};
	const prefix = "ied://" + wsId + "/";
	if (typeof location.uri !== "string" || !location.uri.startsWith(prefix)) return null;
	const path = decodeURIComponent(location.uri.slice(prefix.length));
	const line = location.range ? location.range.start.line + 1 : 1;
	const name = symbol.containerName ? symbol.containerName + "::" + symbol.name : symbol.name;
	return { kind: "symbol", ref: path + ":" + line, label: "#" + name, detail: path };
}

// The mentions whose "@label" the text still holds, each once.
export function keptMentions(text, mentions) {
	const seen = new Set();
	return mentions.filter((mention) => {
		const key = mention.kind + "\n" + mention.ref;
		if (seen.has(key) || !text.includes("@" + mention.label)) return false;
		seen.add(key);
		return true;
	});
}

// request(method, params) -> Promise<result | null>, as the chat pane's. The lists other than files and symbols are
// asked once per @ typed and kept while it is open.
export class MentionSource {
	constructor(request, wsId, chatId) {
		this.request = request;
		this.wsId = wsId;
		this.chatId = chatId;
		this.fixed = null;
	}

	reset() {
		this.fixed = null;
	}

	async rows(query) {
		if (query.startsWith("#")) return this.symbols(query.slice(1));
		const [fixed, files] = await Promise.all([this.fixedRows(), this.request("fs.files", { wsId: this.wsId, query, maxResults: MOST_FILES })]);
		const paths = files ? files.files.map((file) => file.path) : [];
		const fileRows = paths.map((path) => ({ kind: "file", ref: path, label: path }));
		const folderRows = foldersOf(paths).map((folder) => ({ kind: "folder", ref: folder.slice(0, -1), label: folder }));
		return rankRows(query, fileRows.concat(folderRows, fixed));
	}

	async symbols(query) {
		if (!query) return [];
		const found = await this.request("lsp.workspaceSymbols", { wsId: this.wsId, query });
		const rows = found ? found.symbols.map((symbol) => symbolMention(symbol, this.wsId)).filter(Boolean) : [];
		return rankRows("#" + query, rows);
	}

	async fixedRows() {
		if (this.fixed) return this.fixed;
		const [terms, outputs, chats] = await Promise.all([
			this.request("term.list", { wsId: this.wsId }),
			this.request("out.sources", { wsId: this.wsId }),
			this.request("chat.list", { wsId: this.wsId }),
		]);
		const rows = FIXED.slice();
		for (const term of terms ? terms.terminals : []) rows.push({ kind: "terminal", ref: term.termId, label: "terminal:" + (term.title || term.termId), detail: term.alive ? "running" : "exited" });
		for (const source of outputs ? outputs.sources : []) rows.push({ kind: "output", ref: source.sourceId, label: "output:" + (source.label || source.sourceId) });
		for (const chat of chats ? chats.chats : []) if (chat.chatId !== this.chatId) rows.push({ kind: "chat", ref: chat.chatId, label: "chat:" + chat.title, detail: chat.turnCount + " turns" });
		this.fixed = rows;
		return rows;
	}
}
