// modelpicker.js - the composer's Model menu (docs/04 "One choice per chat"): what a chat runs on, a group or one model,
// and how hard it thinks. Groups on the left - each with the model a turn of the chat's mode uses now (its chain's fallbacks
// and pace, from the aiusage chains; a chain without Claude its first model), its default effort and how many agents it
// has, the default marked - and models on the right by family: Claude's under one heading, each other server's under its
// host, a family of more than FOLD_OVER models folded unless it holds the chat's choice. At the bottom the effort slider
// (owner, 2026-10-26: "move the effort from the dropdown and put it into the model selection with a slider at the
// bottom"): the group's default first on a group - a single model has none, only its own default - then the levels the
// model a turn uses takes. Up/Down move in a column (Down past its last row reaches the slider), Left/Right between the
// columns and along the slider, Enter picks, Esc closes; a phone shows a bottom sheet with the lists one above the other.

import { h, icon } from "/ui-kit/dom.js";
import { EFFORTS, effortStops, effortsOf } from "./chatmodel.js";

const PHONE = "(max-width: 719px)";
const FOLD_OVER = 3;
const SHORT_EFFORTS = { low: "Low", medium: "Med", high: "High", xhigh: "XHigh", max: "Max" };

// The family a model is listed under: "Claude", or its server's host ("spark" for http://spark:8080/v1).
export function familyOf(model) {
	if (model.claude) return "Claude";
	const host = /^[a-z]+:\/\/([^/:]+)/i.exec(model.endpoint || "");
	return host ? host[1] : "Other";
}

// The models in families, in the order each first appears: [{name, models}].
export function modelFamilies(models) {
	const families = [];
	for (const model of models) {
		const name = familyOf(model);
		let family = families.find((f) => f.name === name);
		if (!family) families.push(family = { name, models: [] });
		family.models.push(model);
	}
	return families;
}

// What the chat runs on, as its button says it: a model of its own, its group, or the default group.
export function choiceLabel(summary, groupInUse) {
	if (summary.pinnedModel && !summary.group) return summary.pinnedModel;
	if (summary.pinnedModel) return summary.pinnedModel + " in " + summary.group;
	if (summary.group) return summary.group;
	return groupInUse ? groupInUse + " (default)" : "Default";
}

// The settings a picker lists: {groups: [{name, orchestrator, agents, description, isDefault, roles}], models: [{name,
// claude, endpoint, efforts}], chains} from settings.get's values and the aiusage snapshot's chains (each group role with a
// Claude model, and the model a turn of it uses now).
export function choicesFrom(values, chains) {
	const groups = (values["ai.groups"] && values["ai.groups"].value) || {};
	const models = (values["ai.models"] && values["ai.models"].value) || {};
	const names = Object.keys(groups);
	const named = values["ai.defaultGroup"] && values["ai.defaultGroup"].value;
	const fallback = named && groups[named] ? named : names[0];
	return {
		groups: names.map((name) => ({
			name, isDefault: name === fallback, description: groups[name].description || "",
			orchestrator: groups[name].primary ? groups[name].primary.model : "", agents: Object.keys(groups[name].agents || {}).length,
			roles: { primary: groups[name].primary, planning: groups[name].planning, implementation: groups[name].implementation },
		})),
		models: Object.keys(models).map((name) => ({ name, claude: models[name].provider === "claude-code", endpoint: models[name].endpoint || "", efforts: effortsOf(models[name]) })),
		chains: chains || [],
	};
}

// The role a turn in `mode` uses (docs/04 "Which role a turn uses"), as the server's RoleOfMode.
export function roleOfMode(mode) {
	return mode === "plan" ? "planning" : mode === "agent" ? "implementation" : "primary";
}

// What a turn of a chat in `mode` gets from `group` now: {role, model, effort} - the role the group gives the mode (one it
// leaves off is primary), the model its chain uses now (pacing's, from the chains; one without Claude its first model
// ai.models has) and the role's effort ("" the model's own).
export function groupTurn(choices, group, mode) {
	const roles = group.roles || {};
	const wanted = roleOfMode(mode);
	const role = roles[wanted] && typeof roles[wanted].model === "string" ? wanted : "primary";
	const entry = roles[role] && typeof roles[role].model === "string" ? roles[role] : null;
	const effort = (entry && entry.effort) || "";
	const chain = (choices.chains || []).find((c) => c.group === group.name && c.role === role);
	if (chain) return { role, model: chain.now || "", effort };
	const known = choices.models.map((m) => m.name);
	if (!entry) return { role, model: known[0] || "", effort };
	const places = [entry.model].concat((entry.fallback || []).map((place) => (typeof place === "string" ? place : place && place.model)));
	return { role, model: places.find((name) => known.includes(name)) || "", effort };
}

