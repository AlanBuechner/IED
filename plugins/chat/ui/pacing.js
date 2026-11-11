// pacing.js - a Claude model's pace in the default chain (docs/04 "Pacing"), as the Claude page shows it: used while the
// plan's pace against a target is at most `max`, else a turn goes on down the chain; no pace is always.

const PACE_TARGETS = [["both", "session and week"], ["week", "week"], ["session", "session"]];

// "up to 1.25x, week" or "always".
export function paceLabel(pace) {
	if (!pace || !(pace.max > 0)) return "always";
	const target = PACE_TARGETS.find(([id]) => id === (pace.target || "both")) || PACE_TARGETS[0];
	return "up to " + pace.max.toFixed(2) + "x, " + target[1];
}
