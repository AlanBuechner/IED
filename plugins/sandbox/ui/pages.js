// pages.js - the Sandbox activity's documents and its Validation window (docs/34 "The Sandbox activity"): a sandbox's
// page (sandbox:box/<name>: its definition, image and container, what can be done to it, its runs and their output), the
// New Sandbox page (sandbox:new: a system and version from the catalog, a platform, presets), and the last validation's
// pass/fail matrix.

import { h, icon, replace } from "/ui-kit/dom.js";
import { confirmDialog, promptDialog } from "/ui-kit/dialog.js";
import { duration, runOf, runResult, sandboxState, shortPlatform, suggestName, validName, validationSummary } from "./model.js";
import { Console, TONE, resultTone, runHeader, runRow } from "./parts.js";

// --- A sandbox's page ------------------------------------------------------------------------------------------------

export class SandboxPage {
	constructor(ctx, store, actions, name) {
		this.ctx = ctx;
		this.store = store;
		this.actions = actions;
		this.name = name;
		this.picked = null;   // a run of this sandbox picked; null: its newest
		this.console = new Console();
		this.details = h("div.sbx-details");
		this.runs = h("div.sbx-runs.sbx-page-runs", { role: "listbox", "aria-label": "Runs in " + name });
		this.head = h("div.sbx-console-head");
		this.consolePane = h("div.sbx-console.sbx-page-console", null, this.head, this.console.el);
		this.root = h("div.sbx.sbx-page", null, this.details, h("div.sbx-page-bottom", null, h("div.sbx-runs-pane", null, h("div.sbx-section-title", null, "Runs"), this.runs), this.consolePane));
		replace(ctx.el, this.root);
		if (ctx.tab) ctx.tab.setTitle(name);
		this.stops = [store.on(() => this.render()), store.onOutput((runId, text) => this.console.append(runId, text))];
		this.render();
	}

	dispose() {
		for (const stop of this.stops) stop();
	}

	visible(shown) {
		if (shown) this.render();
	}

	async control(action, question, detail) {
		if (question && !(await confirmDialog({ title: question, message: detail, confirm: action === "remove" ? "Remove" : "Reset", danger: action === "remove" }))) return;
		this.picked = null;
		await this.store.request("ied.sandbox/control", { name: this.name, action });
	}

	async runCommand() {
		const box = this.store.sandbox(this.name);
		const command = await promptDialog({ title: "Run in " + this.name, message: "A shell command (sh -lc) run in /workspace, the workspace's folder.", value: this.actions.lastCommand || (box && box.validate) || "", placeholder: "make test" });
		if (!command || !command.trim()) return;
		this.actions.lastCommand = command;
		this.picked = null;
		await this.store.request("ied.sandbox/run", { name: this.name, command });
	}

