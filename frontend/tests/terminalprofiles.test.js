// terminalprofiles.test.js - the terminal dock's profiles (plugins/terminal/ui/profiles.js, docs/05 "Terminal
// profiles"): New Terminal's menu rows, the default's submenu, a command per profile and a tab's icon.

import { test, assertEqual } from "./harness.js";
import { DEFAULT_MENU, defaultRows, profileCommands, profileIcon, profileRows } from "/plugins/ied.terminal/ui/profiles.js";

const CONTRIBUTIONS = [
	{ plugin: { id: "ied.terminal", base: "/plugins/ied.terminal/" }, value: { id: "pwsh", title: "PowerShell", icon: "$(terminal-powershell)", argv: ["pwsh"] } },
	{ plugin: { id: "ied.terminal", base: "/plugins/ied.terminal/" }, value: { id: "gitbash", title: "Git Bash", icon: "$(terminal-git-bash)", argv: ["bash"] } },
	{ plugin: { id: "ied.python", base: "/plugins/ied.python/" }, value: { id: "python", title: "Python", icon: "$(python)", argv: ["python"] } },
	{ plugin: { id: "my.tools", base: "/w/w1/plugins/my.tools/" }, value: { id: "python", title: "Python 3.13", icon: "icons/py.svg", argv: ["py"] } },
];

const ANSWER = {
	profiles: [
		{ id: "pwsh", title: "PowerShell", icon: "$(terminal-powershell)", plugin: "ied.terminal", available: true, problem: "" },
		{ id: "gitbash", title: "Git Bash", icon: "$(terminal-git-bash)", plugin: "ied.terminal", available: false, problem: "bash.exe was not found" },
		{ id: "python", title: "Python 3.13", icon: "icons/py.svg", plugin: "my.tools", available: true, problem: "" },
	],
	default: "pwsh",
	setting: "python",
};

test("terminal profiles: New Terminal's menu", () => {
	const rows = profileRows(ANSWER, CONTRIBUTIONS);
	assertEqual(rows.map((r) => r.kind), ["item", "item", "item", "separator", "submenu"]);
	assertEqual([rows[0].label, rows[0].keys, rows[0].enabled], ["PowerShell", "Default", true], "the default is marked");
	assertEqual([rows[1].label, rows[1].enabled, rows[1].title], ["Git Bash (not found)", false, "bash.exe was not found"], "one that cannot start says so");
	assertEqual([rows[2].icon, rows[2].base], ["icons/py.svg", "/w/w1/plugins/my.tools/"], "an icon of the plugin's own, from its folder");
	assertEqual([rows[4].label, rows[4].menuId], ["Default Profile", DEFAULT_MENU]);
	assertEqual(profileRows({ profiles: [], default: "" }, []).map((r) => r.kind), ["submenu"], "no profiles: only the submenu");
});

test("terminal profiles: the default's submenu checks the setting", () => {
	const rows = defaultRows(ANSWER);
	assertEqual(rows.map((r) => r.label), ["Automatic", "PowerShell", "Python 3.13"], "only those that can start, after Automatic");
	assertEqual(rows.map((r) => r.icon), [null, null, "$(check)"]);
	assertEqual(rows.map((r) => r.profile), ["", "pwsh", "python"]);
	assertEqual(defaultRows({ profiles: ANSWER.profiles, setting: "" })[0].icon, "$(check)", "no setting: Automatic");
});

test("terminal profiles: a command per profile, the last plugin's of an id", () => {
	assertEqual(profileCommands(CONTRIBUTIONS).map((c) => [c.id, c.title, c.category]), [
		["ied.terminal.new.pwsh", "New PowerShell Terminal", "Terminal"],
		["ied.terminal.new.gitbash", "New Git Bash Terminal", "Terminal"],
		["ied.terminal.new.python", "New Python 3.13 Terminal", "Terminal"],
	]);
});

test("terminal profiles: a tab's icon", () => {
	assertEqual(profileIcon(CONTRIBUTIONS, "pwsh"), { icon: "$(terminal-powershell)", base: "/plugins/ied.terminal/" });
	assertEqual(profileIcon(CONTRIBUTIONS, "python").base, "/w/w1/plugins/my.tools/", "the last plugin's");
	assertEqual(profileIcon(CONTRIBUTIONS, "gone"), { icon: "$(terminal)", base: null }, "a profile no longer contributed");
});
