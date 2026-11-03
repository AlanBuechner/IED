// activitydrag.js - a sidebar tab dragged to another window (docs/15 "Windows"), by the browser's own drag and drop, the only
// kind that crosses windows. A window of the same workspace is one whole drop target, over its iframes too; let go outside
// every window, the tab gets a window of its own there. Types are all another window can read before the drop, so the
// workspace is one of them.

import { h } from "/ui-kit/dom.js";

const TYPE = "application/x-ied-activity";

function workspaceType(wsId) {
	return TYPE + "." + String(wsId).toLowerCase();
}

export function startActivityDrag(event, { activity, wsId, device }) {
	const transfer = event.dataTransfer;
	transfer.effectAllowed = "move";
	transfer.setData(TYPE, JSON.stringify({ activity, wsId, device }));
	transfer.setData(workspaceType(wsId), "1");
}

// Where on the screen a drag was let go when no window took it and the pointer had left this one: {x, y}, else null.
export function droppedOutside(event, page = window) {
	if (event.dataTransfer && event.dataTransfer.dropEffect !== "none") return null;
	const inside = event.clientX >= 0 && event.clientY >= 0 && event.clientX < page.innerWidth && event.clientY < page.innerHeight;
	return inside ? null : { x: event.screenX, y: event.screenY };
}

export class WindowDropZone {
	constructor(shell) {
		this.shell = shell;
		this.own = false;   // the drag started in this window: it has the tab already
		this.el = h("div.window-drop", {
			hidden: true,
			on: {
				dragover: (event) => this.over(event),
				dragleave: () => this.hide(),
				drop: (event) => this.dropped(event),
			},
		}, h("div.window-drop-label", null, "Dock to this window"));
		this.enter = (event) => this.entered(event);
		document.addEventListener("dragenter", this.enter);
		document.body.append(this.el);
	}

	dispose() {
		document.removeEventListener("dragenter", this.enter);
		this.el.remove();
	}

	takes(event) {
		const { wsId } = this.shell;
		return !this.own && Boolean(wsId) && [...event.dataTransfer.types].includes(workspaceType(wsId));
	}

	entered(event) {
		if (!this.takes(event)) return;
		event.preventDefault();
		this.el.hidden = false;
	}

	over(event) {
		if (!this.takes(event)) return;
		event.preventDefault();
		event.dataTransfer.dropEffect = "move";
	}

	hide() {
		this.el.hidden = true;
	}

	dropped(event) {
		event.preventDefault();
		this.hide();
		const { windows, toasts, contributions } = this.shell;
		const data = parsedDrag(event.dataTransfer.getData(TYPE));
		if (!data || !contributions.activity(data.activity)) return;
		if (data.device === windows.device) windows.moveHere(data.activity);
		else toasts.show("That tab belongs to a window of another browser: it can only move between the windows of one.", "info");
	}
}

function parsedDrag(text) {
	try {
		return JSON.parse(text);
	}
	catch {
		return null;
	}
}