	render() {
		const store = this.store;
		const box = store.sandbox(this.name);
		if (!box) {
			replace(this.details, h("div.sbx-empty.muted", null, store.snapshot ? "There is no sandbox named " + this.name + " (its folder in .ied/sandbox/ is gone)." : "Loading..."));
			this.consolePane.hidden = true;
			return;
		}
		const state = sandboxState(box, store.engine());
		const usable = !box.problem && store.ready() && this.actions.trusted();
		const busy = !!box.busy;
		const tool = (iconName, label, title, enabled, fn) => h("button.btn.flat.sbx-tool", { title, disabled: !enabled, on: { click: fn } }, icon(iconName), h("span.sbx-tool-label", null, label));
		const fact = (label, value) => value === "" || value === null || value === undefined ? null : [h("dt", null, label), h("dd", null, value)];
		replace(this.details,
			h("div.sbx-title", null, h("span.sbx-name", null, box.name), h("span.sbx-state." + TONE[state.tone], null, state.text)),
			box.description ? h("div.sbx-desc.muted", null, box.description) : null,
			h("div.sbx-bar.sbx-page-bar", null,
				tool("package", "Build", "Build its image", usable && !busy, () => this.control("build")),
				tool("play", "Start", "Start it", usable && !busy && box.container !== "running", () => this.control("start")),
				tool("debug-stop", "Stop", "Stop it", usable && !busy && box.container === "running", () => this.control("stop")),
				tool("terminal", "Terminal", "A terminal in it", usable && !busy, () => this.actions.terminal(this.name)),
				tool("run-all", "Run...", "Run a command in it", usable, () => this.runCommand()),
				tool("beaker", "Validate", box.validate ? "Run its validate command" : "It has no validate command", usable && !!box.validate, () => this.actions.validate([this.name])),
				tool("debug-restart", "Reset", "A new container from the image: what was installed by hand is gone", usable && !busy && box.container !== "absent", () => this.control("reset", "Reset " + this.name + "?", "Its container is made again from its image; anything installed in it by hand is gone.")),
				tool("trash", "Remove", "Remove its container and images (the definition stays)", usable && !busy, () => this.control("remove", "Remove " + this.name + "'s container and images?", "Its definition (.ied/sandbox/" + this.name + "/sandbox.json) stays; the next run builds it again.")),
				h("span.sbx-spacer"),
				h("button.btn.flat.sbx-tool", { title: "Open .ied/sandbox/" + this.name + "/sandbox.json", on: { click: () => this.actions.openDefinition(box) } }, icon("go-to-file"), h("span.sbx-tool-label", null, "sandbox.json"))),
			box.problem ? h("div.sbx-error-line", null, box.problem) : null,
			box.missingPresets && box.missingPresets.length ? h("div.sbx-warn-line", null, "No packages for " + box.missingPresets.join(", ") + " with this system's package manager: add what they need to \"packages\".") : null,
			h("dl.sbx-facts", null,
				fact("System", box.os || "(an image)"),
				fact("Image", box.image),
				fact("Platform", shortPlatform(box.platform) + (box.emulated ? " - emulated under QEMU (5-20x slower)" : " - native")),
				fact("Presets", (box.presets || []).join(", ") || "none"),
				fact("Packages", (box.packages || []).join(" ")),
				fact("Network", box.network === "full" ? "on (full)" : "off while running (packages install with it)"),
				fact("Limits", box.cpus + " CPUs, " + box.memoryGiB + " GiB"),
				fact("Validate", box.validate ? h("code", null, box.validate) : h("span.muted", null, "none - Validate All asks for a command")),
				fact("Built image", box.imageBuilt ? h("code", null, box.tag) : "not yet (the first run builds it)"),
				fact("Container", box.containerName + " (" + box.container + ")"),
				fact("Dockerfile", box.ownDockerfile ? "its own, in .ied/sandbox/" + box.name + "/" : "made by IED from the definition"),
				fact("Last used", box.lastUsedMs ? new Date(box.lastUsedMs).toLocaleString() : "")));
		const mine = store.runs().filter((run) => run.sandbox === this.name);
		const run = (this.picked && mine.find((r) => r.id === this.picked)) || runOf(mine, this.name);
		replace(this.runs, mine.length ? mine.map((r) => runRow(r, store, run && r.id === run.id, () => { this.picked = r.id; this.render(); })) : h("div.sbx-empty.muted", null, "Nothing has run in it yet."));
		this.consolePane.hidden = !run;
		if (run) {
			replace(this.head, runHeader(run, store));
			this.console.show(run.id, store.outputs.get(run.id) || "");
		}
	}
}

// --- The validation matrix -------------------------------------------------------------------------------------------

export class ValidationView {
	constructor(ctx, store, actions) {
		this.ctx = ctx;
		this.store = store;
		this.actions = actions;
		this.root = h("div.sbx.sbx-validation");
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
		const validation = this.store.snapshot && this.store.snapshot.validation;
		const again = h("button.btn.flat.sbx-small", { disabled: !(this.store.ready() && this.store.sandboxes().length) || !!(validation && validation.running), on: { click: () => this.actions.validateAll() } }, icon("beaker"), validation ? "Validate again" : "Validate All");
		if (!validation || !validation.rows || !validation.rows.length) {
			replace(this.root, h("div.sbx-details", null, h("div.sbx-empty.muted", null, "No validation yet. Validate All runs a command - each sandbox's validate, or one you give - in every sandbox and shows here which pass."), again));
			return;
		}
		const header = h("div.sbx-validation-head", null,
			h("span.sbx-name", null, validationSummary(validation)),
			h("span.muted", null, validation.command ? h("code", null, validation.command) : "each sandbox's validate command"),
			h("span.muted", null, validation.endedMs ? "finished " + new Date(validation.endedMs).toLocaleString() : validation.startedMs ? "started " + new Date(validation.startedMs).toLocaleTimeString() : ""),
			h("span.sbx-spacer"), again);
		const rows = validation.rows.map((row) => {
			const tr = h("tr", { title: row.runId ? "Show its output in Activity" : "", on: { click: () => row.runId && this.actions.showRun(row.runId) } },
				h("td", null, h("span.sbx-badge." + resultTone(row.result), null, row.result === "running" ? icon("loading", "spin") : null, row.result === "fail" ? "FAIL" : row.result)),
				h("td.sbx-cell-name", null, row.sandbox),
				h("td.muted", null, row.os),
				h("td.muted", null, shortPlatform(row.platform)),
				h("td.sbx-cell-num", null, row.result === "fail" ? "exit " + row.exitCode : ""),
				h("td.sbx-cell-num.muted", null, duration(row.ms)));
			const tail = row.tail && row.result !== "pass" ? h("tr.sbx-tail-row", null, h("td", { colspan: "6" }, h("pre.sbx-tail", null, row.tail))) : null;
			return [tr, tail];
		});
		replace(this.root, h("div.sbx-details", null, header, h("table.sbx-matrix", null,
			h("thead", null, h("tr", null, h("th", null, "Result"), h("th", null, "Sandbox"), h("th", null, "System"), h("th", null, "Platform"), h("th", null, ""), h("th", null, "Time"))),
			h("tbody", null, rows))));
	}
}

