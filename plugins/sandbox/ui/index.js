// index.js - the sandbox plugin's UI module (docs/34 "The Sandbox activity"): Sandbox, an activity of the sidebar of its
// own - the Sandboxes window beside the document well, which shows Activity (what runs in the sandboxes and who started
// it: the AI, in which chat, or you) and Validation (the last pass/fail matrix); a sandbox opens as a document
// (sandbox:box/<name>), as does New Sandbox (sandbox:new). The sidebar button carries a badge while the AI runs
// something in a sandbox. What every view shows is the server's (topic sandbox:<wsId>, shared through store.js), so a
// closed browser loses nothing.

import { h, replace } from "/ui-kit/dom.js";
import { ActivityView } from "./activityview.js";
import { DesktopView } from "./desktopview.js";
import { ListView, askValidateCommand } from "./listview.js";
import { activityBadge } from "./model.js";
import { NewPage, SandboxPage, ValidationView } from "./pages.js";
import { addStylesheet } from "./parts.js";
import { storeFor } from "./store.js";

const ACTIVITY = "ied.sandbox";
const ACTIVITY_WINDOW = "ied.sandbox.activity";
const VALIDATION_WINDOW = "ied.sandbox.validation";

let api = null;
let openedBox = null;   // the sandbox whose page was opened last, for the list's selection

function wsIdOf(ctx) {
	return (ctx && ctx.wsId) || api.workspace.wsId;
}

function store(ctx) {
	return storeFor(api, wsIdOf(ctx));
}

async function showActivity() {
	await api.commands.run("shell.showActivity", { id: ACTIVITY });
}

async function showWindow(id) {
	await showActivity();
	await api.commands.run("shell.showView", { id });
}

// What the views ask of each other and of the shell.
const actions = {
	lastCommand: "",
	trusted: () => (api.workspace ? api.workspace.trusted !== false : true),
	openSandbox: () => openedBox,
	async openNew() {
		await showActivity();
		api.tabs.open("sandbox:new", { preview: false, activity: ACTIVITY });
	},
	async openBox(name, preview) {
		openedBox = name;
		await showActivity();
		api.tabs.open("sandbox:box/" + name, { preview, activity: ACTIVITY });
	},
	async openDesktop(name) {
		await showActivity();
		api.tabs.open("sandbox:desktop/" + name, { preview: false, activity: ACTIVITY });
	},
	async showRun(runId) {
		storeFor(api, api.workspace.wsId).showRun(runId);
		await showWindow(ACTIVITY_WINDOW);
	},
	async validate(names) {
		const s = storeFor(api, api.workspace.wsId);
		await showWindow(VALIDATION_WINDOW);
		await s.request("ied.sandbox/validate", { names });
	},
	async validateAll() {
		const s = storeFor(api, api.workspace.wsId);
		if (!s.sandboxes().length) {
			await actions.openNew();
			return;
		}
		const command = await askValidateCommand(s, actions.lastCommand);
		if (command === null || command === undefined) return;
		if (command) actions.lastCommand = command;
		await showWindow(VALIDATION_WINDOW);
		await s.request("ied.sandbox/validate", { command });
	},
	async terminal(name) {
		const s = storeFor(api, api.workspace.wsId);
		const box = s.sandbox(name);
		if (box && box.container !== "running") await s.request("ied.sandbox/control", { name, action: "start" });
		const started = Date.now();
		const tick = () => {
			const now = s.sandbox(name);
			if (now && now.container === "running" && !now.busy) api.commands.run("ied.terminal.new", { profile: "sandbox-" + name });
			else if (Date.now() - started < 120000 && !(now && now.problem)) setTimeout(tick, 500);
		};
		tick();
	},
	openDefinition(box) {
		if (api.files && api.files.open) api.files.open(box.definition, { preview: false });
	},
};

export function activate(ied) {
	api = ied;
	addStylesheet(ied.plugin.base + "ui/sandbox.css");
	ied.tabs.kind("sandbox", {
		view: "SandboxDocument",
		title: (arg) => (arg && arg.startsWith("box/") ? arg.slice(4) : arg && arg.startsWith("desktop/") ? arg.slice(8) + " desktop" : arg === "new" ? "New Sandbox" : "Sandbox"),
		icon: (arg) => (arg === "new" ? "$(add)" : arg && arg.startsWith("desktop/") ? "$(vm-connect)" : "$(vm)"),
	});
	ied.commands.handle("ied.sandbox.open", () => showWindow(ACTIVITY_WINDOW));
	ied.commands.handle("ied.sandbox.new", () => actions.openNew());
	ied.commands.handle("ied.sandbox.validateAll", () => actions.validateAll());
	badgeAiRuns(ied);
}

// The Sandbox button's badge: how many commands the AI is running in sandboxes now.
function badgeAiRuns(ied) {
	let stop = null;
	const follow = () => {
		if (stop) stop();
		stop = null;
		ied.activities.badge(ACTIVITY, null);
		const wsId = ied.workspace.wsId;
		if (!wsId) return;
		const s = storeFor(ied, wsId);
		const render = () => ied.activities.badge(ACTIVITY, activityBadge(s.runs()));
		stop = s.on(render);
		render();
	};
	if (ied.workspace.on) ied.workspace.on(follow);
	follow();
}

export function SandboxList(ctx) {
	return new ListView(ctx, store(ctx), actions);
}

export function SandboxActivity(ctx) {
	return new ActivityView(ctx, store(ctx), actions);
}

export function SandboxValidation(ctx) {
	return new ValidationView(ctx, store(ctx), actions);
}

export function SandboxDocument(ctx) {
	const arg = String(ctx.ref || "").slice("sandbox:".length);
	if (arg.startsWith("box/")) {
		openedBox = arg.slice(4);
		return new SandboxPage(ctx, store(ctx), actions, arg.slice(4));
	}
	if (arg.startsWith("desktop/")) return new DesktopView(ctx, store(ctx), actions, arg.slice(8));
	if (arg === "new") return new NewPage(ctx, store(ctx), actions);
	// An older tab (sandbox:manager, before the activity): it says where things went.
	replace(ctx.el, h("div.sbx-empty", null, h("div", null, "Sandboxes have an activity of their own now: the Sandbox button in the sidebar."),
		h("button.btn.primary", { on: { click: () => { showWindow(ACTIVITY_WINDOW); if (ctx.tab) ctx.tab.close(); } } }, "Open Sandbox")));
	return {};
}
