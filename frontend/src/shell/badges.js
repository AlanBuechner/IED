// badges.js - the badge a plugin puts on its activity's button (ied.activities.badge, docs/02 "UI modules"): the
// desktop sidebar and the phone's bottom nav draw it the same way.

import { h } from "/ui-kit/dom.js";

// A count in the badge's tone, or a dot when it has none; null for no badge.
export function badgeElement(badge) {
	if (!badge) return null;
	return h("span.badge.activity-badge." + badge.tone + (badge.count ? "" : ".dot-only"), { "aria-hidden": "true" }, badge.count ? String(badge.count) : "");
}
