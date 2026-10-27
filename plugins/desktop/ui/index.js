// index.js - the desktop app's UI module (docs/27): window.iedDesktop is there only inside ied-desktop, which adds it
// before the page's scripts, and the shell then sets the `desktopApp` context key that shows this plugin's commands
// (and the shell's own File > Open Folder). A new window opens on the workspace this one shows; Open in Web opens it in
// the system's default browser (the app asks the server for a fresh one-time sign-in URL for the workspace's root).
// In a browser there is no app to ask, so Open in Web opens the same page in a window of the browser's own.

export function activate(ied) {
	const desktop = window.iedDesktop;
	if (!desktop) {
		ied.commands.handle("ied.desktop.openInWeb", () => openInThisBrowser(ied));
		return;
	}
	ied.commands.handle("ied.desktop.newWindow", () => desktop.newWindow(workspaceUrl(location.origin, ied.workspace.wsId)));
	ied.commands.handle("ied.desktop.openInWeb", () => openInWeb(ied, desktop) || ied.toast("No workspace is shown, so there is nothing to open in a browser.", { level: "info" }));
	ied.commands.handle("ied.desktop.devTools", () => desktop.devTools());
}

// The shown workspace, in the default browser: the app's openInWeb asks the server for the workspace's root. False when
// none is shown (a start page: nothing to open).
export function openInWeb(ied, desktop) {
	const root = ied.workspace.root;
	if (!root) return false;
	desktop.openInWeb(root);
	return true;
}

// Already in a browser: a window of this one, signed in as this page is.
function openInThisBrowser(ied) {
	window.open(workspaceUrl(location.origin, ied.workspace.wsId), "_blank");
	return true;
}

// The web client on `wsId` ("#open=<wsId>/", as `ied <folder>` opens it), or IED's home without one.
export function workspaceUrl(origin, wsId) {
	return origin + "/" + (wsId ? "#open=" + encodeURIComponent(wsId) + "/" : "");
}
