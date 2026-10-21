// browserview.js - a Browser tab (docs/27 "The Browser tab"): a page that lives on the server, drawn from the frames this
// view pulls (browser.frame) while it is visible, with the person's mouse, wheel and keys sent back in page pixels; the
// toolbar (back, forward, reload or stop, the address, the viewport, the system browser, the console), the page's
// console and failed requests from page:<pageId>, and the AI's actions drawn over the page as they happen.

import { h, icon, replace } from "/ui-kit/dom.js";

export const VIEWPORTS = [
	{ id: "fit", label: "Fit to the pane" },
	{ id: "page", label: "The page's own size" },
	{ id: "1920x1080", label: "Desktop 1920 x 1080", width: 1920, height: 1080, scale: 1 },
	{ id: "1366x768", label: "Laptop 1366 x 768", width: 1366, height: 768, scale: 1 },
	{ id: "820x1180", label: "Tablet 820 x 1180", width: 820, height: 1180, scale: 2 },
	{ id: "390x844", label: "Phone 390 x 844", width: 390, height: 844, scale: 3 },
];

const RESIZE_PAUSE_MS = 200;
const RETRY_MS = 1000;
const ACTION_SHOWN_MS = 2500;
const CONSOLE_ROWS = 500;
const SPECIAL_KEYS = new Set(["Enter", "Tab", "Escape", "Backspace", "Delete", "ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "Home", "End", "PageUp", "PageDown", "Insert",
	"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"]);

// The CDP modifier bits: Alt 1, Ctrl 2, Meta 4, Shift 8.
export function modifiersOf(event) {
	return (event.altKey ? 1 : 0) | (event.ctrlKey ? 2 : 0) | (event.metaKey ? 4 : 0) | (event.shiftKey ? 8 : 0);
}

// Where an image of `width` x `height` sits in a box of `boxWidth` x `boxHeight`: as large as fits, centered.
export function fitted(width, height, boxWidth, boxHeight) {
	if (!width || !height || !boxWidth || !boxHeight) return { left: 0, top: 0, width: boxWidth, height: boxHeight };
	const scale = Math.min(boxWidth / width, boxHeight / height);
	return { left: Math.round((boxWidth - width * scale) / 2), top: Math.round((boxHeight - height * scale) / 2), width: width * scale, height: height * scale };
}

// A point of the view (relative to the stage) in page CSS pixels; null outside the page's picture.
export function toPage(x, y, shown, page) {
	if (x < shown.left || y < shown.top || x > shown.left + shown.width || y > shown.top + shown.height) return null;
	return { x: Math.round((x - shown.left) / shown.width * page.width), y: Math.round((y - shown.top) / shown.height * page.height) };
}

// What a person typed in the address box, as the server opens it: a scheme added to a bare host.
export function typedUrl(text) {
	const url = text.trim();
	if (!url) return "about:blank";
	if (/^[a-z][a-z0-9+.-]*:/i.test(url) && !/^[a-z0-9.-]+:\d/i.test(url)) return url;
	return (/^(localhost|127\.|\[::1\]|\d)/i.test(url) ? "http://" : "https://") + url;
}

function hostOf(url) {
	try {
		return new URL(url).host || url;
	}
	catch (e) {
		return url;
	}
}

