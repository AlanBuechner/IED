// composer.js - the chat's input bar (docs/15 "Chat", mockup 02): the messages queued for the next turn, the text box
// (Enter sends, Shift+Enter breaks the line, Esc stops a running turn), Queue / Inject / Stop / Send, the chat's group,
// mode and permission, and the running numbers - tokens in and out, speed, time to first token, context use.

import { h, icon, replace } from "/ui-kit/dom.js";
import { MenuHost } from "/ui-kit/menu.js";
import { MODES, PERMISSIONS, compact, contextBar, duration, tokensPerSecond } from "./chatmodel.js";

const menus = new MenuHost();
const MOST_INPUT_PX = 220;
const PHONE = "(max-width: 719px)";

// actions: {send(text, as), stop(), unqueue(index), set(change), groups() -> Promise<[names]>}
export class Composer {
	constructor(actions) {
		this.actions = actions;
		this.queuedEl = h("div.chat-queued-list");
		this.input = h("textarea.chat-input", { rows: 1, "aria-label": "Message", spellcheck: "true" });
		this.input.addEventListener("keydown", (event) => this.key(event));
		this.input.addEventListener("input", () => this.fit());
		this.queueButton = h("button.btn.desk", { title: "Send after this turn ends", on: { click: () => this.send("queue") } }, "Queue");
		this.injectButton = h("button.btn", { title: "Give it to the running turn at its next step (Ctrl+Enter)", on: { click: () => this.send("inject") } }, "Inject");
		this.stopButton = h("button.btn.desk", { title: "Stop the turn (Esc)", on: { click: () => this.actions.stop() } }, icon("debug-stop"), "Stop", h("span.kbd", null, "Esc"));
		this.sendButton = h("button.btn.primary", { title: "Send (Enter)", "aria-label": "Send", on: { click: () => this.send("message") } }, icon("send"));
		this.box = h("div.chat-composer", null, this.input,
			h("div.chat-row2", null, h("span.grow.desk"), this.queueButton, this.injectButton, this.stopButton, this.sendButton));
		this.input.addEventListener("focus", () => this.box.classList.add("focus"));
		this.input.addEventListener("blur", () => this.box.classList.remove("focus"));
		this.selectors = h("div.chat-selectors");
		this.telemetry = h("div.chat-telemetry");
		this.el = h("div.chat-composer-wrap", null, h("div.chat-col", null, this.queuedEl, this.box, this.selectors, this.telemetry));
	}

	key(event) {
		if (event.key === "Enter" && !event.shiftKey && !event.isComposing) {
			event.preventDefault();
			this.send(event.ctrlKey && this.running ? "inject" : "message");
		}
		else if (event.key === "Escape" && this.running) {
			event.preventDefault();
			this.actions.stop();
		}
	}

	fit() {
		this.input.style.height = "auto";
		this.input.style.height = Math.min(this.input.scrollHeight, MOST_INPUT_PX) + "px";
		this.input.style.overflowY = this.input.scrollHeight > MOST_INPUT_PX ? "auto" : "hidden";
	}

	async send(as) {
		const text = this.input.value.trim();
		if (!text) return;
		this.input.value = "";
		this.fit();
		if (!(await this.actions.send(text, as))) {
			this.input.value = text;
			this.fit();
		}
	}

	focus() {
		this.input.focus();
	}

	update(model) {
		const summary = model.summary || {};
		this.running = model.running();
		this.input.placeholder = window.matchMedia(PHONE).matches ? (this.running ? "Message or steer\u2026" : "Message\u2026")
			: "Message " + (summary.title || "the chat") + (this.running ? " - Enter queues it, Ctrl+Enter steers the running turn" : " - Enter sends, Shift+Enter breaks the line");
		this.queueButton.hidden = !this.running;
		this.injectButton.hidden = !this.running;
		this.stopButton.hidden = !this.running;
		replace(this.queuedEl, ...model.queued.map((text, index) => this.queuedRow(text, index)));
		this.renderSelectors(summary, model.groupInUse);
		this.renderTelemetry(model);
	}

	// A queued message: edit it (back into the box), send it now when no turn runs, or drop it.
	queuedRow(text, index) {
		const take = async () => (await this.actions.unqueue(index)) !== null;
		return h("div.chat-queued", null, icon("list-ordered"), h("span.muted", null, "Queued for the next turn:"), h("span.ellip", { title: text }, text),
			this.running ? null : h("button.ib", { title: "Send it now", "aria-label": "Send the queued message now", on: { click: async () => (await take()) && this.actions.send(text, "message") } }, icon("send")),
			h("button.ib", { title: "Edit", "aria-label": "Edit the queued message", on: { click: async () => {
				if (!(await take())) return;
				this.input.value = text;
				this.fit();
				this.focus();
			} } }, icon("edit")),
			h("button.ib", { title: "Remove", "aria-label": "Remove the queued message", on: { click: take } }, icon("close")));
	}

	renderSelectors(summary, groupInUse) {
		const mode = MODES.find(([id]) => id === summary.mode) || MODES[2];
		const permission = PERMISSIONS.find(([id]) => id === summary.permission) || PERMISSIONS[1];
		replace(this.selectors,
			h("span.muted", null, "Group"), this.dropdown(summary.group || groupInUse || "default", "Model group", () => this.groupRows(summary.group || groupInUse), (row) => this.actions.set({ group: row.value })),
			h("span.muted", null, "Mode"), this.dropdown(mode[1], "Mode", () => rowsOf(MODES, summary.mode), (row) => this.actions.set({ mode: row.value })),
			h("span.muted", null, "Permissions"), this.dropdown(permission[1], "Permissions", () => rowsOf(PERMISSIONS, summary.permission), (row) => this.actions.set({ permission: row.value })));
	}

	dropdown(label, title, rows, pick) {
		return h("button.chat-dd", { title, "aria-haspopup": "menu", on: { click: async (event) => {
			const at = event.currentTarget;
			menus.show({ rows: await rows(), at, pick, label: title });
		} } }, h("span", null, label), icon("chevron-down"));
	}

	async groupRows(current) {
		const names = await this.actions.groups();
		if (!names.length) return [{ kind: "item", label: "No groups: the first model in ai.models", enabled: false }];
		return names.map((name) => ({ kind: "item", label: name, value: name, enabled: true, icon: name === current ? "$(check)" : null }));
	}

	renderTelemetry(model) {
		const turn = model.lastTurn();
		const bar = contextBar(model.context);
		const speed = tokensPerSecond(turn);
		replace(this.telemetry,
			turn ? h("span", null, h("b", null, compact(turn.tokensIn)), " in / ", h("b", null, compact(turn.tokensOut)), " out") : null,
			speed ? h("span", null, h("b", null, speed), " tok/s") : null,
			turn && turn.ttftMs ? h("span", null, "TTFT ", h("b", null, duration(turn.ttftMs))) : null,
			bar.window ? h("span.chat-mchip", { title: "Context used of the model's window" }, "ctx ", h("span.chat-bar", null, h("i", { style: { width: Math.min(bar.percent, 100) + "%" } })), h("b", null, bar.percent + "%"), " of " + compact(bar.window)) : null,
			model.model ? h("span.chat-mchip", null, h("span.dot.info"), model.model) : model.summary && model.summary.chatId ? h("span.chat-mchip.c-warn", null, "no model set up (ai.models)") : null);
	}
}

function rowsOf(options, current) {
	return options.map(([id, label]) => ({ kind: "item", label, value: id, enabled: true, icon: id === current ? "$(check)" : null }));
}
