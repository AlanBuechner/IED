// groupsettings.js - Settings > AI > Model Groups (docs/04 "A group is a topology", mockup 04): the groups of ai.groups
// as a bar (the default one starred), and the chosen group's topology. Its orchestrator - the chat's own model - has a
// role per mode, each a model, a temperature, an effort and fallbacks in order: a new group turns on primary alone; a
// role with nothing set is off, grayed out, and uses the primary's model (Ask primary, Plan planning, Agent
// implementation). Its sub-agents each have a name, a role, the mode their chat runs in, what they are for (the
// orchestrator is told) and the agents they may run themselves. A Claude model in a chain - a role's or an agent's, or a
// fallback - has a pace (docs/04 "Pacing"): always, or while the plan's pace is at most a limit; fallbacks move earlier
// and go.

import { h, icon, replace } from "/ui-kit/dom.js";
import { confirmDialog } from "/ui-kit/dialog.js";
import { PACE_LIMITS, PACE_TARGETS, fallbackPlaces, paceFromValue, paceLabel, paceValue, storedFallback } from "./pacing.js";

// The orchestrator's roles: [role, what it is for].
export const ROLES = [
	["primary", "Ask mode, and modes left off"],
	["planning", "Plan mode"],
	["implementation", "Agent mode"],
];
const EFFORTS = ["", "low", "medium", "high", "xhigh", "max"];
const AGENT_MODES = ["agent", "ask", "plan"];

// The model a role gets when it is turned on: the group's primary's, else the first one.
export function roleDefault(group, models) {
	const primary = group.primary && group.primary.model;
	return primary && models.includes(primary) ? primary : models[0] || "";
}

// A name for a new group that `groups` does not have yet.
export function freshName(groups, base) {
	if (!groups[base]) return base;
	let n = 2;
	while (groups[base + " " + n]) n += 1;
	return base + " " + n;
}

// `places` with the one at `index` moved one earlier.
export function movedEarlier(places, index) {
	if (index <= 0 || index >= places.length) return places.slice();
	const next = places.slice();
	[next[index - 1], next[index]] = [next[index], next[index - 1]];
	return next;
}

// `group` with `role` changed by `change` ({model, pace, temperature, effort, fallback}); a role without a model is removed.
export function withRole(group, role, change) {
	const next = Object.assign({}, group);
	const entry = Object.assign({}, next[role] || {}, change);
	for (const key of Object.keys(entry)) if (entry[key] === undefined || entry[key] === "" || (Array.isArray(entry[key]) && !entry[key].length)) delete entry[key];
	if (!entry.model) delete next[role];
	else next[role] = entry;
	return next;
}

// `group` with sub-agent `name` changed by `change` ({model, mode, description, calls, temperature, effort, fallback});
// empty fields are dropped.
export function withAgent(group, name, change) {
	const agents = Object.assign({}, group.agents || {});
	const entry = Object.assign({}, agents[name] || {}, change);
	for (const key of Object.keys(entry)) if (entry[key] === undefined || entry[key] === "" || (Array.isArray(entry[key]) && !entry[key].length)) delete entry[key];
	agents[name] = entry;
	return Object.assign({}, group, { agents });
}

// `group` without sub-agent `name`, which no other agent calls any more.
export function withoutAgent(group, name) {
	const agents = {};
	for (const [other, entry] of Object.entries(group.agents || {})) {
		if (other === name) continue;
		agents[other] = entry.calls ? Object.assign({}, entry, { calls: entry.calls.filter((c) => c !== name) }) : entry;
		if (agents[other].calls && !agents[other].calls.length) delete agents[other].calls;
	}
	const next = Object.assign({}, group, { agents });
	if (!Object.keys(agents).length) delete next.agents;
	return next;
}

// `group` with sub-agent `from` named `to`, in its place and in the agents that call it; unchanged when `to` is taken.
export function withAgentRenamed(group, from, to) {
	if (!to || from === to || (group.agents || {})[to]) return group;
	const agents = {};
	for (const [name, entry] of Object.entries(group.agents || {})) {
		const calls = entry.calls ? entry.calls.map((c) => (c === from ? to : c)) : undefined;
		agents[name === from ? to : name] = calls ? Object.assign({}, entry, { calls }) : entry;
	}
	return Object.assign({}, group, { agents });
}

