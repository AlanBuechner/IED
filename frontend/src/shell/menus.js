// menus.js - menus from contributions (F-SHL-25): a menu id ("menubar/view", "explorer/context", "tab/context") holds
// items grouped by their `group` ("<order>_<name>": groups sorted by it, separated by a line) and ordered by `order`;
// an item whose when-clause fails is left out, one whose command is not enabled is shown disabled. The shell adds
// generated items (View lists every tool window) through providers.

export const MENUBAR = [
	["file", "File"], ["edit", "Edit"], ["view", "View"], ["git", "Git"], ["project", "Project"], ["build", "Build"],
	["debug", "Debug"], ["test", "Test"], ["analyze", "Analyze"], ["tools", "Tools"], ["extensions", "Extensions"],
	["window", "Window"], ["help", "Help"],
];

function groupKey(group) {
	if (!group) return [Number.MAX_SAFE_INTEGER, ""];
	const match = /^(\d+)_?(.*)$/.exec(group);
	return match ? [Number(match[1]), match[2]] : [Number.MAX_SAFE_INTEGER - 1, group];
}

function compareGroups(a, b) {
	const [na, sa] = groupKey(a);
	const [nb, sb] = groupKey(b);
	return na - nb || sa.localeCompare(sb);
}

export class Menus {
	constructor(commands, context, keybindings) {
		this.commands = commands;
		this.context = context;
		this.keybindings = keybindings;
		this.items = new Map();
		this.providers = new Map();
	}

	// item: {command, group, order, when, submenu, args, title, icon, base}; `source`: "shell" or a plugin id.
	add(menuId, item, source) {
		if (!this.items.has(menuId)) this.items.set(menuId, []);
		this.items.get(menuId).push(Object.assign({ source }, item));
	}

	// `fn(context)` gives more items for `menuId` each time it opens.
	provide(menuId, fn) {
		this.providers.set(menuId, fn);
	}

	clearPluginItems() {
		for (const [id, list] of [...this.items]) this.items.set(id, list.filter((item) => item.source === "shell"));
	}

	// The rows to show: {kind: "item", command, args, label, icon, base, enabled, keys} | {kind: "submenu", label, menuId}
	// | {kind: "separator"}. `keys` stand in for context keys in the items' when-clauses.
	resolve(menuId, keys) {
		const provided = this.providers.has(menuId) ? this.providers.get(menuId)() : [];
		const visible = [...(this.items.get(menuId) || []), ...provided].filter((item) => this.context.holds(item.when, keys));
		const groups = new Map();
		for (const item of visible) {
			const key = item.group || "";
			if (!groups.has(key)) groups.set(key, []);
			groups.get(key).push(item);
		}
		const rows = [];
		for (const key of [...groups.keys()].sort(compareGroups)) {
			if (rows.length) rows.push({ kind: "separator" });
			const sorted = groups.get(key).map((item) => this.row(item)).filter(Boolean).sort((a, b) => a.order - b.order || a.label.localeCompare(b.label));
			rows.push(...sorted);
		}
		return rows;
	}

	row(item) {
		const definition = this.commands.get(item.command);
		const label = item.title || (definition ? definition.title : null);
		if (!label) return null;
		const order = typeof item.order === "number" ? item.order : 0;
		if (item.submenu) return { kind: "submenu", label, menuId: item.submenu, order };
		return {
			kind: "item", command: item.command, args: item.args, label, order,
			icon: item.icon || (definition ? definition.icon : null), base: item.icon ? item.base : definition ? definition.base : null,
			enabled: this.commands.isEnabled(item.command), keys: this.keybindings.labelFor(item.command, item.args),
		};
	}

	hasItems(menuId) {
		return this.resolve(menuId).some((row) => row.kind !== "separator");
	}
}
