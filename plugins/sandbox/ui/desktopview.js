// desktopview.js - a sandbox's desktop in the Sandbox activity (sandbox:desktop/<name>, docs/34 "Desktops"): its screen,
// live (frames pulled while the tab shows, as the Browser tab pulls its page's), where the AI's last input landed and
// what it was, the windows on it and the programs launched there. Watching is the default: the AI drives. Take control
// and the mouse, wheel and keys go to the sandbox, and the AI's input waits until you give it back.

import { h, icon, replace } from "/ui-kit/dom.js";
import { promptDialog } from "/ui-kit/dialog.js";
import { aiDriving, desktopKey, duration, runResult, screenPoint } from "./model.js";

const RETRY_MS = 1500;
const WINDOWS_MS = 3000;
const TYPE_BATCH_MS = 120;
const DRAG_PIXELS = 5;

export class DesktopView {
	constructor(ctx, store, actions, name) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.store = store;
		this.actions = actions;
		this.name = name;
		this.alive = true;
		this.shown = true;
		this.seq = 0;
		this.screen = { width: 1280, height: 800 };
		this.windows = [];
		this.typed = "";
		this.typeTimer = null;
		this.down = null;
		this.sending = Promise.resolve();   // the owner's input goes one request at a time, in order
		if (ctx.tab) ctx.tab.setTitle(name + " desktop");
		this.build();
		this.stop = store.on(() => this.render());
		this.render();
		this.pull();
		this.listWindows();
	}

	dispose() {
		this.alive = false;
		this.stop();
		if (this.abort) this.abort.abort();
		clearTimeout(this.windowsTimer);
	}

	visible(shown) {
		this.shown = shown;
		if (shown) {
			this.pull();
			this.listWindows();
		}
	}

	build() {
		this.controlButton = h("button.btn.sbx-tool", { on: { click: () => this.toggleControl() } });
		this.bar = h("div.sbx-bar.sbx-desk-bar", null,
			this.controlButton,
			h("button.btn.flat.sbx-tool", { title: "Start a program on this desktop", on: { click: () => this.launch(false) } }, icon("play"), h("span.sbx-tool-label", null, "Launch...")),
			h("button.btn.flat.sbx-tool", { title: "Start a program under gdb: a crash shows its backtrace", on: { click: () => this.launch(true) } }, icon("debug-alt"), h("span.sbx-tool-label", null, "Debug...")),
			h("button.btn.flat.sbx-tool", { title: "A terminal in this sandbox", on: { click: () => this.actions.terminal(this.name) } }, icon("terminal"), h("span.sbx-tool-label", null, "Terminal")),
			h("span.sbx-spacer"),
			this.size = h("span.muted.sbx-desk-size"));
		this.banner = h("div.sbx-desk-banner", { hidden: true });
		this.picture = h("img.sbx-desk-frame", { alt: "", draggable: "false", hidden: true });
		this.marker = h("div.sbx-desk-marker", { hidden: true });
		this.message = h("div.sbx-desk-message");
		this.stage = h("div.sbx-desk-stage", {
			tabindex: "0", "aria-label": this.name + "'s screen",
			on: {
				mousedown: (e) => this.mouseDown(e), mouseup: (e) => this.mouseUp(e), contextmenu: (e) => e.preventDefault(),
				wheel: (e) => this.wheel(e), keydown: (e) => this.keyDown(e),
			},
		}, this.picture, this.marker, this.message);
		this.windowList = h("div.sbx-desk-list");
		this.appList = h("div.sbx-desk-list");
		this.side = h("div.sbx-desk-side", null, h("div.sbx-section-title", null, "Windows"), this.windowList, h("div.sbx-section-title", null, "Programs"), this.appList);
		this.root = h("div.sbx.sbx-desktop", null, this.bar, this.banner, h("div.sbx-body", null, this.stage, this.side));
		replace(this.ctx.el, this.root);
		new ResizeObserver(() => this.place()).observe(this.stage);
	}

	box() {
		return this.store.sandbox(this.name);
	}

	inControl() {
		const desktop = this.box() && this.box().desktop;
		return !!(desktop && desktop.control === "owner" && desktop.controlClient === this.ied.client.clientId);
	}

	// --- The server -------------------------------------------------------------------------------------------------

	async pull() {
		if (this.pulling) return;
		this.pulling = true;
		this.abort = new AbortController();
		while (this.alive && this.shown) {
			try {
				const frame = await this.ied.client.request("ied.sandbox/frame", { wsId: this.store.wsId, name: this.name, after: this.seq }, { signal: this.abort.signal });
				if (!frame.up) {
					this.showMessage(frame.problem || "The desktop is not running.", true);
					await new Promise((resolve) => setTimeout(resolve, RETRY_MS));
					continue;
				}
				if (frame.width) this.screen = { width: frame.width, height: frame.height };
				if (frame.data && frame.seq > this.seq) this.draw(frame);
				else if (frame.seq < this.seq) this.seq = 0;   // the capture started again
			}
			catch (e) {
				if (e.code === "cancelled" || !this.alive) break;
				this.showMessage("The screen is not showing: " + e.message, false);
				await new Promise((resolve) => setTimeout(resolve, RETRY_MS));
			}
		}
		this.pulling = false;
	}

	draw(frame) {
		this.seq = frame.seq;
		this.picture.src = "data:" + frame.mime + ";base64," + frame.data;
		this.picture.hidden = false;
		this.message.hidden = true;
		this.place();
	}

	showMessage(text, startable) {
		this.picture.hidden = true;
		this.marker.hidden = true;
		this.message.hidden = false;
		const box = this.box();
		replace(this.message, h("div", null, text),
			startable && box && box.desktop ? h("button.btn.primary", { on: { click: () => this.store.request("ied.sandbox/desktop", { name: this.name }) } }, icon("play"), "Start the desktop") : null,
			box && !box.desktop ? h("div.muted", null, "This sandbox has no desktop: its definition needs \"desktop\": true.") : null);
	}

	async listWindows() {
		clearTimeout(this.windowsTimer);
		if (!this.alive || !this.shown) return;
		const box = this.box();
		if (box && box.desktop && box.desktop.up) {
			const answer = await this.ied.client.request("ied.sandbox/windows", { wsId: this.store.wsId, name: this.name }).catch(() => null);
			if (answer) this.windows = answer.windows || [];
			this.renderSide();
		}
		this.windowsTimer = setTimeout(() => this.listWindows(), WINDOWS_MS);
	}

	async toggleControl() {
		await this.store.request("ied.sandbox/desktopControl", { name: this.name, take: !this.inControl() });
		this.stage.focus();
	}

	async launch(debug) {
		const command = await promptDialog({
			title: (debug ? "Debug on " : "Launch on ") + this.name,
			message: debug ? "A program to run under gdb on this desktop: a crash shows the signal and every thread's backtrace (the sandbox needs the debug preset)." : "A program to start on this desktop, as a shell line run in /workspace.",
			value: this.lastCommand || "", placeholder: "build/$IED_SANDBOX/app",
		});
		if (!command || !command.trim()) return;
		this.lastCommand = command;
		await this.store.request("ied.sandbox/launch", { name: this.name, command, debug });
	}

	send(actions) {
		this.sending = this.sending.then(() => this.store.request("ied.sandbox/input", { name: this.name, actions }));
		return this.sending;
	}

	// --- Input (only while in control) ----------------------------------------------------------------------------

	point(event) {
		const rect = this.picture.getBoundingClientRect();
		return screenPoint(event.clientX - rect.left, event.clientY - rect.top, rect.width, rect.height, this.screen.width, this.screen.height);
	}

	inPicture(event) {
		const rect = this.picture.getBoundingClientRect();
		return !this.picture.hidden && event.clientX >= rect.left && event.clientX < rect.right && event.clientY >= rect.top && event.clientY < rect.bottom;
	}

	mouseDown(event) {
		this.stage.focus();
		if (!this.inControl() || !this.inPicture(event)) return;
		event.preventDefault();
		this.down = { at: this.point(event), button: event.button, x: event.clientX, y: event.clientY };
	}

	mouseUp(event) {
		const down = this.down;
		this.down = null;
		if (!down || !this.inControl()) return;
		event.preventDefault();
		const at = this.point(event);
		const moved = Math.abs(event.clientX - down.x) + Math.abs(event.clientY - down.y) > DRAG_PIXELS;
		if (moved && down.button === 0) this.send([{ action: "drag", x: down.at.x, y: down.at.y, to_x: at.x, to_y: at.y }]);
		else this.send([{ action: down.button === 2 ? "right_click" : down.button === 1 ? "middle_click" : "click", x: down.at.x, y: down.at.y }]);
	}

	wheel(event) {
		if (!this.inControl() || !this.inPicture(event)) return;
		event.preventDefault();
		const at = this.point(event);
		const amount = Math.sign(event.deltaY) * Math.max(1, Math.round(Math.abs(event.deltaY) / 100));
		this.send([{ action: "scroll", x: at.x, y: at.y, amount }]);
	}

	keyDown(event) {
		if (!this.inControl()) return;
		const key = desktopKey(event);
		if (!key) return;
		event.preventDefault();
		if (key.text) {
			this.typed += key.text;
			clearTimeout(this.typeTimer);
			this.typeTimer = setTimeout(() => this.flushTyped(), TYPE_BATCH_MS);
			return;
		}
		this.flushTyped();
		this.send([{ action: "key", keys: key.key }]);
	}

	flushTyped() {
		clearTimeout(this.typeTimer);
		if (!this.typed) return;
		const text = this.typed;
		this.typed = "";
		this.send([{ action: "type", text }]);
	}

	// --- Rendering --------------------------------------------------------------------------------------------------

	place() {
		const rect = this.stage.getBoundingClientRect();
		const scale = Math.min(rect.width / this.screen.width, rect.height / this.screen.height, 1.5);
		const width = Math.max(1, Math.floor(this.screen.width * scale));
		const height = Math.max(1, Math.floor(this.screen.height * scale));
		Object.assign(this.picture.style, { width: width + "px", height: height + "px", left: Math.floor((rect.width - width) / 2) + "px", top: Math.floor((rect.height - height) / 2) + "px" });
		this.placeMarker();
	}

	placeMarker() {
		const box = this.box();
		const action = aiDriving(box && box.desktop);
		if (!action || this.picture.hidden || !(action.x >= 0) || !action.x && !action.y) {
			this.marker.hidden = true;
			return;
		}
		const left = parseFloat(this.picture.style.left) || 0;
		const top = parseFloat(this.picture.style.top) || 0;
		const width = parseFloat(this.picture.style.width) || 1;
		const height = parseFloat(this.picture.style.height) || 1;
		this.marker.hidden = false;
		this.marker.style.left = left + (action.x / this.screen.width) * width + "px";
		this.marker.style.top = top + (action.y / this.screen.height) * height + "px";
		this.marker.title = "The AI: " + action.text;
	}

	render() {
		const box = this.box();
		const desktop = box && box.desktop;
		const control = this.inControl();
		const someoneElse = desktop && desktop.control === "owner" && !control;
		replace(this.controlButton, icon(control ? "eye" : "record-keys"), h("span.sbx-tool-label", null, control ? "Give back to the AI" : "Take control"));
		this.controlButton.classList.toggle("primary", !control);
		this.controlButton.title = control ? "Watch again: the AI drives, your input stops" : "Use the mouse and keyboard here: the AI's input waits until you give it back";
		this.root.classList.toggle("in-control", control);
		replace(this.size, desktop ? desktop.width + "x" + desktop.height + (desktop.up ? "" : " - not running") : "no desktop");
		const action = aiDriving(desktop);
		const lines = [];
		if (control) lines.push(h("div.sbx-desk-note.you", null, icon("record-keys"), "You have control: the mouse, wheel and keys go to ", h("strong", null, this.name), ", and the AI's input waits until you give it back."));
		else if (someoneElse) lines.push(h("div.sbx-desk-note", null, icon("account"), "You have control in another window; the AI's input waits."));
		if (action && !control) lines.push(h("div.sbx-desk-note.ai", null, icon("sparkle"), h("strong", null, "The AI is driving: "), action.text));
		this.banner.hidden = !lines.length;
		replace(this.banner, lines);
		this.placeMarker();
		this.renderSide();
	}

	renderSide() {
		replace(this.windowList, this.windows.length ? this.windows.map((w) => h("div.sbx-desk-item" + (w.focused ? ".focused" : ""), { title: "pid " + w.pid + " at (" + w.x + ", " + w.y + ")" },
			icon("window"), h("span.sbx-desk-item-name", null, w.name || "(untitled)"), h("span.muted", null, w.width + "x" + w.height))) : h("div.sbx-empty.muted", null, "No windows."));
		const apps = this.store.runs().filter((run) => run.kind === "app" && run.sandbox === this.name);
		replace(this.appList, apps.length ? apps.map((run) => h("div.sbx-desk-item" + (run.by === "ai" ? ".by-ai" : ""), { title: "Show its output in Activity", on: { click: () => this.actions.showRun(run.id) } },
			run.running ? icon("loading", "spin") : icon(run.exitCode === 0 && !run.stopped ? "pass" : "error"),
			h("span.sbx-desk-item-name", null, run.label),
			h("span.muted", null, run.running ? duration(Date.now() - run.startedMs) : runResult(run)))) : h("div.sbx-empty.muted", null, "Nothing launched yet."));
	}
}
