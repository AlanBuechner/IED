// sidepanel.js - the chat's right panel (mockup 02 "breakdown panel"): the context the last request used, by part,
// against the model's window; the task list the agent keeps (tasks_set / task_update); the files the AI changed in
// this chat with their lines added and removed, each opening in the editor; the tabs it uses that plugins reported (the
// pages it drives), each opening as a tab; its background tasks (docs/04 "Background tasks"), a running one with Stop,
// each unfolding to its output's end and an agent's opening its chat; the AI's clipboard, each clip unfolding to its
// text. Sections fold.

import { h, icon, replace } from "/ui-kit/dom.js";
import { compact, contextBar } from "./chatmodel.js";

const TASK_GLYPHS = { done: "check", skipped: "circle-slash", in_progress: "arrow-right", pending: null };
const RESOURCE_ICONS = { page: "globe", terminal: "terminal", process: "server-process" };

const TASK_STATES = { running: ["loading", "spin", "c-info"], done: ["pass", "", "c-saved"], failed: ["error", "", "c-error"], stopped: ["circle-slash", "", "muted"], lost: ["circle-slash", "", "c-warn"] };

// actions: {openFile(path), fileIcon(path), openTab(ref), openChat(chatId), stopTask(taskId), closeDetails()}
export class SidePanel {
	constructor(actions) {
		this.actions = actions;
		this.closed = new Set();
		this.openClips = new Set();
		this.openTasks = new Set();
		this.title = h("div.panel-title.chat-panel-title");
		this.body = h("div.chat-panel-body");
		this.el = h("aside.chat-panel", { "aria-label": "Chat details" }, this.title, this.body);
	}

	update(model) {
		replace(this.title, h("span.ellip", null, (model.summary && model.summary.title) || "Chat"),
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

	contextSection(model) {
		const bar = contextBar(model.context);
		if (!model.context) return this.section("context", "Context", "", h("div.chat-sec-empty.muted", null, "Shown after the first request."));
		const meta = (bar.estimated ? "~" : "") + (bar.window ? bar.percent + "% of " + compact(bar.window) : compact(bar.used) + " tokens") + (bar.model ? " - " + bar.model : "") + (bar.estimated ? " (estimated)" : "");
		const rows = bar.parts.map((part, i) => h("div.chat-sec-row", null, h("span.sw.s" + (i + 1)), h("span.name", null, part.name), h("span.meta", null, compact(part.tokens))));
		if (bar.window) rows.push(h("div.chat-sec-row", null, h("span.sw.free"), h("span.name.muted", null, bar.free), h("span.meta", null, compact(Math.max(bar.window - bar.used, 0)))));
		const stack = h("div.chat-stack", null, ...bar.parts.map((part, i) => h("i.s" + (i + 1), { style: { width: (part.share * 100).toFixed(2) + "%" } })));
		return this.section("context", "Context", meta, [stack, ...rows]);
	}

	tasksSection(model) {
		const done = model.tasks.filter((t) => t.status === "done" || t.status === "skipped").length;
		const rows = model.tasks.map((task) => h("div.chat-sec-row.task" + (task.status === "in_progress" ? ".now" : ""), { title: task.note || task.title, dataset: { status: task.status } },
			h("span.chat-check" + (task.status === "done" || task.status === "skipped" ? ".on" : ""), null, TASK_GLYPHS[task.status] ? icon(TASK_GLYPHS[task.status]) : null),
			h("span.name" + (task.status === "done" || task.status === "skipped" ? ".muted" : ""), null, task.title),
			task.status === "in_progress" ? h("span.meta", null, "now") : null));
		return this.section("tasks", "Tasks", model.tasks.length ? done + "/" + model.tasks.length : "", rows.length ? rows : h("div.chat-sec-empty.muted", null, "No tasks yet."));
	}

	// What the chat has open (docs/04 "What a chat has open"): shown only when it has something.
	openSection(model) {
		if (!model.resources.length) return null;
		const rows = model.resources.map((resource) => h("button.chat-sec-row.file", { title: "Open " + (resource.title || resource.ref) + " in a tab", dataset: { ref: resource.ref }, on: { click: () => this.actions.openTab(resource.ref) } },
			icon(RESOURCE_ICONS[resource.kind] || "link-external"), h("span.name", null, resource.title || resource.ref)));
		return this.section("open", "Open by this chat", String(model.resources.length), rows);
	}

	filesSection(model) {
		const rows = model.modified.map((file) => h("button.chat-sec-row.file", { title: file.path, on: { click: () => this.actions.openFile(file.path) } },
			this.actions.fileIcon(file.path), h("span.name", null, file.path.split("/").pop()), h("span.dir.muted", null, file.path.includes("/") ? file.path.slice(0, file.path.lastIndexOf("/")) : ""),
			h("span.meta", null, file.plus ? h("span.plus", null, "+" + file.plus) : null, file.minus ? h("span.minus", null, " -" + file.minus) : null)));
		return this.section("files", "Modified files", model.modified.length ? String(model.modified.length) : "", rows.length ? rows : h("div.chat-sec-empty.muted", null, "The AI has not changed a file."));
	}

	// The background tasks the AI started: shown only when it started one; the newest first.
	backgroundSection(model) {
		if (!model.background.length) return null;
		const rows = [];
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
			rows.push(h("div.chat-sec-row.bg-task", { dataset: { task: task.taskId, state: task.state } },
				h("button.chat-bg-open", { title: task.title + (task.summary ? " - " + task.summary : ""), "aria-expanded": open ? "true" : "false", on: { click: toggle } },
					icon(glyph, [spin, tone].filter(Boolean).join(" ")), h("span.name", null, task.title), h("span.meta", null, task.state === "running" ? took : task.summary || task.state)),
				task.chatId ? h("button.ib", { title: "Open the agent's chat", "aria-label": "Open the agent's chat", on: { click: () => this.actions.openChat(task.chatId) } }, icon("comment-discussion")) : null,
				task.state === "running" ? h("button.ib", { title: "Stop " + task.taskId, "aria-label": "Stop " + task.taskId, on: { click: () => this.actions.stopTask(task.taskId) } }, icon("debug-stop")) : null));
			if (open) rows.push(h("pre.chat-clip", null, (task.taskId + " " + task.state + " - " + took + (task.summary ? " - " + task.summary : "")) + (task.output ? "\n" + task.output : "")));
		}
		const running = model.background.filter((t) => t.state === "running").length;
		return this.section("background", "Background tasks", running ? running + " running" : String(model.background.length), rows);
	}

	// The AI's clipboard (clipboard_copy): shown only when it holds something; a clip unfolds to its text.
	clipsSection(model) {
		if (!model.clips.length) return null;
		const rows = [];
		for (const clip of model.clips) {
			const open = this.openClips.has(clip.name);
			const toggle = () => {
				if (open) this.openClips.delete(clip.name);
				else this.openClips.add(clip.name);
				this.toggled();
			};
			rows.push(h("button.chat-sec-row.clip", { title: "{{clip:" + clip.name + "}} from " + clip.source, "aria-expanded": open ? "true" : "false", dataset: { clip: clip.name }, on: { click: toggle } },
				icon(open ? "chevron-down" : "chevron-right"), h("span.name", null, clip.name), h("span.dir.muted", null, clip.source),
				h("span.meta", null, clip.lines + (clip.lines === 1 ? " line" : " lines"))));
			if (open) rows.push(h("pre.chat-clip", null, clip.text + (new TextEncoder().encode(clip.text).length < clip.bytes ? "\n..." : "")));
		}
		return this.section("clips", "Clipboard", String(model.clips.length), rows);
	}
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
