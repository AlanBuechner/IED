// startsettings.js - Settings without a workspace (owner, 2026-10-26: "we should be able to edit user settings without
// haveing a project open"; docs/07 "The settings window"): the start page has no layout to put a tab in, so it shows the
// settings window in its own place, on the User tier - the only one there is without a workspace - with a way back to
// the start page. Settings > ..., the sidebar's Manage menu, the start page's Settings button and Ctrl+, there (VS's
// Go To All while a workspace is shown) all come here.

import { h, icon } from "/ui-kit/dom.js";
import { settingsView } from "./settingsview.js";

export class StartSettings {
	constructor(shell) {
		this.shell = shell;
		this.view = null;    // {el, instance} while the start page shows the settings
	}

	get shown() {
		return this.view !== null;
	}

	// Shows the settings in the start page's place, on `page` when one is given.
	show(page) {
		if (this.view) this.view.instance.show(page, "app");
		else {
			this.shell.nextSettingsPage = page || null;
			this.shell.nextSettingsTier = "app";
			this.shell.nextSettingsProject = null;
			const body = h("div.start-settings-body");
			const instance = settingsView(this.shell, { el: body, ref: "shell:settings", wsId: null, ied: this.shell.shellApi });
			const back = h("button.btn.flat", { title: "Back to the start page", on: { click: () => this.close(true) } }, icon("arrow-left"), "Start Page");
			const el = h("div.start-settings", null, h("div.start-settings-head", null, back, h("span.start-settings-title", null, icon("settings-gear"), "Settings"), h("span.muted", null, "User settings: open a project for its solution and project tiers")), body);
			this.view = { el, instance };
		}
		this.shell.chrome.startPage();
	}

	// The element the start page shows instead of its lists.
	element() {
		return this.view ? this.view.el : null;
	}

	// Gone (a workspace shown, or Back): the start page again when `render`.
	close(render) {
		if (!this.view) return;
		const view = this.view;
		this.view = null;
		if (view.instance && view.instance.dispose) view.instance.dispose();
		if (render && this.shell.chrome && this.shell.showingStartPage) this.shell.chrome.startPage();
	}
}
