// index.js - the desktop app's UI module (docs/27): window.iedDesktop is there only inside ied-desktop, which adds it
// before the page's scripts; then the `desktopApp` context key shows this plugin's commands. A new window opens on the
// workspace this one shows.

export function activate(ied) {
	const desktop = window.iedDesktop;
	ied.context.set("desktopApp", Boolean(desktop));
	if (!desktop) return;
	ied.commands.handle("ied.desktop.newWindow", () => desktop.newWindow(workspaceUrl(location.origin, ied.workspace.wsId)));
	ied.commands.handle("ied.desktop.devTools", () => desktop.devTools());
}

// The web client on `wsId` ("#open=<wsId>/", as `ied <folder>` opens it), or IED's home without one.
export function workspaceUrl(origin, wsId) {
	return origin + "/" + (wsId ? "#open=" + encodeURIComponent(wsId) + "/" : "");
}