export class ModelGroupsPage {
	constructor(ctx) {
		this.ctx = ctx;
		this.settings = ctx.settings;
		this.selected = null;
		this.renaming = false;
		this.bar = h("div.set-bar.set-groups");
		this.table = h("div.set-roles");
		replace(ctx.el,
			h("div.set-crumbs", null, "AI \u203a Model Groups \u00b7 ", h("span.mono", null, "ai.groups")),
			h("h1", null, "Model Groups"),
			h("p.set-lead", null, "A group is a topology: an orchestrator - the chat's own model - and the sub-agents it runs. A chat runs on a group or on one model (the composer's Model menu). The orchestrator's role follows the chat's mode: Ask uses primary, Plan planning, Agent implementation. A turn tries its role's model, then the fallbacks in order; a Claude model with a pace is passed over while your plan runs ahead of it, so the week or the 5-hour session lasts (a cheaper model or a local one takes the turn). The starred group is the default (ai.defaultGroup)."),
			this.bar, this.table);
		this.stop = this.settings.onChange(() => this.render());
		this.render();
	}

	groups() {
		return Object.assign({}, this.written || this.settings.value("ai.groups").value || {});
	}

	models() {
		return Object.keys(this.settings.value("ai.models").value || {});
	}

	isClaude(name) {
		const profile = (this.settings.value("ai.models").value || {})[name];
		return Boolean(profile && profile.provider === "claude-code");
	}

	defaultGroup() {
		return this.settings.value("ai.defaultGroup").value || "";
	}

	render() {
		const groups = this.groups();
		const names = Object.keys(groups);
		if (!this.selected || !groups[this.selected]) this.selected = names.includes(this.defaultGroup()) ? this.defaultGroup() : names[0] || null;
		this.renderBar(names);
		this.renderRoles(groups[this.selected]);
	}

	renderBar(names) {
		const chips = names.map((name) => this.renaming && name === this.selected ? this.renameField(name) : h("button.set-group" + (name === this.selected ? ".on" : ""), {
			dataset: { group: name }, on: { click: () => {
				this.selected = name;
				this.render();
			} },
		}, name === this.defaultGroup() ? icon("star-full") : null, name));
		replace(this.bar, h("span.set-group-list", null, ...chips),
			h("button.btn", { on: { click: () => this.create() } }, icon("add"), "New"),
			this.selected ? h("button.btn", { on: { click: () => this.duplicate() } }, icon("copy"), "Duplicate") : null,
			this.selected ? h("button.btn", { on: { click: () => {
				this.renaming = true;
				this.render();
			} } }, "Rename") : null,
			this.selected && this.selected !== this.defaultGroup() ? h("button.btn", { on: { click: () => this.settings.set("ai.defaultGroup", this.selected, "app") } }, icon("star-empty"), "Make default") : null,
			h("span.grow"),
			this.selected ? h("button.btn.danger", { on: { click: () => this.remove() } }, icon("trash"), "Delete") : null);
	}

	renameField(name) {
		const field = h("input.input-field", { value: name, "aria-label": "Group name", spellcheck: "false" });
		let done = false;
		const finish = async (keep) => {
			if (done) return;
			done = true;
			this.renaming = false;
			const next = field.value.trim();
			if (keep && next && next !== name) await this.rename(name, next);
			this.render();
		};
		field.addEventListener("keydown", (event) => {
			event.stopPropagation();
			if (event.key === "Enter") finish(true);
			else if (event.key === "Escape") finish(false);
		});
		field.addEventListener("blur", () => finish(true));
		requestAnimationFrame(() => {
			field.focus();
			field.select();
		});
		return field;
	}

	renderRoles(group) {
		if (!group) {
			replace(this.table, h("p.muted", null, "No group yet: a chat then uses the first model in Models. Make one with New."));
			return;
		}
		const models = this.models();
		const description = h("input.input-field.set-group-about", { "aria-label": "What this group is for", placeholder: "What this group is for (the Model menu shows it)", value: group.description || "", spellcheck: "true" });
		description.addEventListener("change", () => this.save(Object.assign({}, group, { description: description.value.trim() || undefined })));
		const agents = Object.entries(group.agents || {});
		replace(this.table,
			description,
			h("h2.set-sub", null, "Orchestrator"),
			h("div.set-role.head", null, h("span"), h("span", null, "Role"), h("span", null, "Model"), h("span", null, "Pace (Claude)"), h("span", null, "Temp"), h("span", null, "Effort")),
			...ROLES.map(([role, what]) => this.roleRow(group, role, what, models)),
			h("h2.set-sub", null, "Sub-agents"),
			h("p.set-lead", null, "The orchestrator runs these with agent_run, each as a chat of its own in the background, and hears its answer when it ends. It is told what each is for, so say it plainly: what it is good at, what it is not. An agent runs the agents its Calls names. A chat on a single model runs none."),
			agents.length ? h("div.set-role.set-agent.head", null, h("span", null, "Agent"), h("span", null, "Model"), h("span", null, "Pace (Claude)"), h("span", null, "Mode"), h("span", null, "Temp"), h("span", null, "Effort"), h("span")) : null,
			...agents.map(([name, entry]) => this.agentRow(group, name, entry, models)),
			h("div.set-bar", null, h("button.btn.set-add-agent", { disabled: !models.length, on: { click: () => this.addAgent(group, models) } }, icon("add"), "Add agent")));
	}

