// parts.js - pieces the Sandbox activity's views share: a run's row (who started it - the AI and its chat, or you - its
// sandbox, command and how it is going), a console that follows a run's output, Docker's state in a line, and the tones
// of states and results. Tokens only (docs/07): AI is the AI accent, running/pass green, busy blue, failed red.

import { h, icon, replace } from "/ui-kit/dom.js";
import { ago, duration, runResult, shortPlatform, whoOf } from "./model.js";

export const TONE = { ok: "sbx-ok", info: "sbx-info", warn: "sbx-warn", error: "sbx-error", "": "sbx-plain" };

export function resultTone(result) {
	if (result === "pass") return "sbx-ok";
	if (result === "fail" || result === "error") return "sbx-error";
	if (result === "running") return "sbx-info";
	if (result === "stopped" || result === "skipped") return "sbx-warn";
	return "sbx-plain";
}

// A run's state as an icon: turning while it runs, a check or a cross once it ended.
export function runIcon(run) {
	if (run.running) return h("span.sbx-run-icon.sbx-info", { title: "running" }, icon("loading", "spin"));
	const failed = run.exitCode !== 0 || run.stopped || run.problem;
	if (run.stopped) return h("span.sbx-run-icon.sbx-warn", { title: run.stopped }, icon("debug-stop"));
	return h("span.sbx-run-icon." + (failed ? "sbx-error" : "sbx-ok"), { title: runResult(run) }, icon(failed ? "error" : "pass"));
}

// Who started a run: "AI" in the AI's accent (with its chat's title in the tooltip), or "You".
export function whoChip(run, store) {
	const who = whoOf(run, store.chatTitles);
	if (!who.who) return null;
	return h("span.sbx-who" + (who.ai ? ".ai" : ""), { title: who.ai ? "Started by the AI" + (who.chat ? " in the chat \u201c" + who.chat + "\u201d" : "") : "Started by " + who.who.toLowerCase() },
		who.ai ? icon("sparkle") : null, who.who);
}

// One run in a list: its state, who started it, the sandbox, the command and when.
export function runRow(run, store, selected, onClick) {
	const now = Date.now();
	const when = run.running ? duration(now - run.startedMs) || "starting" : runResult(run) + " \u00b7 " + (duration(run.endedMs - run.startedMs) || "0 s") + " \u00b7 " + ago(run.endedMs, now);
	return h("div.sbx-run" + (selected ? ".sel" : "") + (run.by === "ai" ? ".by-ai" : ""), { role: "option", "aria-selected": selected ? "true" : "false", dataset: { run: String(run.id) }, on: { click: onClick } },
		runIcon(run),
		h("div.sbx-run-main", null,
			h("div.sbx-run-top", null, whoChip(run, store), h("span.sbx-run-sandbox", null, run.sandbox || "Docker")),
			h("div.sbx-run-label", { title: run.label }, run.label)),
		h("span.sbx-run-when.muted", null, when));
}

// A run's header in a console: who (and the AI's chat, with a way to it), where, what, how it is going, Stop.
export function runHeader(run, store) {
	const who = whoOf(run, store.chatTitles);
	const box = run.sandbox ? store.sandbox(run.sandbox) : null;
	const where = run.sandbox ? run.sandbox + (box ? " (" + shortPlatform(box.platform) + (box.emulated ? ", emulated" : "") + ")" : "") : "Docker";
	const state = run.running ? "running " + duration(Date.now() - run.startedMs) : runResult(run) + " after " + (duration(run.endedMs - run.startedMs) || "0 s");
	return [
		h("div.sbx-console-who", null,
			whoChip(run, store),
			who.ai && who.chat ? h("span.sbx-console-chat", null, "\u201c" + who.chat + "\u201d") : null,
			who.ai && run.chatId ? h("button.btn.flat.sbx-small", { title: "Open the chat the AI ran it from", on: { click: () => openChat(store.ied, run.chatId) } }, icon("comment-discussion"), "Open chat") : null,
			h("span.sbx-spacer"),
			h("span.sbx-console-state." + (run.running ? "sbx-info" : run.exitCode === 0 && !run.stopped && !run.problem ? "sbx-ok" : "sbx-error"), null, state),
			run.running ? h("button.btn.flat.sbx-small", { title: "Stop it (the command inside the sandbox too)", on: { click: () => store.request("ied.sandbox/stopRun", { id: run.id }) } }, icon("debug-stop"), "Stop") : null),
		h("div.sbx-console-what", null, h("span.sbx-console-where", null, where), h("code.sbx-console-command", { title: run.label }, run.label)),
	];
}

export async function openChat(ied, chatId) {
	await ied.commands.run("shell.showActivity", { id: "ied.chat" });
	ied.tabs.open("chat:" + chatId, { preview: false });
}

// A run's output that keeps to its end while the reader is there, and stays where the reader scrolled otherwise.
export class Console {
	constructor() {
		this.el = h("pre.sbx-console-text", { tabindex: "0" });
		this.runId = null;
	}

	show(runId, text) {
		const same = runId === this.runId;
		const atEnd = this.el.scrollHeight - this.el.scrollTop - this.el.clientHeight < 24;
		this.runId = runId;
		this.el.textContent = text || "";
		if (!same || atEnd) this.el.scrollTop = this.el.scrollHeight;
	}

	append(runId, text) {
		if (runId === this.runId) this.show(runId, text);
	}
}

// Docker's state in one line, with the one action it needs.
export function engineLine(store) {
	const engine = store.engine();
	if (!engine) return [icon("loading", "spin"), h("span.muted", null, "Looking at Docker...")];
	if (engine.state === "ready") {
		return [h("span.sbx-dot.sbx-ok", { title: "Docker's engine is running" }), h("span.sbx-engine-name", null, "Docker " + engine.serverVersion),
			h("span.muted.sbx-engine-facts", { title: [engine.product, engine.os + "/" + engine.arch, engine.cpus + " CPUs"].filter(Boolean).join(" \u00b7 ") }, engine.os + "/" + engine.arch)];
	}
	const tone = engine.state === "starting" ? "sbx-info" : engine.state === "missing" || engine.state === "failed" ? "sbx-error" : "sbx-warn";
	return [engine.state === "starting" ? icon("loading", "spin") : h("span.sbx-dot." + tone), h("span.sbx-engine-name", null, "Docker " + engine.state),
		engine.state === "stopped" || engine.state === "failed" ? h("button.btn.primary.sbx-small", { on: { click: () => store.request("ied.sandbox/engine", { action: "start" }) } }, icon("play"), "Start Docker") : null,
		engine.state === "missing" ? h("a.sbx-link", { href: "https://www.docker.com/products/docker-desktop/", target: "_blank", rel: "noopener" }, "Get Docker Desktop") : null,
		h("div.sbx-engine-advice.muted", null, engine.advice || engine.error || "")];
}

// The platforms Docker runs, native ones marked.
export function platformChips(engine) {
	return ((engine && engine.platforms) || []).map((p) => h("span.sbx-chip" + (p.native ? ".native" : ""), { title: p.platform + (p.native ? " (native)" : " (emulated: 5-20x slower)") }, shortPlatform(p.platform)));
}

export function addStylesheet(href) {
	if (document.querySelector('link[href="' + href + '"]')) return;
	document.head.append(Object.assign(document.createElement("link"), { rel: "stylesheet", href }));
}

export { replace };
