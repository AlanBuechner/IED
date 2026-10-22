// untitled.js - a new file as a browser's new tab (docs/15 "Docking"): "+" on a document strip or Ctrl+N opens
// Untitled-N.txt, a file of the workspace's local state (.ied/local/untitled/), so the server keeps it like any
// document - saved as it is typed, there after a restart or on another device. Saving it asks where it goes in the
// workspace and moves it there with its unsaved edits (fs.rename: the document and its tab follow). Closing an empty one
// deletes it; one with text is kept, and the toast reopens it.

import { promptDialog } from "/ui-kit/dialog.js";

export const UNTITLED_FOLDER = ".ied/local/untitled";
const NAME = /^Untitled-(\d+)\.txt$/;

export function isUntitled(path) {
	return Boolean(path) && path.startsWith(UNTITLED_FOLDER + "/");
}

// The names of the untitled files there are, and their sizes: {name: size}.
async function untitledFiles(ied) {
	try {
		const listed = await ied.client.request("fs.list", { wsId: ied.workspace.wsId, path: UNTITLED_FOLDER });
		return Object.fromEntries(listed.entries.filter((e) => e.kind === "file").map((e) => [e.name, e.size || 0]));
	}
	catch {
		return {};
	}
}

// The first Untitled-N.txt no file and no open tab has.
export function freeName(names) {
	const taken = new Set(names.map((name) => NAME.exec(name)).filter(Boolean).map((m) => Number(m[1])));
	let n = 1;
	while (taken.has(n)) n++;
	return "Untitled-" + n + ".txt";
}

// A new untitled file, opened in the focused document group.
export async function newUntitled(ied) {
	const open = ied.tabs.state().open.filter((ref) => ref.startsWith("doc:" + UNTITLED_FOLDER + "/")).map((ref) => ref.slice(ref.lastIndexOf("/") + 1));
	const path = UNTITLED_FOLDER + "/" + freeName([...Object.keys(await untitledFiles(ied)), ...open]);
	try {
		await ied.client.request("fs.write", { wsId: ied.workspace.wsId, path, text: "", expectedRevision: 0 });
		ied.tabs.open("doc:" + path, { preview: false });
	}
	catch (e) {
		ied.report("a new file: " + e.message, e);
	}
}

// Saving an untitled file: where it goes in the workspace, then moved there and saved. False when that was cancelled.
export async function saveUntitledAs(ied, path, save) {
	const name = path.slice(path.lastIndexOf("/") + 1);
	const target = await promptDialog({ title: "Save As", message: "Its path in the workspace:", value: name, confirm: "Save",
		check: (text) => (/(^|\/)\.\.(\/|$)/.test(text) || text.startsWith("/") || isUntitled(text) ? "A path inside the workspace, like src/notes.md" : "") });
	if (!target) return false;
	try {
		await ied.client.request("fs.rename", { wsId: ied.workspace.wsId, from: path, to: target });
	}
	catch (e) {
		ied.toast("Not saved as " + target + ": " + e.message, { level: "error" });
		return false;
	}
	await save();
	return true;
}

// An untitled tab was closed: an empty file goes; one with text stays, and the toast offers to reopen it.
export async function untitledClosed(ied, ref) {
	const path = ref.slice("doc:".length);
	const name = path.slice(path.lastIndexOf("/") + 1);
	const files = await untitledFiles(ied);
	if (!(name in files)) return;
	if (files[name] === 0) {
		ied.client.request("fs.delete", { wsId: ied.workspace.wsId, path }).catch(() => {});
		return;
	}
	ied.toast(name + " was closed without being saved to the workspace; it is kept.", { level: "info", action: { label: "Reopen", run: () => ied.tabs.open(ref, { preview: false }) } });
}
