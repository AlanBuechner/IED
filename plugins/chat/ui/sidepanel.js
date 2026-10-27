// sidepanel.js - the chat's right panel (mockup 02 "breakdown panel"): the context the last request used, by part,
// against the model's window; the task list the agent keeps (tasks_set / task_update); the files the AI changed in
// this chat with their lines added and removed, each opening in the editor; the tabs it uses that plugins reported (the
// pages it drives), each opening as a tab; its background tasks (docs/04 "Background tasks"), a running one with Stop,
// each unfolding to its output's end and an agent's opening its chat; the AI's clipboard, each clip unfolding to its
// text. Sections fold, and a long list shows ai.chats.panelRows rows until Show all: its first, or for the task list
// the ones around what the AI does now, under a row that counts those above.

import { h, icon, replace } from "/ui-kit/dom.js";
import { MenuHost } from "/ui-kit/menu.js";
import { compact, contextBar } from "./chatmodel.js";

const menus = new MenuHost();
const HOLD_MS = 500;         // a finger held this long on a row opens its menu
const HOLD_SLOP_PX = 10;
const HELD_MENU_MS = 800;    // the browser's own contextmenu after a hold is the same menu

const TASK_GLYPHS = { done: "check", skipped: "circle-slash", in_progress: "arrow-right", pending: null };
const RESOURCE_ICONS = { page: "globe", terminal: "terminal", process: "server-process" };

const TASK_STATES = { running: ["loading", "spin", "c-info"], done: ["pass", "", "c-saved"], failed: ["error", "", "c-error"], stopped: ["circle-slash", "", "muted"], lost: ["circle-slash", "", "c-warn"] };

export const PREVIEW_ROWS = 5;

// actions: {openFile(path), fileIcon(path), openTab(ref), openChat(chatId), stopTask(taskId), closeDetails(),
// runCommand(id, args)}
export class SidePanel {
	constructor(actions) {
		this.actions = actions;
		this.heldMs = 0;   // when a held finger last opened a row's menu
		this.closed = new Set();
		this.all = new Set();   // sections showing every row
		this.previewRows = PREVIEW_ROWS;
		this.openClips = new Set();
		this.openTasks = new Set();
		this.title = h("div.panel-title.chat-panel-title");
		this.body = h("div.chat-panel-body");
		this.el = h("aside.chat-panel", { "aria-label": "Chat details" }, this.title, this.body);
	}

	update(model) {
		replace(this.title, h("span.ellip", null, "Details"),
			h("button.ib.chat-phone-only", { title: "Close details", "aria-label": "Close details", on: { click: () => this.actions.closeDetails() } }, icon("close")));
		replace(this.body, this.contextSection(model), this.tasksSection(model), this.backgroundSection(model), this.openSection(model), this.filesSection(model), this.clipsSection(model));
	}

	// The panel drawn again after a fold or unfold.
	toggled() {
		this.el.dispatchEvent(new CustomEvent("chat-panel-toggle"));
	}

	section(id, title, meta, rows) {
		const closed = this.closed.has(id);
		const toggle = () => {
			if (closed) this.closed.delete(id);
			else this.closed.add(id);
			this.toggled();
		};
		return h("section.chat-sec" + (closed ? ".closed" : ""), { dataset: { section: id } },
			h("button.chat-sec-head", { "aria-expanded": closed ? "false" : "true", on: { click: toggle } }, icon(closed ? "chevron-right" : "chevron-down"), h("span", null, title), h("span.meta", null, meta)),
			closed ? null : rows);
	}

	// A list's entries (a row, or a row and what it unfolds to), previewRows of them only - from the one before `keep`,
	// the row the preview keeps in view, with a row above saying what it leaves out there (`above(count)`) - and Show
	// all below; every one once that is pressed.
	limited(id, entries, keep = 0, above = (count) => count + " above") {
		if (entries.length <= this.previewRows) return entries.flat();
		const all = this.all.has(id);
		const toggle = () => {
			if (all) this.all.delete(id);
			else this.all.add(id);
			this.toggled();
		};
		const before = this.previewRows > 1 ? 1 : 0;
		const start = all ? 0 : Math.max(0, Math.min(keep - before, entries.length - this.previewRows));
		const shown = all ? entries : entries.slice(start, start + this.previewRows);
		const earlier = start > 0 ? h("button.chat-sec-more.above", { title: "Show all " + entries.length, on: { click: toggle } }, icon("chevron-up"), above(start)) : null;
		const more = h("button.chat-sec-more", { "aria-expanded": all ? "true" : "false", title: all ? "Show only " + this.previewRows : (entries.length - shown.length) + " more", on: { click: toggle } },
			icon(all ? "chevron-up" : "chevron-down"), all ? "Show fewer" : "Show all " + entries.length);
		return [earlier, ...shown.flat(), more];
	}

