// modelpicker.js - the composer's Model menu (docs/04 "Which model a turn uses"): what a chat runs on and how hard it
// thinks. Model groups are gone (owner, 2026-11-11: "i dont realy use the model groups so we can get rid of that"): first
// Default - the default chain, with the model a turn uses on it now (pacing's step down as the plan runs ahead of pace) -
// then the models by family, each by its label (the Claude CLI's name: "Opus 5.5"): Claude's under one heading - each
// family's profile with the model it runs now, then Versions, folded, every version the CLI offers (docs/04 "The Claude
// models") - each other server's under its host, a family of more than FOLD_OVER models folded unless it holds the chat's
// choice. At the
// bottom the effort slider (owner, 2026-10-26): the model's own default first, then the levels the model a turn uses takes
// (a Claude model's own list, from the CLI). Up/Down move along the list (Down past its last row reaches the slider),
// Enter picks, Esc closes; a phone shows a bottom sheet.

import { h, icon } from "/ui-kit/dom.js";
import { EFFORTS, effortDefaultDetail, effortStops, effortsOf } from "./chatmodel.js";
import { catalogEfforts, familyName, follows, runsOf, versionsOf } from "./claudecatalog.js";

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

// What the chat runs on, as its button says it: the label of the model its next turn starts on (nextModel), its own or
// its default's (owner, 2026-11-12: "dont list "default, default" for the model actually say what it is in the chat");
// Default only while nothing says which.
export function choiceLabel(name, choices) {
	if (!name) return "Default";
	const model = choices && choices.models ? choices.models.find((m) => m.name === name) : null;
	return model && model.label ? model.label : name;
}

// The model a chat's next turn starts on, `current` as showModelPicker's: its own (`model`), else what the server says
// (`now`, its snapshot's), else - a chat not sent yet, or before its snapshot - its use's default model as the settings
// have it (docs/04 "Defaults by use"), else the default chain's now. "" while the models are not known.
export function nextModel(choices, current) {
	if (current.model) return current.model;
	if (current.now) return current.now;
	if (!choices) return "";
	const use = useOf(choices, current.use);
	if (use && use.model && choices.models.some((m) => m.name === use.model)) return use.model;
	return defaultNow(choices);
}

// A use's entry in the choices ({id, title, model, effort}), the chat use when the chat names none.
export function useOf(choices, use) {
	return choices ? (choices.uses || []).find((u) => u.id === (use || "chat")) : null;
}

// The settings a picker lists: {models: [{name, label, claude, endpoint, efforts, defaultEffort, family, follows,
// version}], defaultModel, chains, uses} from settings.get's values, the aiusage snapshot's chains (the default chain, and
// the model a turn on it uses now) and its catalog (`claude.models`): a Claude profile labelled by the model it runs now,
// with the effort Claude Code gives it when a request names none, then every version the CLI offers that no profile is
// named after (`version`). `uses` are the aiUses contributions ({id, title, model, effort} naming settings): each with
// its default model and effort as `values` has them (docs/04 "Defaults by use").
export function choicesFrom(values, chains, catalog, uses) {
	const setting = (key) => (key && values[key] && typeof values[key].value === "string" ? values[key].value : "");
	const models = (values["ai.models"] && values["ai.models"].value) || {};
	const listed = Object.keys(models).map((name) => {
		const profile = models[name];
		const claude = profile.provider === "claude-code";
		const runs = claude ? runsOf(profile.model || "default", catalog) : null;
		return { name, label: profile.label || (runs && runs.label) || name, claude, endpoint: profile.endpoint || "", family: runs ? runs.family : "",
			follows: claude && follows(profile.model), efforts: runs && !Array.isArray(profile.efforts) ? catalogEfforts(runs) : effortsOf(profile),
			defaultEffort: runs ? runs.defaultEffort || "" : "" };
	});
	const versions = versionsOf(catalog).filter((m) => !models[m.value]).map((m) => ({ name: m.value, label: m.label || m.value, claude: true, endpoint: "",
		family: m.family, version: true, efforts: catalogEfforts(m), defaultEffort: m.defaultEffort || "" }));
	return {
		models: listed.concat(versions),
		defaultModel: (values["ai.defaultModel"] && values["ai.defaultModel"].value) || "",
		chains: chains || [],
		uses: (uses || []).map((use) => ({ id: use.id, title: use.title, model: setting(use.model), effort: setting(use.effort) })),
	};
}