// --- New Sandbox -----------------------------------------------------------------------------------------------------

export class NewPage {
	constructor(ctx, store, actions) {
		this.ctx = ctx;
		this.store = store;
		this.actions = actions;
		this.form = null;
		this.root = h("div.sbx.sbx-newpage");
		this.details = h("div.sbx-details");
		this.root.append(this.details);
		replace(ctx.el, this.root);
		if (ctx.tab) ctx.tab.setTitle("New Sandbox");
		this.stop = store.on(() => this.render());
		store.loadCatalog().then(() => this.render());
		this.render();
	}

	dispose() {
		this.stop();
	}

	defaultForm(catalog) {
		const systems = catalog.systems || [];
		const os = systems.find((system) => system.id === "ubuntu") || systems[0];
		const version = os ? (os.versions.find((v) => v.default) || os.versions[0]) : null;
		return { os: os ? os.id : "", version: version ? version.version : "", platform: catalog.native, presets: ["build"], packages: "", validate: "", network: "none", name: "", nameEdited: false };
	}

	render() {
		const catalog = this.store.catalog;
		if (!catalog) {
			replace(this.details, h("div.sbx-empty.muted", null, icon("loading", "spin"), " Reading the catalog..."));
			return;
		}
		if (!this.form) this.form = this.defaultForm(catalog);
		if (this.root.contains(document.activeElement) && document.activeElement.tagName === "INPUT") return;   // never under the cursor
		this.draw();
	}

