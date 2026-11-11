// boardlist.js - the Boards tool window (docs/32 "The board in the UI", "Groups"): the workspace's boards, live, in the
// owner's groups - folded or open as this device left them - then the boards in none. A click opens a board; a board is
// dragged onto a group (or between two of its boards) or moved from its menu; a group's menu renames or deletes it (its
// boards stay, ungrouped) and makes a board in it. The arrangement lives in the workspace's .ied/boards.json, written
// whole through ied.board/groups.

import { h, icon, replace } from "/ui-kit/dom.js";
import { MenuHost } from "/ui-kit/menu.js";
import { promptDialog } from "/ui-kit/dialog.js";

const menus = new MenuHost();
const CLOSED_KEY = "ied.board.closedGroups";
const UNGROUPED = "";
const DRAG_TYPE = "application/x-ied-board";

export const boardName = (path) => path.split("/").pop().replace(/\.iedboard$/, "");

function stored(key, fallback) {
	try {
		const value = localStorage.getItem(key);
		return value === null ? fallback : JSON.parse(value);
	}
	catch {
		return fallback;
	}
}

function store(key, value) {
	try {
		localStorage.setItem(key, JSON.stringify(value));
	}
	catch {
		// a private window: the choice lasts as long as the page
	}
}

// The list's sections: each group in order with the boards it names that exist, then the boards in none
// ({name: "", boards}). Pure, for the tests.
export function sectionsOf(list) {
	const boards = new Map((list.boards || []).map((b) => [b.path, b]));
	const placed = new Set();
	const sections = (list.groups || []).map((group) => {
		const own = group.boards.filter((path) => boards.has(path) && !placed.has(path));
		for (const path of own) placed.add(path);
		return { name: group.name, boards: own.map((path) => boards.get(path)) };
	});
	sections.push({ name: UNGROUPED, boards: (list.boards || []).filter((b) => !placed.has(b.path)) });
	return sections;
}

// The groups with `path` moved into `group` (UNGROUPED: out of every group) before `before` (a path of that group; null:
// at its end). Pure, for the tests.
export function movedTo(groups, path, group, before) {
	const next = groups.map((g) => ({ name: g.name, boards: g.boards.filter((p) => p !== path) }));
	const into = next.find((g) => g.name === group);
	if (!into) return next;
	const at = before ? into.boards.indexOf(before) : -1;
	if (at < 0) into.boards.push(path);
	else into.boards.splice(at, 0, path);
	return next;
}

