// settingsoverlay.js - Settings over the whole shell (owner, 2026-11-11: "re work the settings to be an overlay rather
// then a tab in the text editor"; docs/07 "The settings window"): the settings window (settingsview.js) in a panel over a
// veil, Visual Studio's Tools > Options shown where the person is instead of a tab among their documents - a full
// screen sheet on a phone. Changes apply as they are made (the server owns them), so there is no OK or Cancel: Esc, the
// close button or a click on the veil closes it, and the focus goes back where it was. It opens on the page it was
// left on (per device) unless asked for another, on the shown workspace's tiers - only the User tier without one (the
// start page). The shell's keys wait while it is up; its own (Ctrl+E, the fields') work.

import { h, icon } from "/ui-kit/dom.js";
import { settingsView } from "./settingsview.js";

const PAGE_KEY = "ied.settings.page";

function remembered() {
	try {
		return JSON.parse(localStorage.getItem(PAGE_KEY) || "null");
	}
	catch {
		return null;
	}
}

function remember(page) {
	try {
		localStorage.setItem(PAGE_KEY, JSON.stringify(page));
	}
	catch {
		// a private window: it opens on the first page next time
	}
}

export class SettingsOverlay {
	constructor(shell) {
		this.shell = shell;
		this.open = null;   // {veil, view, wsId, before} while it is shown
	}

	get shown() {
		return this.open !== null;
	}

	// Shows the settings on `page` ("custom:shell.keyboard", "page:AI"; the last page shown when none is given), editing
	// `tier` ("app", "solution", "project") of `project`. Shown already for this workspace, it moves there.
	show(page, tier, project) {
		const wsId = this.shell.wsId || null;
		if (this.open && this.open.wsId === wsId) {
			this.open.view.show(page, tier, project);
			this.open.view.focus();
			return;
		}
		this.close();
		const shell = this.shell;
		shell.nextSettingsPage = page || remembered();
		shell.nextSettingsTier = wsId ? tier || null : "app";
		shell.nextSettingsProject = project || null;
		const body = h("div.settings-overlay-body");
		const where = wsId ? this.workspaceName(wsId) : "User settings: open a project for its solution and project tiers";
		const close = h("button.ib.settings-overlay-close", { title: "Close (Esc)", "aria-label": "Close settings", on: { click: () => this.close() } }, icon("close"));
		const panel = h("div.settings-overlay", { role: "dialog", "aria-modal": "true", "aria-label": "Settings" },
			h("div.settings-overlay-head", null, h("span.settings-overlay-title", null, icon("settings-gear"), "Settings"), h("span.muted.settings-overlay-where", null, where), h("span.grow"), close),
			body);
		const veil = h("div.settings-veil", { on: {
			mousedown: (event) => {
				if (event.target === veil) this.close();
			},
			keydown: (event) => {
				if (event.key === "Escape" && !event.defaultPrevented) {
					event.preventDefault();
					this.close();
				}
				else if (event.key === "Tab") this.trapTab(event, panel);
			},
		} }, panel);
		const before = document.activeElement;
		document.body.append(veil);
		const view = settingsView(shell, { el: body, ref: "shell:settings", wsId, ied: shell.shellApi });
		this.open = { veil, view, wsId, before };
		requestAnimationFrame(() => {
			if (this.open && this.open.view === view) view.focus();
		});
	}

	// The name the shown workspace goes by, as the title bar says it.
	workspaceName(wsId) {
		const ws = this.shell.server && (this.shell.server.workspaces || []).find((w) => w.wsId === wsId);
		return ws ? ws.name || ws.root || "" : "";
	}

	// Tab and Shift+Tab stay inside the panel while it is up.
	trapTab(event, panel) {
		const focusable = [...panel.querySelectorAll("button, input, select, textarea, [tabindex]:not([tabindex='-1'])")].filter((el) => !el.disabled && el.offsetParent !== null);
		if (!focusable.length) return;
		const first = focusable[0];
		const last = focusable[focusable.length - 1];
		if (event.shiftKey && document.activeElement === first) {
			event.preventDefault();
			last.focus();
		}
		else if (!event.shiftKey && document.activeElement === last) {
			event.preventDefault();
			first.focus();
		}
	}

	close() {
		if (!this.open) return;
		const { veil, view, before } = this.open;
		this.open = null;
		remember(view.pageId);
		view.dispose();
		veil.remove();
		if (before && before.isConnected && typeof before.focus === "function") before.focus({ preventScroll: true });
	}
}
