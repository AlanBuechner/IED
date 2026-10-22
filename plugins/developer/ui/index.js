// index.js - the developer plugin's UI module (docs/28): the status bar's DEV while chats may fix IED's own source (IED
// runs from its checkout and developer.enabled holds), naming the source in its tooltip, and the command that opens
// its settings.

import { h, icon, replace } from "/ui-kit/dom.js";

export function activate(ied) {
	ied.commands.handle("ied.developer.settings", () => ied.commands.run("shell.settings", { page: "page:Environment/Developer" }));
}

export function DeveloperStatus(ctx) {
	let source = "";
	let enabled = true;
	const render = () => {
		ctx.el.hidden = !source || !enabled;
		ctx.el.title = "Developer mode: chats may fix IED and the C4 toolchain in " + source + " (every write and command there asks first)";
		replace(ctx.el, icon("tools"), h("span", null, "DEV"));
	};
	const stopState = ctx.ied.client.subscribe("ied.developer/state", {
		snap: (data) => {
			source = data.source || "";
			render();
		},
	});
	const stopSettings = ctx.ied.client.subscribe("settings:" + ctx.wsId, {
		snap: (data) => {
			enabled = settingOf(data.values);
			render();
		},
		evt: (kind, data) => {
			if (kind === "settings.changed" && data.values) enabled = settingOf(data.values);
			render();
		},
	});
	render();
	return { dispose: () => {
		stopState();
		stopSettings();
	} };
}

function settingOf(values) {
	const setting = values && values["developer.enabled"];
	return !setting || setting.value !== false;
}
