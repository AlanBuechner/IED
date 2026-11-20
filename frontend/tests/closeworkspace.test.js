// closeworkspace.test.js - Close Workspace (closeworkspace.js, docs/10 "Workspaces"): what its server says closing would
// lose, the question asked, and what each answer does - Save saves first, Don't Save closes, Cancel keeps it open.

import { test, assertEqual, assertTrue } from "./harness.js";
import { closingCheck, closeMessage, closeWorkspace } from "../src/shell/closeworkspace.js";

const READY = { wsId: "w2", name: "Valley", kind: "solution", root: "C:/work/Valley", server: { state: "ready" } };

function fakeClient(answers) {
	const sent = [];
	return {
		sent,
		request(method, params) {
			sent.push([method, params]);
			const answer = answers[method];
			if (answer instanceof Error) return Promise.reject(answer);
			if (answer === "never") return new Promise(() => {});
			return Promise.resolve(answer === undefined ? {} : answer);
		},
	};
}

function fakeShell(client, workspaces = [READY]) {
	const reports = [];
	return { wsId: "w1", server: { workspaces }, client, reports, report: (message) => reports.push(message) };
}

// Dialogs that answer as told and keep what they were asked.
function fakeDialogs(answer) {
	const asked = [];
	return {
		asked,
		confirmDialog: (options) => { asked.push(["confirm", options]); return Promise.resolve(answer); },
		choiceDialog: (options) => { asked.push(["choice", options]); return Promise.resolve(answer); },
	};
}

const UNSAVED = { documents: [{ docId: "d1", path: "src/game.cf" }, { docId: "d2", path: "README.md" }] };
const RUNS = { runs: [{ runId: "r1", label: "Build Valley", status: "running" }, { runId: "r0", label: "Run Valley", status: "succeeded" }] };

test("close workspace: its server says what closing would lose", async () => {
	const client = fakeClient({ "doc.unsaved": UNSAVED, "task.runs": RUNS });
	const check = await closingCheck(client, READY);
	assertEqual(check, { unsaved: ["src/game.cf", "README.md"], running: ["Build Valley"], checked: true }, "a finished run is not lost");
	assertEqual(client.sent.map(([m, p]) => [m, p.wsId]), [["doc.unsaved", "w2"], ["task.runs", "w2"]], "asked of that workspace's server");
	const failed = await closingCheck(fakeClient({}), { ...READY, server: { state: "failed" } });
	assertEqual(failed, { unsaved: [], running: [], checked: true }, "a server that is not ready has nothing open to lose, and is not asked");
});

test("close workspace: a server that does not answer is said so, and closing still goes on", async () => {
	const timers = { setTimeout: (fn) => { fn(); return 1; }, clearTimeout: () => {} };
	const check = await closingCheck(fakeClient({ "doc.unsaved": "never" }), READY, 5000, timers);
	assertEqual(check.checked, false);
	assertTrue(/could not be checked/.test(closeMessage(check).textContent));
	const refused = await closingCheck(fakeClient({ "doc.unsaved": new Error("no such method") }), READY);
	assertEqual(refused.checked, false, "an older server without doc.unsaved");
});

test("close workspace: the message names the unsaved files, what runs and what stays", () => {
	const many = Array.from({ length: 11 }, (_, i) => "src/f" + i + ".cf");
	const message = closeMessage({ unsaved: many, running: ["Build Valley"], checked: true });
	const files = [...message.querySelectorAll(".close-files li")].map((li) => li.textContent);
	assertEqual(files.length, 9, "eight files and the rest counted");
	assertEqual(files[8], "and 3 more");
	assertTrue(/Still running: Build Valley/.test(message.querySelector(".close-running").textContent));
	assertTrue(/Chats, layouts and settings stay/.test(message.textContent));
	assertTrue(!closeMessage({ unsaved: [], running: [], checked: true }).querySelector(".close-files"), "nothing unsaved, no list");
});

test("close workspace: nothing unsaved asks Close or Cancel; Close closes", async () => {
	const client = fakeClient({ "doc.unsaved": { documents: [] }, "task.runs": { runs: [] } });
	const dialogs = fakeDialogs(true);
	assertEqual(await closeWorkspace(fakeShell(client), "w2", dialogs), true);
	assertEqual(dialogs.asked.map(([kind, o]) => [kind, o.title, o.confirm, o.danger]), [["confirm", "Close Valley", "Close", false]]);
	assertEqual(client.sent.map(([m]) => m), ["doc.unsaved", "task.runs", "ws.close"]);
	const kept = fakeClient({ "doc.unsaved": { documents: [] }, "task.runs": RUNS });
	const cancelled = fakeDialogs(false);
	assertEqual(await closeWorkspace(fakeShell(kept), "w2", cancelled), false);
	assertEqual(cancelled.asked[0][1].danger, true, "a running build makes Close the destructive answer");
	assertTrue(!kept.sent.some(([m]) => m === "ws.close"), "Cancel keeps it open");
});

test("close workspace: unsaved files ask Save, Don't Save or Cancel", async () => {
	const saving = fakeClient({ "doc.unsaved": UNSAVED, "task.runs": { runs: [] }, "doc.saveAll": { saved: 2 } });
	const save = fakeDialogs("save");
	assertEqual(await closeWorkspace(fakeShell(saving), "w2", save), true);
	assertEqual(save.asked[0][1].choices.map((c) => c.label), ["Save", "Don't Save"]);
	assertEqual(saving.sent.map(([m]) => m), ["doc.unsaved", "task.runs", "doc.saveAll", "ws.close"], "saved before it closes");
	const discarding = fakeClient({ "doc.unsaved": UNSAVED, "task.runs": { runs: [] } });
	assertEqual(await closeWorkspace(fakeShell(discarding), "w2", fakeDialogs("discard")), true);
	assertEqual(discarding.sent.map(([m]) => m), ["doc.unsaved", "task.runs", "ws.close"], "Don't Save closes as it is");
	const cancelling = fakeClient({ "doc.unsaved": UNSAVED, "task.runs": { runs: [] } });
	assertEqual(await closeWorkspace(fakeShell(cancelling), "w2", fakeDialogs(null)), false);
	assertEqual(cancelling.sent.map(([m]) => m), ["doc.unsaved", "task.runs"]);
});

test("close workspace: a save that fails keeps it open and says why", async () => {
	const client = fakeClient({ "doc.unsaved": UNSAVED, "task.runs": { runs: [] }, "doc.saveAll": new Error("access denied") });
	const shell = fakeShell(client);
	assertEqual(await closeWorkspace(shell, "w2", fakeDialogs("save")), false);
	assertTrue(!client.sent.some(([m]) => m === "ws.close"));
	assertEqual(shell.reports, ["The files were not saved, so the workspace stays open: access denied"]);
});

test("close workspace: no wsId closes the shown one", async () => {
	const client = fakeClient({ "doc.unsaved": { documents: [] }, "task.runs": { runs: [] } });
	const shell = fakeShell(client, [{ ...READY, wsId: "w1", name: "Claymore" }]);
	const dialogs = fakeDialogs(true);
	await closeWorkspace(shell, null, dialogs);
	assertEqual(dialogs.asked[0][1].title, "Close Claymore");
	assertEqual(client.sent[client.sent.length - 1], ["ws.close", { wsId: "w1" }]);
});