// Shows the picker at `at` (the button) for a chat whose choice is `current` ({group, model, mode, effort, now - the model
// its next turn starts on -, roleEffort - its group's effort for it, null before a snapshot says}); `pick({group, model})`
// gets a change of choice (each clears the other) and closes it, `setEffort(effort)` a change of effort ("" the default),
// which leaves it open. Returns the function that closes it.
export function showModelPicker(at, choices, current, pick, setEffort) {
	const sheet = window.matchMedia(PHONE).matches;
	const close = () => {
		document.removeEventListener("pointerdown", outside, true);
		popup.remove();
		if (at.isConnected) at.focus({ preventScroll: true });
	};
	const outside = (event) => {
		if (!popup.contains(event.target) && !at.contains(event.target)) close();
	};
	const chosen = (change) => {
		close();
		pick(change);
	};
	const groupsCol = h("div.chat-picker-col", { role: "group", "aria-label": "Groups" }, h("div.chat-picker-head", null, "Groups"),
		...groupRows(choices, current, chosen));
	const modelsCol = h("div.chat-picker-col", { role: "group", "aria-label": "Models" }, h("div.chat-picker-head", null, "Models"),
		...modelRows(choices.models, current, chosen));
	const popup = h("div.popup.chat-picker" + (sheet ? ".sheet" : ""), { role: "dialog", "aria-label": "Run this chat on", on: { keydown: (event) => key(event, popup, close) } },
		h("div.chat-picker-cols", null, groupsCol, modelsCol), effortSection(choices, current, setEffort));
	document.body.append(popup);
	if (!sheet) place(popup, at);
	document.addEventListener("pointerdown", outside, true);
	const first = popup.querySelector(".chat-picker-row.on") || popup.querySelector(".chat-picker-row");
	if (first) first.focus({ preventScroll: true });
	return close;
}

// The group the chat runs on (its own, else the default), null for a chat on a single model.
function currentGroup(choices, current) {
	if (current.model && !current.group) return null;
	return choices.groups.find((g) => g.name === current.group) || choices.groups.find((g) => g.isDefault) || null;
}

function groupRows(choices, current, chosen) {
	const groups = choices.groups;
	if (!groups.length) return [h("div.chat-picker-empty.muted", null, "No groups yet: Settings > AI > Model Groups")];
	const onDefault = !current.group && !current.model;
	const mine = currentGroup(choices, current);
	return groups.map((group) => {
		const on = current.group ? current.group === group.name && !current.model : onDefault && group.isDefault;
		const turn = groupTurn(choices, group, current.mode);
		const now = group === mine && !current.model && current.now ? current.now : turn.model;
		const effort = group === mine && typeof current.roleEffort === "string" ? current.roleEffort : turn.effort;
		const facts = [now ? "now " + now : "no model", "effort " + (effort ? effortName(effort) : "the model's own"), group.agents ? group.agents + (group.agents === 1 ? " agent" : " agents") : "no agents"];
		const title = (group.description ? group.description + ". " : "") + "A turn in this mode uses " + (now || "no model") + " now, as its " + turn.role + " chain's fallbacks and pace have it.";
		return h("button.chat-picker-row.grp" + (on ? ".on" : ""), { type: "button", title, "aria-pressed": on ? "true" : "false", dataset: { group: group.name, now }, on: { click: () => chosen({ group: group.name, model: "" }) } },
			icon(on ? "check" : "type-hierarchy"),
			h("span.grp-text", null, h("span.name", null, group.name + (group.isDefault ? " (default)" : "")), h("span.sub", null, facts.join(" - "))));
	});
}

function effortName(id) {
	return (EFFORTS.find(([e]) => e === id) || [id, id])[1];
}

function modelRows(models, current, chosen) {
	if (!models.length) return [h("div.chat-picker-empty.muted", null, "No models yet: Settings > AI > Models")];
	const rows = [];
	for (const family of modelFamilies(models)) {
		const holds = family.models.some((m) => m.name === current.model);
		const open = holds || family.models.length <= FOLD_OVER;
		const folds = family.models.length > 1;
		const items = family.models.map((model) => row(model.name === current.model, model.name, model.claude ? "Claude subscription" : "", model.name, () => chosen({ model: model.name, group: "" }), model.claude ? "sparkle" : "server"));
		if (!folds) {
			rows.push(...items);
			continue;
		}
		const list = h("div.chat-picker-family", { hidden: !open }, ...items);
		const head = h("button.chat-picker-row.fam", { type: "button", "aria-expanded": open ? "true" : "false", title: "Show or fold " + family.name + "'s models",
			on: { click: () => {
				list.hidden = !list.hidden;
				head.setAttribute("aria-expanded", list.hidden ? "false" : "true");
				replaceChevron(head, list.hidden);
			} } },
		icon(open ? "chevron-down" : "chevron-right"), h("span.name", null, family.name), h("span.meta", null, String(family.models.length)));
		rows.push(head, list);
	}
	return rows;
}

function replaceChevron(head, folded) {
	head.firstChild.replaceWith(icon(folded ? "chevron-right" : "chevron-down"));
}

function row(on, label, detail, title, run, glyph) {
	return h("button.chat-picker-row" + (on ? ".on" : ""), { type: "button", title, "aria-pressed": on ? "true" : "false", on: { click: run } },
		icon(on ? "check" : glyph), h("span.name", null, label), detail ? h("span.meta", null, detail) : null);
}

