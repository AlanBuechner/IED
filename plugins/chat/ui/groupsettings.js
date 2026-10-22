// groupsettings.js - Settings > AI > Model Groups (docs/04 "Model groups and roles", mockup 04): the groups of ai.groups
// as a bar (the default one starred), and the chosen group's roles - a model, a temperature, an effort and fallbacks in
// order. A new group turns on primary alone; a role with nothing set is off, grayed out, and uses the primary's model.
// A chat's mode picks the role (Ask primary, Plan planning, Agent implementation); the other roles are kept for the
// features that will use them. A Claude model in a chain - the role's or a fallback - has a pace (docs/04 "Pacing"):
// always, or while the plan's pace is at most a limit; fallbacks move earlier and go.

import { h, icon, replace } from "/ui-kit/dom.js";
import { confirmDialog } from "/ui-kit/dialog.js";
import { PACE_LIMITS, PACE_TARGETS, fallbackPlaces, paceFromValue, paceLabel, paceValue, storedFallback } from "./pacing.js";

export const ROLES = [
	["primary", "Ask mode, and roles left off", true],
	["planning", "Plan mode", true],
	["implementation", "Agent mode", true],
	["worker", "sub-agents", false],
	["research", "sub-agents", false],
	["verifier", "independent check", false],
	["summarizer", "compression", false],
	["titler", "titles, commit messages", false],
	["vision", "images, video frames", false],
	["completion", "ghost text (FIM)", false],
	["inline", "Ctrl+I edits", false],
];
const EFFORTS = ["", "low", "medium", "high"];

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
			h("p.set-lead", null, "A group gives every role a model, a temperature, an effort and fallbacks. Chats pick a group, and a chat's mode picks the role: Ask uses primary, Plan planning, Agent implementation. A turn tries its role's model, then the fallbacks in order; a Claude model with a pace is passed over while your plan runs ahead of it, so the week or the 5-hour session lasts (a cheaper model or a local one takes the turn). The starred group is the default (ai.defaultGroup)."),
			this.bar, this.table);
		this.stop = this.settings.onChange(() => this.render());
		this.render();
	}

	groups() {
		return Object.assign({}, this.settings.value("ai.groups").value || {});
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
		replace(this.table,
			h("div.set-role.head", null, h("span"), h("span", null, "Role"), h("span", null, "Model"), h("span", null, "Pace (Claude)"), h("span", null, "Temp"), h("span", null, "Effort")),
			...ROLES.map(([role, what, used]) => this.roleRow(group, role, what, used, models)));
	}

	// A role is on while it has a model; an off role is grayed out with its controls locked until it is turned on.
	roleRow(group, role, what, used, models) {
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
		const state = on ? what + (used ? "" : " (not used yet)") : role === "primary" ? "off: chats use the first model" : "off: uses primary";
		return h("div.set-role" + (on ? "" : ".off"), { dataset: { role, state: on ? "on" : "off" } },
			toggle,
			h("span.set-role-name", null, h("b", null, role), h("span.muted", null, state)),
			model, pace, temperature, effort,
			h("span.set-chips", null, entry.model ? h("span.set-then", null, places.length ? "then" : "no fallbacks") : null, ...places.map((place, i) => this.placeChip(place, i, places, setPlaces)), entry.model ? add : null));
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
		const error = await this.settings.set("ai.groups", groups, "app");
		if (error) this.ctx.ied.toast(error, { level: "error" });
	}

	async create() {
		const groups = this.groups();
		const name = freshName(groups, "group");
		const first = this.models()[0];
		groups[name] = first ? { primary: { model: first } } : {};
		this.selected = name;
		await this.settings.set("ai.groups", groups, "app");
	}

	async duplicate() {
		const groups = this.groups();
		const name = freshName(groups, this.selected + " copy");
		groups[name] = JSON.parse(JSON.stringify(groups[this.selected]));
		this.selected = name;
		await this.settings.set("ai.groups", groups, "app");
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
		await this.settings.set("ai.groups", renamed, "app");
		if (this.defaultGroup() === from) await this.settings.set("ai.defaultGroup", to, "app");
	}

	async remove() {
		const name = this.selected;
		if (!(await confirmDialog({ title: "Delete group", message: "Delete the group " + name + "? Chats that use it move to the default group.", confirm: "Delete", danger: true }))) return;
		const groups = this.groups();
		delete groups[name];
		this.selected = null;
		await this.settings.set("ai.groups", groups, "app");
		if (this.defaultGroup() === name) await this.settings.reset("ai.defaultGroup", "app");
	}

	dispose() {
		this.stop();
	}
}
