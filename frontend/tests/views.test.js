// views.test.js - the views behind tab refs (views.js): a view whose factory answers later and whose tab closed meanwhile
// is disposed when it arrives (each such Markdown file's editor once stayed alive, with its document's subscription, while
// the person browsed), and a tab's title is read without making its view.

import { test, assertEqual } from "./harness.js";
import { ViewHost } from "../src/shell/views.js";

function hostOf() {
	const reports = [];
	const shell = { contributions: { toolWindow: () => null, plugin: () => null }, report: (message) => reports.push(message), shellApi: {}, wsId: "w1" };
	return { host: new ViewHost(shell), reports };
}

// A view that counts its disposals.
function made(name) {
	return { name, disposed: 0, dispose() { this.disposed += 1; } };
}

// A kind whose factory answers when the test says: `answer[ref](instance)`.
function slowKind(host) {
	const answer = {};
	host.registerKind("slow", { view: (ctx) => new Promise((resolve) => (answer[ctx.ref] = resolve)), title: (arg) => "Slow " + arg });
	return answer;
}

test("views: a view that arrives after its tab was released is disposed", async () => {
	const { host, reports } = hostOf();
	const answer = slowKind(host);
	host.get("slow:a");
	host.release("slow:a");
	const late = made("late");
	answer["slow:a"](late);
	await Promise.resolve();
	assertEqual([late.disposed, host.entries.has("slow:a"), reports], [1, false, []], "disposed at once, and not kept");
});

test("views: a view that arrives while its tab is open is kept until released", async () => {
	const { host } = hostOf();
	const answer = slowKind(host);
	const entry = host.get("slow:a");
	const view = made("view");
	answer["slow:a"](view);
	await Promise.resolve();
	assertEqual([entry.instance === view, view.disposed], [true, 0], "kept");
	host.release("slow:a");
	assertEqual(view.disposed, 1, "disposed once, with its tab");
});

test("views: a tab opened again before its first view arrives keeps only the new one", async () => {
	const { host } = hostOf();
	const answers = [];
	host.registerKind("slow", { view: () => new Promise((resolve) => answers.push(resolve)) });
	host.get("slow:a");
	host.release("slow:a");
	const again = host.get("slow:a");
	const first = made("first");
	const second = made("second");
	answers[0](first);
	answers[1](second);
	await Promise.resolve();
	assertEqual([first.disposed, second.disposed, again.instance === second], [1, 0, true]);
});

test("views: a tab's title is read without making its view", () => {
	const { host } = hostOf();
	let count = 0;
	host.registerKind("doc", { view: (ctx) => {
		count += 1;
		ctx.tab.setTitle("Renamed");
		return made("doc");
	}, title: (arg) => arg.toUpperCase() });
	assertEqual([host.titleOf("doc:a"), count, host.entries.size], ["A", 0, 0], "its kind's title, nothing made");
	host.get("doc:a");
	assertEqual([host.titleOf("doc:a"), count], ["Renamed", 1], "a mounted view's own title");
	assertEqual(host.titleOf("nothing:x"), "nothing:x", "a kind nobody renders: the ref");
});
