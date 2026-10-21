// tasks.js - the startup project's tasks (docs/06 "Protocol and state"): the toolbar's startup project, dropdowns and
// task buttons, a status item while a run goes on and after it ends, and the Build / Debug / Test commands. Everything
// comes from the server's tasks:<wsId> topic, so every window shows the same selections and the same runs.

import { h, icon, iconFrom, replace } from "/ui-kit/dom.js";

const TICK_MS = 1000;

// What the toolbar shows of one entry of the offer's toolbar: "|", a variable's key or a task's key.
export function toolbarEntries(offer) {
	if (!offer) return [];
	const variables = new Map(offer.variables.map((v) => [v.key, v]));
	const tasks = new Map(offer.tasks.map((t) => [t.key, t]));
	return offer.toolbar.map((key) => (key === "|" ? { kind: "separator" } : variables.has(key) ? { kind: "variable", variable: variables.get(key) } : tasks.has(key) ? { kind: "task", task: tasks.get(key) } : { kind: "unknown", key }));
}

// "0:12", "1:05:03".
export function elapsedText(ms) {
	const total = Math.max(0, Math.floor(ms / 1000));
	const pad = (n) => String(n).padStart(2, "0");
	const hours = Math.floor(total / 3600);
	return hours ? hours + ":" + pad(Math.floor(total / 60) % 60) + ":" + pad(total % 60) : Math.floor(total / 60) + ":" + pad(total % 60);
}

// The workspace's offer and runs, kept from tasks:<wsId> for every view of this page.
export class TaskState {
	constructor(ied) {
		this.ied = ied;
		this.wsId = null;
		this.offer = null;
		this.runs = new Map();
		this.ended = null;
		this.listeners = new Set();
		this.stop = null;
	}

	follow(wsId) {
		if (wsId === this.wsId) return;
		if (this.stop) this.stop();
		this.wsId = wsId;
		this.offer = null;
		this.runs = new Map();
		this.ended = null;
		this.stop = wsId ? this.ied.client.subscribe("tasks:" + wsId, {
			snap: (data) => {
				this.offer = data.offer;
				this.runs = new Map((data.runs || []).map((r) => [r.runId, r]));
				this.ended = data.ended || null;
				this.changed();
			},
			evt: (kind, data) => this.event(kind, data),
		}) : null;
	}

	event(kind, data) {
		if (kind === "tasks.changed") this.offer = data.offer;
		else if (kind === "run.started") this.runs.set(data.runId, data);
		else if (kind === "run.ended") {
			this.runs.delete(data.runId);
			this.ended = data;
		}
		this.changed();
	}

	on(fn) {
		this.listeners.add(fn);
		fn(this);
		return () => this.listeners.delete(fn);
	}

	// The status bar's color (and the phone's toolbar strip's) follows the runs: building, then failed or idle.
	changed() {
		this.ied.context.set("statusState", this.runs.size ? "building" : this.ended && this.ended.status === "failed" ? "failed" : "idle");
		for (const fn of [...this.listeners]) fn(this);
	}

	activeRun(task) {
		for (const run of this.runs.values()) if (run.task === task && run.project === (this.offer && this.offer.project)) return run;
		return null;
	}

	// Runs a task by key or slot, showing its output; a refusal is a toast saying why.
	async run(what) {
		try {
			const started = await this.ied.client.request("task.run", Object.assign({ wsId: this.wsId }, what));
			await this.ied.commands.run("ied.output.show", { sourceId: started.output });
			return started;
		}
		catch (e) {
			this.ied.toast(e.message, { level: e.code === "not_found" ? "info" : "error" });
			return null;
		}
	}

	async select(key, value) {
		try {
			await this.ied.client.request("task.select", { wsId: this.wsId, key, value });
		}
		catch (e) {
			this.ied.toast(e.message, { level: "error" });
		}
	}

	async setStartup(project) {
		try {
			await this.ied.client.request("task.setStartup", { wsId: this.wsId, project });
		}
		catch (e) {
			this.ied.toast(e.message, { level: "error" });
		}
	}

	async cancelAll() {
		for (const run of [...this.runs.values()]) await this.ied.client.request("task.cancel", { runId: run.runId }).catch(() => {});
	}
}

// The toolbar's part: the startup project when there are several, then the offer's toolbar.
export function TaskToolbar(ctx, state) {
	state.follow(ctx.wsId);
	const render = () => {
		const offer = state.offer;
		if (!offer) return replace(ctx.el);
		const parts = [];
		if (offer.projects.length > 1) {
			parts.push(h("select.input-field.tb-select", { title: "Startup project", "aria-label": "Startup project", on: { change: (event) => state.setStartup(event.target.value) } },
				...offer.projects.map((name) => h("option", { value: name, selected: name === offer.startupProject ? "" : null }, name))));
		}
		for (const entry of toolbarEntries(offer)) parts.push(entryElement(entry, state));
		if (offer.problems.length) parts.push(h("span.tb-problem", { title: offer.problems.join("\n") }, icon("warning", "c-warn")));
		replace(ctx.el, ...parts);
	};
	const stop = state.on(render);
	return { dispose: stop };
}

function entryElement(entry, state) {
	if (entry.kind === "separator") return h("span.tb-sep");
	if (entry.kind === "variable") return variableElement(entry.variable, state);
	if (entry.kind === "task") {
		const task = entry.task;
		const running = state.activeRun(task.key);
		const label = task.label + (running ? " (running)" : "");
		return h("button.ib" + (running ? ".running" : ""), { title: task.tooltip || label, "aria-label": label, on: { click: () => state.run({ task: task.key }) } },
			running ? icon("loading", "spin") : (task.icon && iconFrom(task.icon, "/")) || h("span.tb-label", null, task.label));
	}
	return h("span.tb-label.c-warn", { title: "No variable or task is named " + entry.key }, entry.key);
}

function variableElement(variable, state) {
	const label = variable.label || variable.key;
	if (variable.kind === "toggle") {
		return h("label.tb-toggle", { title: label }, h("input", { type: "checkbox", checked: variable.value === "true" ? "" : null, on: { change: (event) => state.select(variable.key, event.target.checked ? "true" : "false") } }), label);
	}
	if (variable.kind === "text") {
		return h("input.input-field.tb-text", { value: variable.value, title: label, "aria-label": label, on: { change: (event) => state.select(variable.key, event.target.value) } });
	}
	return h("select.input-field.tb-select", { title: label, "aria-label": label, on: { change: (event) => state.select(variable.key, event.target.value) } },
		...variable.values.map((value) => h("option", { value, selected: value === variable.value ? "" : null }, value)));
}

// The status bar's part: the run going on with its time, else how the last one ended.
export function TaskStatus(ctx, state) {
	state.follow(ctx.wsId);
	let timer = null;
	const render = () => {
		const runs = [...state.runs.values()];
		clearTimeout(timer);
		if (runs.length) {
			const run = runs[runs.length - 1];
			replace(ctx.el, icon("loading", "spin"), " " + run.label + " " + run.project + "... " + elapsedText(Date.now() - (run.startedMs || Date.now())));
			timer = setTimeout(render, TICK_MS);
			return;
		}
		const ended = state.ended;
		if (!ended) return replace(ctx.el);
		const word = ended.status === "succeeded" ? "succeeded" : ended.status === "cancelled" ? "cancelled" : "failed";
		replace(ctx.el, icon(word === "succeeded" ? "check" : word === "cancelled" ? "circle-slash" : "error"), " " + ended.label + " " + word);
	};
	const stop = state.on(render);
	return { dispose: () => { stop(); clearTimeout(timer); } };
}
