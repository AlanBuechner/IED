// board.js - a board as a document (docs/32 "The board in the UI"): the toolbar (tools, colors, widths, eraser,
// undo, zoom, image, the AI button that opens the board's chat) and the canvas. Its side panel (Layers, Plan,
// History) shows in the Board tool window under the board list, or in a sheet over the board on a phone; its chat,
// beside the boards, follows the board shown.

import { h, icon, replace } from "/ui-kit/dom.js";
import { BoardModel } from "./model.js";
import { BoardCanvas } from "./canvas.js";
import { BoardInput, TOOLS } from "./tools.js";
import { PALETTE_NAMES } from "./render.js";
import { LayersPanel, PlanPanel, HistoryPanel, PanelFrame } from "./panels.js";
import { addBoard, removeBoard, setCurrentBoard } from "./current.js";
import { union, EMPTY, isEmpty } from "./geom.js";

const WIDTHS = [1, 2, 4, 8];
const PHONE = 720;
// The Board tool window (Layers, Plan, History), the chat list's section for the boards' chats, and the share of the
// width a board's chat takes when it opens a group of its own beside the boards.
const PANEL_WINDOW = "ied.board.panel";
const CHAT_SECTION = "Boards";
const CHAT_SHARE = 0.3;
const PANEL_PLACED_KEY = "ied.board.panelPlaced";

