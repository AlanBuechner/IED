// contributions.test.js - the plugins' contributions as the shell reads them (docs/02 "UI modules"): contributions.on
// tells a UI module each time they are read again or cleared, after the read, and a listener that fails is reported
// without keeping the others from hearing.

import { test, assertEqual, assertTrue } from "./harness.js";
import { Commands } from "../src/shell/commands.js";
import { ContextKeys } from "../src/shell/context.js";
import { Keybindings } from "../src/shell/keybindings.js";
import { Menus } from "../src/shell/menus.js";
import { Contributions, importRetrying } from "../src/shell/contributions.js";

// A shell whose server answers plugins.contributions with `answers`, one per read.
function shellAnswering(answers) {
	const reports = [];
	const context = new ContextKeys();
	const commands = new Commands(context, (message) => reports.push(message));
	const keybindings = new Keybindings(commands, context, { report: (message) => reports.push(message) });
	const client = { request: async (method) => (method === "plugins.contributions" ? { plugins: answers.shift() } : null) };
	return { client, context, commands, keybindings, menus: new Menus(commands, context, keybindings), modules: new Map(), reports, report: (message) => reports.push(message) };
}

const LANGS = { id: "langs", source: "project", contributes: { languages: [{ id: "c4", extensions: [".cf"] }] } };

test("contributions: on() hears each read, after it, and each clear", async () => {
	const shell = shellAnswering([[], [LANGS]]);
	const contributions = new Contributions(shell);
	const heard = [];
	const stop = contributions.on(() => heard.push(contributions.of("languages").map((entry) => entry.value.id)));
	await contributions.load("w1");
	await contributions.load("w1");
	assertEqual(heard, [[], ["c4"]], "a plugin trusted in: its language is there when the listener runs");
	assertEqual(contributions.of("languages")[0].plugin.base, "/w/w1/plugins/langs/");
	contributions.clear();
	assertEqual(heard.length, 3, "cleared for the start page");
	stop();
	contributions.clear();
	assertEqual(heard.length, 3, "stopped");
});

test("contributions: a listener that fails is reported and the others still hear", async () => {
	const shell = shellAnswering([[LANGS]]);
	const contributions = new Contributions(shell);
	let heard = 0;
	contributions.on(() => {
		throw new Error("boom");
	});
	contributions.on(() => (heard += 1));
	await contributions.load("w1");
	assertEqual(heard, 1);
	assertEqual(shell.reports.length, 1);
	assertEqual(shell.reports[0].includes("boom"), true, shell.reports[0]);
});

test("contributions: a UI module that could not be fetched is asked for again under a URL of its own", async () => {
	const asked = [];
	const flaky = async (url) => {
		asked.push(url);
		if (asked.length < 3) throw new TypeError("Failed to fetch dynamically imported module: " + url);
		return { activate: null };
	};
	const module = await importRetrying("/plugins/ied.chat/ui/index.js", flaky, async () => {});
	assertTrue(module !== null, "the third try loads it");
	assertEqual(asked, ["/plugins/ied.chat/ui/index.js", "/plugins/ied.chat/ui/index.js?retry=1", "/plugins/ied.chat/ui/index.js?retry=2"]);
});

test("contributions: a module that ran and threw, or kept failing, is not asked for again forever", async () => {
	let calls = 0;
	const throwing = async () => { calls += 1; throw new SyntaxError("Unexpected token"); };
	let error = null;
	try { await importRetrying("/a.js", throwing, async () => {}); } catch (e) { error = e; }
	assertTrue(error instanceof SyntaxError && calls === 1, "a module's own error is its answer");
	calls = 0;
	const gone = async () => { calls += 1; throw new TypeError("Failed to fetch dynamically imported module"); };
	error = null;
	try { await importRetrying("/b.js", gone, async () => {}); } catch (e) { error = e; }
	assertTrue(error instanceof TypeError && calls === 3, "three tries, then the failure");
});
