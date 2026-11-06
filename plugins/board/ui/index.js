// index.js - the planning board's UI module (docs/32): boards as documents ("board:<path>"), the Boards tool window,
// and the commands - New Board, Open Board (the Solution Explorer's menu on a .iedboard), View > Boards.

import { h, icon, replace } from "/ui-kit/dom.js";
import { promptDialog } from "/ui-kit/dialog.js";
import { BoardView } from "./board.js";

const name = (path) => path.split("/").pop().replace(/\.iedboard$/, "");

function openBoard(ied, path) {
	ied.tabs.open("board:" + path, { preview: false });
}

async function newBoard(ied) {
	const path = await promptDialog({
		title: "New board",
		message: "A planning board is a file in the workspace: sketch on it, and let the AI clean it up, turn it into Mermaid and a plan, and build it.",
		value: "design/board.iedboard",
		confirm: "Create",
		check: (value) => (value.trim() ? "" : "Name the board's file"),
	});
	if (!path) return;
	const made = await ied.client.request("ied.board/create", { wsId: ied.workspace.wsId, path: path.trim() }).catch((e) => {
		ied.toast("Cannot create the board: " + (e.message || e), { level: "error" });
		return null;
	});
	if (made) openBoard(ied, made.path);
}

export function activate(ied) {
	if (!document.querySelector("link[href='" + ied.plugin.base + "ui/board.css']")) document.head.append(h("link", { rel: "stylesheet", href: ied.plugin.base + "ui/board.css" }));
	ied.tabs.kind("board", { view: "BoardDocument", title: (path) => name(path), icon: () => "$(edit)", resource: (path) => path });
	ied.commands.handle("ied.board.show", () => ied.commands.run("shell.showActivity", { id: "ied.board" }));
	ied.commands.handle("ied.board.new", () => newBoard(ied));
	ied.commands.handle("ied.board.open", (args) => {
		const path = args && args.path ? args.path : ied.context.get("resourcePath");
		if (path) openBoard(ied, path);
	});
}

export function BoardDocument(ctx) {
	const view = new BoardView(ctx, ctx.ref.slice("board:".length));
	// For the e2e checks and the browser's console: the view behind a board tab.
	ctx.el.boardView = view;
	return { dispose: () => view.dispose(), focus: () => view.focus(), visible: (shown) => { if (shown) view.canvas.fit(); } };
}

// The Boards tool window: the workspace's boards, live; a click opens one.
export function BoardList(ctx) {
	const list = h("div.board-list", { role: "listbox", "aria-label": "Boards" });
	ctx.el.classList.add("board-list-window");
	replace(ctx.el,
		h("div.panel-tools", null,
			h("button.btn.flat", { title: "New board", on: { click: () => newBoard(ctx.ied) } }, icon("add"), "New board")),
		h("div.scroll", null, list));
	const render = (data) => {
		const boards = data.boards || [];
		replace(list, boards.length ? boards.map((b) => h("div.board-list-row", { role: "option", tabindex: "0", title: b.path, on: { click: () => openBoard(ctx.ied, b.path), keydown: (e) => { if (e.key === "Enter") openBoard(ctx.ied, b.path); } } },
			icon("edit"),
			h("span.ellip.grow", null, b.title || name(b.path)),
			h("span.muted.ellip", null, b.path.includes("/") ? b.path.slice(0, b.path.lastIndexOf("/")) : ""))) : [h("div.muted.board-empty", null, "No boards yet. A board is where you sketch a design before it is built.")]);
	};
	const stop = ctx.ied.client.subscribe("ied.board/ws:" + ctx.wsId, { snap: render, evt: (kind, data) => render(data) });
	return { dispose: () => stop() };
}
