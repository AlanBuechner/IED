// sidepanel.js - the chat's right panel (mockup 02 "breakdown panel"): the context the last request used, by part,
// against the model's window; the task list the agent keeps (tasks_set / task_update); the files the AI changed in
// this chat with their lines added and removed, each opening in the editor. Sections fold.

import { h, icon, replace } from "/ui-kit/dom.js";
import { compact, contextBar } from "./chatmodel.js";

const TASK_GLYPHS = { done: "check", skipped: "circle-slash", in_progress: "arrow-right", pending: null };

// actions: {openFile(path), fileIcon(path)}
export class SidePanel {
	constructor(actions) {
		this.actions = actions;
		this.closed = new Set();
		this.title = h("div.panel-title.chat-panel-title");
		this.body = h("div.chat-panel-body");
		this.el = h("aside.chat-panel", { "aria-label": "Chat details" }, this.title, this.body);
	}

	update(model) {
		replace(this.title, h("span.ellip", null, (model.summary && model.summary.title) || "Chat"));
		replace(this.body, this.contextSection(model), this.tasksSection(model), this.filesSection(model));
	}

	section(id, title, meta, rows) {
		const closed = this.closed.has(id);
		const toggle = () => {
			if (closed) this.closed.delete(id);
			else this.closed.add(id);
			this.el.dispatchEvent(new CustomEvent("chat-panel-toggle"));
		};
		return h("section.chat-sec" + (closed ? ".closed" : ""), { dataset: { section: id } },
			h("button.chat-sec-head", { "aria-expanded": closed ? "false" : "true", on: { click: toggle } }, icon(closed ? "chevron-right" : "chevron-down"), h("span", null, title), h("span.meta", null, meta)),
			closed ? null : rows);
	}

	contextSection(model) {
		const bar = contextBar(model.context);
		if (!model.context) return this.section("context", "Context", "", h("div.chat-sec-empty.muted", null, "Shown after the first request."));
		const meta = bar.window ? bar.percent + "% of " + compact(bar.window) : compact(bar.used) + " tokens";
		const rows = bar.parts.map((part, i) => h("div.chat-sec-row", null, h("span.sw.s" + (i + 1)), h("span.name", null, part.name), h("span.meta", null, compact(part.tokens))));
		if (bar.window) rows.push(h("div.chat-sec-row", null, h("span.sw.free"), h("span.name.muted", null, "Free (compress at 85%)"), h("span.meta", null, compact(Math.max(bar.window - bar.used, 0)))));
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

	filesSection(model) {
		const rows = model.modified.map((file) => h("button.chat-sec-row.file", { title: file.path, on: { click: () => this.actions.openFile(file.path) } },
			this.actions.fileIcon(file.path), h("span.name", null, file.path.split("/").pop()), h("span.dir.muted", null, file.path.includes("/") ? file.path.slice(0, file.path.lastIndexOf("/")) : ""),
			h("span.meta", null, file.plus ? h("span.plus", null, "+" + file.plus) : null, file.minus ? h("span.minus", null, " -" + file.minus) : null)));
		return this.section("files", "Modified files", model.modified.length ? String(model.modified.length) : "", rows.length ? rows : h("div.chat-sec-empty.muted", null, "The AI has not changed a file."));
	}
}