export class BrowserView {
	constructor(ctx, pageId) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.pageId = pageId;
		this.page = null;
		this.seq = 0;
		this.alive = true;
		this.shown = false;
		this.pulling = false;
		this.mode = null;
		this.frameSize = null;
		this.moving = null;
		this.movePending = null;
		this.consoleSeen = 0;
		this.errors = 0;
		this.build();
		this.stopTopic = this.ied.client.subscribe("page:" + pageId, { snap: (data) => this.restore(data), evt: (kind, data) => this.event(kind, data) });
		this.resizer = new ResizeObserver(() => this.stageResized());
		this.resizer.observe(this.stage);
	}

	// --- Building ---------------------------------------------------------------------------------------------------

	build() {
		const button = (name, title, run) => h("button.ib", { title, "aria-label": title, on: { click: run } }, icon(name));
		this.backButton = button("arrow-left", "Back (Alt+Left)", () => this.call("browser.back"));
		this.forwardButton = button("arrow-right", "Forward (Alt+Right)", () => this.call("browser.forward"));
		this.reloadButton = button("refresh", "Reload (F5)", () => this.call(this.page && this.page.loading ? "browser.stop" : "browser.reload"));
		this.address = h("input.browser-address", { type: "text", spellcheck: "false", autocomplete: "off", "aria-label": "Address", placeholder: "Type a URL: localhost:5173, example.com, file:///..." });
		this.address.addEventListener("keydown", (event) => this.addressKey(event));
		this.address.addEventListener("focus", () => this.address.select());
		this.viewport = h("select.browser-viewport", { title: "Viewport", "aria-label": "Viewport" }, ...VIEWPORTS.map((v) => h("option", { value: v.id }, v.label)));
		this.viewport.addEventListener("change", () => this.chooseViewport(this.viewport.value));
		this.aiBadge = h("span.browser-ai", { hidden: true, title: "The AI opened this page" }, "AI");
		this.consoleButton = button("debug-console", "Console", () => this.toggleConsole());
		this.errorBadge = h("span.browser-errors", { hidden: true });
		this.consoleButton.append(this.errorBadge);
		this.toolbar = h("div.browser-toolbar", null, this.backButton, this.forwardButton, this.reloadButton, this.address, this.aiBadge, this.viewport,
			button("link-external", "Open in the system browser", () => this.openExternal()), this.consoleButton);
		this.caption = h("div.browser-caption", { hidden: true });
		this.frame = h("img.browser-frame", { alt: "", draggable: "false" });
		this.ring = h("div.browser-ring", { hidden: true });
		this.box = h("div.browser-box", { hidden: true });
		this.message = h("div.browser-message.muted", null, "Loading the page...");
		this.keys = h("textarea.browser-keys", { "aria-label": "Keyboard input to the page", autocomplete: "off", autocapitalize: "off", spellcheck: "false" });
		this.stage = h("div.browser-stage", { tabindex: "-1" }, this.frame, this.box, this.ring, this.message, this.keys);
		this.consoleList = h("div.browser-console-list", { role: "log", "aria-label": "Console" });
		this.consolePanel = h("div.browser-console", { hidden: true }, h("div.browser-console-head", null, h("span.grow", null, "Console"),
			h("button.ib", { title: "Clear", "aria-label": "Clear the console", on: { click: () => this.clearConsole() } }, icon("clear-all"))), this.consoleList);
		replace(this.ctx.el, h("div.browser-view", null, this.toolbar, this.caption, this.stage, this.consolePanel));
		this.listen();
	}

	listen() {
		this.stage.addEventListener("pointerdown", (event) => this.pointer(event, "down"));
		this.stage.addEventListener("pointerup", (event) => this.pointer(event, "up"));
		this.stage.addEventListener("pointermove", (event) => this.pointer(event, "move"));
		this.stage.addEventListener("wheel", (event) => this.wheel(event), { passive: false });
		this.stage.addEventListener("contextmenu", (event) => event.preventDefault());
		this.keys.addEventListener("keydown", (event) => this.key(event, "down"));
		this.keys.addEventListener("keyup", (event) => this.key(event, "up"));
		this.keys.addEventListener("input", (event) => this.typed(event));
		this.keys.addEventListener("compositionend", (event) => this.composed(event));
		this.keys.addEventListener("paste", (event) => this.pasted(event));
	}

	// --- The page's state -------------------------------------------------------------------------------------------

	restore(data) {
		this.replaceConsole(data.console || [], data.network || []);
		this.changed(data.page);
		if (data.lastAction) this.showAction(data.lastAction, false);
		if (!this.mode) this.chooseViewport(data.page.opener === "ai" ? "page" : "fit");
	}

	event(kind, data) {
		if (kind === "changed") this.changed(data);
		else if (kind === "console") this.addConsole(data.entries.map((e) => this.consoleRow(e)));
		else if (kind === "network") this.addConsole(data.entries.map((e) => this.networkRow(e)));
		else if (kind === "action") this.showAction(data, true);
		else if (kind === "closed") this.closed();
	}

	changed(page) {
		this.page = page;
		this.backButton.disabled = !page.canGoBack;
		this.forwardButton.disabled = !page.canGoForward;
		replace(this.reloadButton, icon(page.loading ? "close" : "refresh"));
		this.reloadButton.title = page.loading ? "Stop" : "Reload (F5)";
		if (document.activeElement !== this.address) this.address.value = page.url === "about:blank" ? "" : page.url;
		this.aiBadge.hidden = page.opener !== "ai";
		this.ctx.tab.setTitle(page.title || (page.url && page.url !== "about:blank" ? hostOf(page.url) : "Browser"));
		this.place();
	}

	closed() {
		this.alive = false;
		this.message.textContent = "This page was closed.";
		this.message.hidden = false;
		this.frame.hidden = true;
	}

	// --- Frames -----------------------------------------------------------------------------------------------------

	visible(shown) {
		this.shown = shown;
		if (shown) this.pull();
	}

	async pull() {
		if (this.pulling) return;
		this.pulling = true;
		this.abort = new AbortController();
		while (this.alive && this.shown) {
			try {
				const frame = await this.ied.client.request("browser.frame", { pageId: this.pageId, after: this.seq }, { signal: this.abort.signal });
				if (frame.data && frame.seq > this.seq) this.draw(frame);
			}
			catch (e) {
				if (e.code === "not_found") this.closed();
				else if (e.code !== "cancelled") {
					this.message.textContent = "The page is not showing: " + e.message;
					this.message.hidden = false;
					await new Promise((resolve) => setTimeout(resolve, RETRY_MS));
				}
			}
		}
		this.pulling = false;
	}

	draw(frame) {
		this.seq = frame.seq;
		this.frameSize = { width: frame.width, height: frame.height };
		this.frame.src = "data:" + frame.mime + ";base64," + frame.data;
		this.frame.hidden = false;
		this.message.hidden = true;
		this.place();
	}

	// The picture's place in the stage: the shown frame's CSS size (the page's until one came) fitted into the stage.
	place() {
		if (!this.page) return;
		const rect = this.stage.getBoundingClientRect();
		const size = this.shownSize();
		this.picture = fitted(size.width, size.height, rect.width, rect.height);
		Object.assign(this.frame.style, { left: this.picture.left + "px", top: this.picture.top + "px", width: this.picture.width + "px", height: this.picture.height + "px" });
		this.placeAction();
	}

	stageResized() {
		this.place();
		if (this.mode !== "fit") return;
		clearTimeout(this.resizeTimer);
		this.resizeTimer = setTimeout(() => this.fitToStage(), RESIZE_PAUSE_MS);
	}

	fitToStage() {
		const rect = this.stage.getBoundingClientRect();
		const width = Math.round(rect.width);
		const height = Math.round(rect.height);
		if (width < 200 || height < 150 || !this.page) return;
		const scale = Math.round((window.devicePixelRatio || 1) * 100) / 100;
		if (this.page.width === width && this.page.height === height && this.page.scale === scale) return;
		this.call("browser.resize", { width, height, scale });
	}

	chooseViewport(id) {
		this.mode = id;
		this.viewport.value = id;
		const preset = VIEWPORTS.find((v) => v.id === id);
		if (id === "fit") this.fitToStage();
		else if (preset && preset.width) this.call("browser.resize", { width: preset.width, height: preset.height, scale: preset.scale });
	}

	// --- Input ------------------------------------------------------------------------------------------------------

	// The CSS size of what the picture shows: a frame may still be of the page's size before a resize.
	shownSize() {
		return this.frameSize && this.frameSize.width && this.frameSize.height ? this.frameSize : this.page;
	}

	at(event) {
		const rect = this.stage.getBoundingClientRect();
		return this.picture && this.page ? toPage(event.clientX - rect.left, event.clientY - rect.top, this.picture, this.shownSize()) : null;
	}

	pointer(event, action) {
		const point = this.at(event);
		if (action === "down") {
			this.keys.focus({ preventScroll: true });
			this.stage.setPointerCapture(event.pointerId);
		}
		if (!point) return;
		event.preventDefault();
		const button = ["left", "middle", "right"][event.button] || "none";
		const mouse = { type: "mouse", action, x: point.x, y: point.y, button: action === "move" ? (event.buttons & 1 ? "left" : "none") : button,
			buttons: event.buttons, clickCount: action === "move" ? 0 : Math.max(1, event.detail || 1), modifiers: modifiersOf(event) };
		if (action === "move") this.move(mouse);
		else this.send([mouse]);
	}

	// One move in flight at a time; the newest waiting one goes next.
	move(mouse) {
		if (this.moving) {
			this.movePending = mouse;
			return;
		}
		this.moving = this.send([mouse]).finally(() => {
			this.moving = null;
			const next = this.movePending;
			this.movePending = null;
			if (next) this.move(next);
		});
	}

	wheel(event) {
		const point = this.at(event);
		if (!point) return;
		event.preventDefault();
		const unit = event.deltaMode === 1 ? 40 : event.deltaMode === 2 ? this.page.height : 1;
		this.send([{ type: "mouse", action: "wheel", x: point.x, y: point.y, deltaX: event.deltaX * unit, deltaY: event.deltaY * unit, modifiers: modifiersOf(event) }]);
	}

	key(event, action) {
		if (event.isComposing) return;
		const special = SPECIAL_KEYS.has(event.key) || event.ctrlKey || event.altKey || event.metaKey;
		if (!special) return;
		event.preventDefault();
		event.stopPropagation();
		const text = action === "down" && event.key === "Enter" ? "\r" : "";
		this.send([{ type: "key", action, key: event.key, code: event.code, keyCode: event.keyCode, text, modifiers: modifiersOf(event) }]);
	}

	typed(event) {
		if (event.isComposing) return;
		const text = this.keys.value;
		this.keys.value = "";
		if (text) this.send([{ type: "text", text }]);
	}

	composed(event) {
		this.keys.value = "";
		if (event.data) this.send([{ type: "text", text: event.data }]);
	}

	pasted(event) {
		event.preventDefault();
		const text = event.clipboardData ? event.clipboardData.getData("text/plain") : "";
		if (text) this.send([{ type: "text", text }]);
	}

	send(events) {
		return this.ied.client.request("browser.input", { pageId: this.pageId, events }).catch(() => {});
	}

	// --- The toolbar ------------------------------------------------------------------------------------------------

	call(method, params) {
		return this.ied.client.request(method, Object.assign({ pageId: this.pageId }, params || {}))
			.catch((e) => this.ied.toast("The browser: " + e.message, { level: "error" }));
	}

	addressKey(event) {
		if (event.key === "Enter") {
			event.preventDefault();
			this.call("browser.navigate", { url: typedUrl(this.address.value) });
			this.keys.focus({ preventScroll: true });
		}
		else if (event.key === "Escape") {
			this.address.value = this.page ? this.page.url : "";
			this.keys.focus({ preventScroll: true });
		}
	}

	openExternal() {
		const url = this.page && this.page.url;
		if (!url || !/^https?:/i.test(url)) return;
		if (window.iedDesktop) window.iedDesktop.openExternal(url);
		else window.open(url, "_blank", "noopener,noreferrer");
	}

	// --- The AI's actions -------------------------------------------------------------------------------------------

	showAction(action, fresh) {
		if (action.by !== "ai") return;
		this.caption.textContent = "AI: " + action.label;
		this.caption.hidden = false;
		clearTimeout(this.captionTimer);
		this.captionTimer = setTimeout(() => (this.caption.hidden = true), ACTION_SHOWN_MS * 2);
		if (!fresh) return;
		this.action = action;
		this.box.hidden = !action.box;
		this.ring.hidden = action.x === undefined;
		this.placeAction();
		this.ring.classList.remove("pulse");
		void this.ring.offsetWidth;
		this.ring.classList.add("pulse");
		clearTimeout(this.actionTimer);
		this.actionTimer = setTimeout(() => {
			this.action = null;
			this.box.hidden = true;
			this.ring.hidden = true;
		}, ACTION_SHOWN_MS);
	}

	// The shown action's box and ring over the picture as it is placed now (it moves when the pane resizes).
	placeAction() {
		const action = this.action;
		if (!action || !this.picture || !this.page) return;
		const size = this.shownSize();
		const scaleX = this.picture.width / size.width;
		const scaleY = this.picture.height / size.height;
		if (action.box)
			Object.assign(this.box.style, { left: this.picture.left + action.box.x * scaleX + "px", top: this.picture.top + action.box.y * scaleY + "px",
				width: action.box.width * scaleX + "px", height: action.box.height * scaleY + "px" });
		if (action.x !== undefined) Object.assign(this.ring.style, { left: this.picture.left + action.x * scaleX + "px", top: this.picture.top + action.y * scaleY + "px" });
	}

	// --- The console ------------------------------------------------------------------------------------------------

	consoleRow(entry) {
		const where = entry.url ? hostOf(entry.url) + (entry.line ? ":" + entry.line : "") : "";
		if (entry.level === "error") this.errors += 1;
		return { seq: "c" + entry.seq, row: h("div.browser-entry." + entry.level, null, h("span.browser-entry-text", null, entry.text), where ? h("span.browser-entry-where.muted", { title: entry.url }, where) : null) };
	}

	networkRow(entry) {
		this.errors += 1;
		return { seq: "n" + entry.seq, row: h("div.browser-entry.error", null, h("span.browser-entry-text", null, (entry.method || "GET") + " " + entry.url),
			h("span.browser-entry-where", null, (entry.status ? entry.status + " " : "") + entry.error)) };
	}

	replaceConsole(entries, failed) {
		this.errors = 0;
		this.consoleList.replaceChildren();
		this.addConsole(entries.map((e) => this.consoleRow(e)).concat(failed.map((e) => this.networkRow(e))));
	}

	addConsole(rows) {
		for (const { row } of rows) this.consoleList.append(row);
		while (this.consoleList.childElementCount > CONSOLE_ROWS) this.consoleList.firstElementChild.remove();
		this.consoleList.scrollTop = this.consoleList.scrollHeight;
		this.errorBadge.textContent = this.errors > 99 ? "99+" : String(this.errors);
		this.errorBadge.hidden = this.errors === 0;
	}

	clearConsole() {
		this.consoleList.replaceChildren();
		this.errors = 0;
		this.errorBadge.hidden = true;
	}

	toggleConsole() {
		this.consolePanel.hidden = !this.consolePanel.hidden;
		this.consoleButton.classList.toggle("on", !this.consolePanel.hidden);
	}

	focus() {
		if (this.page && this.page.url === "about:blank") this.address.focus();
		else this.keys.focus({ preventScroll: true });
	}

	dispose() {
		this.alive = false;
		if (this.abort) this.abort.abort();
		if (this.stopTopic) this.stopTopic();
		this.resizer.disconnect();
		clearTimeout(this.resizeTimer);
		clearTimeout(this.captionTimer);
		clearTimeout(this.actionTimer);
	}
}