export class BoardView {
	constructor(ctx, path) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.path = path;
		this.model = new BoardModel(ctx.ied, ctx.wsId, path);
		this.images = new Map();
		this.activeLayerId = null;
		this.local = {};
		this.statusText = h("span.board-status.muted");
		this.stage = h("div.board-stage");
		this.toolbar = h("div.board-toolbar", { role: "toolbar", "aria-label": "Board tools" });
		this.sheetFrame = new PanelFrame();
		this.sheet = h("aside.board-sheet", { "aria-label": "Layers, plan and history", hidden: true }, this.sheetFrame.el);
		this.message = h("div.board-message", { hidden: true });
		ctx.el.classList.add("board-doc");
		replace(ctx.el, this.toolbar, h("div.board-main", null, this.stage, this.sheet), this.message);
		this.canvas = new BoardCanvas(this.stage, this.model, (src) => this.image(src));
		this.input = new BoardInput(this);
		this.panels = { layers: new LayersPanel(this), plan: new PlanPanel(this), history: new HistoryPanel(this) };
		this.buildToolbar();
		this.localReady = new Promise((resolve) => { this.localLoaded = resolve; });
		addBoard(this);
		const touched = () => setCurrentBoard(this);
		ctx.el.addEventListener("pointerdown", touched, true);
		ctx.el.addEventListener("focusin", touched);
		this.stopModel = this.model.on((change) => this.changed(change));
		this.stopSettings = this.ied.client.subscribe("settings:" + ctx.wsId, {
			snap: (data) => this.settings(data.values || {}),
			evt: (kind, data) => { if (kind === "settings.changed" && data.values) this.settings(data.values); },
		});
		this.paste = (e) => this.onPaste(e);
		document.addEventListener("paste", this.paste);
		this.stage.addEventListener("dragover", (e) => { if ([...(e.dataTransfer.items || [])].some((i) => i.kind === "file")) e.preventDefault(); });
		this.stage.addEventListener("drop", (e) => this.onDrop(e));
		this.firstShow = true;
		this.model.open();
		ctx.tab.setTitle(path.split("/").pop().replace(/\.iedboard$/, ""));
	}

	dispose() {
		document.removeEventListener("paste", this.paste);
		this.panels.plan.save();
		this.disposed = true;
		removeBoard(this);
		this.stopModel();
		if (this.stopSettings) this.stopSettings();
		this.input.dispose();
		this.canvas.dispose();
		this.model.dispose();
	}

	focus() {
		this.canvas.canvas.focus({ preventScroll: true });
	}

	// --- toolbar ---------------------------------------------------------------------------------------------------

	buildToolbar() {
		this.toolButtons = new Map();
		const tools = TOOLS.map((t) => {
			const b = h("button.ib.board-tool", { title: t.title + " (" + t.key.toUpperCase() + ")", "aria-label": t.title, "aria-pressed": "false", dataset: { tool: t.id }, on: { click: () => this.input.setTool(t.id) } }, icon(t.icon));
			this.toolButtons.set(t.id, b);
			return b;
		});
		this.swatches = PALETTE_NAMES.map((name) => h("button.board-swatch", { title: name, "aria-label": "Color " + name, dataset: { color: name }, style: { "--swatch": name === "ink" ? "var(--text-0)" : "var(--hue-" + name + ")" }, on: { click: (e) => this.setColor(name, e.shiftKey) } }));
		this.fillButton = h("button.ib.board-fill", { title: "Fill new shapes with the color (Shift+click a color sets the fill alone)", "aria-label": "Fill", "aria-pressed": "false", on: { click: () => this.toggleFill() } }, icon("symbol-color"));
		this.widthButtons = WIDTHS.map((w) => h("button.ib.board-width", { title: "Line width " + w, "aria-label": "Line width " + w, dataset: { width: String(w) }, on: { click: () => this.setWidth(w) } }, h("span.board-width-bar", { style: { "--w": Math.min(w, 6) + "px" } })));
		this.eraserMode = h("button.btn.flat.board-eraser-mode", { title: "Eraser: cut through strokes, or take whole strokes and shapes", on: { click: () => this.toggleEraser() } }, "Partial");
		this.eraserSize = h("input.board-eraser-size", { type: "range", min: "6", max: "80", value: "16", title: "Eraser size", "aria-label": "Eraser size", on: { input: (e) => { this.input.eraser.size = Number(e.target.value); } } });
		this.eraserGroup = h("span.board-group.board-eraser", { hidden: true }, this.eraserMode, this.eraserSize);
		this.snapButton = h("button.ib.board-snap", { title: "Snap to alignment: edges, centers and equal gaps (hold Alt while dragging to place freely)", "aria-label": "Snap to alignment", "aria-pressed": "true", on: { click: () => this.setSnap(!this.input.snap) } }, icon("magnet"));
		this.zoomLabel = h("button.btn.flat.board-zoom", { title: "Zoom: click for 100%", on: { click: () => { this.canvas.camera.zoom = 1; this.canvas.request(); this.updateZoom(); } } }, "100%");
		const sep = () => h("span.board-sep");
		replace(this.toolbar,
			h("span.board-group.board-tools", null, tools),
			sep(),
			h("span.board-group.board-colors", null, this.swatches, this.fillButton),
			sep(),
			h("span.board-group", null, this.widthButtons),
			this.eraserGroup,
			sep(),
			h("button.ib", { title: "Insert an image (or paste or drop one)", "aria-label": "Insert image", on: { click: () => this.pickImage() } }, icon("file-media")),
			h("button.ib", { title: "Undo (Ctrl+Z)", "aria-label": "Undo", on: { click: () => this.model.undo() } }, icon("discard")),
			h("button.ib", { title: "Redo (Ctrl+Y)", "aria-label": "Redo", on: { click: () => this.model.redo() } }, icon("redo")),
			this.snapButton,
			h("span.grow"),
			// The view's end of the toolbar keeps together when a narrow board (its chat beside it) wraps the toolbar.
			h("span.board-group.board-end", null,
				this.statusText,
				h("button.ib", { title: "Zoom out", "aria-label": "Zoom out", on: { click: () => this.zoomBy(0.8) } }, icon("zoom-out")),
				this.zoomLabel,
				h("button.ib", { title: "Zoom in", "aria-label": "Zoom in", on: { click: () => this.zoomBy(1.25) } }, icon("zoom-in")),
				h("button.ib", { title: "Show everything (Shift+1)", "aria-label": "Fit", on: { click: () => this.canvas.show(this.canvas.contentBox()) } }, icon("screen-full")),
				h("button.btn.flat.board-ai-button", { title: "Open the board's chat: it sees the board, its layers, your selection and its plans, and changes them live", on: { click: () => this.openChat() } }, icon("sparkle", "c-ai"), h("span.lbl", null, "AI")),
				h("button.ib.board-panel-toggle", { title: "Layers, plan and history", "aria-label": "Layers, plan and history", on: { click: () => this.showPanels() } }, icon("layers"))));
		this.toolChanged();
		this.styleChanged();
		this.canvas.overlays.push(() => this.updateZoom());
	}

	// The board's settings: board.snap (docs/32 "Snapping").
	settings(values) {
		const snap = values["board.snap"];
		this.input.snap = !snap || snap.value !== false;
		this.snapButton.setAttribute("aria-pressed", String(this.input.snap));
	}

	// The magnet: board.snap for every board, at the User tier.
	async setSnap(on) {
		this.input.snap = on;
		this.snapButton.setAttribute("aria-pressed", String(on));
		try {
			await this.ied.client.request("settings.set", { tier: "app", key: "board.snap", value: on, wsId: this.ctx.wsId });
		}
		catch (e) {
			this.ied.toast("Cannot save the snapping setting: " + (e.message || e), { level: "error" });
		}
	}

	zoomBy(factor) {
		this.canvas.zoomAt(this.canvas.width / 2, this.canvas.height / 2, factor);
	}

	updateZoom() {
		const text = Math.round(this.canvas.camera.zoom * 100) + "%";
		if (this.zoomLabel.textContent !== text) this.zoomLabel.textContent = text;
	}

	toolChanged() {
		for (const [id, b] of this.toolButtons) b.setAttribute("aria-pressed", String(id === this.input.tool));
		this.eraserGroup.hidden = this.input.tool !== "eraser";
		this.stage.dataset.tool = this.input.tool;
	}

	styleChanged() {
		const s = this.input.style;
		for (const b of this.swatches) b.classList.toggle("on", b.dataset.color === s.color);
		this.fillButton.setAttribute("aria-pressed", String(s.fill !== "none"));
		for (const b of this.widthButtons) b.setAttribute("aria-pressed", String(Number(b.dataset.width) === s.width));
	}

	// A color for what comes next, and for what is selected.
	setColor(name, fillOnly) {
		const s = this.input.style;
		if (fillOnly) s.fill = name === "ink" ? "none" : name;
		else {
			s.color = name;
			if (s.fill !== "none") s.fill = name === "ink" ? "none" : name;
		}
		this.styleChanged();
		const ids = [...this.input.selection];
		if (!ids.length) return;
		const ops = ids.map((id) => {
			const el = this.model.get(id);
			if (!el) return null;
			if (fillOnly || el.type === "note") return { op: "update", id, fill: name === "ink" ? "none" : name };
			if (el.type === "image") return null;
			return { op: "update", id, color: name };
		}).filter(Boolean);
		if (ops.length) this.model.edit(ops, "color");
	}

	toggleFill() {
		const s = this.input.style;
		s.fill = s.fill === "none" ? (s.color === "ink" ? "gray" : s.color) : "none";
		this.styleChanged();
		const ids = [...this.input.selection].filter((id) => { const el = this.model.get(id); return el && el.type !== "image" && el.type !== "stroke"; });
		if (ids.length) this.model.edit(ids.map((id) => ({ op: "update", id, fill: s.fill })), "fill");
	}

	setWidth(w) {
		this.input.style.width = w;
		this.styleChanged();
		const ids = [...this.input.selection].filter((id) => { const el = this.model.get(id); return el && el.type !== "image" && el.type !== "text"; });
		if (ids.length) this.model.edit(ids.map((id) => ({ op: "update", id, width: w })), "width");
	}

	toggleEraser() {
		const e = this.input.eraser;
		e.mode = e.mode === "partial" ? "stroke" : "partial";
		this.eraserMode.textContent = e.mode === "partial" ? "Partial" : "Whole";
	}

	status(text) {
		this.statusText.textContent = text || "";
	}

	// --- layers ----------------------------------------------------------------------------------------------------

	activeLayer() {
		const layers = this.model.layers;
		return layers.find((l) => l.id === this.activeLayerId) || layers[layers.length - 1] || null;
	}

	setActiveLayer(id) {
		this.activeLayerId = id;
		this.panels.layers.render();
	}

	// --- panel -----------------------------------------------------------------------------------------------------

	// Layers, Plan and History (owner, 2026-11-11: "the side panel with the layers and the history etc could be moved
	// to a panel under the list of boards"): the Board tool window, which shows the board worked on now; on a phone, a
	// sheet over the lower half of the board, which the button opens and closes.
	showPanels() {
		if (window.innerWidth < PHONE) {
			const open = this.sheet.hidden;
			this.sheet.hidden = !open;
			this.ctx.el.classList.toggle("panel-open", open);
			if (open) this.sheetFrame.attach(this);
			return;
		}
		setCurrentBoard(this);
		this.ied.commands.run("shell.showView", { id: PANEL_WINDOW });
	}

	// The Board tool window joins a layout saved before it existed, once per device, when a board shows in the Boards
	// activity: under the board list, where the default layout has it.
	placePanelWindow(state) {
		if (window.innerWidth < PHONE || state.open.includes(PANEL_WINDOW)) return;
		const home = (state.groups || []).find((g) => g.tabs.includes(this.ctx.ref));
		if (!home || home.activity !== "ied.board") return;
		try {
			if (localStorage.getItem(PANEL_PLACED_KEY)) return;
			localStorage.setItem(PANEL_PLACED_KEY, "1");
		}
		catch {
			return;
		}
		this.ied.commands.run("shell.showView", { id: PANEL_WINDOW });
		requestAnimationFrame(() => requestAnimationFrame(() => this.focus()));
	}

	// The board's tab was shown: it is the board worked on now, and its chat comes beside it.
	shown() {
		setCurrentBoard(this);
		const state = this.ied.tabs.state();
		this.placePanelWindow(state);
		this.followChat();
	}

	// --- the model's changes ---------------------------------------------------------------------------------------

	changed(change) {
		if (change.kind === "error") {
			this.message.hidden = false;
			replace(this.message, icon("error", "c-error"), " Cannot open the board: " + this.model.error);
			return;
		}
		if (change.kind === "snapshot") {
			this.canvas.invalidateAll();
			if (this.firstShow) {
				this.firstShow = false;
				this.restoreLocal();
			}
		}
		else if (change.kind === "changed" || change.kind === "local") {
			for (const [id, before] of change.before || []) this.canvas.invalidate(before);
			for (const id of change.ids || []) {
				const el = this.model.get(id);
				if (el) {
					this.canvas.invalidate(this.model.boundsOf(el));
					for (const c of this.model.connectorsOf(id)) this.canvas.invalidate(this.model.boundsOf(c));
				}
			}
			if (change.layers) this.canvas.invalidateAll();
			if (change.mapped) for (const [temp, id] of change.mapped) if (this.input.selection.delete(temp)) this.input.selection.add(id);
			for (const id of [...this.input.selection]) if (!this.model.get(id)) this.input.selection.delete(id);
			if (change.kind === "changed" && change.by === "ai") this.canvas.flash((change.ids || []).filter((id) => this.model.get(id)));
		}
		for (const [name, panel] of Object.entries(this.panels)) if (panel.el.isConnected && (change.kind !== "plans" || name === "plan")) panel.render();
		this.ctx.tab.setTitle(this.model.title || this.path.split("/").pop().replace(/\.iedboard$/, ""));
		this.canvas.request();
	}

	selectionChanged() {
		const n = this.input.selection.size;
		this.status(n ? n + " selected" : "");
	}

	async restoreLocal() {
		const local = await this.model.request("ied.board/local", {}).catch(() => null);
		this.local = local || {};
		this.localLoaded();
		if (this.local.camera) {
			this.canvas.camera = this.local.camera;
			this.canvas.request();
		}
		else this.canvas.show(this.canvas.contentBox(), 1);
	}

	saveLocal(fields) {
		Object.assign(this.local, fields);
		this.model.request("ied.board/local", { set: this.local });
	}

	// --- images ----------------------------------------------------------------------------------------------------

	image(src) {
		if (!src) return null;
		let entry = this.images.get(src);
		if (!entry) {
			const img = new Image();
			entry = { img, ready: false };
			img.onload = () => {
				entry.ready = true;
				for (const el of this.model.all()) if (el.type === "image" && el.src === src) this.canvas.invalidate(this.model.boundsOf(el));
			};
			img.src = "/w/" + encodeURIComponent(this.ctx.wsId) + "/files/" + src.split("/").map(encodeURIComponent).join("/");
			this.images.set(src, entry);
		}
		return entry.ready ? entry.img : null;
	}

	pickImage() {
		const input = h("input", { type: "file", accept: "image/*", multiple: true });
		input.addEventListener("change", () => { for (const file of input.files) this.addImage(file); });
		input.click();
	}

	onPaste(e) {
		if (!this.ctx.el.contains(document.activeElement) || this.input.editor) return;
		const items = [...(e.clipboardData ? e.clipboardData.items : [])];
		const file = items.find((i) => i.kind === "file" && i.type.startsWith("image/"));
		if (file) {
			e.preventDefault();
			return this.addImage(file.getAsFile());
		}
		const text = e.clipboardData && e.clipboardData.getData("text/plain");
		if (text) {
			e.preventDefault();
			try {
				const data = JSON.parse(text);
				if (data && data.iedBoard && Array.isArray(data.elements)) return this.input.pasteElements(data.elements);
			}
			catch (_) { /* plain text */ }
			if (this.input.clipboard && text.startsWith("{\"iedBoard\"")) return this.input.pasteElements(this.input.clipboard);
			const layer = this.activeLayer();
			const [cx, cy] = this.canvas.toWorld(this.canvas.width / 2, this.canvas.height / 2);
			const size = this.input.measure({ fontSize: 16 }, text);
			this.model.edit([{ op: "add", type: "text", layer: layer.id, x: Math.round(cx), y: Math.round(cy), w: size.w, h: size.h, text }], "paste");
		}
		else if (this.input.clipboard) {
			e.preventDefault();
			this.input.pasteElements(this.input.clipboard);
		}
	}

	onDrop(e) {
		const files = [...(e.dataTransfer.files || [])].filter((f) => f.type.startsWith("image/"));
		if (!files.length) return;
		e.preventDefault();
		const r = this.stage.getBoundingClientRect();
		const [wx, wy] = this.canvas.toWorld(e.clientX - r.left, e.clientY - r.top);
		files.forEach((f, i) => this.addImage(f, wx + i * 30, wy + i * 30));
	}

	// An image as PNG (what the server draws for the AI), kept beside the board, placed at (x, y) or the view's middle.
	async addImage(file, x, y) {
		if (!file) return;
		this.status("Adding the image...");
		try {
			const bitmap = await createImageBitmap(file);
			const scale = Math.min(1, 4096 / Math.max(bitmap.width, bitmap.height));
			const canvas = document.createElement("canvas");
			canvas.width = Math.round(bitmap.width * scale);
			canvas.height = Math.round(bitmap.height * scale);
			canvas.getContext("2d").drawImage(bitmap, 0, 0, canvas.width, canvas.height);
			const png = await new Promise((resolve) => canvas.toBlob(resolve, "image/png"));
			const reply = await fetch("/w/" + encodeURIComponent(this.ctx.wsId) + "/api/upload", { method: "PUT", body: png, headers: { "Content-Type": "image/png" }, credentials: "same-origin" });
			if (!reply.ok) throw new Error("upload failed (" + reply.status + ")");
			const { blobId } = await reply.json();
			const stored = await this.ied.client.request("ied.board/image", { boardId: this.model.boardId, blobId });
			const [cx, cy] = x !== undefined ? [x, y] : this.canvas.toWorld(this.canvas.width / 2, this.canvas.height / 2);
			const most = 640 / this.canvas.camera.zoom;
			const fit = Math.min(1, most / Math.max(stored.iw, stored.ih));
			const w = Math.round(stored.iw * fit), h2 = Math.round(stored.ih * fit);
			const layer = this.activeLayer();
			const added = await this.model.edit([{ op: "add", type: "image", layer: layer.id, src: stored.src, iw: stored.iw, ih: stored.ih, x: Math.round(cx - w / 2), y: Math.round(cy - h2 / 2), w, h: h2 }], "image");
			this.input.setTool("select");
			const id = added && Object.values(added.refs || {})[0];
			if (id) this.input.select([id], false);
			this.status("");
		}
		catch (e) {
			this.status("");
			this.ied.toast("Cannot add the image: " + (e.message || e), { level: "error" });
		}
	}

	// --- the AI ----------------------------------------------------------------------------------------------------

	// The board's chat - the one it talked to before, else a new one with `create` - as its summary. It is kept in the
	// chat list's Boards section (docs/04 "Chat sections"), out of the way of the owner's other chats; one made before
	// sections existed moves there now.
	async chat(create) {
		await this.localReady;
		if (this.local.chatId) {
			const list = await this.ied.client.request("chat.list", { wsId: this.ctx.wsId }).catch(() => null);
			const summary = list && (list.chats || []).find((c) => c.chatId === this.local.chatId);
			if (summary) {
				if (summary.section !== CHAT_SECTION) this.ied.client.request("chat.set", { chatId: summary.chatId, section: CHAT_SECTION }).catch(() => null);
				return summary;
			}
		}
		if (!create) return null;
		if (!this.creatingChat) this.creatingChat = this.createChat().finally(() => { this.creatingChat = null; });
		return this.creatingChat;
	}

	async createChat() {
		const title = "Board: " + (this.model.title || this.path.split("/").pop().replace(/\.iedboard$/, ""));
		const made = await this.ied.client.request("chat.create", { wsId: this.ctx.wsId, title, section: CHAT_SECTION }).catch((e) => { this.ied.toast("Cannot start a chat: " + e.message, { level: "error" }); return null; });
		if (made) this.saveLocal({ chatId: made.chatId });
		return made;
	}

	// Beside the boards, a narrow group of its own the first time (owner, 2026-11-11: "by default the chat should show
	// up in a smaller window"), tethered to the board: it shows whenever the board does, and closes with it.
	showChat(chatId) {
		const ref = "chat:" + chatId;
		if (window.innerWidth < PHONE || !this.ied.tabs.openBeside) this.ied.tabs.open(ref, { preview: false });
		else this.ied.tabs.openBeside(this.ctx.ref, ref, { tether: true, closes: true, size: CHAT_SHARE });
	}

	// The board's chat follows the board (owner, 2026-11-11: "when i switch the board it will switch to the
	// corresponding chat"): its tab, tethered to the board, shows whenever the board does; a board whose chat is not open
	// brings it - the one it talked to before, else a new one - when another board's chat shows beside the boards.
	async followChat() {
		if (window.innerWidth < PHONE || !this.ied.tabs.openBeside) return;
		await this.localReady;
		if (this.local.chatId && !this.sectionChecked) {
			this.sectionChecked = true;
			this.chat(false);
		}
		if (this.disposed || (this.local.chatId && this.ied.tabs.state().open.includes("chat:" + this.local.chatId))) return;
		if (!this.boardChatBeside(this.ied.tabs.state())) return;
		const summary = await this.chat(true);
		if (!summary || this.disposed) return;
		const state = this.ied.tabs.state();
		if (state.shown.includes(this.ctx.ref) && this.boardChatBeside(state)) this.showChat(summary.chatId);
	}

	// Another board's chat is the tab shown in a group of this board's activity other than this board's.
	boardChatBeside(state) {
		const groups = state.groups || [];
		const home = groups.find((g) => g.tabs.includes(this.ctx.ref));
		if (!home) return false;
		return groups.some((g) => g !== home && g.activity === home.activity && g.active && g.active.startsWith("chat:")
			&& this.ied.tabs.partners(g.active).some((ref) => ref.startsWith("board:") && ref !== this.ctx.ref));
	}

	// The AI button (owner, 2026-11-11: "i should be able to open the chat window without it auto sending a message"):
	// the board's chat beside the board, nothing sent. A chat that has no turns yet gets the board (and the selection)
	// written in its empty message box, for the owner to finish or clear.
	async openChat() {
		const summary = await this.chat(true);
		if (!summary) return;
		this.showChat(summary.chatId);
		const ids = [...this.input.selection].filter((id) => !id.startsWith("t"));
		const text = !summary.turnCount ? "On the planning board " + this.path + (ids.length ? " (selected: " + ids.join(", ") + ")" : "") + ": " : undefined;
		this.ied.commands.run("ied.chat.focusInput", { chatId: summary.chatId, text });
	}
}

export { union, EMPTY, isEmpty };
