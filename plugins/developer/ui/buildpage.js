// buildpage.js - Settings > Environment > Developer (docs/28): developer.enabled (only when IED is built from source), and the desktop build - the checkout
// it comes from, what the install was made from and how far the checkout has moved on since - with Rebuild and relaunch:
// `nuke install-ied` on the server (ied.developer/rebuild), its log as it runs, then the desktop app moved onto the new
// install (window.iedDesktop.relaunch, when the app has it). The server owns the build; this page only shows it.

import { h, icon, replace } from "/ui-kit/dom.js";

const STATE_TOPIC = "ied.developer/state";

export function shortCommit(commit) {
	return commit ? commit.slice(0, 7) : "unknown";
}

// "4:07" for 247 seconds.
export function duration(ms) {
	const seconds = Math.max(0, Math.round(ms / 1000));
	return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0");
}

// The build in one line: "Building - 1:23", "Built in 3:12", "Failed (exit 1) after 0:42", "Canceled after 0:10".
export function buildLine(build, nowMs) {
	if (!build || build.state === "idle") return "";
	const took = duration((build.state === "running" ? nowMs : build.endedMs) - build.startedMs);
	if (build.state === "running") return "Building - " + took;
	if (build.state === "succeeded") return "Built in " + took;
	if (build.state === "canceled") return "Canceled after " + took;
	return "Failed (exit " + build.exitCode + ") after " + took;
}

// What the page says of the source and the install: [label, value] rows.
export function installRows(state) {
	const rows = [["Source checkout", state.source || "none: IED runs neither from a checkout nor from an install of one"]];
	if (!state.source) return rows;
	if (state.install) {
		const when = state.install.installedMs ? new Date(state.install.installedMs).toLocaleString() : "unknown";
		rows.push(["Desktop install", shortCommit(state.install.commit) + ", " + when + (state.install.dirty ? " (with uncommitted changes)" : "")]);
	}
	else rows.push(["Desktop install", "this IED runs from the checkout; Rebuild installs it as the desktop app"]);
	const ahead = state.behind > 0 ? ", " + state.behind + (state.behind === 1 ? " commit" : " commits") + " newer than the install" : state.behind === 0 ? ", what the install was made from" : "";
	rows.push(["Checkout", (state.head ? "HEAD " + shortCommit(state.head) : "HEAD unknown") + ahead]);
	return rows;
}

// Whether this window is the desktop app with relaunch (docs/27).
export function canRelaunch(win) {
	return Boolean(win.iedDesktop && typeof win.iedDesktop.relaunch === "function");
}

// Relaunches `win` once the next build has run and succeeded, whether or not the page that asked is still open; a build
// that fails, is canceled or is lost (a server that restarted shows none) ends the wait. Gives the function that stops
// waiting.
export function relaunchWhenBuilt(client, win) {
	let ran = false;
	let done = false;
	let stop = null;
	const end = () => {
		if (done) return;
		done = true;
		if (stop) stop();
	};
	const watch = (data) => {
		const state = data.build.state;
		if (done) return;
		if (state === "idle") {
			if (ran) end();
			return;
		}
		if (state === "running") {
			ran = true;
			return;
		}
		if (!ran) return;
		end();
		if (state === "succeeded" && canRelaunch(win)) win.iedDesktop.relaunch();
	};
	stop = client.subscribe(STATE_TOPIC, { snap: watch, evt: (kind, data) => kind === "changed" && watch(data) });
	if (done) stop();
	return end;
}