	// A role is on while it has a model; an off role is grayed out with its controls locked until it is turned on.
	roleRow(group, role, what, models) {
		const entry = group[role] || {};
		const on = Boolean(entry.model);
		const locked = on ? {} : { disabled: true };
		const change = (patch) => this.save(withRole(group, role, patch));
		const toggle = h("input.set-role-toggle", Object.assign({
			type: "checkbox", "aria-label": role + " on", title: on ? "Turn " + role + " off: the group no longer sets it" : "Turn " + role + " on",
			on: { change: (event) => change(event.target.checked ? { model: roleDefault(group, models) } : { model: "" }) },
		}, on ? { checked: true } : {}, on || models.length ? {} : { disabled: true }));
		const model = h("select.input-field.set-role-model", Object.assign({ "aria-label": role + " model", on: { change: (event) => change({ model: event.target.value, pace: this.isClaude(event.target.value) ? entry.pace : undefined }) } }, locked),
			h("option", { value: "" }, "-"), ...models.map((name) => h("option", Object.assign({ value: name }, entry.model === name ? { selected: true } : {}), name)),
			entry.model && !models.includes(entry.model) ? h("option", { value: entry.model, selected: true }, entry.model + " (not in Models)") : null);
		const temperature = h("input.input-field.set-num", Object.assign({ "aria-label": role + " temperature", inputmode: "decimal", placeholder: "-", value: entry.temperature === undefined ? "" : String(entry.temperature) }, locked));
		temperature.addEventListener("change", () => {
			const text = temperature.value.trim();
			const value = Number(text);
			if (text && (!Number.isFinite(value) || value < 0 || value > 2)) {
				temperature.value = entry.temperature === undefined ? "" : String(entry.temperature);
				this.ctx.ied.toast("A temperature is a number from 0 to 2.", { level: "warn" });
				return;
			}
			change({ temperature: text ? value : undefined });
		});
		const effort = h("select.input-field.set-role-effort", Object.assign({ "aria-label": role + " effort", on: { change: (event) => change({ effort: event.target.value || undefined }) } }, locked),
			...EFFORTS.map((e) => h("option", Object.assign({ value: e }, (entry.effort || "") === e ? { selected: true } : {}), e || "-")));
		const pace = on && this.isClaude(entry.model) ? this.paceSelect(role + " pace", entry.pace, (next) => change({ pace: next })) : h("span.set-role-pace.muted", null, on ? "-" : "");
		const places = fallbackPlaces(entry);
		const setPlaces = (next) => change({ fallback: storedFallback(next) });
		const add = h("select.input-field.set-add", { "aria-label": role + " add a fallback", on: { change: (event) => {
			if (event.target.value) setPlaces(places.concat({ model: event.target.value }));
		} } }, h("option", { value: "" }, "+"), ...models.filter((m) => m !== entry.model && !places.some((place) => place.model === m)).map((m) => h("option", { value: m }, m)));
		const state = on ? what : role === "primary" ? "off: chats use the first model" : "off: uses primary";
		return h("div.set-role" + (on ? "" : ".off"), { dataset: { role, state: on ? "on" : "off" } },
			toggle,
			h("span.set-role-name", null, h("b", null, role), h("span.muted", null, state)),
			model, pace, temperature, effort,
			h("span.set-chips", null, entry.model ? h("span.set-then", null, places.length ? "then" : "no fallbacks") : null, ...places.map((place, i) => this.placeChip(place, i, places, setPlaces)), entry.model ? add : null));
	}

