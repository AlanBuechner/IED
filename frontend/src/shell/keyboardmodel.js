// keyboardmodel.js - what the Keyboard settings page shows and writes (docs/07 "Host settings", docs/15 "Commands and
// keys"): a row per command (and per argument a binding gives it, `View: Solution Explorer`) with its keys - the
// defaults, the owner's, the defaults the owner took away - and the owner's keyboard.bindings edited as a list.

// "View: Solution Explorer" for shell.showView {id}, else the command's label with its arguments.
export function commandLabel(shell, command, args) {
	const id = args && args.id;
	const tool = command === "shell.showView" && id ? shell.contributions.toolWindow(id) : null;
	const activity = command === "shell.showActivity" && id ? shell.contributions.activity(id) : null;
	if (tool || activity) return "View: " + (tool || activity).title;
	return shell.commands.label(command) + (args === undefined ? "" : " " + JSON.stringify(args));
}

function argsKey(args) {
	return JSON.stringify(args === undefined ? null : args);
}

// commands: [{id}]; bindings: Keybindings.all(); label(command, args). Rows sorted by label:
// [{command, args, label, keys: [{chord, label, when, source, user, removed}]}].
export function commandRows(commands, bindings, label) {
	const rows = new Map();
	const row = (command, args) => {
		const key = command + "\u0000" + argsKey(args);
		if (!rows.has(key)) rows.set(key, { command, args, label: label(command, args), keys: [] });
		return rows.get(key);
	};
	for (const definition of commands) row(definition.id, undefined);
	for (const b of bindings) {
		row(b.command, b.args).keys.push({ chord: b.keys.join(" "), label: b.label, when: b.when || "", source: b.source, user: b.source === "user", removed: Boolean(b.removed) });
	}
	return [...rows.values()].sort((a, b) => a.label.localeCompare(b.label));
}

// The active bindings (not taken away) of `chord` whose when-clause is `when`, other than `row`'s: the conflicts.
export function conflicts(rows, chord, when, row) {
	const out = [];
	for (const other of rows) {
		if (other === row) continue;
		for (const key of other.keys) if (!key.removed && key.chord === chord && (key.when || "") === (when || "")) out.push(other.label);
	}
	return out;
}

function sameTarget(entry, command, args) {
	return entry.command === command && argsKey(entry.args) === argsKey(args);
}

// The owner's list with `chord` added to the command (with `when`).
export function withKey(entries, command, args, chord, when) {
	const entry = { command, key: chord };
	if (when) entry.when = when;
	if (args !== undefined) entry.args = args;
	return (entries || []).concat([entry]);
}

// The owner's list without `key` on the command: the owner's own entry goes; a default is taken away.
export function withoutKey(entries, command, args, key) {
	const list = entries || [];
	if (key.user) return list.filter((e) => !(sameTarget(e, command, args) && !e.remove && e.key === key.chord && (e.when || "") === (key.when || "")));
	return list.concat([{ command, key: key.chord, remove: true }]);
}

// The owner's list without anything it says about the command: its default keys come back.
export function withDefaults(entries, command, args) {
	return (entries || []).filter((e) => !(sameTarget(e, command, args) || (e.remove && e.command === command)));
}

// Whether the owner changed anything about the row's command.
export function changed(row) {
	return row.keys.some((k) => k.user || k.removed);
}
