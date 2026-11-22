// shownworkspace.js - which workspace a window shows when it starts or its server comes back (docs/15 "The address",
// docs/10 "Workspaces"). A workspace's id is its folder's for good (the master keeps them), but a window also keeps the
// folder it showed: an id that names another folder now (a master that lost its workspaces.json) is never taken for
// this window's, so a reconnecting page never goes on showing - and editing - another project under its old tabs.

// Two spellings of one folder ("C:\Work\Game", "c:/work/game/") are one root, as the server's TrustKey has it: case
// folded only for a Windows root (a drive letter, a \\server share), whose names ignore it - on Linux "/home/a/Game"
// and "/home/a/game" are two folders (src/fs/PathCase.ch).
export function rootKey(root) {
	const path = String(root || "").replace(/\\/g, "/").replace(/\/+$/, "");
	return /^([A-Za-z]:|\/\/)/.test(path) ? path.toLowerCase() : path;
}

export function sameRoot(a, b) {
	return Boolean(a) && Boolean(b) && rootKey(a) === rootKey(b);
}

// The workspace to show among `workspaces` ([{wsId, root}]): the one a launch asked for (`launched`, an `#open=`),
// else the address's (`asked`, `?ws=`), else the tab's last (`remembered`) - each only while it is still the folder the
// tab showed (`root`, when known) - else that folder under whatever id it has, else the first. Null with none open.
export function preferredWorkspaceOf({ workspaces, launched, asked, remembered, root }) {
	const listed = (id) => workspaces.find((w) => w.wsId === id) || null;
	const fits = (id) => {
		const ws = id ? listed(id) : null;
		return Boolean(ws) && (!root || sameRoot(ws.root, root));
	};
	if (launched && listed(launched)) return launched;
	const kept = [asked, remembered].find(fits);
	if (kept) return kept;
	const byRoot = root ? workspaces.find((w) => sameRoot(w.root, root)) : null;
	if (byRoot) return byRoot.wsId;
	return workspaces.length ? workspaces[0].wsId : null;
}

// Whether the server's description of `wsId` (its `root`) is another folder than the one this window shows (`shown`).
export function namesAnotherFolder(shown, root) {
	return Boolean(shown) && Boolean(root) && !sameRoot(shown, root);
}
