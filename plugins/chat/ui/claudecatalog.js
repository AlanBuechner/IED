// claudecatalog.js - the Claude models the CLI offers (aiusage `claude.models`, docs/04 "The Claude models"), as the Model
// menu and Settings > AI > Models show them: a family's profile (claude-fable, `model: "fable"`) runs the newest model of
// its family the CLI offers, and every version it offers (a full id) is a model a chat can be pinned to.

import { EFFORTS } from "./chatmodel.js";

const IDS = EFFORTS.map(([id]) => id);

// "Fable" for "fable".
export function familyName(family) {
	return family ? family[0].toUpperCase() + family.slice(1) : "";
}

// The catalog model a claude-code profile's `model` runs now: an alias its family's newest (`runs`), a family's name with
// no alias its newest, a full id itself; null when the catalog does not have it (not asked yet).
export function runsOf(model, catalog) {
	const list = catalog || [];
	const named = list.find((m) => m.value === model);
	if (named) return named.alias ? list.find((m) => m.value === named.runs) || list.find((m) => m.resolved === named.runs) || named : named;
	return list.find((m) => m.family === model && m.newest) || null;
}

// Whether a profile's `model` follows its family (an alias or a family's name) rather than naming one version.
export function follows(model) {
	return !!model && model !== "default" && !model.startsWith("claude-");
}

// A catalog model's efforts as the effort slider takes them: those the CLI listed, in order; none listed, none taken.
export function catalogEfforts(model) {
	return model && Array.isArray(model.efforts) ? IDS.filter((id) => model.efforts.includes(id)) : [];
}

// The versions a chat can be pinned to: the catalog's full ids, newest of each family first as the CLI lists them.
export function versionsOf(catalog) {
	return (catalog || []).filter((m) => !m.alias);
}
