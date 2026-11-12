// index.js - the sandbox plugin's UI module (docs/34 "The Sandboxes window"): one tab, sandbox:manager, over the
// workspace's sandboxes - Docker's state, the list, a sandbox's details, the New Sandbox form, the validation matrix and
// the output of what runs. What it shows is the server's (the plugin's code, topic sandbox:<wsId>), so a closed browser
// loses nothing: a build or a validation goes on, and its output is there when the tab opens again.

import { SandboxesView } from "./sandboxes.js";

export const SANDBOX_TAB = "sandbox:manager";

const views = new Set();
let pending = null;   // what the command that opened the tab asked for: "new" or "validate"

export function activate(ied) {
	addStylesheet(ied.plugin.base + "ui/sandbox.css");
	ied.tabs.kind("sandbox", { view: "SandboxTab", title: () => "Sandboxes", icon: () => "$(vm)" });
	const open = (what) => {
		pending = what;
		for (const view of views) view.ask(what);
		if (views.size) pending = null;
		ied.tabs.open(SANDBOX_TAB, { preview: false });
	};
	ied.commands.handle("ied.sandbox.open", () => open(null));
	ied.commands.handle("ied.sandbox.new", () => open("new"));
	ied.commands.handle("ied.sandbox.validateAll", () => open("validate"));
}

export function SandboxTab(ctx) {
	const view = new SandboxesView(ctx, () => views.delete(view));
	views.add(view);
	if (pending) {
		view.ask(pending);
		pending = null;
	}
	return view;
}

function addStylesheet(href) {
	if (document.querySelector('link[href="' + href + '"]')) return;
	document.head.append(Object.assign(document.createElement("link"), { rel: "stylesheet", href }));
}
