// index.js - the nuke plugin's UI module: the Package Manager (docs/31 section 8), a tab like Visual Studio's NuGet
// window over the workspace's Nuke packages - the ones it uses (Installed), those with newer releases (Updates), what a
// source offers (Browse) and the sources - opened from Tools > Manage Packages... or from a solution, project or
// package in Solution Explorer. What it shows lives on the server (the plugin's code, topic packages:<wsId>), so a
// closed browser loses nothing: an action keeps running and its output is there when the tab opens again.

import { PackagesView } from "./packages.js";

export const PACKAGES_TAB = "packages:manager";

const views = new Set();
let wanted = null;   // the package the last Manage Packages... named, for the view it opens

export function activate(ied) {
	addStylesheet(ied.plugin.base + "ui/packages.css");
	ied.tabs.kind("packages", { view: "PackagesTab", title: () => "Packages", icon: () => "$(package)" });
	ied.commands.handle("ied.nuke.packages", (args) => {
		wanted = args && args.kind === "package" ? args.name : null;
		for (const view of views) view.reveal(wanted);
		ied.tabs.open(PACKAGES_TAB, { preview: false });
	});
}

export function PackagesTab(ctx) {
	const view = new PackagesView(ctx, wanted, () => views.delete(view));
	views.add(view);
	return view;
}

function addStylesheet(href) {
	if (document.querySelector('link[href="' + href + '"]')) return;
	document.head.append(Object.assign(document.createElement("link"), { rel: "stylesheet", href }));
}
