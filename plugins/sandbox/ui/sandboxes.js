// sandboxes.js - the Sandboxes tab (docs/34 "The Sandboxes window"), laid out like a Visual Studio tool window: Docker's
// state on top, a toolbar for the selected sandbox, the sandboxes on the left, on the right the selected one's details,
// the New Sandbox form or the validation matrix, and the output of the selected run below. Everything shown is the
// server's snapshot on sandbox:<wsId> and its "output" events; the buttons ask the plugin's methods (ied.sandbox/*),
// whose jobs go on with the tab closed.

import { h, icon, replace } from "/ui-kit/dom.js";
import { confirmDialog, promptDialog } from "/ui-kit/dialog.js";
import {
	appendOutput, duration, enginePlatforms, keepOutputs, runOf, runResult, sandboxState, shortPlatform, suggestName,
	validName, validationOf, validationSummary,
} from "./model.js";

const TONE = { ok: "sbx-ok", info: "sbx-info", warn: "sbx-warn", error: "sbx-error", "": "" };

export class SandboxesView {
	constructor(ctx, disposed) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.wsId = ctx.wsId || this.ied.workspace.wsId;
		this.disposed = disposed;
		this.snapshot = null;
		this.outputs = new Map();      // runId -> text
		this.selected = null;          // the selected sandbox's name
		this.pane = "details";         // "details", "new" or "validation"
		this.shownRun = null;          // a run picked from the matrix; null: the selected sandbox's
		this.problem = "";
		this.catalog = null;
		this.form = null;              // the New Sandbox form's choices
		this.showingDetails = false;   // a phone shows the right pane instead of the list
		this.lastCommand = "";
		this.build();
		this.stopTopic = this.ied.client.subscribe("sandbox:" + this.wsId, {
			snap: (data) => this.received(data, true),
			evt: (kind, data) => {
				if (kind === "changed") this.received(data, false);
				else if (kind === "output") this.output(data);
			},
		});
		this.request("ied.sandbox/state", {});
	}

	// What a command asked of an open tab: "new" shows the form, "validate" runs Validate All.
	ask(what) {
		if (what === "new") this.showNew();
		else if (what === "validate") this.validateAll();
	}

	dispose() {
		if (this.stopTopic) this.stopTopic();
		this.disposed();
	}

	// --- Building ---------------------------------------------------------------------------------------------------

	build() {
		this.engine = h("div.sbx-engine");
		const tool = (id, iconName, label, title, fn) => h("button.btn.flat.sbx-tool", { title, dataset: { tool: id }, on: { click: fn } }, icon(iconName), h("span.sbx-tool-label", null, label));
		this.tools = {
			new: tool("new", "add", "New", "New sandbox from the catalog", () => this.showNew()),
			build: tool("build", "package", "Build", "Build the selected sandbox's image", () => this.control("build")),
			start: tool("start", "play", "Start", "Start the selected sandbox", () => this.control("start")),
			stop: tool("stop", "debug-stop", "Stop", "Stop the selected sandbox", () => this.control("stop")),
			terminal: tool("terminal", "terminal", "Terminal", "Open a terminal in the selected sandbox", () => this.terminal()),
			run: tool("run", "run-all", "Run...", "Run a command in the selected sandbox", () => this.runCommand()),
			reset: tool("reset", "debug-restart", "Reset", "A new container from the image: what was installed by hand is gone", () => this.control("reset", "Reset " + this.selected + "?", "Its container is made again from its image; anything installed in it by hand is gone.")),
			remove: tool("remove", "trash", "Remove", "Remove the container and images (the definition stays)", () => this.control("remove", "Remove " + this.selected + "'s container and images?", "Its definition (.ied/sandbox/" + this.selected + "/sandbox.json) stays; the next run builds it again.")),
		};
		this.validateButton = h("button.btn.sbx-validate", { title: "Run each sandbox's validate command, or one you give, in every sandbox", on: { click: () => this.validateAll() } }, icon("beaker"), h("span.sbx-tool-label", null, "Validate All"));
		this.bar = h("div.sbx-bar", null, Object.values(this.tools), h("span.sbx-spacer"), this.validateButton,
			h("button.ib", { title: "Refresh", "aria-label": "Refresh", on: { click: () => this.request("ied.sandbox/engine", { action: "refresh" }) } }, icon("refresh")));
		this.banner = h("div.sbx-banner", { hidden: true });
		this.list = h("div.sbx-list", { role: "listbox", tabindex: "0", "aria-label": "Sandboxes", on: { keydown: (event) => this.listKey(event) } });
		this.paneTabs = h("div.sbx-pane-tabs", { role: "tablist" });
		this.details = h("div.sbx-details");
		this.right = h("div.sbx-right", null, this.paneTabs, this.details);
		this.outputHead = h("div.sbx-output-head");
		this.outputText = h("pre.sbx-output-text", { tabindex: "0" });
		this.outputPane = h("div.sbx-output", { hidden: true }, this.outputHead, this.outputText);
		this.root = h("div.sbx", null, this.engine, this.bar, this.banner, h("div.sbx-body", null, this.list, this.right), this.outputPane);
		replace(this.ctx.el, this.root);
		this.render();
	}

	// --- The server -------------------------------------------------------------------------------------------------

	received(data, fromSnapshot) {
		const before = this.snapshot;
		this.snapshot = data;
		keepOutputs(this.outputs, data.runs, fromSnapshot);
		const names = (data.sandboxes || []).map((box) => box.name);
		if (!this.selected || !names.includes(this.selected)) this.selected = names[0] || null;
		if (!names.length && this.pane === "details" && before) this.pane = "new";
		if (before && data.validation && data.validation.running && !(before.validation && before.validation.running)) this.pane = "validation";
		this.render();
	}

	output(data) {
		const text = appendOutput(this.outputs, data.runId, data.text || "");
		const shown = this.shownRunObject();
		if (shown && shown.id === data.runId) this.renderOutputText(text, true);
	}

	async request(method, params) {
		this.problem = "";
		try {
			return await this.ied.client.request(method, Object.assign({ wsId: this.wsId }, params));
		} catch (e) {
			this.problem = (e && e.message) || String(e);
			this.render();
			return null;
		}
	}

	async control(action, question, detail) {
		if (!this.selected) return;
		if (question && !(await confirmDialog({ title: question, message: detail, confirm: action === "remove" ? "Remove" : "Reset", danger: action === "remove" }))) return;
		this.shownRun = null;
		await this.request("ied.sandbox/control", { name: this.selected, action });
	}

	async runCommand() {
		if (!this.selected) return;
		const box = this.sandbox(this.selected);
		const command = await promptDialog({ title: "Run in " + this.selected, message: "A shell command (sh -lc) run in /workspace, the workspace's folder.", value: this.lastCommand || (box && box.validate) || "", placeholder: "make test" });
		if (!command || !command.trim()) return;
		this.lastCommand = command;
		this.shownRun = null;
		await this.request("ied.sandbox/run", { name: this.selected, command });
	}

	async validateAll() {
		const boxes = (this.snapshot && this.snapshot.sandboxes) || [];
		if (!boxes.length) {
			this.showNew();
			return;
		}
		let command = "";
		if (boxes.some((box) => !box.validate)) {
			const last = this.snapshot.validation && this.snapshot.validation.command;
			command = await promptDialog({ title: "Validate in all sandboxes", message: "The command to run in each sandbox (sandboxes with a validate command of their own run it when this is empty).", value: last || this.lastCommand || "", placeholder: "cmake -B build/$IED_SANDBOX && cmake --build build/$IED_SANDBOX && ctest --test-dir build/$IED_SANDBOX" });
			if (command === null || command === undefined) return;
			this.lastCommand = command;
		}
		this.pane = "validation";
		this.showingDetails = true;
		this.render();
		await this.request("ied.sandbox/validate", { command: command || "" });
	}

	async terminal() {
		if (!this.selected) return;
		const box = this.sandbox(this.selected);
		if (box && box.container !== "running") await this.request("ied.sandbox/control", { name: this.selected, action: "start" });
		this.waitRunning(this.selected, () => this.ied.commands.run("ied.terminal.new", { profile: "sandbox-" + this.selected }));
	}

	// Runs `then` once `name` is running (a start takes a moment, a first build minutes), at most two minutes.
	waitRunning(name, then) {
		const started = Date.now();
		const tick = () => {
			const box = this.sandbox(name);
			if (box && box.container === "running" && !box.busy) then();
			else if (Date.now() - started < 120000 && !(box && box.problem)) setTimeout(tick, 500);
		};
		tick();
	}

	sandbox(name) {
		return ((this.snapshot && this.snapshot.sandboxes) || []).find((box) => box.name === name) || null;
	}

	// --- Rendering --------------------------------------------------------------------------------------------------

	render() {
		this.renderEngine();
		this.renderBar();
		this.renderBanner();
		this.renderList();
		this.renderPaneTabs();
		if (this.pane === "new") this.renderNew();
		else if (this.pane === "validation") this.renderValidation();
		else this.renderDetails();
		this.renderOutput();
		this.root.classList.toggle("sbx-showing-details", this.showingDetails);
	}

	renderEngine() {
		const engine = this.snapshot && this.snapshot.engine;
		if (!engine) {
			replace(this.engine, icon("loading", "spin"), h("span.muted", null, "Looking at Docker..."));
			return;
		}
		const parts = [];
		if (engine.state === "ready") {
			const platforms = enginePlatforms(engine);
			parts.push(h("span.sbx-dot.sbx-ok", { title: "Docker's engine is running" }), h("span.sbx-engine-name", null, "Docker " + engine.serverVersion));
			const facts = [engine.product, engine.os + "/" + engine.arch, engine.cpus ? engine.cpus + " CPUs" : "", engine.memoryBytes ? Math.round(engine.memoryBytes / 1073741824) + " GiB" : ""].filter(Boolean);
			parts.push(h("span.muted.sbx-engine-facts", null, facts.join(" \u00b7 ")));
			parts.push(h("span.sbx-platforms", { title: "Platforms Docker runs here: native, and under QEMU emulation" },
				platforms.native.map((p) => h("span.sbx-chip.native", { title: p + " (native)" }, shortPlatform(p))),
				platforms.emulated.map((p) => h("span.sbx-chip", { title: p + " (emulated: 5-20x slower)" }, shortPlatform(p)))));
		}
		else {
			const tone = engine.state === "starting" ? "sbx-info" : engine.state === "missing" || engine.state === "failed" ? "sbx-error" : "sbx-warn";
			parts.push(engine.state === "starting" ? icon("loading", "spin") : h("span.sbx-dot." + tone), h("span.sbx-engine-name", null, "Docker " + engine.state));
			parts.push(h("span.sbx-engine-advice", null, engine.advice || engine.error || ""));
			if (engine.state === "stopped" || engine.state === "failed") parts.push(h("button.btn.primary", { on: { click: () => this.request("ied.sandbox/engine", { action: "start" }) } }, icon("play"), "Start Docker"));
			if (engine.state === "missing") parts.push(h("a.sbx-link", { href: "https://www.docker.com/products/docker-desktop/", target: "_blank", rel: "noopener" }, "Get Docker Desktop"));
		}
		replace(this.engine, parts);
	}

	renderBar() {
		const box = this.selected && this.sandbox(this.selected);
		const ready = this.snapshot && this.snapshot.engine && this.snapshot.engine.state === "ready";
		const usable = !!(box && !box.problem && ready && this.trusted());
		const busy = !!(box && box.busy);
		const set = (name, enabled) => { this.tools[name].disabled = !enabled; };
		set("new", this.trusted());
		set("build", usable && !busy);
		set("start", usable && !busy && box.container !== "running");
		set("stop", usable && !busy && box.container === "running");
		set("terminal", usable && !busy);
		set("run", usable);
		set("reset", usable && !busy && box.container !== "absent");
		set("remove", usable && !busy);
		this.validateButton.disabled = !(ready && this.trusted() && (this.snapshot.sandboxes || []).length) || !!(this.snapshot.validation && this.snapshot.validation.running);
	}

	trusted() {
		return this.ied.workspace ? this.ied.workspace.trusted !== false : true;
	}

	renderBanner() {
		const lines = [];
		if (this.problem) lines.push(h("div.sbx-error-line", null, this.problem));
		if (!this.trusted()) lines.push(h("div.sbx-warn-line", null, "This workspace is not trusted: sandboxes run its code, so nothing runs until you trust it."));
		const dockerProblem = this.snapshot && this.snapshot.dockerProblem;
		if (dockerProblem && this.snapshot.engine && this.snapshot.engine.state === "ready") lines.push(h("div.sbx-error-line", null, dockerProblem));
		this.banner.hidden = !lines.length;
		replace(this.banner, lines);
	}

	renderList() {
		const boxes = (this.snapshot && this.snapshot.sandboxes) || [];
		const engine = this.snapshot && this.snapshot.engine;
		const validation = this.snapshot && this.snapshot.validation;
		if (!this.snapshot) {
			replace(this.list, h("div.sbx-empty.muted", null, "Loading..."));
			return;
		}
		if (!boxes.length) {
			replace(this.list, h("div.sbx-empty", null,
				h("div", null, "No sandboxes yet."),
				h("div.muted", null, "A sandbox is a Linux system - any distribution, on this processor or an emulated one - that builds and runs this workspace's code."),
				h("button.btn.primary", { disabled: !this.trusted(), on: { click: () => this.showNew() } }, icon("add"), "New Sandbox")));
			return;
		}
		replace(this.list, boxes.map((box) => {
			const state = sandboxState(box, engine);
			const result = validationOf(validation, box.name);
			const row = h("div.sbx-row" + (box.name === this.selected ? ".sel" : ""), {
				role: "option", "aria-selected": box.name === this.selected ? "true" : "false", dataset: { name: box.name },
				on: { click: () => this.select(box.name), dblclick: () => this.select(box.name, true) },
			},
			h("span.sbx-row-icon." + (TONE[state.tone] || "sbx-plain"), null, icon(state.icon, state.spin ? "spin" : undefined)),
			h("div.sbx-row-main", null,
				h("div.sbx-row-name", null, box.name),
				h("div.sbx-row-desc.muted", null, (box.os || box.image) + " \u00b7 " + shortPlatform(box.platform) + (box.emulated ? " (emulated)" : ""))),
			h("div.sbx-row-side", null,
				h("span.sbx-state." + (TONE[state.tone] || "sbx-plain"), null, state.text),
				result ? h("span.sbx-badge." + resultTone(result.result), { title: "Last validation: " + result.result }, result.result === "pass" ? "pass" : result.result === "fail" ? "FAIL" : result.result) : null));
			return row;
		}));
	}

	select(name, open) {
		this.selected = name;
		this.shownRun = null;
		if (this.pane !== "validation" || open) this.pane = "details";
		this.showingDetails = true;
		this.render();
	}

	listKey(event) {
		const names = ((this.snapshot && this.snapshot.sandboxes) || []).map((box) => box.name);
		const at = names.indexOf(this.selected);
		if (event.key === "ArrowDown" && at < names.length - 1) this.select(names[at + 1]);
		else if (event.key === "ArrowUp" && at > 0) this.select(names[at - 1]);
		else return;
		event.preventDefault();
	}

	renderPaneTabs() {
		const tab = (id, label) => h("button.sbx-pane-tab" + (this.pane === id ? ".on" : ""), { role: "tab", "aria-selected": this.pane === id ? "true" : "false", on: { click: () => { this.pane = id; if (id === "new") this.showNew(); else this.render(); } } }, label);
		const summary = validationSummary(this.snapshot && this.snapshot.validation);
		replace(this.paneTabs,
			h("button.ib.sbx-back", { title: "Back to the list", "aria-label": "Back", on: { click: () => { this.showingDetails = false; this.render(); } } }, icon("arrow-left")),
			tab("details", this.selected ? this.selected : "Details"),
			tab("validation", summary ? "Validation \u00b7 " + summary : "Validation"),
			tab("new", "New Sandbox"));
	}

	renderDetails() {
		const box = this.selected && this.sandbox(this.selected);
		if (!box) {
			replace(this.details, h("div.sbx-empty.muted", null, "Select a sandbox, or make one with New."));
			return;
		}
		const engine = this.snapshot.engine;
		const state = sandboxState(box, engine);
		const fact = (label, value) => value === "" || value === null || value === undefined ? null : [h("dt", null, label), h("dd", null, value)];
		const run = runOf(this.snapshot.runs, box.name);
		replace(this.details,
			h("div.sbx-title", null, h("span.sbx-name", null, box.name), h("span.sbx-state." + (TONE[state.tone] || "sbx-plain"), null, state.text)),
			box.description ? h("div.sbx-desc.muted", null, box.description) : null,
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
				fact("Last used", box.lastUsedMs ? new Date(box.lastUsedMs).toLocaleString() : ""),
				fact("Last run", run ? h("span", null, h("code", null, run.label), " - " + runResult(run)) : "")),
			h("div.sbx-actions", null,
				h("button.btn", { on: { click: () => this.openDefinition(box) } }, icon("go-to-file"), "Open sandbox.json"),
				h("button.btn", { disabled: !box.validate, on: { click: () => this.request("ied.sandbox/validate", { names: [box.name] }).then(() => { this.pane = "validation"; this.render(); }) } }, icon("beaker"), "Validate")));
	}

	openDefinition(box) {
		const path = box.definition;
		if (this.ied.files && this.ied.files.open) this.ied.files.open(path);
		else this.ied.commands.run("ied.editor.open", { path });
	}

	renderValidation() {
		const validation = this.snapshot && this.snapshot.validation;
		if (!validation || !validation.rows || !validation.rows.length) {
			replace(this.details, h("div.sbx-empty.muted", null, "No validation yet. Validate All runs a command - each sandbox's validate, or one you give - in every sandbox and shows here which pass."));
			return;
		}
		const header = h("div.sbx-validation-head", null,
			h("span.sbx-name", null, validationSummary(validation)),
			h("span.muted", null, validation.command ? h("code", null, validation.command) : "each sandbox's validate command"),
			h("span.muted", null, validation.endedMs ? "finished " + new Date(validation.endedMs).toLocaleString() : validation.startedMs ? "started " + new Date(validation.startedMs).toLocaleTimeString() : ""));
		const rows = validation.rows.map((row) => {
			const tone = resultTone(row.result);
			const tr = h("tr" + (this.shownRun === row.runId && row.runId ? ".sel" : ""), { on: { click: () => { if (row.runId) { this.shownRun = row.runId; this.render(); } } } },
				h("td", null, h("span.sbx-badge." + tone, null, row.result === "running" ? h("span", null, icon("loading", "spin")) : null, row.result === "fail" ? "FAIL" : row.result)),
				h("td.sbx-cell-name", null, row.sandbox),
				h("td.muted", null, row.os),
				h("td.muted", null, shortPlatform(row.platform)),
				h("td.sbx-cell-num", null, row.result === "fail" ? "exit " + row.exitCode : ""),
				h("td.sbx-cell-num.muted", null, duration(row.ms)));
			const tail = row.tail && row.result !== "pass" ? h("tr.sbx-tail-row", null, h("td", { colspan: "6" }, h("pre.sbx-tail", null, row.tail))) : null;
			return [tr, tail];
		});
		replace(this.details, header, h("table.sbx-matrix", null,
			h("thead", null, h("tr", null, h("th", null, "Result"), h("th", null, "Sandbox"), h("th", null, "System"), h("th", null, "Platform"), h("th", null, ""), h("th", null, "Time"))),
			h("tbody", null, rows)));
	}

	// --- New Sandbox ----------------------------------------------------------------------------------------------

	async showNew() {
		this.pane = "new";
		this.showingDetails = true;
		this.render();
		if (!this.catalog) {
			this.catalog = await this.request("ied.sandbox/catalog", {});
			if (this.catalog && !this.form) this.form = this.defaultForm();
			this.render();
		}
	}

	defaultForm() {
		const systems = this.linuxSystems();
		const os = systems.find((system) => system.id === "ubuntu") || systems[0];
		const version = os ? (os.versions.find((v) => v.default) || os.versions[0]) : null;
		return { os: os ? os.id : "", version: version ? version.version : "", platform: this.catalog.native, presets: ["build"], packages: "", validate: "", network: "none", name: "", nameEdited: false };
	}

	linuxSystems() {
		return ((this.catalog && this.catalog.systems) || []);
	}

	formSystem() {
		return this.linuxSystems().find((system) => system.id === this.form.os) || null;
	}

	formVersion() {
		const os = this.formSystem();
		return os ? (os.versions.find((v) => v.version === this.form.version) || os.versions[0]) : null;
	}

	renderNew() {
		if (!this.catalog || !this.form) {
			replace(this.details, h("div.sbx-empty.muted", null, icon("loading", "spin"), " Reading the catalog..."));
			return;
		}
		const form = this.form;
		const os = this.formSystem();
		const version = this.formVersion();
		const platforms = version ? version.platforms : [];
		if (!platforms.some((p) => p.platform === form.platform)) form.platform = (platforms.find((p) => p.native) || platforms[0] || { platform: "" }).platform;
		const taken = ((this.snapshot && this.snapshot.sandboxes) || []).map((box) => box.name);
		if (!form.nameEdited) form.name = suggestName(form.os, form.version, form.platform, version && version.default, this.catalog.native, taken);
		const platform = platforms.find((p) => p.platform === form.platform);
		const windows = os && os.engine === "windows";
		const field = (label, control, hint) => h("label.sbx-field", null, h("span.sbx-field-label", null, label), control, hint ? h("span.sbx-field-hint.muted", null, hint) : null);
		const select = (value, options, onChange) => h("select.input-field", { on: { change: (event) => onChange(event.target.value) } },
			options.map(([v, text, disabled]) => h("option", { value: v, selected: v === value, disabled: !!disabled }, text)));
		const presetBoxes = ((this.catalog && this.catalog.presets) || []).map((preset) => {
			const has = !os || !os.packageManager || (preset.packages && preset.packages[os.packageManager]);
			const on = form.presets.includes(preset.id);
			return h("label.sbx-preset" + (on ? ".on" : "") + (has ? "" : ".off"), { title: preset.title + (has ? "" : " - none for " + (os && os.packageManager)) },
				h("input", { type: "checkbox", checked: on, disabled: !has, on: { change: (event) => { form.presets = event.target.checked ? [...form.presets, preset.id] : form.presets.filter((id) => id !== preset.id); this.render(); } } }),
				preset.id);
		});
		const nameOk = validName(form.name) && !taken.includes(form.name);
		replace(this.details,
			h("div.sbx-title", null, h("span.sbx-name", null, "New Sandbox")),
			h("div.sbx-desc.muted", null, "Writes .ied/sandbox/<name>/sandbox.json (commit it). The image is built on the first run or with Create and Build."),
			h("div.sbx-form", null,
				field("System", select(form.os, this.linuxSystems().map((system) => [system.id, system.title + (system.engine === "windows" ? " (Windows engine - not yet)" : "")]), (value) => {
					form.os = value;
					const next = this.formSystem();
					const v = next && (next.versions.find((x) => x.default) || next.versions[0]);
					form.version = v ? v.version : "";
					this.render();
				}), os && os.note ? os.note : (os ? (os.packageManager || "no package manager") : "")),
				field("Version", select(form.version, (os ? os.versions : []).map((v) => [v.version, v.title + (v.default ? " - default" : "") + (v.downloaded ? " - downloaded" : "")]), (value) => { form.version = value; this.render(); }),
					version ? h("span", null, h("code", null, version.pull || version.image), " ", h("button.btn.flat.sbx-small", { title: "Download this image now (docker pull)", on: { click: () => this.request("ied.sandbox/pull", { image: version.image, platform: form.platform }) } }, icon("cloud-download"), version.downloaded ? "Download again" : "Download")) : null),
				field("Platform", select(form.platform, platforms.map((p) => [p.platform, shortPlatform(p.platform) + (p.native ? " - native" : p.runs ? " - emulated" : " - needs emulators")]), (value) => { form.platform = value; this.render(); }),
					platform && !platform.native ? (platform.runs ? "Runs under QEMU emulation: exact instructions, 5-20x slower." : h("span", null, "Docker has no emulator for it yet. ", h("button.btn.flat.sbx-small", { on: { click: () => this.installEmulators() } }, "Install emulators"))) : null),
				field("Presets", h("div.sbx-presets", null, presetBoxes)),
				field("Packages", h("input.input-field", { type: "text", value: form.packages, placeholder: "more packages, by this system's names", on: { input: (event) => { form.packages = event.target.value; } } })),
				field("Validate", h("input.input-field", { type: "text", value: form.validate, placeholder: "cmake -B build/$IED_SANDBOX && cmake --build build/$IED_SANDBOX && ctest --test-dir build/$IED_SANDBOX", on: { input: (event) => { form.validate = event.target.value; } } }), "The command Validate All runs here. Build into build/$IED_SANDBOX: sandboxes share the workspace and run at once."),
				field("Network", select(form.network, [["none", "Off while running (safer)"], ["full", "On"]], (value) => { form.network = value; }), "Installing packages always has the network."),
				field("Name", h("input.input-field" + (nameOk ? "" : ".sbx-bad"), { type: "text", value: form.name, on: { input: (event) => { form.name = event.target.value; form.nameEdited = true; this.renderNewButtons(); } } }), nameOk ? null : "Lowercase letters, digits and '-', not taken."),
				this.newButtons = h("div.sbx-actions")));
		this.renderNewButtons(windows);
	}

	renderNewButtons(windows) {
		if (!this.newButtons) return;
		const form = this.form;
		const taken = ((this.snapshot && this.snapshot.sandboxes) || []).map((box) => box.name);
		const ok = validName(form.name) && !taken.includes(form.name) && this.trusted() && !windows;
		replace(this.newButtons,
			h("button.btn.primary", { disabled: !ok, on: { click: () => this.create(true) } }, icon("package"), "Create and Build"),
			h("button.btn", { disabled: !ok, on: { click: () => this.create(false) } }, "Create"),
			h("button.btn.flat", { on: { click: () => { this.pane = "details"; this.render(); } } }, "Cancel"));
	}

	async installEmulators() {
		if (!(await confirmDialog({ title: "Install QEMU's emulators?", message: "Runs a privileged container (tonistiigi/binfmt --install all) that registers emulators for every processor in Docker's virtual machine.", confirm: "Install" }))) return;
		await this.request("ied.sandbox/engine", { action: "emulators" });
	}

	async create(build) {
		const form = this.form;
		const answer = await this.request("ied.sandbox/create", {
			name: form.name, os: form.os + ":" + form.version, platform: form.platform === this.catalog.native ? "" : form.platform,
			presets: form.presets, packages: form.packages.split(/\s+/).filter(Boolean), validate: form.validate, network: form.network, build,
		});
		if (!answer) return;
		if (answer.notes) this.ied.toast(answer.notes, { level: "warn" });
		this.selected = answer.name;
		this.form = this.defaultForm();
		this.pane = "details";
		this.render();
	}

	// --- Output -----------------------------------------------------------------------------------------------------

	shownRunObject() {
		const runs = (this.snapshot && this.snapshot.runs) || [];
		if (this.shownRun) return runs.find((run) => run.id === this.shownRun) || null;
		if (this.pane === "new") return runs.find((run) => !run.sandbox && run.running) || runs.find((run) => !run.sandbox) || null;
		return this.selected ? runOf(runs, this.selected) : null;
	}

	renderOutput() {
		const run = this.shownRunObject();
		this.outputPane.hidden = !run;
		if (!run) return;
		const failed = !run.running && (run.exitCode !== 0 || run.stopped || run.problem);
		this.outputPane.classList.toggle("failed", !!failed);
		replace(this.outputHead,
			run.running ? icon("loading", "spin") : icon(failed ? "error" : "pass", failed ? "sbx-error" : "sbx-ok"),
			h("span.sbx-output-sandbox", null, run.sandbox || "docker"),
			h("code.sbx-output-command", { title: run.label }, run.label),
			h("span.sbx-output-state.muted", null, runResult(run) + (run.endedMs ? " \u00b7 " + duration(run.endedMs - run.startedMs) : "")),
			run.running ? h("button.btn.flat.sbx-small", { on: { click: () => this.request("ied.sandbox/stopRun", { id: run.id }) } }, icon("debug-stop"), "Stop") : null);
		this.renderOutputText(this.outputs.get(run.id) || "", false);
	}

	renderOutputText(text, appended) {
		const el = this.outputText;
		const atEnd = el.scrollHeight - el.scrollTop - el.clientHeight < 24;
		el.textContent = text;
		if (atEnd || !appended) el.scrollTop = el.scrollHeight;
	}

	visible(shown) {
		if (shown) this.render();
	}
}

function resultTone(result) {
	if (result === "pass") return "sbx-ok";
	if (result === "fail" || result === "error") return "sbx-error";
	if (result === "running") return "sbx-info";
	if (result === "stopped" || result === "skipped") return "sbx-warn";
	return "sbx-plain";
}