	contextSection(model) {
		const bar = contextBar(model.context);
		if (!model.context) return this.section("context", "Context", "", h("div.chat-sec-empty.muted", null, "Shown after the first request."));
		const meta = bar.window ? bar.percent + "% of " + compact(bar.window) : compact(bar.used) + " tokens";
		const rows = bar.parts.map((part, i) => h("div.chat-sec-row", null, h("span.sw.s" + (i + 1)), h("span.name", null, part.name), h("span.meta", null, compact(part.tokens))));
		if (bar.window) rows.push(h("div.chat-sec-row", null, h("span.sw.free"), h("span.name.muted", null, bar.free), h("span.meta", null, compact(Math.max(bar.window - bar.used, 0)))));
		const stack = h("div.chat-stack", null, ...bar.parts.map((part, i) => h("i.s" + (i + 1), { style: { width: (part.share * 100).toFixed(2) + "%" } })));
		return this.section("context", "Context", meta, [stack, ...rows]);
	}

	tasksSection(model) {
		const done = model.tasks.filter((t) => t.status === "done" || t.status === "skipped").length;
		const now = model.tasks.findIndex((t) => t.status === "in_progress");
		const rows = model.tasks.map((task) => h("div.chat-sec-row.task" + (task.status === "in_progress" ? ".now" : ""), { title: task.note || task.title, dataset: { status: task.status } },
			h("span.chat-check" + (task.status === "done" || task.status === "skipped" ? ".on" : ""), null, TASK_GLYPHS[task.status] ? icon(TASK_GLYPHS[task.status]) : null),
			h("span.name" + (task.status === "done" || task.status === "skipped" ? ".muted" : ""), null, task.title),
			task.status === "in_progress" ? h("span.meta", null, "now") : null));
		const next = model.tasks.findIndex((t) => t.status === "pending");
		const keep = now >= 0 ? now : next >= 0 ? next : model.tasks.length - 1;
		return this.section("tasks", "Tasks", model.tasks.length ? done + "/" + model.tasks.length : "", rows.length ? this.limited("tasks", rows, keep, (count) => doneAbove(model.tasks, count)) : h("div.chat-sec-empty.muted", null, "No tasks yet."));
	}

	// What the chat has open (docs/04 "What a chat has open"): shown only when it has something. A click opens a row's tab
	// - a view of it; closing that tab leaves it to the AI - and its menu (right click, or press and hold on a phone) has
	// Open and Force close, which closes it for good through the command its plugin named.
	openSection(model) {
		if (!model.resources.length) return null;
		const rows = model.resources.map((resource) => {
			const name = resource.title || resource.ref;
			const menu = (at) => this.resourceMenu(at, resource);
			const row = h("button.chat-sec-row.file", {
				title: "Open " + name + " in a tab" + (resource.close ? " (right-click to force close it)" : ""), dataset: { ref: resource.ref },
				on: { click: () => this.actions.openTab(resource.ref), contextmenu: (event) => {
					event.preventDefault();
					if (Date.now() - this.heldMs > HELD_MENU_MS) menu(event);
				} },
			}, icon(RESOURCE_ICONS[resource.kind] || "link-external"), h("span.name", null, name));
			holdForMenu(row, (at) => {
				this.heldMs = Date.now();
				menu(at);
			});
			return row;
		});
		return this.section("open", "Open by this chat", String(model.resources.length), this.limited("open", rows));
	}

	resourceMenu(at, resource) {
		menus.show({ at, label: resource.title || resource.ref, rows: [
			{ kind: "item", label: "Open", id: "open", enabled: true },
			{ kind: "separator" },
			{ kind: "item", label: "Force close", id: "close", enabled: Boolean(resource.close) },
		], pick: (row) => {
			if (row.id === "open") this.actions.openTab(resource.ref);
			else if (row.id === "close") this.actions.runCommand(resource.close, { ref: resource.ref });
		} });
	}

	filesSection(model) {
		const rows = model.modified.map((file) => h("button.chat-sec-row.file", { title: file.path, on: { click: () => this.actions.openFile(file.path) } },
			this.actions.fileIcon(file.path), h("span.name", null, file.path.split("/").pop()), h("span.dir.muted", null, file.path.includes("/") ? file.path.slice(0, file.path.lastIndexOf("/")) : ""),
			h("span.meta", null, file.plus ? h("span.plus", null, "+" + file.plus) : null, file.minus ? h("span.minus", null, " -" + file.minus) : null)));
		return this.section("files", "Modified files", model.modified.length ? String(model.modified.length) : "", rows.length ? this.limited("files", rows) : h("div.chat-sec-empty.muted", null, "The AI has not changed a file."));
	}

