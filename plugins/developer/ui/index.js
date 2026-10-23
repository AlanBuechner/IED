// index.js - the developer plugin's UI module (docs/28): Settings > Environment > Developer (developer.enabled, and
// Rebuild and relaunch of the desktop build), the status bar's DEV while chats may fix IED's own source - naming the
// source, a rebuild running or failed, and how far the checkout is ahead of the install - and the command that opens
// its settings.

import { h, icon, replace } from "/ui-kit/dom.js";
import { BuildPage } from "./buildpage.js";

const PAGE = "custom:ied.developer";

export function activate(ied) {
	addStylesheet(ied.plugin.base + "ui/developer.css");
	ied.commands.handle("ied.developer.settings", () => ied.commands.run("shell.settings", { page: PAGE }));
}

export function DeveloperSettings(ctx) {
	addStylesheet(ctx.ied.plugin.base + "ui/developer.css");
	return new BuildPage(ctx);
}

// What DEV says of the build: [label, color class, tooltip line].
export function statusOf(state) {
	const build = state.build || { state: "idle" };
	if (build.state === "running") return ["DEV: building", "c-info", "Rebuilding the desktop build from the checkout"];
	if (build.state === "failed") return ["DEV: build failed", "c-error", "The last rebuild failed (exit " + build.exitCode + "); its log is in Settings > Environment > Developer"];
	if (state.install && state.behind > 0) return ["DEV", "", "The checkout is " + state.behind + (state.behind === 1 ? " commit" : " commits") + " newer than the desktop build"];
	return ["DEV", "", ""];
}

export function DeveloperStatus(ctx) {
	let state = { source: "" };
	let enabled = false;
	const render = () => {
		const [label, color, line] = statusOf(state);
		ctx.el.hidden = !state.source || !enabled;
		ctx.el.title = "Developer mode: chats may fix IED and the C4 toolchain in " + state.source + " (writes and commands there follow each chat's permission mode)" + (line ? "\n" + line : "");
		replace(ctx.el, icon(state.build && state.build.state === "running" ? "sync" : "tools"), h("span" + (color ? "." + color : ""), null, label));
	};
	const stopState = ctx.ied.client.subscribe("ied.developer/state", {
		snap: (data) => {
			state = data;
			render();
		},
		evt: (kind, data) => {
			if (kind !== "changed") return;
			state = data;
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

// developer.enabled is off unless set: it is offered only where IED is built from source.
export function settingOf(values) {
	const setting = values && values["developer.enabled"];
	return Boolean(setting && setting.value === true);
}

function addStylesheet(href) {
	if (document.querySelector('link[href="' + href + '"]')) return;
	document.head.append(Object.assign(document.createElement("link"), { rel: "stylesheet", href }));
}
