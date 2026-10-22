// developer.test.js - Settings > Environment > Developer (plugins/developer/ui, docs/28): the desktop build's rows, the
// build's one line, whether the window can relaunch, and what DEV says of a build.

import { test, assertEqual, assertTrue } from "./harness.js";
import { buildLine, canRelaunch, duration, installRows, relaunchWhenBuilt, shortCommit } from "/plugins/ied.developer/ui/buildpage.js";
import { statusOf } from "/plugins/ied.developer/ui/index.js";

const HEAD = "0f458b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e6f70";
const INSTALLED = "307c681aabbccddeeff00112233445566778899";

function state(over) {
	return Object.assign({ source: "C:/GameDev/IED", install: { commit: INSTALLED, dirty: false, installedMs: 0 }, head: HEAD, behind: 3,
		build: { state: "idle", startedMs: 0, endedMs: 0, exitCode: 0, lines: [] } }, over);
}

test("developer: a commit shows as its first seven", () => {
	assertEqual(shortCommit(INSTALLED), "307c681");
	assertEqual(shortCommit(""), "unknown");
});

test("developer: durations are minutes and seconds", () => {
	assertEqual([0, 42000, 247000].map(duration), ["0:00", "0:42", "4:07"]);
	assertEqual(duration(-5000), "0:00", "a clock that went back is no time");
});

test("developer: the build's line says how it went and how long it took", () => {
	assertEqual(buildLine({ state: "idle" }, 0), "");
	assertEqual(buildLine({ state: "running", startedMs: 1000, endedMs: 0 }, 84000), "Building - 1:23");
	assertEqual(buildLine({ state: "succeeded", startedMs: 1000, endedMs: 193000 }, 999999), "Built in 3:12");
	assertEqual(buildLine({ state: "failed", startedMs: 0, endedMs: 42000, exitCode: 1 }, 0), "Failed (exit 1) after 0:42");
	assertEqual(buildLine({ state: "canceled", startedMs: 0, endedMs: 10000 }, 0), "Canceled after 0:10");
});

test("developer: the rows name the source, the install and how far the checkout has moved on", () => {
	const rows = installRows(state({}));
	assertEqual(rows.map((r) => r[0]), ["Source checkout", "Desktop install", "Checkout"]);
	assertEqual(rows[0][1], "C:/GameDev/IED");
	assertTrue(rows[1][1].startsWith("307c681, "), rows[1][1]);
	assertEqual(rows[2][1], "HEAD 0f458b1, 3 commits newer than the install");
	assertEqual(installRows(state({ behind: 1 }))[2][1], "HEAD 0f458b1, 1 commit newer than the install");
	assertEqual(installRows(state({ behind: 0 }))[2][1], "HEAD 0f458b1, what the install was made from");
	assertTrue(installRows(state({ install: { commit: INSTALLED, dirty: true, installedMs: 0 } }))[1][1].endsWith("(with uncommitted changes)"));
});

test("developer: without an install the checkout is what runs; without a source there is nothing to build", () => {
	const fromCheckout = installRows(state({ install: null, behind: -1 }));
	assertTrue(fromCheckout[1][1].startsWith("this IED runs from the checkout"), fromCheckout[1][1]);
	assertEqual(fromCheckout[2][1], "HEAD 0f458b1", "no count without an install to count from");
	const none = installRows(state({ source: "" }));
	assertEqual(none.length, 1);
	assertTrue(none[0][1].startsWith("none: "), none[0][1]);
});

test("developer: only the desktop app with relaunch can relaunch", () => {
	assertEqual(canRelaunch({}), false);
	assertEqual(canRelaunch({ iedDesktop: { newWindow() {} } }), false, "an older desktop app");
	assertEqual(canRelaunch({ iedDesktop: { relaunch() {} } }), true);
});

// A client whose ied.developer/state is driven by hand: `send(state)` delivers a snapshot first, then changed events.
function fakeClient() {
	const client = { subscribers: 0, handlers: null };
	client.subscribe = (topic, handlers) => {
		client.subscribers += 1;
		client.handlers = handlers;
		return () => { client.subscribers -= 1; };
	};
	client.send = (buildState, first) => {
		const data = state({ build: { state: buildState } });
		if (first) client.handlers.snap(data);
		else client.handlers.evt("changed", data);
	};
	return client;
}

function desktop() {
	const win = { relaunched: 0 };
	win.iedDesktop = { relaunch: () => { win.relaunched += 1; } };
	return win;
}

test("developer: relaunch waits for the next build to succeed, not the last one", () => {
	const client = fakeClient();
	const win = desktop();
	relaunchWhenBuilt(client, win);
	client.send("succeeded", true);
	assertEqual(win.relaunched, 0, "the build before this one");
	client.send("running");
	client.send("running");
	assertEqual(win.relaunched, 0);
	client.send("succeeded");
	assertEqual([win.relaunched, client.subscribers], [1, 0], "once, and it stops listening");
	client.handlers.evt("changed", state({ build: { state: "succeeded" } }));
	assertEqual(win.relaunched, 1, "never twice");
});

test("developer: a failed or canceled build, or a refused one, relaunches nothing", () => {
	for (const ending of ["failed", "canceled"]) {
		const client = fakeClient();
		const win = desktop();
		relaunchWhenBuilt(client, win);
		client.send("idle", true);
		client.send("running");
		client.send(ending);
		assertEqual([win.relaunched, client.subscribers], [0, 0], ending);
	}
	const client = fakeClient();
	const win = desktop();
	const stop = relaunchWhenBuilt(client, win);
	client.send("idle", true);
	stop();
	stop();
	assertEqual(client.subscribers, 0, "the request was refused; stopping twice is stopping once");
	const restarted = fakeClient();
	relaunchWhenBuilt(restarted, win);
	restarted.send("idle", true);
	restarted.send("running");
	restarted.handlers.snap(state({ build: { state: "idle" } }));
	assertEqual(restarted.subscribers, 0, "a server that restarted lost the build");
	assertEqual(win.relaunched, 0);
});

test("developer: DEV says when a build runs, fails or the checkout is ahead", () => {
	assertEqual(statusOf(state({ behind: 0 })), ["DEV", "", ""]);
	assertEqual(statusOf(state({ build: { state: "running" } }))[0], "DEV: building");
	const failed = statusOf(state({ build: { state: "failed", exitCode: 2 } }));
	assertEqual(failed.slice(0, 2), ["DEV: build failed", "c-error"]);
	assertTrue(failed[2].includes("exit 2"), failed[2]);
	assertEqual(statusOf(state({}))[2], "The checkout is 3 commits newer than the desktop build");
	assertEqual(statusOf(state({ install: null, behind: -1 }))[2], "", "nothing to be behind without an install");
	assertEqual(statusOf({ source: "C:/x" }), ["DEV", "", ""], "a snapshot from before builds");
});