	// A sub-agent: its name, model, pace, mode, temperature and effort; what it is for; the agents it calls; its fallbacks.
	agentRow(group, name, entry, models) {
		const change = (patch) => this.save(withAgent(group, name, patch));
		const label = "Agent " + name;
		const nameField = h("input.input-field.set-agent-name", { "aria-label": label + " name", value: name, spellcheck: "false" });
		nameField.addEventListener("change", () => {
			const next = nameField.value.trim();
			if (next && next !== name && (group.agents || {})[next]) this.ctx.ied.toast("There is an agent " + next + " already.", { level: "warn" });
			this.save(withAgentRenamed(group, name, next));
		});
		const model = h("select.input-field.set-role-model", { "aria-label": label + " model", on: { change: (event) => change({ model: event.target.value, pace: this.isClaude(event.target.value) ? entry.pace : undefined }) } },
			...models.map((m) => h("option", Object.assign({ value: m }, entry.model === m ? { selected: true } : {}), m)),
			entry.model && !models.includes(entry.model) ? h("option", { value: entry.model, selected: true }, entry.model + " (not in Models)") : null);
		const pace = this.isClaude(entry.model) ? this.paceSelect(label + " pace", entry.pace, (next) => change({ pace: next })) : h("span.set-role-pace.muted", null, "-");
		const mode = h("select.input-field.set-agent-mode", { "aria-label": label + " mode", on: { change: (event) => change({ mode: event.target.value === "agent" ? undefined : event.target.value }) } },
			...AGENT_MODES.map((m) => h("option", Object.assign({ value: m }, (entry.mode || "agent") === m ? { selected: true } : {}), m)));
		const temperature = h("input.input-field.set-num", { "aria-label": label + " temperature", inputmode: "decimal", placeholder: "-", value: entry.temperature === undefined ? "" : String(entry.temperature) });
		temperature.addEventListener("change", () => {
			const text = temperature.value.trim();
			const value = Number(text);
			if (text && (!Number.isFinite(value) || value < 0 || value > 2)) {
				temperature.value = entry.temperature === undefined ? "" : String(entry.temperature);
				this.ctx.ied.toast("A temperature is a number from 0 to 2.", { level: "warn" });
				return;
			}
			change({ temperature: text ? value : undefined });
		});
		const effort = h("select.input-field.set-role-effort", { "aria-label": label + " effort", on: { change: (event) => change({ effort: event.target.value || undefined }) } },
			...EFFORTS.map((e) => h("option", Object.assign({ value: e }, (entry.effort || "") === e ? { selected: true } : {}), e || "-")));
		const about = h("input.input-field.set-agent-about", { "aria-label": label + " is for", placeholder: "What it is for - the orchestrator reads this", value: entry.description || "", spellcheck: "true" });
		about.addEventListener("change", () => change({ description: about.value.trim() }));
		const calls = entry.calls || [];
		const others = calls.includes("*") ? [] : Object.keys(group.agents || {}).filter((other) => other !== name && !calls.includes(other));
		const addCall = h("select.input-field.set-add", { "aria-label": label + " calls another agent", on: { change: (event) => {
			if (event.target.value) change({ calls: event.target.value === "*" ? ["*"] : calls.filter((c) => c !== "*").concat(event.target.value) });
		} } }, h("option", { value: "" }, "+"), calls.includes("*") ? null : h("option", { value: "*" }, "every agent"), ...others.map((o) => h("option", { value: o }, o)));
		const places = fallbackPlaces(entry);
		const setPlaces = (next) => change({ fallback: storedFallback(next) });
		const addPlace = h("select.input-field.set-add", { "aria-label": label + " add a fallback", on: { change: (event) => {
			if (event.target.value) setPlaces(places.concat({ model: event.target.value }));
		} } }, h("option", { value: "" }, "+"), ...models.filter((m) => m !== entry.model && !places.some((place) => place.model === m)).map((m) => h("option", { value: m }, m)));
		return h("div.set-role.set-agent", { dataset: { agent: name } },
			nameField, model, pace, mode, temperature, effort,
			h("button.ib", { title: "Remove " + name, "aria-label": "Remove agent " + name, on: { click: () => this.save(withoutAgent(group, name)) } }, icon("trash")),
			about,
			h("span.set-chips", null, h("span.set-then", null, calls.length ? "calls" : "calls no agent"),
				...calls.map((c) => h("span.chip.set-call", { dataset: { call: c } }, c === "*" ? "every agent" : c,
					h("button.ib", { title: "Stop calling " + c, "aria-label": "Stop " + name + " calling " + c, on: { click: () => change({ calls: calls.filter((other) => other !== c) }) } }, icon("close")))),
				addCall),
			h("span.set-chips", null, h("span.set-then", null, places.length ? "then" : "no fallbacks"), ...places.map((place, i) => this.placeChip(place, i, places, setPlaces)), addPlace));
	}

