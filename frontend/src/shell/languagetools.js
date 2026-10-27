// languagetools.js - Settings > Languages > Tools (docs/26 "Language tools", docs/07): every tool the workspace's language
// plugins name - its compiler, language server, debugger - with what IED found (version, where), the version the
// workspace pins, and Install for one or for every missing tool; the install runs on the server, its output in the
// Output window's Language Tools source. installTools is also the workspace banner's Install (banners.js).

import { h, icon, replace } from "/ui-kit/dom.js";
import { confirmDialog } from "/ui-kit/dialog.js";
import { installPlan, kindText, lacking, pinText, stateText, toolGroups, userScopeQuestion } from "./toolsmodel.js";

const CRUMB = "Languages " + String.fromCharCode(0x203a) + " Tools";

// Installs `ids` (empty: every tool the workspace lacks) with what they need first, asking before steps that write
// outside IED's tools folder; true when the install started.
export async function installTools(shell, wsId, snapshot, ids) {
	const plan = installPlan(snapshot, ids);
	if (!plan.length) {
		shell.toasts.show("No tool to install: IED cannot install what is missing; Settings > Languages says how to get it.", "info");
		return false;
	}
	const question = userScopeQuestion(snapshot, plan);
	if (question && !(await confirmDialog(question))) return false;
	try {
		await shell.client.request("tools.install", { wsId, tools: plan, allowUserScope: Boolean(question) });
		shell.toasts.show("Installing " + plan.length + (plan.length === 1 ? " tool" : " tools") + ".", "info", { label: "Show Output", run: () => shell.commands.run("ied.output.show", { sourceId: "tools" }) });
		return true;
	}
	catch (e) {
		shell.report("The install did not start: " + e.message, e);
		return false;
	}
}

export class LanguageToolsPage {
	constructor(shell, ctx) {
		this.shell = shell;
		this.ctx = ctx;
		this.snapshot = null;
		this.status = h("span.lt-status.muted", { role: "status" });
		this.installAll = h("button.btn.primary", { on: { click: () => installTools(shell, ctx.wsId, this.snapshot, []) } }, icon("cloud-download"), "Install Missing");
		this.check = h("button.btn", { on: { click: () => this.detect() } }, icon("refresh"), "Check Again");
		this.output = h("button.btn.flat", { on: { click: () => shell.commands.run("ied.output.show", { sourceId: "tools" }) } }, icon("output"), "Output");
		this.list = h("div.lt-list", { role: "table", "aria-label": "Language tools" });
		ctx.el.append(h("div.lt-page", null,
			h("div.set-crumbs", null, CRUMB),
			h("h1", null, "Language Tools"),
			h("div.set-desc", null, "The compilers, language servers and debuggers the workspace's languages need, with the version IED found and the one the workspace pins. Install puts what is missing into IED's tools folder (or, asked first, where its makers install it for your user); a setting naming a program wins over all of this."),
			h("div.lt-tools", null, this.installAll, this.check, this.output, this.status),
			this.list));
		this.stop = shell.client.subscribe("tools:" + ctx.wsId, {
			snap: (data) => this.received(data),
			evt: (kind, data) => kind === "tools.changed" && this.received(data),
		});
	}

	dispose() {
		this.stop();
	}

	received(data) {
		this.snapshot = data;
		this.render();
	}

	async detect() {
		try {
			await this.shell.client.request("tools.detect", { wsId: this.ctx.wsId });
		}
		catch (e) {
			this.shell.report("tools.detect: " + e.message, e);
		}
	}

	render() {
		const snapshot = this.snapshot;
		const missing = snapshot.tools.filter(lacking);
		const busy = Boolean(snapshot.install || snapshot.busy);
		this.installAll.disabled = busy || !snapshot.trusted || installPlan(snapshot, []).length === 0;
		this.check.disabled = busy || snapshot.detecting || !snapshot.trusted;
		replace(this.status, snapshot.detecting ? "Checking..." : snapshot.busy ? "Another workspace is installing tools." : snapshot.install ? "Installing..." :
			!snapshot.trusted ? "" : missing.length ? missing.length + (missing.length === 1 ? " tool is missing." : " tools are missing.") : snapshot.tools.length ? "Everything is installed." : "");
		if (!snapshot.trusted) {
			replace(this.list, h("div.lt-untrusted", null, icon("shield"), h("span", null, "Trust the workspace to check its tools: finding them runs programs (their version)."),
				h("button.btn", { on: { click: () => this.shell.commands.run("shell.trustWorkspace") } }, "Trust Workspace")));
			return;
		}
		const groups = toolGroups(snapshot);
		replace(this.list, groups.length ? groups.map((group) => this.group(group)) : h("p.muted", null, "None of the workspace's languages names a tool."));
	}

	group(group) {
		return h("section.lt-group", { dataset: { plugin: group.plugin } }, h("h2", null, group.title), ...group.tools.map((tool) => this.row(tool)));
	}

	row(tool) {
		const state = stateText(tool);
		const pin = pinText(tool.pin);
		const busy = Boolean(this.snapshot.install || this.snapshot.busy);
		const action = lacking(tool) && tool.canInstall
			? h("button.btn", { disabled: busy, "aria-label": "Install " + tool.title, on: { click: () => installTools(this.shell, this.ctx.wsId, this.snapshot, [tool.id]) } }, icon("cloud-download"), "Install")
			: lacking(tool) && tool.homepage ? h("a.btn.flat", { href: tool.homepage, target: "_blank", rel: "noopener" }, icon("link-external"), "How to install") : null;
		return h("div.lt-row." + state.tone, { role: "row", dataset: { tool: tool.id, state: tool.state } },
			h("div.lt-name", { role: "cell" }, h("span.lt-title", null, tool.title), h("span.muted", null, kindText(tool.kind))),
			h("div.lt-state", { role: "cell" },
				h("span.lt-tone", null, state.tone === "busy" ? icon("sync", "spin") : h("span.lt-dot"), state.text),
				tool.path ? h("span.mono.muted.lt-path", { title: tool.path }, tool.path) : null,
				pin && tool.state !== "wrongVersion" ? h("span.muted", null, "Pinned: " + pin) : null,
				tool.problem && tool.state !== "found" ? h("span.lt-problem", null, tool.problem) : null),
			h("div.lt-action", { role: "cell" }, action));
	}
}
