// packages.js - the Package Manager tab (docs/31 section 8): Installed, Updates, Browse and Sources over a list, the
// selected package's details beside it (a phone: one or the other), and the last action's output below. Everything it
// shows is the server's snapshot on packages:<wsId>; its buttons ask the nuke plugin's bindings to run `nuke package
// ...` there, so an action goes on with the tab closed.

import { h, icon, replace } from "/ui-kit/dom.js";
import { confirmDialog, choiceDialog } from "/ui-kit/dialog.js";
import {
	browseRows, checkoutOf, descriptionOf, filtered, installedRows, packageName, requirementFor, requirementMeaning, requirementOf,
	rowBringing, sameRepository, sourceOf, addWords, treeLink, updateOf, updateRows,
} from "./packagemodel.js";

const TABS = [["installed", "Installed"], ["updates", "Updates"], ["browse", "Browse"], ["sources", "Repositories"]];
const STALE_MS = 60000;   // a status older than this is read again when the tab opens

export class PackagesView {
	constructor(ctx, wanted, disposed) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.wsId = ctx.wsId || this.ied.workspace.wsId;
		this.disposed = disposed;
		this.snapshot = null;
		this.tab = "installed";
		this.selected = {};          // tab -> the selected row's key
		this.wanted = wanted;        // a package path to select once the status has it
		this.browseSource = null;
		this.versions = new Map();   // row key -> the version picked
		this.problem = "";
		this.outputOpen = false;
		this.showingDetails = false; // a phone shows the details instead of the list
		this.lastRun = 0;
		this.build();
		this.stopTopic = this.ied.client.subscribe("packages:" + this.wsId, { snap: (data) => this.received(data, true), evt: (kind, data) => kind === "changed" && this.received(data, false) });
	}

	// --- Building ---------------------------------------------------------------------------------------------------

	build() {
		this.tabButtons = TABS.map(([id, label]) => h("button.pm-tab", { role: "tab", dataset: { tab: id }, on: { click: () => this.choose(id) } }, label));
		this.search = h("input.input-field.pm-search", { type: "search", placeholder: "Search", "aria-label": "Search the packages", on: { input: () => this.render() } });
		this.busy = h("span.pm-busy.muted", { role: "status" });
		this.bar = h("div.pm-bar", null, h("nav.pm-tabs", { role: "tablist" }, this.tabButtons), this.search, this.busy,
			h("button.ib", { title: "Refresh (what this machine knows)", "aria-label": "Refresh", on: { click: () => this.refresh(false) } }, icon("refresh")),
			h("button.btn.flat.pm-fetch", { title: "Ask each source for newer releases and fetch the develop clones", on: { click: () => this.refresh(true) } }, "Check for updates"));
		this.banner = h("div.pm-banner", { hidden: true });
		this.list = h("div.pm-list", { role: "listbox", tabindex: "0", "aria-label": "Packages", on: { keydown: (event) => this.listKey(event) } });
		this.details = h("div.pm-details");
		this.outputHead = h("button.pm-output-head", { on: { click: () => this.toggleOutput() } });
		this.outputText = h("pre.pm-output-text");
		this.output = h("div.pm-output", { hidden: true }, this.outputHead, this.outputText);
		this.root = h("div.pm", null, this.bar, this.banner, h("div.pm-body", null, this.list, this.details), this.output);
		replace(this.ctx.el, this.root);
		this.render();
	}

	// --- The server's state -----------------------------------------------------------------------------------------

	received(data, first) {
		const before = this.snapshot && this.snapshot.run;
		this.snapshot = data;
		if (data.run && data.run.id !== this.lastRun) {
			if (this.lastRun && data.run.running) this.outputOpen = true;
			this.lastRun = data.run.id;
		}
		if (before && before.running && data.run && !data.run.running && this.browsed()) this.call("nuke.packages.browse", { source: this.browseSource, offline: true });
		if (first && (!data.statusMs || Date.now() - data.statusMs > STALE_MS) && !data.busy) this.refresh(false);
		this.render();
	}

	body() {
		return (this.snapshot && this.snapshot.status) || { sources: [], requirements: [], packages: [], checkouts: [] };
	}

	call(name, args) {
		this.problem = "";
		return this.ied.bindings.call(name, Object.assign({ wsId: this.wsId }, args)).catch((e) => {
			this.problem = e.message || String(e);
			this.render();
		});
	}

	refresh(fetch) {
		this.call("nuke.packages.refresh", { fetch });
		const source = this.tab === "browse" && this.browseSource;
		if (fetch && source) this.call("nuke.packages.browse", { source });
	}

	async run(words, question) {
		if (question && !(await confirmDialog(question))) return;
		this.outputOpen = true;
		await this.call("nuke.packages.run", { args: words });
	}

	// --- Rendering --------------------------------------------------------------------------------------------------

	render() {
		const body = this.body();
		this.selectWanted(body);
		for (const button of this.tabButtons) {
			const on = button.dataset.tab === this.tab;
			button.classList.toggle("on", on);
			button.setAttribute("aria-selected", on ? "true" : "false");
		}
		const updates = updateRows(body).length;
		replace(this.tabButtons[1], "Updates", updates ? h("span.badge.info", null, String(updates)) : null);
		this.renderBusy();
		this.renderBanner(body);
		const rows = this.rows(body);
		this.renderList(body, rows);
		this.renderDetails(body, rows.find((row) => row.key === this.selected[this.tab]) || null);
		this.renderOutput();
		this.root.classList.toggle("pm-showing-details", this.showingDetails);
	}

	renderBusy() {
		const snapshot = this.snapshot;
		const busy = snapshot ? snapshot.busy : "loading";
		const queued = snapshot && snapshot.queued ? " (" + snapshot.queued + " more queued)" : "";
		replace(this.busy, busy ? icon("loading", "spin") : null, busy ? "nuke package " + busy + "..." + queued : "");
	}

	renderBanner(body) {
		const lines = [];
		if (this.problem) lines.push(h("div.pm-error", null, this.problem));
		if (this.snapshot && this.snapshot.statusError) lines.push(h("div.pm-error", null, "nuke package status failed: " + this.snapshot.statusError));
		for (const line of body.unfetched || []) lines.push(h("div.pm-warn", null, line));
		for (const checkout of body.checkouts || []) {
			if (!checkout.writable) lines.push(h("div.pm-warn", null, checkout.source + " is " + checkout.behind + " commit(s) behind " + checkout.upstream + ": its files are read-only until you pull it."));
		}
		this.banner.hidden = lines.length === 0;
		replace(this.banner, lines);
	}

	rows(body) {
		let rows;
		if (this.tab === "installed") rows = installedRows(body);
		else if (this.tab === "updates") rows = updateRows(body);
		else if (this.tab === "browse") rows = this.browsed() && this.browsed().result ? browseRows(this.browsed().result) : [];
		else rows = (body.sources || []).map((source) => ({ key: source.name, item: source }));
		return filtered(rows, this.search.value);
	}

	browsed() {
		const all = (this.snapshot && this.snapshot.browse) || [];
		return all.find((b) => b.source === this.browseSource) || null;
	}

	renderList(body, rows) {
		const head = this.tab === "browse" ? this.browseHead(body) : this.tab === "updates" && rows.length ? this.updateAll(rows) : null;
		const items = rows.map((row) => this.rowElement(body, row));
		replace(this.list, head, items.length ? items : h("div.pm-empty.muted", null, this.emptyText(body)));
	}

	emptyText(body) {
		if (!this.snapshot || !this.snapshot.status) return this.snapshot && this.snapshot.statusError ? "No status." : "Reading the workspace's packages...";
		if (this.search.value.trim()) return "Nothing matches.";
		if (this.tab === "installed") return (body.sources || []).length ? "No packages yet: add one from Browse." : "No packages yet: add a source under Sources, then a package from Browse.";
		if (this.tab === "updates") return "Every package is at its newest release.";
		if (this.tab === "browse") {
			const found = this.browsed();
			if (!this.browseSource) return "Choose a source.";
			return found && found.error ? found.error : found ? "The source has no packages." : "Reading " + this.browseSource + "...";
		}
		return "No sources: add one above.";
	}

	rowElement(body, row) {
		const selected = row.key === this.selected[this.tab];
		const element = h("div.pm-row" + (selected ? ".sel" : ""), {
			role: "option", "aria-selected": selected ? "true" : "false", dataset: { key: row.key },
			on: { click: () => this.select(row.key), dblclick: () => this.select(row.key, true) },
		}, icon(this.tab === "sources" ? "repo" : "package"), h("div.pm-row-main", null, h("div.pm-row-name", null, row.key), h("div.pm-row-desc.muted", null, this.rowDescription(row))),
		h("div.pm-row-side", null, this.rowSide(body, row)));
		return element;
	}

	rowDescription(row) {
		if (this.tab === "sources") return row.item.url + (row.item.transitive ? " (named by a package's dependencies)" : "");
		return row.item.description !== undefined ? row.item.description : descriptionOf(row.item);
	}

	rowSide(body, row) {
		const item = row.item;
		if (this.tab === "sources") return row.item.transitive ? h("span.chip", null, "transitive") : null;
		if (this.tab === "browse") {
			const newest = (item.versions || [])[0];
			return [item.locked ? h("span.chip.ok", { title: "The workspace has " + item.locked }, item.locked) : null, h("span.pm-version", null, newest || "unreleased")];
		}
		const update = updateOf(item);
		const mode = item.mode === "develop" ? h("span.chip.info", { title: "Built from the develop clone " + item.folder }, "develop") : h("span.pm-lock", { title: "Locked: read-only" }, icon("lock"));
		return [mode, h("span.pm-version", null, item.version), update ? h("span.chip" + (update.breaking ? ".warn" : ".info"), { title: update.breaking ? "A newer major release: change the requirement to take it" : "An update within the requirement" }, (update.breaking ? "major " : "") + update.version) : null];
	}

	browseHead(body) {
		const sources = body.sources || [];
		if (!this.browseSource && sources.length) this.browseSource = sources[0].name;
		const picker = h("select.input-field.pm-source", { "aria-label": "Source", on: { change: (event) => this.browse(event.target.value) } },
			sources.map((s) => h("option", { value: s.name, selected: s.name === this.browseSource }, s.name)));
		return h("div.pm-list-head", null, h("span.muted", null, "Source"), picker,
			h("button.ib", { title: "Read the source again", "aria-label": "Read the source again", on: { click: () => this.browse(this.browseSource, true) } }, icon("refresh")));
	}

	browse(source, again) {
		this.browseSource = source;
		if (source && (again || !this.browsed())) this.call("nuke.packages.browse", { source });
		this.render();
	}

	updateAll(rows) {
		return h("div.pm-list-head", null, h("span.muted.grow", null, rows.length + " package(s) with newer releases"),
			h("button.btn.primary", { title: "nuke package update: the newest versions the requirements allow", on: { click: () => this.run(["update"]) } }, "Update all"));
	}

	select(key, open) {
		this.selected[this.tab] = key;
		if (open || window.matchMedia("(max-width: 719px)").matches) this.showingDetails = true;
		this.render();
	}

	selectWanted(body) {
		if (!this.wanted || !this.snapshot || !this.snapshot.status) return;
		const row = rowBringing(body, this.wanted);
		this.wanted = null;
		if (!row) return;
		this.tab = "installed";
		this.selected.installed = row.key;
		this.showingDetails = window.matchMedia("(max-width: 719px)").matches;
	}

	// The package a Manage Packages... run names (a Solution Explorer package node), in the open tab.
	reveal(path) {
		this.wanted = path;
		this.render();
	}

	choose(tab) {
		this.tab = tab;
		this.showingDetails = false;
		if (tab === "browse" && this.browseSource === null) {
			const sources = this.body().sources || [];
			if (sources.length) this.browse(sources[0].name);
		}
		this.render();
	}

	listKey(event) {
		const keys = [...this.list.querySelectorAll(".pm-row")].map((row) => row.dataset.key);
		if (!keys.length || (event.key !== "ArrowDown" && event.key !== "ArrowUp" && event.key !== "Enter")) return;
		event.preventDefault();
		const at = keys.indexOf(this.selected[this.tab]);
		if (event.key === "Enter") return at >= 0 && this.select(keys[at], true);
		this.select(keys[Math.max(0, Math.min(keys.length - 1, at + (event.key === "ArrowDown" ? 1 : -1)))]);
		const row = this.list.querySelector(".pm-row.sel");
		if (row) row.scrollIntoView({ block: "nearest" });
	}

	// --- Details ----------------------------------------------------------------------------------------------------

	renderDetails(body, row) {
		const back = h("button.btn.flat.pm-back", { on: { click: () => { this.showingDetails = false; this.render(); } } }, icon("arrow-left"), " Packages");
		if (!row) {
			replace(this.details, h("div.pm-empty.muted", null, this.tab === "sources" ? "Select a source." : "Select a package to see its versions, links and dependencies."));
			return;
		}
		const content = this.tab === "sources" ? this.sourceDetails(body, row.item) : this.tab === "browse" ? this.browseDetails(body, row) : this.installedDetails(body, row);
		replace(this.details, back, content);
	}

	installedDetails(body, row) {
		const item = row.item;
		const requirement = requirementOf(body, item);
		const checkout = checkoutOf(body, item);
		const links = item.links || {};
		return [
			h("div.pm-title", null, icon("package"), h("span.pm-name", null, row.key), h("span.pm-version", null, item.version),
				item.mode === "develop" ? h("span.chip.info", null, "develop") : h("span.chip", { title: "An installed version is never edited: develop the source to change it" }, "locked")),
			h("div.pm-desc", null, descriptionOf(item)),
			this.facts([
				["Source", this.link(item.source + " (" + item.url + ")", links.repository)],
				["Requirement", requirement ? requirement.requirement + (requirement.kind !== "version" ? " (" + requirement.kind + ")" : "") : "none of the workspace's own: " + this.requirers(item)],
				["Commit", this.link(item.commit.slice(0, 12), links.commit || links.tree)],
				["Folder", item.folder + (item.writable ? "" : " (read-only)")],
				["Packages", (item.uses || []).join(", ")],
				["Projects", (item.projects || []).join(", ") || "none"],
				checkout ? ["Clone", (checkout.branch || "detached") + ", " + checkout.ahead + " ahead, " + checkout.behind + " behind " + (checkout.upstream || "no upstream")] : null,
			]),
			this.versionPicker(body, row, requirement),
			this.actions(body, item, requirement, checkout),
			this.section("Depends on", (item.dependsOn || []).map((d) => this.edge(packageName(d.source, d.path), d.requirement + " -> " + d.version, d.because))),
			this.section("Required by", (item.requiredBy || []).map((r) => h("div.pm-edge", null, h("span", null, r.by), h("span.muted", null, " needs " + r.requirement)))),
			this.section("Contents", (item.packages || []).map((p) => h("div.pm-edge", null, h("span", null, p.path), h("span.muted", null, " " + (p.description || "") + (p.projects.length ? " (" + p.projects.join(", ") + ")" : ""))))),
		];
	}

	requirers(item) {
		return (item.requiredBy || []).map((r) => r.by + " " + r.requirement).join("; ") || "-";
	}

	versionPicker(body, row, requirement) {
		const item = row.item;
		const versions = item.versions || [];
		if (!versions.length) return null;
		const chosen = this.versions.get(row.key) || (updateOf(item) || { version: item.version }).version;
		const wanted = requirementFor(versions, versions.includes(chosen) ? chosen : versions[0]);
		const select = h("select.input-field", { "aria-label": "Version", on: { change: (event) => { this.versions.set(row.key, event.target.value); this.render(); } } },
			versions.map((v) => h("option", { value: v, selected: v === chosen }, v + (v === item.version ? " (installed)" : ""))));
		const words = addWords(requirement ? requirement.source : sourceOf(item), wanted, requirement && requirement.project);
		const same = requirement && requirement.requirement === wanted;
		const apply = h("button.btn", {
			disabled: same || undefined,
			title: "nuke package " + words.join(" "),
			on: { click: () => this.run(words) },
		}, requirement ? "Change requirement" : "Require it directly");
		return h("div.pm-picker", null, h("span.muted", null, "Version"), select, apply, h("div.pm-meaning.muted", null, wanted + ": " + requirementMeaning(wanted)));
	}

	actions(body, item, requirement, checkout) {
		const source = item.source;
		const update = updateOf(item);
		const buttons = [];
		if (update && !update.breaking) buttons.push(this.button("Update to " + update.version, ["update", packageName(item.source, item.path)], true));
		if (item.mode === "develop") {
			if (checkout && checkout.behind) buttons.push(this.button("Pull", ["pull", source], true));
			buttons.push(h("button.btn", { on: { click: () => this.release(item, source) } }, "Release..."));
			buttons.push(this.button("Lock", ["lock", source], false, { title: "Back to locked installs", message: "The workspace goes back to read-only installs of " + item.source + ". The clone's commits must be pushed first; it stays on disk.", confirm: "Lock" }));
		}
		else {
			buttons.push(this.button("Develop", ["develop", source], false, null, "Clone " + item.source + " into the packages folder and build from it, to change it with the program"));
		}
		if (requirement) buttons.push(this.button("Remove", ["remove", requirement.name].concat(requirement.project ? ["--project", requirement.project] : []), false,
			{ title: "Remove " + requirement.name, message: (requirement.project || "The project") + " stops using " + requirement.source + " (its [dependencies] entry goes).", confirm: "Remove", danger: true }));
		return h("div.pm-actions", null, buttons);
	}

	button(label, words, primary, question, title) {
		return h("button.btn" + (primary ? ".primary" : ""), { title: title || "nuke package " + words.join(" "), on: { click: () => this.run(words, question) } }, label);
	}

	async release(item, source) {
		const part = await choiceDialog({
			title: "Release " + packageName(item.source, item.path),
			message: "Tags the next version of " + (item.path || item.source) + " at the clone's HEAD and pushes its branch and the tag; packages of the repository that need it are released again too.",
			choices: [{ label: "Patch", value: "patch", primary: true }, { label: "Minor", value: "minor" }, { label: "Major", value: "major" }],
		});
		if (part) this.run(["release", item.path || ".", "--" + part, "--source=" + source]);
	}

	browseDetails(body, row) {
		const item = row.item;
		const result = this.browsed().result;
		const versions = item.versions || [];
		const chosen = this.versions.get(row.key) || versions[0];
		const wanted = chosen ? requirementFor(versions, chosen) : null;
		const source = item.path ? result.url + "/" + item.path : result.url;
		const install = () => this.run(wanted ? addWords(source, wanted) : ["add", source, "--rev", result.commit]);
		const picker = versions.length
			? h("div.pm-picker", null, h("span.muted", null, "Version"),
				h("select.input-field", { "aria-label": "Version", on: { change: (event) => { this.versions.set(row.key, event.target.value); this.render(); } } }, versions.map((v) => h("option", { value: v, selected: v === chosen }, v))),
				h("button.btn.primary", { title: "nuke package " + addWords(source, wanted).join(" "), on: { click: install } }, item.requirement ? "Change requirement" : "Install"),
				h("div.pm-meaning.muted", null, wanted + ": " + requirementMeaning(wanted)))
			: h("div.pm-picker", null, h("span.muted", null, "No release yet."), h("button.btn", { on: { click: install } }, "Install at " + result.commit.slice(0, 8)));
		return [
			h("div.pm-title", null, icon("package"), h("span.pm-name", null, row.key), item.locked ? h("span.chip.ok", null, "installed " + item.locked) : null),
			h("div.pm-desc", null, item.description || ""),
			this.facts([
				["Source", this.link(result.source + " (" + result.url + ")", result.web)],
				["Versions", versions.join(", ") || "none released"],
				["Released with", item.line && item.line !== item.path ? item.line + " (its nearest released folder)" : null],
				["Requirement", item.requirement || "not used"],
				["Projects", (item.projects || []).join(", ") || "none"],
				chosen && result.web ? ["At " + chosen, this.link("its files at " + chosen, treeLink(result.web, item.line, chosen))] : null,
			]),
			picker,
			item.requirement ? h("div.pm-actions", null, this.button("Remove", ["remove", source], false, { title: "Remove " + item.path, message: "The projects that use " + source + " stop using it.", confirm: "Remove", danger: true })) : null,
			this.section("Depends on", (item.dependencies || []).map((d) => this.edge(d.url ? packageName(result.source, d.path) : d.path, d.url ? d.requirement : "(a project of this package)", ""))),
		];
	}

	sourceDetails(body, source) {
		const used = (body.requirements || []).filter((r) => sameRepository(r.url, source.url)).map((r) => r.path + " " + r.requirement);
		return [
			h("div.pm-title", null, icon("repo"), h("span.pm-name", null, source.name), source.transitive ? h("span.chip", null, "transitive") : null),
			this.facts([["URL", source.url], ["Page", this.link(source.web || "none (a local repository)", source.web)], ["Used", used.join(", ") || "nothing yet"]]),
			h("div.pm-actions", null,
				h("button.btn.primary", { on: { click: () => { this.tab = "browse"; this.browse(source.name); } } }, "Browse")),
		];
	}

	facts(pairs) {
		return h("dl.pm-facts", null, pairs.filter((pair) => pair && pair[1] !== null && pair[1] !== undefined).map(([label, value]) => [h("dt", null, label), h("dd", null, value)]));
	}

	section(title, children) {
		return children.length ? h("div.pm-section", null, h("div.pm-section-title", null, title), children) : null;
	}

	edge(name, detail, because) {
		const body = this.body();
		const target = installedRows(body).find((row) => row.key === name);
		const label = target ? h("a.pm-link", { href: "#", on: { click: (event) => { event.preventDefault(); this.tab = "installed"; this.select(name, true); } } }, name) : h("span", null, name);
		return h("div.pm-edge", null, label, h("span.muted", null, " " + detail + (because ? " (" + because + ")" : "")));
	}

	link(text, url) {
		if (!url) return text;
		return h("a.pm-link", { href: url, target: "_blank", rel: "noopener", title: url, on: { click: (event) => this.openLink(event, url) } }, text);
	}

	openLink(event, url) {
		event.preventDefault();
		Promise.resolve(this.ied.commands.run("ied.browser.open", { url })).catch(() => window.open(url, "_blank", "noopener"));
	}

	// --- Output -----------------------------------------------------------------------------------------------------

	renderOutput() {
		const run = this.snapshot && this.snapshot.run;
		this.output.hidden = !run;
		if (!run) return;
		const state = run.running ? "running..." : run.exitCode === 0 ? "done" : "failed (exit " + run.exitCode + ")";
		this.output.classList.toggle("failed", !run.running && run.exitCode !== 0);
		this.output.classList.toggle("open", this.outputOpen);
		replace(this.outputHead, icon(this.outputOpen ? "chevron-down" : "chevron-right"), h("span.pm-output-command", null, "nuke package " + run.args.join(" ")), h("span.pm-output-state", null, state));
		this.outputText.textContent = run.running ? "" : run.output.trim() || "(no output)";
	}

	toggleOutput() {
		this.outputOpen = !this.outputOpen;
		this.render();
	}

	dispose() {
		if (this.stopTopic) this.stopTopic();
		this.disposed();
	}
}