	async addAgent(group, models) {
		const name = freshName(group.agents || {}, "agent");
		await this.save(withAgent(group, name, { model: roleDefault(group, models) }));
	}

	// A fallback: its place, its pace when it is a Claude model, Try earlier and Remove.
	placeChip(place, index, places, setPlaces) {
		const m = place.model;
		return h("span.chip.set-place", { dataset: { place: m } }, (index + 1) + ". " + m,
			this.isClaude(m) ? this.paceSelect("Fallback " + m + " pace", place.pace, (next) => setPlaces(places.map((other, j) => j === index ? { model: other.model, pace: next } : other))) : null,
			index > 0 ? h("button.ib", { title: "Try " + m + " earlier", "aria-label": "Move fallback " + m + " earlier", on: { click: () => setPlaces(movedEarlier(places, index)) } }, icon("arrow-left")) : null,
			h("button.ib", { title: "Remove " + m, "aria-label": "Remove fallback " + m, on: { click: () => setPlaces(places.filter((other) => other.model !== m)) } }, icon("close")));
	}

	// When a Claude model's place is used: always, or while the plan's pace against a target is at most a limit.
	paceSelect(label, pace, pick) {
		const value = paceValue(pace);
		const known = !value || PACE_TARGETS.some(([target]) => PACE_LIMITS.some((max) => max + "|" + target === value));
		return h("select.input-field.set-role-pace", {
			"aria-label": label, title: "Used always, or while your plan's pace against the target is at most this; over it, a turn goes on down the chain",
			on: { change: (event) => pick(paceFromValue(event.target.value)) },
		}, h("option", Object.assign({ value: "" }, value ? {} : { selected: true }), "always"),
			known ? null : h("option", { value, selected: true }, paceLabel(pace)),
			...PACE_TARGETS.map(([target, name]) => h("optgroup", { label: "on pace with the " + name }, ...PACE_LIMITS.map((max) => {
				const option = max + "|" + target;
				return h("option", Object.assign({ value: option }, option === value ? { selected: true } : {}), "up to " + max.toFixed(2) + "x " + name);
			}))));
	}

	async save(group) {
		const groups = this.groups();
		groups[this.selected] = group;
		await this.writeGroups(groups);
	}

	// Writes ai.groups and draws it at once. Until the server answers, the page reads what it wrote, so a change made
	// before then builds on this one instead of undoing it; the answer comes after settings.changed has the new value.
	async writeGroups(groups) {
		this.written = groups;
		this.render();
		const error = await this.settings.set("ai.groups", groups, "app");
		if (error) this.ctx.ied.toast(error, { level: "error" });
		if (this.written !== groups) return;
		this.written = null;
		this.render();
	}

	async create() {
		const groups = this.groups();
		const name = freshName(groups, "group");
		const first = this.models()[0];
		groups[name] = first ? { primary: { model: first } } : {};
		this.selected = name;
		await this.writeGroups(groups);
	}

	async duplicate() {
		const groups = this.groups();
		const name = freshName(groups, this.selected + " copy");
		groups[name] = JSON.parse(JSON.stringify(groups[this.selected]));
		this.selected = name;
		await this.writeGroups(groups);
	}

	async rename(from, to) {
		const groups = this.groups();
		if (groups[to]) {
			this.ctx.ied.toast("There is a group " + to + " already.", { level: "warn" });
			return;
		}
		const renamed = {};
		for (const [name, group] of Object.entries(groups)) renamed[name === from ? to : name] = group;
		this.selected = to;
		await this.writeGroups(renamed);
		if (this.defaultGroup() === from) await this.settings.set("ai.defaultGroup", to, "app");
	}

	async remove() {
		const name = this.selected;
		if (!(await confirmDialog({ title: "Delete group", message: "Delete the group " + name + "? Chats that use it move to the default group.", confirm: "Delete", danger: true }))) return;
		const groups = this.groups();
		delete groups[name];
		this.selected = null;
		await this.writeGroups(groups);
		if (this.defaultGroup() === name) await this.settings.reset("ai.defaultGroup", "app");
	}

	dispose() {
		this.stop();
	}
}