export class BuildPage {
	// ctx: {el, ied, settings} (a settings page's context)
	constructor(ctx) {
		this.ied = ctx.ied;
		this.settings = ctx.settings;
		this.state = null;
		this.setting = h("div.dev-setting");
		this.rows = h("div.dev-rows");
		this.actions = h("div.dev-actions");
		this.status = h("span.dev-status");
		this.note = h("p.set-desc.dev-note", { hidden: true });
		this.log = h("pre.dev-log", { "aria-label": "Build log" });
		ctx.el.append(h("div.set-crumbs", null, "Environment \u203a Developer"), h("h1", null, "Developer"), this.setting,
			h("h2.dev-head", null, "Desktop build"),
			h("p.set-desc.dev-desc", null, "Keeps the desktop app built from your checkout: Rebuild runs ", h("code", null, "nuke install-ied"), " there (the server, the CLI, the desktop app, the web client and the plugins), then the app relaunches onto it."),
			this.rows, h("div.dev-bar", null, this.actions, this.status), this.note, this.log);
		this.renderSetting();
		this.stopSettings = this.settings.onChange(() => this.renderSetting());
		this.stopState = this.ied.client.subscribe(STATE_TOPIC, {
			snap: (data) => this.shown(data),
			evt: (kind, data) => this.changed(kind, data),
		});
		this.ied.client.request("ied.developer/refresh", {}).catch(() => {});
		this.ticker = setInterval(() => this.state && this.state.build.state === "running" && this.renderStatus(), 1000);
	}

	dispose() {
		clearInterval(this.ticker);
		this.stopSettings();
		this.stopState();
	}

	// The switch only where it can do something: IED built from its source checkout.
	renderSetting() {
		if (!this.state) replace(this.setting);
		else if (!this.state.source) replace(this.setting, h("p.set-desc.dev-off", null, "Developer mode is offered only when IED is built from its source checkout: run it from the checkout, or install it from one with ", h("code", null, "nuke install-ied"), "."));
		else replace(this.setting, ...this.settings.keys.map((key) => this.settings.row(key)));
	}

	shown(data) {
		this.state = data;
		this.renderSetting();
		replace(this.rows, ...installRows(data).flatMap(([label, value]) => [h("span.k", null, label), h("span.v", null, value)]));
		replace(this.log, data.build.lines.join("\n"));
		this.log.scrollTop = this.log.scrollHeight;
		this.renderActions();
		this.renderStatus();
	}

	changed(kind, data) {
		if (kind === "changed") this.shown(data);
		else if (kind === "build.lines" && this.state) {
			const follow = this.log.scrollHeight - this.log.scrollTop - this.log.clientHeight < 40;
			this.state.build.lines.push(...data.lines);
			this.log.append((this.log.textContent ? "\n" : "") + data.lines.join("\n"));
			if (follow) this.log.scrollTop = this.log.scrollHeight;
		}
	}

	renderActions() {
		const build = this.state.build;
		const running = build.state === "running";
		const relaunchable = canRelaunch(window) && Boolean(this.state.install);
		const buttons = [];
		if (this.state.source && !running) {
			if (relaunchable) buttons.push(h("button.btn.primary", { on: { click: () => this.rebuild(true) } }, icon("refresh"), "Rebuild and relaunch"));
			buttons.push(h("button.btn", { on: { click: () => this.rebuild(false) } }, icon("tools"), "Rebuild"));
		}
		if (running) buttons.push(h("button.btn", { on: { click: () => this.cancel() } }, icon("debug-stop"), "Cancel"));
		if (build.state === "succeeded" && relaunchable) buttons.push(h("button.btn", { on: { click: () => this.relaunch() } }, icon("debug-restart"), "Relaunch"));
		replace(this.actions, ...buttons);
		const restartByHand = build.state === "succeeded" && !relaunchable;
		this.note.hidden = !restartByHand;
		this.note.textContent = restartByHand ? "Installed. Restart IED to use the new build: `ied stop`, then open IED." : "";
	}

	renderStatus() {
		const build = this.state.build;
		this.status.textContent = buildLine(build, Date.now());
		this.status.className = "dev-status" + (build.state === "failed" ? " c-error" : build.state === "succeeded" ? " c-saved" : "");
	}

	async rebuild(relaunchAfter) {
		const stopWaiting = relaunchAfter ? relaunchWhenBuilt(this.ied.client, window) : null;
		try {
			await this.ied.client.request("ied.developer/rebuild", {});
		}
		catch (e) {
			if (stopWaiting) stopWaiting();
			this.ied.toast(e.message, { level: "error" });
		}
	}

	cancel() {
		this.ied.client.request("ied.developer/cancel", {}).catch((e) => this.ied.toast(e.message, { level: "error" }));
	}

	relaunch() {
		if (canRelaunch(window)) window.iedDesktop.relaunch();
	}
}
