// closeworkspace.js - Close Workspace (docs/10 "Workspaces", docs/15 "Start page"): a workspace runs in a server of its
// own with its language servers, terminals and debug sessions, and closing it stops them all - how the owner frees what
// a project they are not using holds. Before it closes, the person is asked, as VS asks before it closes a solution,
// with what would be lost: its unsaved documents (Save, Don't Save, Cancel) and the builds and runs still going. Chats,
// layouts and settings stay on disk and come back when it opens again. The start page's open rows, the workspace
// switcher, File > Close Workspace, the status bar's server item and View > Workspace Servers all close through here.

import { h } from "/ui-kit/dom.js";
import { confirmDialog, choiceDialog } from "/ui-kit/dialog.js";

const CHECK_MS = 5000;
const FILES_SHOWN = 8;
const KEPT = "Its server stops: language servers, terminals and debug sessions end, and a running chat is interrupted. Chats, layouts and settings stay for when it opens again.";

// What closing `ws` would lose, asked of its server: {unsaved: [path], running: [label], checked}. A server that is not
// ready has no documents open (they ended with it); one that does not answer in time leaves `checked` false.
export async function closingCheck(client, ws, timeoutMs = CHECK_MS, timers = globalThis) {
	if (!ws.server || ws.server.state !== "ready") return { unsaved: [], running: [], checked: true };
	let timer = null;
	const late = new Promise((resolve) => { timer = timers.setTimeout(() => resolve(null), timeoutMs); });
	const asked = Promise.all([
		client.request("doc.unsaved", { wsId: ws.wsId }),
		client.request("task.runs", { wsId: ws.wsId }).catch(() => ({ runs: [] })),
	]);
	try {
		const answer = await Promise.race([asked, late]);
		if (!answer) return { unsaved: [], running: [], checked: false };
		const [docs, runs] = answer;
		return {
			unsaved: (docs.documents || []).map((d) => d.path),
			running: (runs.runs || []).filter((r) => r.status === "running" || r.status === "queued").map((r) => r.label || r.task),
			checked: true,
		};
	}
	catch {
		return { unsaved: [], running: [], checked: false };
	}
	finally {
		timers.clearTimeout(timer);
	}
}

// The dialog's message for `check`: the unsaved files first (the decision), then what still runs, then what stays.
export function closeMessage(check) {
	const parts = [];
	if (check.unsaved.length) {
		const shown = check.unsaved.slice(0, FILES_SHOWN);
		const more = check.unsaved.length - shown.length;
		parts.push(h("p", null, check.unsaved.length === 1 ? "Save changes to this file?" : "Save changes to these files?"),
			h("ul.close-files", null, ...shown.map((path) => h("li", { title: path }, path)), more > 0 ? h("li.more", null, "and " + more + " more") : null));
	}
	if (check.running.length) parts.push(h("p.close-running", null, "Still running: " + check.running.join(", ") + ". Closing stops it."));
	if (!check.checked) parts.push(h("p.close-running", null, "Its server did not answer, so unsaved changes could not be checked."));
	parts.push(h("p", null, KEPT));
	return h("div.close-message", null, ...parts);
}

// Asks, then closes `wsId` (the shown workspace when none is named): true when it was asked to close. Unsaved files are
// saved first when the person chooses Save; a save that fails keeps the workspace open and says why.
export async function closeWorkspace(shell, wsId, dialogs = { confirmDialog, choiceDialog }) {
	const target = wsId || shell.wsId;
	if (!target) return false;
	const ws = (shell.server.workspaces || []).find((w) => w.wsId === target) || { wsId: target, name: target };
	const check = await closingCheck(shell.client, ws);
	const title = "Close " + (ws.name || target);
	const message = closeMessage(check);
	if (check.unsaved.length) {
		const choice = await dialogs.choiceDialog({ title, message, choices: [{ label: "Save", value: "save", primary: true }, { label: "Don't Save", value: "discard" }] });
		if (!choice) return false;
		if (choice === "save") {
			try {
				await shell.client.request("doc.saveAll", { wsId: target });
			}
			catch (e) {
				shell.report("The files were not saved, so the workspace stays open: " + e.message, e);
				return false;
			}
		}
	}
	else if (!(await dialogs.confirmDialog({ title, message, confirm: "Close", danger: check.running.length > 0 }))) return false;
	try {
		await shell.client.request("ws.close", { wsId: target });
		return true;
	}
	catch (e) {
		shell.report("The workspace did not close: " + e.message, e);
		return false;
	}
}