	draw() {
		const catalog = this.store.catalog;
		const form = this.form;
		const systems = catalog.systems || [];
		const os = systems.find((system) => system.id === form.os) || null;
		const version = os ? (os.versions.find((v) => v.version === form.version) || os.versions[0]) : null;
		const platforms = version ? version.platforms : [];
		if (!platforms.some((p) => p.platform === form.platform)) form.platform = (platforms.find((p) => p.native) || platforms[0] || { platform: "" }).platform;
		const taken = this.store.sandboxes().map((box) => box.name);
		if (!form.nameEdited) form.name = suggestName(form.os, form.version, form.platform, version && version.default, catalog.native, taken);
		const platform = platforms.find((p) => p.platform === form.platform);
		const windows = os && os.engine === "windows";
		const field = (label, control, hint) => h("label.sbx-field", null, h("span.sbx-field-label", null, label), control, hint ? h("span.sbx-field-hint.muted", null, hint) : null);
		const select = (value, options, onChange) => h("select.input-field", { on: { change: (event) => onChange(event.target.value) } },
			options.map(([v, text]) => h("option", { value: v, selected: v === value }, text)));
		const presetBoxes = (catalog.presets || []).map((preset) => {
			const has = !os || !os.packageManager || (preset.packages && preset.packages[os.packageManager]);
			const on = form.presets.includes(preset.id);
			return h("label.sbx-preset" + (on ? ".on" : "") + (has ? "" : ".off"), { title: preset.title + (has ? "" : " - none for " + (os && os.packageManager)) },
				h("input", { type: "checkbox", checked: on, disabled: !has, on: { change: (event) => { form.presets = event.target.checked ? [...form.presets, preset.id] : form.presets.filter((id) => id !== preset.id); this.draw(); } } }),
				preset.id);
		});
		const nameOk = validName(form.name) && !taken.includes(form.name);
		const ok = nameOk && this.actions.trusted() && !windows;
		this.buttons = h("div.sbx-actions", null,
			h("button.btn.primary", { disabled: !ok, on: { click: () => this.create(true) } }, icon("package"), "Create and Build"),
			h("button.btn", { disabled: !ok, on: { click: () => this.create(false) } }, "Create"),
			h("button.btn.flat", { on: { click: () => this.ctx.tab && this.ctx.tab.close() } }, "Cancel"));
		replace(this.details,
			h("div.sbx-title", null, h("span.sbx-name", null, "New Sandbox")),
			h("div.sbx-desc.muted", null, "Writes .ied/sandbox/<name>/sandbox.json (commit it). The image is built on the first run or with Create and Build."),
			h("div.sbx-form", null,
				field("System", select(form.os, systems.map((system) => [system.id, system.title + (system.engine === "windows" ? " (Windows engine - not yet)" : "")]), (value) => {
					form.os = value;
					const next = systems.find((system) => system.id === value);
					const v = next && (next.versions.find((x) => x.default) || next.versions[0]);
					form.version = v ? v.version : "";
					this.draw();
				}), os && os.note ? os.note : (os ? (os.packageManager || "no package manager") : "")),
				field("Version", select(form.version, (os ? os.versions : []).map((v) => [v.version, v.title + (v.default ? " - default" : "") + (v.downloaded ? " - downloaded" : "")]), (value) => { form.version = value; this.draw(); }),
					version ? h("span", null, h("code", null, version.pull || version.image), " ", h("button.btn.flat.sbx-small", { title: "Download this image now (docker pull)", on: { click: () => this.store.request("ied.sandbox/pull", { image: version.image, platform: form.platform }) } }, icon("cloud-download"), version.downloaded ? "Download again" : "Download")) : null),
				field("Platform", select(form.platform, platforms.map((p) => [p.platform, shortPlatform(p.platform) + (p.native ? " - native" : p.runs ? " - emulated" : " - needs emulators")]), (value) => { form.platform = value; this.draw(); }),
					platform && !platform.native ? (platform.runs ? "Runs under QEMU emulation: exact instructions, 5-20x slower." : h("span", null, "Docker has no emulator for it yet. ", h("button.btn.flat.sbx-small", { on: { click: () => this.installEmulators() } }, "Install emulators"))) : null),
				field("Presets", h("div.sbx-presets", null, presetBoxes)),
				field("Packages", h("input.input-field", { type: "text", value: form.packages, placeholder: "more packages, by this system's names", on: { input: (event) => { form.packages = event.target.value; } } })),
				field("Validate", h("input.input-field", { type: "text", value: form.validate, placeholder: "cmake -B build/$IED_SANDBOX && cmake --build build/$IED_SANDBOX && ctest --test-dir build/$IED_SANDBOX", on: { input: (event) => { form.validate = event.target.value; } } }), "The command Validate All runs here. Build into build/$IED_SANDBOX: sandboxes share the workspace and run at once."),
				field("Network", select(form.network, [["none", "Off while running (safer)"], ["full", "On"]], (value) => { form.network = value; }), "Installing packages always has the network."),
				field("Name", h("input.input-field" + (nameOk ? "" : ".sbx-bad"), { type: "text", value: form.name, on: { input: (event) => { form.name = event.target.value; form.nameEdited = true; } , change: () => this.draw() } }), nameOk ? null : "Lowercase letters, digits and '-', not taken."),
				this.buttons));
	}

	async installEmulators() {
		if (!(await confirmDialog({ title: "Install QEMU's emulators?", message: "Runs a privileged container (tonistiigi/binfmt --install all) that registers emulators for every processor in Docker's virtual machine.", confirm: "Install" }))) return;
		await this.store.request("ied.sandbox/engine", { action: "emulators" });
	}

	async create(build) {
		const form = this.form;
		const answer = await this.store.request("ied.sandbox/create", {
			name: form.name, os: form.os + ":" + form.version, platform: form.platform === this.store.catalog.native ? "" : form.platform,
			presets: form.presets, packages: form.packages.split(/\s+/).filter(Boolean), validate: form.validate, network: form.network, build,
		});
		if (!answer) return;
		if (answer.notes) this.store.ied.toast(answer.notes, { level: "warn" });
		this.form = null;
		this.actions.openBox(answer.name, false);
		if (this.ctx.tab) this.ctx.tab.close();
	}
}
