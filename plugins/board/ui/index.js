// index.js - the planning board's UI module (docs/32): boards as documents ("board:<path>"), the Boards tool window
// and the Board tool window under it, and the commands - New Board, Open Board (the Solution Explorer's menu on a
// .iedboard), View > Boards.

import { h, replace } from "/ui-kit/dom.js";
import { BoardView } from "./board.js";
import { PanelFrame } from "./panels.js";
import { currentBoard, onCurrentBoard } from "./current.js";
import { BoardListView } from "./boardlist.js";
import { showNewBoard } from "./newboard.js";

const name = (path) => path.split("/").pop().replace(/\.iedboard$/, "");

function openBoard(ied, path) {
	ied.tabs.open("board:" + path, { preview: false });
}

// A new board file (its name and folder asked, newboard.js), opened; its path, or null when none was made.
// `options.folder`: the folder the dialog starts with.
async function newBoard(ied, options) {
	const path = await showNewBoard(ied, options);
	if (!path) return null;
	openBoard(ied, path);
	return path;
}

export function activate(ied) {
	if (!document.querySelector("link[href='" + ied.plugin.base + "ui/board.css']")) document.head.append(h("link", { rel: "stylesheet", href: ied.plugin.base + "ui/board.css" }));
	ied.tabs.kind("board", { view: "BoardDocument", title: (path) => name(path), icon: () => "$(edit)", resource: (path) => path });
	ied.commands.handle("ied.board.show", () => ied.commands.run("shell.showActivity", { id: "ied.board" }));
	ied.commands.handle("ied.board.new", (args) => newBoard(ied, args && args.folder !== undefined ? { folder: args.folder } : {}));
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
	const view = new BoardListView(ctx, (path) => openBoard(ctx.ied, path), (options) => newBoard(ctx.ied, options));
	return { dispose: () => view.dispose() };
}