// The effort slider for the chat's choice: its stops (effortStops) for the model a turn uses, the chat's effort on it, a
// label per stop to click or tap, and what the stop chosen means.
function effortSection(choices, current, setEffort) {
	const group = currentGroup(choices, current);
	const alone = !group && !!current.model;
	const turn = group ? groupTurn(choices, group, current.mode) : null;
	const modelName = alone ? current.model : current.now || (turn && turn.model) || "";
	const profile = choices.models.find((m) => m.name === modelName);
	const supported = profile ? profile.efforts : EFFORTS.map(([id]) => id);
	const groupEffort = alone ? "" : typeof current.roleEffort === "string" ? current.roleEffort : turn ? turn.effort : "";
	const stops = effortStops(current.effort || "", groupEffort, alone, supported);
	let at = Math.max(0, stops.findIndex((stop) => stop.value === (current.effort || "")));
	const value = h("span.chat-effort-value");
	const slider = h("input.chat-effort-slider", { type: "range", min: "0", max: String(Math.max(0, stops.length - 1)), step: "1", "aria-label": "Effort", disabled: stops.length < 2 ? true : null });
	slider.value = String(at);
	const ticks = stops.map((stop, i) => h("button.chat-effort-tick", { type: "button", tabindex: "-1", dataset: { effort: stop.value }, title: stop.label + (stop.detail ? ": " + stop.detail : ""),
		style: { left: (stops.length > 1 ? (i / (stops.length - 1)) * 100 : 0) + "%" }, on: { click: () => choose(i) } }, stop.value ? SHORT_EFFORTS[stop.value] || stop.label : alone ? "Default" : "Group"));
	const describe = (i) => {
		const stop = stops[i];
		value.textContent = stop.label + (stop.detail ? " (" + stop.detail + ")" : "");
		slider.setAttribute("aria-valuetext", value.textContent);
		ticks.forEach((tick, j) => tick.classList.toggle("on", j === i));
	};
	const choose = (i) => {
		slider.value = String(i);
		describe(i);
		if (i === at) return;
		at = i;
		setEffort(stops[i].value);
	};
	slider.addEventListener("input", () => describe(Number(slider.value)));
	slider.addEventListener("change", () => choose(Number(slider.value)));
	describe(at);
	return h("div.chat-picker-effort", { dataset: { model: modelName } },
		h("div.chat-picker-effort-head", null, h("span.chat-picker-head", null, "Effort"), value, h("span.grow"),
			modelName ? h("span.meta", { title: "The model a turn uses now" }, modelName) : null),
		stops.length < 2 ? h("div.muted", null, (modelName || "This model") + " takes no effort level: it runs as its server does by default.") : slider,
		stops.length < 2 ? null : h("div.chat-effort-ticks", null, ...ticks));
}

// Above the button when there is room (the composer is at the bottom), held by its bottom so a family unfolding grows
// it upward; else below. Inside the window.
function place(popup, at) {
	const anchor = at.getBoundingClientRect();
	const rect = popup.getBoundingClientRect();
	if (anchor.top - rect.height - 4 >= 4) {
		popup.style.bottom = (window.innerHeight - anchor.top + 4) + "px";
		popup.style.maxHeight = (anchor.top - 8) + "px";
	}
	else popup.style.top = Math.max(4, Math.min(anchor.bottom + 4, window.innerHeight - rect.height - 4)) + "px";
	popup.style.left = Math.max(4, Math.min(anchor.left, window.innerWidth - rect.width - 4)) + "px";
}

// Up/Down within a column (folded families' rows skipped; Down past the last row to the effort slider), Left/Right to the
// other column, Esc closes. The slider keeps its own keys.
function key(event, popup, close) {
	if (event.key === "Escape") {
		event.preventDefault();
		close();
		return;
	}
	const rows = (col) => [...col.querySelectorAll(".chat-picker-row")].filter((el) => !el.closest("[hidden]"));
	const col = event.target.closest(".chat-picker-col");
	if (!col) return;
	if (event.key === "ArrowDown" || event.key === "ArrowUp") {
		event.preventDefault();
		const list = rows(col);
		const at = list.indexOf(event.target);
		const slider = popup.querySelector(".chat-effort-slider:not([disabled])");
		if (event.key === "ArrowDown" && at === list.length - 1 && slider) {
			slider.focus();
			return;
		}
		const next = list[Math.max(0, Math.min(list.length - 1, at + (event.key === "ArrowDown" ? 1 : -1)))];
		if (next) next.focus();
	}
	else if (event.key === "ArrowLeft" || event.key === "ArrowRight") {
		const cols = [...popup.querySelectorAll(".chat-picker-col")];
		const other = cols[(cols.indexOf(col) + 1) % cols.length];
		const target = other.querySelector(".chat-picker-row.on") || rows(other)[0];
		if (target) {
			event.preventDefault();
			target.focus();
		}
	}
}
