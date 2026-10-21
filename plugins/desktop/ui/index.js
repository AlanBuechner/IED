// index.js - the desktop app's UI module (docs/27): window.iedDesktop is there only inside ied-desktop, which adds it
// before the page's scripts; then the `desktopApp` context key shows this plugin's commands.

export function activate(ied) {
	const desktop = window.iedDesktop;
	ied.context.set("desktopApp", Boolean(desktop));
	if (!desktop) return;
	ied.commands.handle("ied.desktop.newWindow", () => desktop.newWindow(""));
	ied.commands.handle("ied.desktop.devTools", () => desktop.devTools());
}
