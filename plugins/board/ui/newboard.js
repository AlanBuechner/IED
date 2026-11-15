// newboard.js - New Board (docs/32 "The board in the UI"): the board's name and the folder it goes in, chosen apart
// (owner, 2026-11-15: "when we create a new board we should be able to select the folder separately from the name").
// The folder field suggests the workspace's folders as it is typed - those holding boards first, then the folders
// under what is typed (fs.list), so a path is picked a level at a time - and a folder that is not there yet is made
// with the board. Create runs ied.board/create and says the server's refusal in place. A sheet on a phone.

import { h, icon, replace } from "/ui-kit/dom.js";

const FOLDER_KEY = "ied.board.newFolder";
const DEFAULT_FOLDER = "design";
const EXTENSION = ".iedboard";
const MOST_SUGGESTED = 12;
const NAME_MAX = 120;
// Characters no file or folder name of Windows takes (control characters are refused apart).
const RESERVED = /[<>:"|?*]/;
// Folders the Boards list never looks in (the server's Walk in BoardService.cf, which refuses a board there): not
// suggested.
const UNLISTED = new Set(["node_modules", "build", "vendor", "toolchains", "packages", "third_party", "bin", "obj", "target", "__pycache__"]);

const hasControl = (text) => [...text].some((c) => c.charCodeAt(0) < 32);

// A typed folder as a workspace path: '/' separators, no leading "./" or slashes at either end.
export function cleanFolder(text) {
	return String(text || "").trim().replace(/\\/g, "/").replace(/\/{2,}/g, "/").replace(/^(\.\/)+/, "").replace(/^\/+|\/+$/g, "");
}

// Why `typed` cannot be the board's folder, or "".
export function folderProblem(typed) {
	const raw = String(typed || "").trim().replace(/\\/g, "/");
	if (/^([A-Za-z]:|\/)/.test(raw)) return "The folder is inside the workspace: a path like design/checkout";
	const folder = cleanFolder(raw);
	if (!folder) return "";
	const parts = folder.split("/");
	if (parts.some((p) => p === "." || p === "..")) return "The folder is inside the workspace: no '.' or '..'";
	if (parts.some((p) => RESERVED.test(p) || hasControl(p))) return "A folder's name has none of < > : \" | ? *";
	const hidden = parts.find((p) => p.startsWith(".") || UNLISTED.has(p));
	if (hidden) return "The Boards list does not look in " + hidden + "/: choose another folder";
	return "";
}

// A typed name as the board's file name, without the extension.
export function cleanName(text) {
	return String(text || "").trim().replace(/\.iedboard$/i, "").trim();
}

// Why `name` (cleaned) cannot name a board, or "".
export function nameProblem(name) {
	if (!name) return "Name the board";
	if (/[\\/]/.test(name)) return "The name is the file's alone: choose its folder below";
	if (RESERVED.test(name) || hasControl(name)) return "A board's name has none of < > : \" | ? *";
	if (name === "." || name === ".." || /[. ]$/.test(name)) return "A board's name does not end in '.' or a space";
	if (name.length > NAME_MAX) return "A board's name is at most " + NAME_MAX + " characters";
	return "";
}

export function boardFile(folder, name) {
	return (folder ? folder + "/" : "") + name + EXTENSION;
}

export function folderOf(path) {
	const at = path.lastIndexOf("/");
	return at < 0 ? "" : path.slice(0, at);
}

const taken = (boards, path) => boards.some((b) => b.path.toLowerCase() === path.toLowerCase());

// The first name of `stem` no board in `folder` has: board, board2, board3...
export function freeName(boards, folder, stem = "board") {
	for (let n = 1; ; n++) {
		const name = n === 1 ? stem : stem + n;
		if (!taken(boards, boardFile(folder, name))) return name;
	}
}

// The folder most of `paths` (boards) are in - the earliest of those tied - or null for none.
export function commonFolder(paths) {
	const counts = new Map();
	for (const path of paths) counts.set(folderOf(path), (counts.get(folderOf(path)) || 0) + 1);
	let best = null;
	for (const [folder, count] of counts) if (best === null || count > counts.get(best)) best = folder;
	return best;
}

// The folders offered for `typed`: the known ones (those holding boards, the folders listed so far) that begin with it,
// any case, then those holding it elsewhere - only those under it when it ends in '/'; sorted, at most MOST_SUGGESTED.
// Pure, for the tests.
export function folderSuggestions(typed, known) {
	const text = cleanFolder(typed).toLowerCase();
	const all = [...new Set(known)].filter((f) => f && !folderProblem(f));
	if (text && /\/\s*$/.test(String(typed).replace(/\\/g, "/"))) return all.filter((f) => f.toLowerCase().startsWith(text + "/")).sort().slice(0, MOST_SUGGESTED);
	const starts = all.filter((f) => f.toLowerCase().startsWith(text)).sort();
	const holds = text ? all.filter((f) => !f.toLowerCase().startsWith(text) && f.toLowerCase().includes(text)).sort() : [];
	return [...starts, ...holds].slice(0, MOST_SUGGESTED);
}

function remembered() {
	try {
		return localStorage.getItem(FOLDER_KEY);
	}
	catch {
		return null;
	}
}

function remember(folder) {
	try {
		localStorage.setItem(FOLDER_KEY, folder);
	}
	catch {
		// a private window keeps nothing
	}
}

// Opens the dialog; resolves the new board's path once made, or null when cancelled. `folder`: the folder to start
// with (a group's boards' own); else the one chosen last on this device, else that of most boards, else design.
export async function showNewBoard(ied, { folder } = {}) {
	let boards = [];
	try {
		boards = (await ied.client.request("ied.board/list", { wsId: ied.workspace.wsId })).boards || [];
	}
	catch {
		// the dialog still works; the server refuses a name that is taken
	}
	const start = folder ?? remembered() ?? commonFolder(boards.map((b) => b.path)) ?? DEFAULT_FOLDER;
	return new Promise((resolve) => new NewBoardDialog(ied, boards, start, resolve).open());
}

class NewBoardDialog {
	constructor(ied, boards, folder, resolve) {
		this.ied = ied;
		this.boards = boards;
		this.resolve = resolve;
		this.listed = new Map();          // folder -> its subfolders (full paths), once fs.list answered
		this.known = new Set(boards.map((b) => folderOf(b.path)));
		this.selected = -1;
		this.shown = [];
		this.nameTouched = false;
		this.creating = false;
		const phone = window.matchMedia("(max-width: 719px)").matches;
		this.name = h("input.input-field", { type: "text", value: freeName(boards, cleanFolder(folder)), "aria-label": "Board name", spellcheck: "false", autocomplete: "off", on: {
			input: () => {
				this.nameTouched = true;
				this.check();
			},
		} });
		this.folder = h("input.input-field.mono", { type: "text", value: folder, placeholder: "the workspace's folder", "aria-label": "Folder", spellcheck: "false", autocomplete: "off", role: "combobox", "aria-autocomplete": "list", "aria-expanded": "false", on: {
			input: () => this.folderTyped(),
			focus: () => this.suggest(),
			blur: () => setTimeout(() => this.hideSuggestions(), 120),
			keydown: (event) => this.folderKey(event),
		} });
		this.toggle = h("button.ib.board-new-browse", { type: "button", title: "Show the folders", "aria-label": "Show the folders", tabindex: "-1", on: {
			mousedown: (event) => event.preventDefault(),
			click: () => {
				if (this.list.hidden) this.suggest(true);
				else this.hideSuggestions();
				this.folder.focus();
			},
		} }, icon("chevron-down"));
		this.list = h("div.board-new-folders", { role: "listbox", "aria-label": "Folders", hidden: true });
		this.where = h("div.board-new-where.muted.mono");
		this.problem = h("div.dialog-problem.c-error");
		this.make = h("button.btn.primary", { on: { click: () => this.create() } }, "Create");
		this.cancel = h("button.btn", { on: { click: () => this.finish(null) } }, "Cancel");
		this.box = h("div.popup.dialog.board-new" + (phone ? ".sheet" : ""), {
			role: "dialog", "aria-modal": "true", "aria-label": "New board",
			on: { keydown: (event) => this.boxKey(event) },
		},
		h("div.dialog-title", null, "New board"),
		h("div.dialog-message", null, "A planning board is a file in the workspace: sketch on it, and let the AI clean it up, turn it into Mermaid and a plan, and build it."),
		h("label.board-new-field", null, h("span", null, "Name"), this.name),
		h("div.board-new-field", null, h("label", { for: "board-new-folder" }, "Folder"),
			h("div.board-new-folder", null, this.folder, this.toggle, this.list)),
		this.where, this.problem,
		h("div.dialog-buttons", null, this.make, this.cancel));
		this.folder.id = "board-new-folder";
		this.veil = h("div.dialog-veil", { on: { mousedown: (event) => event.target === this.veil && this.finish(null) } }, this.box);
	}

	open() {
		this.previous = document.activeElement;
		document.body.append(this.veil);
		this.check();
		this.name.focus();
		this.name.select();
		this.load(cleanFolder(this.folder.value));
	}

	finish(path) {
		this.veil.remove();
		if (this.previous && this.previous.isConnected) this.previous.focus({ preventScroll: true });
		this.resolve(path);
	}

	boxKey(event) {
		event.stopPropagation();
		if (event.key === "Escape") {
			if (!this.list.hidden) this.hideSuggestions();
			else this.finish(null);
		}
		else if (event.key === "Enter" && document.activeElement !== this.cancel) this.create();
		else return;
		event.preventDefault();
	}

	// --- the folder and its suggestions ------------------------------------------------------------------------------

	folderTyped() {
		if (!this.nameTouched) this.name.value = freeName(this.boards, cleanFolder(this.folder.value));
		this.check();
		this.suggest(true);
		const typed = cleanFolder(this.folder.value);
		this.load(folderOf(typed));
		this.load(typed);
	}

	// The subfolders of `folder` ("" the workspace's own), once: they join the suggestions.
	async load(folder) {
		if (this.listed.has(folder) || folderProblem(folder)) return;
		this.listed.set(folder, null);
		let entries = [];
		try {
			entries = (await this.ied.client.request("fs.list", { wsId: this.ied.workspace.wsId, path: folder })).entries || [];
		}
		catch {
			// not a folder (yet): nothing under it
		}
		const subfolders = entries.filter((e) => e.kind === "dir").map((e) => e.path);
		this.listed.set(folder, subfolders);
		for (const path of subfolders) this.known.add(path);
		this.check();
		if (!this.list.hidden) this.suggest(true);
	}

	// Whether `folder` is there: true, false, or null when that is not known yet.
	exists(folder) {
		if (!folder || this.known.has(folder)) return true;
		const parent = this.listed.get(folderOf(folder));
		return parent ? parent.includes(folder) : null;
	}

	suggest(force) {
		if (!force && document.activeElement !== this.folder) return;
		this.shown = folderSuggestions(this.folder.value, this.known);
		this.selected = Math.min(this.selected, this.shown.length - 1);
		if (!this.shown.length) return this.hideSuggestions();
		replace(this.list, ...this.shown.map((folder, i) => h("div.board-new-folder-row" + (i === this.selected ? ".sel" : ""), {
			role: "option", "aria-selected": i === this.selected ? "true" : "false", dataset: { folder },
			on: {
				mousedown: (event) => event.preventDefault(),
				click: () => this.pick(folder),
			},
		}, icon("folder"), h("span.ellip", null, folder))));
		this.list.hidden = false;
		this.folder.setAttribute("aria-expanded", "true");
		const row = this.list.children[this.selected];
		if (row) row.scrollIntoView({ block: "nearest" });
	}

	hideSuggestions() {
		this.list.hidden = true;
		this.selected = -1;
		this.folder.setAttribute("aria-expanded", "false");
	}

	// A suggested folder chosen: it goes in the field, and its own folders are offered next.
	pick(folder) {
		this.folder.value = folder;
		this.selected = -1;
		this.folderTyped();
		this.folder.focus();
	}

	folderKey(event) {
		if (event.key === "ArrowDown" || event.key === "ArrowUp") {
			event.preventDefault();
			if (this.list.hidden) this.suggest(true);
			if (!this.shown.length) return;
			const step = event.key === "ArrowDown" ? 1 : -1;
			this.selected = Math.max(0, Math.min(this.shown.length - 1, this.selected + step));
			this.suggest(true);
		}
		else if ((event.key === "Enter" || event.key === "Tab") && !this.list.hidden && this.selected >= 0) {
			event.preventDefault();
			event.stopPropagation();
			this.pick(this.shown[this.selected]);
		}
	}

	// --- checking and making ---------------------------------------------------------------------------------------

	check() {
		const name = cleanName(this.name.value);
		const folder = cleanFolder(this.folder.value);
		const path = boardFile(folder, name);
		const why = nameProblem(name) || folderProblem(this.folder.value) || (taken(this.boards, path) ? "There is a board of that name in " + (folder || "the workspace's folder") : "");
		const fresh = !why && folder && this.exists(folder) === false;
		this.where.textContent = why ? "" : path + (fresh ? "  (a new folder)" : "");
		this.problem.textContent = why;
		this.make.disabled = Boolean(why) || this.creating;
		return !why;
	}

	async create() {
		if (this.creating || !this.check()) return;
		const folder = cleanFolder(this.folder.value);
		const path = boardFile(folder, cleanName(this.name.value));
		this.creating = true;
		this.name.disabled = this.folder.disabled = this.make.disabled = true;
		replace(this.make, icon("loading", "spin"), "Creating");
		let made;
		try {
			made = await this.ied.client.request("ied.board/create", { wsId: this.ied.workspace.wsId, path });
		}
		catch (e) {
			this.creating = false;
			this.name.disabled = this.folder.disabled = false;
			replace(this.make, "Create");
			this.check();
			this.problem.textContent = e.code === "conflict" ? "There is already a file at " + path : "Cannot create the board: " + (e.message || e);
			return;
		}
		remember(folder);
		this.finish(made.path);
	}
}