// What a chat runs at when it names no effort (effortLabel's `fallback`): `defaultEffort` - its use's, as its snapshot
// says; null before a snapshot (a chat not sent yet), the use's as the settings have it - by the use's title, else the
// own default of the model a turn uses (`modelName`) by its label.
export function effortFallback(choices, use, defaultEffort, modelName) {
	const entry = useOf(choices, use);
	const own = defaultEffort === null || defaultEffort === undefined ? (entry ? entry.effort : "") : defaultEffort;
	if (own) return { effort: own, use: entry ? entry.title : "its use" };
	const profile = choices && choices.models.find((m) => m.name === modelName);
	return { effort: profile ? profile.defaultEffort || "" : "", model: profile ? profile.label || profile.name : "" };
}

// The model a turn on the default chain uses now: pacing's pick when the chain has Claude, else its first model.
export function defaultNow(choices) {
	const chain = (choices.chains || []).find((c) => c.role === "primary") || (choices.chains || [])[0];
	if (chain && chain.now) return chain.now;
	if (choices.defaultModel && choices.models.some((m) => m.name === choices.defaultModel)) return choices.defaultModel;
	return choices.models.length ? choices.models[0].name : "";
}

// Shows the picker at `at` (the button) for a chat whose choice is `current` ({model - "" for its use's default or the
// default chain -, mode, effort, now - the model its next turn starts on, "" before a snapshot -, use, defaultEffort -
// its use's, as the snapshot says, null before one}); `pick({model})` gets a change of choice and closes it,
// `setEffort(effort)` a change of effort ("" the default), which leaves it open. Returns the function that closes it.
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
	// What Default runs now: the server's word when the chat follows it, else its use's model or the default chain's.
	const now = nextModel(choices, { model: "", now: current.model ? "" : current.now, use: current.use });
	const label = (name) => (choices.models.find((m) => m.name === name) || { label: name }).label;
	// Default: the use's own default model when its settings name one (docs/04 "Defaults by use"), else the default chain.
	const use = useOf(choices, current.use);
	const byUse = use && use.model && choices.models.some((m) => m.name === use.model);
	const defaultRow = h("button.chat-picker-row.grp" + (current.model ? "" : ".on"), {
		type: "button", "aria-pressed": current.model ? "false" : "true", dataset: { default: "1", now },
		title: byUse ? use.title + "'s default model (Settings > AI > Defaults): " + label(use.model)
			: "The default chain (ai.defaultModel): " + (now ? label(now) + " now" : "no model") + "; with Claude it steps down to lighter models while the plan runs ahead of its pace",
		on: { click: () => chosen({ model: "" }) },
	}, icon(current.model ? "layers" : "check"), h("span.grp-text", null, h("span.name", null, "Default"),
		h("span.sub", null, byUse ? label(use.model) + ", " + use.title + "'s default" : now ? "now " + label(now) : "no model yet")));
	const list = h("div.chat-picker-col", { role: "group", "aria-label": "Models" }, defaultRow, h("div.chat-picker-head", null, "Models"),
		...modelRows(choices.models, current, chosen));
	const popup = h("div.popup.chat-picker" + (sheet ? ".sheet" : ""), { role: "dialog", "aria-label": "Run this chat on", on: { keydown: (event) => key(event, popup, close) } },
		h("div.chat-picker-cols", null, list), effortSection(choices, current, setEffort, now));
	document.body.append(popup);
	if (!sheet) place(popup, at);
	document.addEventListener("pointerdown", outside, true);
	const first = popup.querySelector(".chat-picker-row.on") || popup.querySelector(".chat-picker-row");
	if (first) first.focus({ preventScroll: true });
	return close;
}

function modelRows(models, current, chosen) {
	if (!models.length) return [h("div.chat-picker-empty.muted", null, "No models yet: Settings > AI > Models")];
	const rows = [];
	const modelRow = (model) => row(model.name === current.model, model.label || model.name, model.label && model.label !== model.name ? model.name : model.claude ? "Claude subscription" : "",
		modelTitle(model), () => chosen({ model: model.name }), model.claude ? "sparkle" : "server");
	for (const family of modelFamilies(models)) {
		const holds = family.models.some((m) => m.name === current.model);
		const main = family.models.filter((m) => !m.version);
		const versions = family.models.filter((m) => m.version);
		const open = holds || main.length <= FOLD_OVER;
		const items = main.map(modelRow);
		if (versions.length) {
			const inVersions = versions.some((m) => m.name === current.model);
			items.push(...fold("Versions", versions.length, versions.map(modelRow), inVersions, "Every version Claude Code offers, each kept while it does", ".sub"));
		}
		if (family.models.length <= 1) {
			rows.push(...items);
			continue;
		}
		rows.push(...fold(family.name, main.length, items, open, "Show or fold " + family.name + "'s models", ""));
	}
	return rows;
}

