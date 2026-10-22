// pacing.js - a Claude model's pace in a group's chain (docs/04 "Pacing"), as the settings pages show and edit it: used while
// the plan's pace against a target is at most `max`, else a turn goes on down the chain; no pace is always.

export const PACE_LIMITS = [0.75, 1, 1.25, 1.5, 2];
export const PACE_TARGETS = [["both", "session and week"], ["week", "week"], ["session", "session"]];

// "up to 1.25x, week" or "always".
export function paceLabel(pace) {
	if (!pace || !(pace.max > 0)) return "always";
	const target = PACE_TARGETS.find(([id]) => id === (pace.target || "both")) || PACE_TARGETS[0];
	return "up to " + pace.max.toFixed(2) + "x, " + target[1];
}

// A pace as a select's value ("1.25|week"), "" for always, and back.
export function paceValue(pace) {
	return pace && pace.max > 0 ? pace.max + "|" + (pace.target || "both") : "";
}

export function paceFromValue(value) {
	if (!value) return undefined;
	const [max, target] = value.split("|");
	return target === "both" ? { max: Number(max) } : { max: Number(max), target };
}

// The places of a role's chain after its model: a fallback is a name or {model, pace}.
export function fallbackPlaces(entry) {
	return ((entry && entry.fallback) || []).map((place) => typeof place === "string" ? { model: place } : { model: place.model, pace: place.pace });
}

// Places back as ai.groups keeps them: a name when it has no pace.
export function storedFallback(places) {
	return places.map((place) => place.pace && place.pace.max > 0 ? { model: place.model, pace: place.pace } : place.model);
}
