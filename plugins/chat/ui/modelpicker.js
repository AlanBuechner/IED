// modelpicker.js - the composer's Model menu (docs/04 "One choice per chat"): what a chat runs on, a group or one model.
// Groups on the left - each with its orchestrator and how many agents it has, the default marked - and models on the
// right by family: Claude's under one heading, each other server's under its host, a family of more than FOLD_OVER models
// folded unless it holds the chat's choice. Up/Down move in a column, Left/Right between them, Enter picks, Esc closes; a phone shows a bottom
// sheet with the two lists one above the other.

import { h, icon } from "/ui-kit/dom.js";

const PHONE = "(max-width: 719px)";
const FOLD_OVER = 3;

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

// The settings a picker lists: {groups: [{name, orchestrator, agents, description, isDefault}], models: [{name, claude,
// endpoint}]} from settings.get's values.
export function choicesFrom(values) {
	const groups = (values["ai.groups"] && values["ai.groups"].value) || {};
	const models = (values["ai.models"] && values["ai.models"].value) || {};
	const names = Object.keys(groups);
	const named = values["ai.defaultGroup"] && values["ai.defaultGroup"].value;
	const fallback = named && groups[named] ? named : names[0];
	return {
		groups: names.map((name) => ({
			name, isDefault: name === fallback, description: groups[name].description || "",
			orchestrator: groups[name].primary ? groups[name].primary.model : "", agents: Object.keys(groups[name].agents || {}).length,
		})),
		models: Object.keys(models).map((name) => ({ name, claude: models[name].provider === "claude-code", endpoint: models[name].endpoint || "" })),
	};
}

// Shows the picker at `at` (the button) for a chat whose choice is `current` ({group, model}); `pick({group, model})`
// gets the change (each clears the other). Returns the function that closes it.
export function showModelPicker(at, choices, current, pick) {
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
		...groupRows(choices.groups, current, chosen));
	const modelsCol = h("div.chat-picker-col", { role: "group", "aria-label": "Models" }, h("div.chat-picker-head", null, "Models"),
		...modelRows(choices.models, current, chosen));
	const popup = h("div.popup.chat-picker" + (sheet ? ".sheet" : ""), { role: "dialog", "aria-label": "Run this chat on", on: { keydown: (event) => key(event, popup, close) } },
		groupsCol, modelsCol);
	document.body.append(popup);
	if (!sheet) place(popup, at);
	document.addEventListener("pointerdown", outside, true);
	const first = popup.querySelector(".chat-picker-row.on") || popup.querySelector(".chat-picker-row");
	if (first) first.focus({ preventScroll: true });
	return close;
}

function groupRows(groups, current, chosen) {
	if (!groups.length) return [h("div.chat-picker-empty.muted", null, "No groups yet: Settings > AI > Model Groups")];
	const onDefault = !current.group && !current.model;
	return groups.map((group) => {
		const on = current.group ? current.group === group.name && !current.model : onDefault && group.isDefault;
		const detail = [group.orchestrator, group.agents ? group.agents + (group.agents === 1 ? " agent" : " agents") : "no agents"].filter(Boolean).join(" - ");
		return row(on, group.name + (group.isDefault ? " (default)" : ""), detail, group.description || detail, () => chosen({ group: group.name, model: "" }), "type-hierarchy");
	});
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

// Up/Down within a column (folded families' rows skipped), Left/Right to the other column, Esc closes.
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