// A Claude model's title: what a family's profile runs now, or the version a chat is pinned to.
function modelTitle(model) {
	if (model.version) return model.name + ": this version, offered by Claude Code";
	if (model.follows && model.family) return model.name + ": the newest " + familyName(model.family) + " Claude Code offers - " + model.label + " now";
	return model.name;
}

// A folding heading and its rows, open or not: [head, list].
function fold(name, count, items, open, title, kind) {
	const list = h("div.chat-picker-family" + kind, { hidden: !open }, ...items);
	const head = h("button.chat-picker-row.fam" + kind, { type: "button", "aria-expanded": open ? "true" : "false", title,
		on: { click: () => {
			list.hidden = !list.hidden;
			head.setAttribute("aria-expanded", list.hidden ? "false" : "true");
			replaceChevron(head, list.hidden);
		} } },
	icon(open ? "chevron-down" : "chevron-right"), h("span.name", null, name), h("span.meta", null, String(count)));
	return [head, list];
}

function replaceChevron(head, folded) {
	head.firstChild.replaceWith(icon(folded ? "chevron-right" : "chevron-down"));
}

function row(on, label, detail, title, run, glyph) {
	return h("button.chat-picker-row" + (on ? ".on" : ""), { type: "button", title, "aria-pressed": on ? "true" : "false", on: { click: run } },
		icon(on ? "check" : glyph), h("span.name", null, label), detail ? h("span.meta", null, detail) : null);
}

// The effort slider for the chat's choice: its stops (effortStops) for the model a turn uses (`now` on the default
// chain), the chat's effort on it, a label per stop to click or tap, and what the stop chosen means.
function effortSection(choices, current, setEffort, now) {
	const modelName = current.model || now || "";
	const profile = choices.models.find((m) => m.name === modelName);
	const supported = profile ? profile.efforts : EFFORTS.map(([id]) => id);
	const fallback = effortFallback(choices, current.use, current.defaultEffort, modelName);
	const stops = effortStops(current.effort || "", supported, fallback);
	let at = Math.max(0, stops.findIndex((stop) => stop.value === (current.effort || "")));
	const value = h("span.chat-effort-value");
	const slider = h("input.chat-effort-slider", { type: "range", min: "0", max: String(Math.max(0, stops.length - 1)), step: "1", "aria-label": "Effort", disabled: stops.length < 2 ? true : null });
	slider.value = String(at);
	const ticks = stops.map((stop, i) => h("button.chat-effort-tick", { type: "button", tabindex: "-1", dataset: { effort: stop.value }, title: stop.label + (stop.detail ? ": " + stop.detail : ""),
		style: { left: (stops.length > 1 ? (i / (stops.length - 1)) * 100 : 0) + "%" }, on: { click: () => choose(i) } },
		stop.value ? SHORT_EFFORTS[stop.value] || stop.label : stop.effort ? ["Default", h("span.chat-effort-tick-sub", null, SHORT_EFFORTS[stop.effort] || stop.effort)] : "Default"));
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
	const shown = profile && profile.label ? profile.label : modelName;
	return h("div.chat-picker-effort", { dataset: { model: modelName } },
		h("div.chat-picker-effort-head", null, h("span.chat-picker-head", null, "Effort"), value, h("span.grow"),
			modelName ? h("span.meta", { title: "The model a turn uses now" }, shown) : null),
		stops.length < 2 ? h("div.muted", null, (shown || "This model") + " takes no effort level: it runs as " + (profile && profile.claude ? "Claude Code runs it" : "its server does") + " by default.") : slider,
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

// Up/Down along the list (folded families' rows skipped; Down past the last row to the effort slider), Esc closes. The
// slider keeps its own keys.
function key(event, popup, close) {
	if (event.key === "Escape") {
		event.preventDefault();
		close();
		return;
	}
	const col = event.target.closest(".chat-picker-col");
	if (!col || (event.key !== "ArrowDown" && event.key !== "ArrowUp")) return;
	event.preventDefault();
	const list = [...col.querySelectorAll(".chat-picker-row")].filter((el) => !el.closest("[hidden]"));
	const at = list.indexOf(event.target);
	const slider = popup.querySelector(".chat-effort-slider:not([disabled])");
	if (event.key === "ArrowDown" && at === list.length - 1 && slider) {
		slider.focus();
		return;
	}
	const next = list[Math.max(0, Math.min(list.length - 1, at + (event.key === "ArrowDown" ? 1 : -1)))];
	if (next) next.focus();
}
