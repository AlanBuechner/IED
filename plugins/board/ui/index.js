// index.js - the planning board's UI module (docs/32): boards as documents ("board:<path>"), the Boards tool window
// and the Board tool window under it, and the commands - New Board, Open Board (the Solution Explorer's menu on a
// .iedboard), View > Boards.

import { h, replace } from "/ui-kit/dom.js";
import { promptDialog } from "/ui-kit/dialog.js";
import { BoardView } from "./board.js";
import { PanelFrame } from "./panels.js";
import { currentBoard, onCurrentBoard } from "./current.js";
import { BoardListView } from "./boardlist.js";

const name = (path) => path.split("/").pop().replace(/\.iedboard$/, "");

function openBoard(ied, path) {
	ied.tabs.open("board:" + path, { preview: false });
}

// A new board file, opened; its path, or null when none was made.
async function newBoard(ied) {
	const path = await promptDialog({
		title: "New board",
		message: "A planning board is a file in the workspace: sketch on it, and let the AI clean it up, turn it into Mermaid and a plan, and build it.",
		value: "design/board.iedboard",
		confirm: "Create",
		check: (value) => (value.trim() ? "" : "Name the board's file"),
	});
	if (!path) return null;
	const made = await ied.client.request("ied.board/create", { wsId: ied.workspace.wsId, path: path.trim() }).catch((e) => {
		ied.toast("Cannot create the board: " + (e.message || e), { level: "error" });
		return null;
	});
	if (!made) return null;
	openBoard(ied, made.path);
	return made.path;
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
	return {
		dispose: () => view.dispose(),
		focus: () => view.focus(),
		visible: (shown) => {
			if (!shown) return;
			view.canvas.fit();
			view.shown();
		},
	};
}

// The Board tool window, under the list (docs/32 "The board in the UI"): the layers, plan and history of the board
// worked on now - the one shown or touched last.
export function BoardPanel(ctx) {
	const frame = new PanelFrame();
	ctx.el.classList.add("board-panel-window");
	replace(ctx.el, frame.el);
	const stop = onCurrentBoard((view) => frame.attach(view));
	frame.attach(currentBoard());
	return { dispose: () => stop(), visible: (shown) => { if (shown) frame.attach(currentBoard()); } };
}

// The Boards tool window: the workspace's boards in the owner's groups, live (boardlist.js).
export function BoardList(ctx) {
	const view = new BoardListView(ctx, (path) => openBoard(ctx.ied, path), () => newBoard(ctx.ied));
	return { dispose: () => view.dispose() };
}
