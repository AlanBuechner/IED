// profiles.js - terminal profiles in the dock (docs/05 "Terminal profiles"): the rows of New Terminal's menu from the
// server's term.profiles answer ({profiles: [{id, title, icon, plugin, available, problem}], default}), the default's
// submenu, a command per contributed profile, and the icon a terminal's tab shows.

export const DEFAULT_MENU = "terminal.defaultProfile";

// A profile's icon as a manifest names it, with the base URL of the plugin that contributes it (an SVG of its own).
export function profileIcon(contributions, profileId) {
	const found = contributions.filter((c) => c.value.id === profileId).pop();
	return found && found.value.icon ? { icon: found.value.icon, base: found.plugin.base } : { icon: "$(terminal)", base: null };
}

// New Terminal's menu: a row per profile (one that cannot start disabled, saying why), the default marked, then the
// default's submenu.
export function profileRows(answer, contributions) {
	const rows = (answer.profiles || []).map((profile) => {
		const shown = profileIcon(contributions, profile.id);
		return {
			kind: "item", id: "profile", profile: profile.id, enabled: profile.available,
			label: profile.title + (profile.available ? "" : " (not found)"), icon: shown.icon, base: shown.base,
			keys: profile.id === answer.default ? "Default" : "", title: profile.problem || "",
		};
	});
	if (rows.length) rows.push({ kind: "separator" });
	rows.push({ kind: "submenu", label: "Default Profile", menuId: DEFAULT_MENU, icon: "$(settings-gear)" });
	return rows;
}

// The default's submenu: the profiles that can start, the setting's one checked; "Automatic" clears the setting.
export function defaultRows(answer) {
	const setting = answer.setting || "";
	const rows = [{ kind: "item", id: "default", profile: "", enabled: true, label: "Automatic", icon: !setting ? "$(check)" : null, keys: "" }];
	for (const profile of answer.profiles || [])
		if (profile.available) rows.push({ kind: "item", id: "default", profile: profile.id, enabled: true, label: profile.title, icon: setting === profile.id ? "$(check)" : null, keys: "" });
	return rows;
}

// A command per contributed profile, "Terminal: New Python Terminal": id ied.terminal.new.<profile id>.
export function profileCommands(contributions) {
	const seen = new Map();
	for (const c of contributions) seen.set(c.value.id, c);
	return [...seen.values()].map((c) => ({
		id: "ied.terminal.new." + c.value.id, title: "New " + c.value.title + " Terminal", category: "Terminal", icon: c.value.icon || "$(terminal)", profile: c.value.id,
	}));
}
