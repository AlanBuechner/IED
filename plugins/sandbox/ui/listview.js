// listview.js - the Sandbox activity's Sandboxes window (docs/34 "The Sandbox activity"): Docker's state and the
// platforms it runs, then the workspace's sandboxes - system, platform, state, the last validation, and a mark while the
// AI works in one. A sandbox opens as a document of its own (sandbox:box/<name>); New and Validate All are here.

import { h, icon, replace } from "/ui-kit/dom.js";
import { promptDialog } from "/ui-kit/dialog.js";
import { sandboxState, shortPlatform, validationOf } from "./model.js";
import { TONE, engineLine, platformChips, resultTone } from "./parts.js";

export class ListView {
	constructor(ctx, store, actions) {
		this.ctx = ctx;
		this.store = store;
		this.actions = actions;
		this.engine = h("div.sbx-engine");
		this.platforms = h("div.sbx-platforms");
		this.newButton = h("button.btn.flat.sbx-tool", { title: "New sandbox from the catalog", on: { click: () => actions.openNew() } }, icon("add"), h("span.sbx-tool-label", null, "New"));
		this.validateButton = h("button.btn.flat.sbx-tool", { title: "Run each sandbox's validate command, or one you give, in every sandbox", on: { click: () => actions.validateAll() } }, icon("beaker"), h("span.sbx-tool-label", null, "Validate All"));
		this.bar = h("div.sbx-bar", null, this.newButton, this.validateButton, h("span.sbx-spacer"),
			h("button.ib", { title: "Look at Docker again", "aria-label": "Refresh", on: { click: () => store.request("ied.sandbox/engine", { action: "refresh" }) } }, icon("refresh")));
		this.list = h("div.sbx-list", { role: "listbox", tabindex: "0", "aria-label": "Sandboxes" });
		this.root = h("div.sbx.sbx-listwin", null, this.engine, this.platforms, this.bar, this.list);
		replace(ctx.el, this.root);
		this.stop = store.on(() => this.render());
		this.render();
	}

	dispose() {
		this.stop();
	}

	visible(shown) {
		if (shown) this.render();
	}

	render() {
		const store = this.store;
		replace(this.engine, engineLine(store));
		replace(this.platforms, platformChips(store.engine()));
		this.platforms.hidden = !store.ready();
		const trusted = this.actions.trusted();
		this.newButton.disabled = !trusted;
		const validation = store.snapshot && store.snapshot.validation;
		this.validateButton.disabled = !(store.ready() && trusted && store.sandboxes().length) || !!(validation && validation.running);
		const boxes = store.sandboxes();
		if (!store.snapshot) {
			replace(this.list, h("div.sbx-empty.muted", null, "Loading..."));
			return;
		}
		if (!boxes.length) {
			replace(this.list, h("div.sbx-empty", null,
				h("div", null, "No sandboxes yet."),
				h("div.muted", null, "A sandbox is a Linux system - any distribution, on this processor or an emulated one - that builds and runs this workspace's code."),
				trusted ? h("button.btn.primary", { on: { click: () => this.actions.openNew() } }, icon("add"), "New Sandbox") : h("div.sbx-warn-line", null, "Trust the workspace first: sandboxes run its code.")));
			return;
		}
		const aiIn = new Set(store.runs().filter((run) => run.running && run.by === "ai").map((run) => run.sandbox));
		replace(this.list, boxes.map((box) => {
			const state = sandboxState(box, store.engine());
			const result = validationOf(validation, box.name);
			return h("div.sbx-row" + (box.name === this.actions.openSandbox() ? ".sel" : ""), {
				role: "option", dataset: { name: box.name }, title: box.description || "",
				on: { click: () => this.actions.openBox(box.name, true), dblclick: () => this.actions.openBox(box.name, false) },
			},
			h("span.sbx-row-icon." + TONE[state.tone], null, icon(state.icon, state.spin ? "spin" : undefined)),
			h("div.sbx-row-main", null,
				h("div.sbx-row-name", null, box.name, aiIn.has(box.name) ? h("span.sbx-ai-mark", { title: "The AI is running something in it" }, icon("sparkle"), "AI") : null),
				h("div.sbx-row-desc.muted", null, (box.os || box.image) + " \u00b7 " + shortPlatform(box.platform) + (box.emulated ? " (emulated)" : ""))),
			box.desktop ? h("button.ib.sbx-row-desk", { title: "Its desktop: watch the AI or take control", "aria-label": "Desktop", on: { click: (e) => { e.stopPropagation(); this.actions.openDesktop(box.name); } } }, icon("vm-connect")) : null,
			h("div.sbx-row-side", null,
				h("span.sbx-state." + TONE[state.tone], null, state.text),
				result ? h("span.sbx-badge." + resultTone(result.result), { title: "Last validation: " + result.result }, result.result === "fail" ? "FAIL" : result.result) : null));
		}));
	}
}

// Asks for the command Validate All runs where a sandbox has no validate of its own; null when the owner cancels.
export async function askValidateCommand(store, lastCommand) {
	const boxes = store.sandboxes();
	if (!boxes.some((box) => !box.validate)) return "";
	const last = store.snapshot && store.snapshot.validation && store.snapshot.validation.command;
	return promptDialog({
		title: "Validate in all sandboxes",
		message: "The command to run in each sandbox (sandboxes with a validate command of their own run it when this is empty). Build into build/$IED_SANDBOX: sandboxes share the workspace and run at once.",
		value: last || lastCommand || "",
		placeholder: "cmake -B build/$IED_SANDBOX && cmake --build build/$IED_SANDBOX && ctest --test-dir build/$IED_SANDBOX",
	});
}