	// The background tasks the AI started: shown only when it started one; the newest first.
	backgroundSection(model) {
		if (!model.background.length) return null;
		const entries = [];
		const now = Date.now();
		for (const task of [...model.background].reverse()) {
			const open = this.openTasks.has(task.taskId);
			const toggle = () => {
				if (open) this.openTasks.delete(task.taskId);
				else this.openTasks.add(task.taskId);
				this.toggled();
			};
			const [glyph, spin, tone] = TASK_STATES[task.state] || TASK_STATES.lost;
			const took = task.state === "running" ? "since " + clockOf(task.startedMs) : taskTime((task.endedMs || now) - task.startedMs);
			const row = h("div.chat-sec-row.bg-task", { dataset: { task: task.taskId, state: task.state } },
				h("button.chat-bg-open", { title: task.title + (task.summary ? " - " + task.summary : ""), "aria-expanded": open ? "true" : "false", on: { click: toggle } },
					icon(glyph, [spin, tone].filter(Boolean).join(" ")), h("span.name", null, task.title), h("span.meta", null, task.state === "running" ? took : task.summary || task.state)),
				task.chatId ? h("button.ib", { title: "Open the agent's chat", "aria-label": "Open the agent's chat", on: { click: () => this.actions.openChat(task.chatId) } }, icon("comment-discussion")) : null,
				task.state === "running" ? h("button.ib", { title: "Stop " + task.taskId, "aria-label": "Stop " + task.taskId, on: { click: () => this.actions.stopTask(task.taskId) } }, icon("debug-stop")) : null);
			entries.push(open ? [row, h("pre.chat-clip", null, (task.taskId + " " + task.state + " - " + took + (task.summary ? " - " + task.summary : "")) + (task.output ? "\n" + task.output : ""))] : row);
		}
		const running = model.background.filter((t) => t.state === "running").length;
		return this.section("background", "Background tasks", running ? running + " running" : String(model.background.length), this.limited("background", entries));
	}

	// The AI's clipboard (clipboard_copy): shown only when it holds something; a clip unfolds to its text.
	clipsSection(model) {
		if (!model.clips.length) return null;
		const entries = [];
		for (const clip of model.clips) {
			const open = this.openClips.has(clip.name);
			const toggle = () => {
				if (open) this.openClips.delete(clip.name);
				else this.openClips.add(clip.name);
				this.toggled();
			};
			const row = h("button.chat-sec-row.clip", { title: "{{clip:" + clip.name + "}} from " + clip.source, "aria-expanded": open ? "true" : "false", dataset: { clip: clip.name }, on: { click: toggle } },
				icon(open ? "chevron-down" : "chevron-right"), h("span.name", null, clip.name), h("span.dir.muted", null, clip.source),
				h("span.meta", null, clip.lines + (clip.lines === 1 ? " line" : " lines")));
			entries.push(open ? [row, h("pre.chat-clip", null, clip.text + (new TextEncoder().encode(clip.text).length < clip.bytes ? "\n..." : ""))] : row);
		}
		return this.section("clips", "Clipboard", String(model.clips.length), this.limited("clips", entries));
	}
}

// `open({x, y})` when a finger (or pen) is held on `el` without moving: a phone's right click. The click that ends the
// hold is swallowed.
function holdForMenu(el, open) {
	let timer = 0;
	let start = null;
	let swallow = false;
	const cancel = () => {
		clearTimeout(timer);
		timer = 0;
	};
	el.addEventListener("pointerdown", (event) => {
		if (event.pointerType === "mouse") return;
		start = { x: event.clientX, y: event.clientY };
		cancel();
		timer = setTimeout(() => {
			timer = 0;
			swallow = true;
			open(start);
		}, HOLD_MS);
	});
	el.addEventListener("pointermove", (event) => {
		if (timer && start && Math.hypot(event.clientX - start.x, event.clientY - start.y) > HOLD_SLOP_PX) cancel();
	});
	el.addEventListener("pointerup", cancel);
	el.addEventListener("pointercancel", cancel);
	el.addEventListener("click", (event) => {
		if (!swallow) return;
		swallow = false;
		event.preventDefault();
		event.stopImmediatePropagation();
	}, true);
}

// What the task preview leaves out above it: "4 done above" when they are all done or skipped.
function doneAbove(tasks, count) {
	return count + (tasks.slice(0, count).every((t) => t.status === "done" || t.status === "skipped") ? " done above" : " above");
}

// "42 s", "3 min": how long a task ran.
export function taskTime(ms) {
	const seconds = Math.max(0, Math.round(ms / 1000));
	if (seconds < 120) return seconds + " s";
	if (seconds < 7200) return Math.round(seconds / 60) + " min";
	return Math.round(seconds / 3600) + " h";
}

function clockOf(ms) {
	const at = new Date(ms);
	return String(at.getHours()).padStart(2, "0") + ":" + String(at.getMinutes()).padStart(2, "0");
}