export class BoardListView {
	constructor(ctx, openBoard, newBoard) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.openBoard = openBoard;
		this.newBoard = newBoard;
		this.data = { groups: [], boards: [] };
		this.query = "";
		this.closed = new Set(stored(CLOSED_KEY, []));
		this.filter = h("input.input-field.board-list-filter", { type: "search", placeholder: "Search boards", "aria-label": "Search boards", spellcheck: "false", on: { input: () => {
			this.query = this.filter.value.trim().toLowerCase();
			this.render();
		} } });
		this.list = h("div.board-list", { role: "tree", "aria-label": "Boards" });
		ctx.el.classList.add("board-list-window");
		replace(ctx.el,
			h("div.panel-tools", null,
				this.filter,
				h("button.ib", { title: "New board", "aria-label": "New board", on: { click: () => this.newBoard() } }, icon("add")),
				h("button.ib", { title: "New group", "aria-label": "New group", on: { click: () => this.newGroup() } }, icon("new-folder"))),
			h("div.scroll", null, this.list));
		this.stop = this.ied.client.subscribe("ied.board/ws:" + ctx.wsId, {
			snap: (data) => this.show(data),
			evt: (kind, data) => this.show(data),
		});
	}

	show(data) {
		this.data = { groups: data.groups || [], boards: data.boards || [] };
		this.render();
	}

	matches(board) {
		return !this.query || (board.title || boardName(board.path)).toLowerCase().includes(this.query) || board.path.toLowerCase().includes(this.query);
	}

	render() {
		const sections = sectionsOf(this.data);
		if (!this.data.boards.length && !this.data.groups.length) {
			return replace(this.list, h("div.muted.board-empty", null, "No boards yet. A board is where you sketch a design before it is built."));
		}
		const grouped = this.data.groups.length > 0;
		replace(this.list, ...sections.map((section) => {
			const boards = section.boards.filter((b) => this.matches(b));
			if (section.name === UNGROUPED) {
				if (!grouped) return h("div.board-list-rows", { role: "group", on: this.dropOn(UNGROUPED, null) }, ...boards.map((b) => this.row(b, UNGROUPED)));
				if (!boards.length && !this.dragging) return null;
				return this.section(UNGROUPED, "Ungrouped", boards);
			}
			if (this.query && !boards.length) return null;
			return this.section(section.name, section.name, boards);
		}).filter(Boolean));
	}

	section(group, title, boards) {
		const closed = this.closed.has(group) && !this.query;
		const head = h("button.board-set-head", {
			"aria-expanded": closed ? "false" : "true", dataset: { group },
			on: Object.assign({
				click: () => this.toggle(group),
				contextmenu: (event) => group !== UNGROUPED && this.groupMenu(event, group),
			}, this.dropOn(group, null)),
		}, icon(closed ? "chevron-right" : "chevron-down"), group === UNGROUPED ? null : icon("folder"), h("span.grow.ellip", null, title), h("span.meta", null, String(boards.length)));
		const rows = closed ? null : h("div.board-list-rows", { role: "group", on: this.dropOn(group, null) },
			...(boards.length ? boards.map((b) => this.row(b, group)) : [h("div.muted.board-set-empty", null, group === UNGROUPED ? "Drop a board here to take it out of its group." : "Empty: drag a board here, or use a board's Move to.")]));
		return h("section.board-set" + (closed ? ".closed" : ""), { dataset: { group } }, head, rows);
	}

	row(board, group) {
		const open = () => this.openBoard(board.path);
		return h("div.board-list-row", {
			role: "treeitem", tabindex: "0", title: board.path, draggable: "true", dataset: { path: board.path, group },
			on: Object.assign({
				click: open,
				keydown: (e) => {
					if (e.key === "Enter") open();
					else if (e.key === "ContextMenu" || (e.key === "F10" && e.shiftKey)) {
						e.preventDefault();
						this.boardMenu(e.target, board, group);
					}
				},
				contextmenu: (event) => this.boardMenu(event, board, group),
				dragstart: (event) => {
					event.dataTransfer.setData(DRAG_TYPE, board.path);
					event.dataTransfer.effectAllowed = "move";
					this.dragging = board.path;
					// Ungrouped shows as a drop target while a board is dragged.
					requestAnimationFrame(() => this.render());
				},
				dragend: () => {
					this.dragging = null;
					this.render();
				},
			}, this.dropOn(group, board.path)),
		},
		icon("edit"),
		h("span.ellip.grow", null, board.title || boardName(board.path)),
		h("span.muted.ellip", null, board.path.includes("/") ? board.path.slice(0, board.path.lastIndexOf("/")) : ""));
	}

	// Drop handlers that move the dragged board into `group`, before `before` (null: at its end).
	dropOn(group, before) {
		return {
			dragover: (event) => {
				if (!event.dataTransfer.types.includes(DRAG_TYPE)) return;
				event.preventDefault();
				event.stopPropagation();
				event.dataTransfer.dropEffect = "move";
				event.currentTarget.classList.add("drop");
			},
			dragleave: (event) => event.currentTarget.classList.remove("drop"),
			drop: (event) => {
				const path = event.dataTransfer.getData(DRAG_TYPE);
				event.currentTarget.classList.remove("drop");
				if (!path) return;
				event.preventDefault();
				event.stopPropagation();
				if (path !== before) this.move(path, group, before);
			},
		};
	}

	toggle(group) {
		if (this.closed.has(group)) this.closed.delete(group);
		else this.closed.add(group);
		store(CLOSED_KEY, [...this.closed]);
		this.render();
	}

	// --- Changing the arrangement --------------------------------------------------------------------------------------

	async save(groups) {
		try {
			this.show(await this.ied.client.request("ied.board/groups", { wsId: this.ctx.wsId, groups }));
			return true;
		}
		catch (e) {
			this.ied.toast("Cannot change the board groups: " + (e.message || e), { level: "error" });
			return false;
		}
	}

	groups() {
		return this.data.groups.map((g) => ({ name: g.name, boards: g.boards.slice() }));
	}

	move(path, group, before) {
		return this.save(movedTo(this.groups(), path, group, before));
	}

	async askName(title, value, confirm) {
		const taken = new Set(this.data.groups.map((g) => g.name));
		const name = await promptDialog({ title, value, placeholder: "Group name", confirm, check: (text) => {
			const trimmed = text.trim();
			if (!trimmed) return "Name the group";
			if (trimmed !== value && taken.has(trimmed)) return "There is a group of that name";
			return "";
		} });
		return name ? name.trim() : null;
	}

	// A new group, at the end; `path`: a board moved into it.
	async newGroup(path) {
		const name = await this.askName("New group", "", "Create");
		if (!name) return;
		const groups = this.groups();
		groups.push({ name, boards: [] });
		await this.save(path ? movedTo(groups, path, name, null) : groups);
		this.closed.delete(name);
	}

	async renameGroup(group) {
		const name = await this.askName("Rename group", group, "Rename");
		if (!name || name === group) return;
		const wasClosed = this.closed.delete(group);
		if (wasClosed) this.closed.add(name);
		store(CLOSED_KEY, [...this.closed]);
		await this.save(this.groups().map((g) => (g.name === group ? { name, boards: g.boards } : g)));
	}

	deleteGroup(group) {
		return this.save(this.groups().filter((g) => g.name !== group));
	}

	moveGroup(group, by) {
		const groups = this.groups();
		const at = groups.findIndex((g) => g.name === group);
		const to = at + by;
		if (at < 0 || to < 0 || to >= groups.length) return null;
		const [moved] = groups.splice(at, 1);
		groups.splice(to, 0, moved);
		return this.save(groups);
	}

	// --- Menus ---------------------------------------------------------------------------------------------------------

	boardMenu(at, board, group) {
		if (at.preventDefault) at.preventDefault();
		const others = this.data.groups.filter((g) => g.name !== group);
		menus.show({ at, label: board.title || boardName(board.path), rows: [
			{ kind: "item", label: "Open", id: "open", enabled: true, keys: "Enter" },
			{ kind: "separator" },
			{ kind: "submenu", label: "Move to Group", menuId: "move" },
			group !== UNGROUPED ? { kind: "item", label: "Remove from Group", id: "ungroup", enabled: true } : null,
		].filter(Boolean), submenu: () => [
			...others.map((g) => ({ kind: "item", label: g.name, id: "to:" + g.name, enabled: true })),
			others.length ? { kind: "separator" } : null,
			{ kind: "item", label: "New Group…", id: "new", enabled: true },
		].filter(Boolean), pick: (row) => {
			if (row.id === "open") this.openBoard(board.path);
			else if (row.id === "ungroup") this.move(board.path, UNGROUPED, null);
			else if (row.id === "new") this.newGroup(board.path);
			else if (row.id.startsWith("to:")) this.move(board.path, row.id.slice(3), null);
		} });
	}

	groupMenu(event, group) {
		event.preventDefault();
		const at = this.data.groups.findIndex((g) => g.name === group);
		menus.show({ at: event, label: group, rows: [
			{ kind: "item", label: "New Board in Group…", id: "board", enabled: true },
			{ kind: "separator" },
			{ kind: "item", label: "Rename Group…", id: "rename", enabled: true },
			{ kind: "item", label: "Move Up", id: "up", enabled: at > 0 },
			{ kind: "item", label: "Move Down", id: "down", enabled: at >= 0 && at < this.data.groups.length - 1 },
			{ kind: "separator" },
			{ kind: "item", label: "Delete Group (keeps its boards)", id: "delete", enabled: true },
		], pick: async (row) => {
			if (row.id === "board") {
				const made = await this.newBoard();
				if (made) await this.move(made, group, null);
			}
			else if (row.id === "rename") this.renameGroup(group);
			else if (row.id === "up") this.moveGroup(group, -1);
			else if (row.id === "down") this.moveGroup(group, 1);
			else if (row.id === "delete") this.deleteGroup(group);
		} });
	}

	dispose() {
		this.stop();
	}
}
