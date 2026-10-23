// transcript.js - a chat's turns as the activity shows them (docs/15 "Chat", mockup 02): the owner's message, then the
// agent's segments - thinking (collapsed), Markdown text, tool cards (expandable to their result), approval cards with
// the change's diff, the ask_user question, harness notes as grey lines - and the turn's footer: how it stopped, what
// the harness recovered, tokens and speed. Segments update in place as their events arrive.

import { h, icon, replace } from "/ui-kit/dom.js";
import { renderMarkdown } from "/ui-kit/markdown.js";
import { showImage } from "/ui-kit/imageview.js";
import { argsLine, compact, diffRows, duration, tokensPerSecond } from "./chatmodel.js";
import { userChips } from "./userchips.js";

const QUIET_STOPS = new Set(["finished", "user_cancelled"]);
const STOP_LABELS = { finished: "Finished", user_cancelled: "Stopped", interrupted: "Interrupted by a restart", no_model: "No model" };

function clock(ms) {
	if (!ms) return "";
	const at = new Date(ms);
	return String(at.getHours()).padStart(2, "0") + ":" + String(at.getMinutes()).padStart(2, "0");
}

// actions: {approve(approval, decision, message), allowAlways(approval), answer(question, text), openFile(path, line?),
// openChat(chatId), codeBlock(code, fence, closed) (a fenced block's colored <code>), colorDiff(path, spans)}
export class TranscriptView {
	constructor(model, actions) {
		this.model = model;
		this.actions = actions;
		this.el = h("div.chat-col");
		this.turnEls = new Map();
		this.segmentEls = new Map();
		this.dirty = new Set();
		this.expanded = new Set();
		this.frame = 0;
		this.prefillTimer = 0;
	}

	render() {
		this.turnEls.clear();
		this.segmentEls.clear();
		const earlier = this.model.hasMore ? h("div.chat-harness", null, icon("history"), h("span", null, "Earlier turns"), h("button.btn.flat", { on: { click: () => this.actions.loadEarlier() } }, "Show")) : null;
		const turns = this.model.turns.map((turn) => this.turnEl(turn));
		replace(this.el, earlier, ...turns, this.model.turns.length ? null : this.emptyEl());
	}

	emptyEl() {
		return h("div.chat-empty", null, icon("comment-discussion"), h("p", null, "Ask a question or describe a change. The turn runs on the server: close the browser and it keeps going."));
	}

	// Applies one change from ChatModel.event.
	update(change) {
		if (change.all) return this.render();
		if (change.segment) this.segmentChanged(change.segment, change.turn);
		else if (change.turn) this.turnChanged(change.turn);
		return null;
	}

	turnChanged(turn) {
		const existing = this.turnEls.get(turn.turnId);
		if (!existing) {
			const empty = this.el.querySelector(".chat-empty");
			if (empty) empty.remove();
			this.el.append(this.turnEl(turn));
			return;
		}
		replace(existing.agentHead, ...this.agentHeadParts(turn));
		this.showPrefill(existing.prefill, turn);
		replace(existing.foot, ...this.footParts(turn));
		existing.foot.hidden = !turn.stop;
	}

	segmentChanged(segment, turn) {
		const holder = this.turnEls.get(turn.turnId);
		if (!holder) return;
		let el = this.segmentEls.get(segment.segId);
		if (!el) {
			el = h("div.chat-seg", { dataset: { seg: segment.segId, type: segment.type } });
			this.segmentEls.set(segment.segId, el);
			holder.body.append(el);
			this.fill(el, segment);
			return;
		}
		this.dirty.add(segment.segId);
		if (!this.frame) this.frame = requestAnimationFrame(() => this.flush());
	}

	flush() {
		this.frame = 0;
		for (const segId of this.dirty) {
			const found = this.model.segment(segId);
			const el = this.segmentEls.get(segId);
			if (found && el) this.fill(el, found.segment);
		}
		this.dirty.clear();
	}

	// --- Turns -------------------------------------------------------------------------------------------------------

	turnEl(turn) {
		const user = h("div.chat-user", null, turn.user ? turn.user.text : "", this.model.summary ? userChips(turn.user, this.model.summary.wsId, this.actions) : null);
		const agentHead = h("div.chat-head.agent");
		replace(agentHead, ...this.agentHeadParts(turn));
		const body = h("div.chat-body");
		for (const segment of turn.segments) {
			const el = h("div.chat-seg", { dataset: { seg: segment.segId, type: segment.type } });
			this.segmentEls.set(segment.segId, el);
			this.fill(el, segment);
			body.append(el);
		}
		const prefill = h("div.chat-prefill");
		this.showPrefill(prefill, turn);
		const foot = h("div.chat-foot");
		replace(foot, ...this.footParts(turn));
		foot.hidden = !turn.stop;
		const el = h("section.chat-turn", { dataset: { turn: turn.turnId } }, this.userHead(turn), user, agentHead, body, prefill, foot);
		this.turnEls.set(turn.turnId, { el, agentHead, body, prefill, foot });
		return el;
	}

	// Who sent the turn's message: the owner, or another chat (docs/04 "Chats talk to each other"), which a click opens.
	userHead(turn) {
		const from = turn.user && turn.user.from;
		if (!from) return h("div.chat-head", null, icon("person"), h("b", null, "You"), h("span.muted", null, clock(turn.startedMs)));
		return h("div.chat-head.from", null, icon("comment-discussion"), h("b", null, "From chat"),
			h("button.chat-from-link", { title: "Open " + from.title, on: { click: () => this.actions.openChat(from.chatId) } }, from.title || from.chatId),
			h("span.muted", null, clock(turn.startedMs)));
	}

	// While the model's server reads the prompt (docs/12 "Watchdogs"): from llama.cpp's progress how much, of how many
	// tokens (the cached ones were not read again), and for how long; from another server only the estimate, its bar
	// moving with the clock.
	showPrefill(el, turn) {
		const progress = !turn.stop && turn.prefill;
		el.hidden = !progress;
		if (!progress) return;
		if (progress.estimated) {
			this.tickPrefill();
			const elapsed = Math.max(0, Date.now() - progress.startedMs);
			const share = progress.estimatedMs > 0 ? Math.min(99, Math.floor(elapsed * 100 / progress.estimatedMs)) : 0;
			replace(el, icon("loading", "spin"), h("span", null, "Reading the prompt"), h("span.chat-bar", null, h("i", { style: { width: share + "%" } })),
				h("span.meta", null, "about " + duration(progress.estimatedMs) + " for " + compact(progress.total) + " tokens (estimated) \u00b7 " + duration(elapsed)));
			return;
		}
		const percent = progress.total > 0 ? Math.min(100, Math.floor(progress.processed * 100 / progress.total)) : 0;
		replace(el, icon("loading", "spin"), h("span", null, "Reading the prompt"), h("span.chat-bar", null, h("i", { style: { width: percent + "%" } })), h("b", null, percent + "%"),
			h("span.meta", null, compact(progress.processed) + " of " + compact(progress.total) + " tokens" + (progress.cache ? " (" + compact(progress.cache) + " cached)" : "") + " \u00b7 " + duration(progress.time_ms || 0)));
	}

	// While an estimated read shows, its bar moves with the clock; the timer stops when none shows.
	tickPrefill() {
		if (this.prefillTimer) return;
		this.prefillTimer = setInterval(() => {
			let shown = false;
			for (const [turnId, holder] of this.turnEls) {
				const turn = this.model.turn(turnId);
				if (!turn || turn.stop || !turn.prefill || !turn.prefill.estimated) continue;
				shown = true;
				this.showPrefill(holder.prefill, turn);
			}
			if (shown && this.el.isConnected) return;
			clearInterval(this.prefillTimer);
			this.prefillTimer = 0;
		}, 1000);
	}

	agentHeadParts(turn) {
		const running = !turn.stop;
		const took = turn.endedMs && turn.startedMs ? duration(turn.endedMs - turn.startedMs) : "";
		return [icon("sparkle", "c-ai"), h("b", null, turn.mode === "agent" ? "Agent" : turn.mode === "plan" ? "Planner" : "Assistant"),
			h("span.muted", null, [turn.model, turn.group].filter(Boolean).join(" \u00b7 ")),
			h("span.meta", null, running ? h("span.chat-live", null, icon("loading", "spin"), "running") : took)];
	}

	footParts(turn) {
		if (!turn.stop) return [];
		const quiet = QUIET_STOPS.has(turn.stop);
		const parts = [h("span.chat-stop" + (quiet ? "" : ".attn"), { title: "The stop reason (docs/12)" }, STOP_LABELS[turn.stop] || "Paused: " + turn.stop.replace(/_/g, " "))];
		if (turn.recoveries && turn.recoveries.length) parts.push(h("span.chat-recoveries", { title: "What the harness did to keep the run going" }, "harness: " + turn.recoveries.join("; ")));
		const speed = tokensPerSecond(turn);
		parts.push(h("span.meta", null, compact(turn.tokensIn) + " in / " + compact(turn.tokensOut) + " out" + (turn.ttftMs ? " \u00b7 TTFT " + duration(turn.ttftMs) : "") + (speed ? " \u00b7 " + speed + " tok/s" : "")));
		return parts;
	}

	// --- Segments ----------------------------------------------------------------------------------------------------

	fill(el, segment) {
		if (segment.type === "text") replace(el, ...this.textParts(segment));
		else if (segment.type === "thinking") replace(el, ...this.thinkingParts(segment));
		else if (segment.type === "tool") replace(el, this.toolCard(segment));
		else if (segment.type === "harness") replace(el, this.harnessLine(segment));
		else if (segment.type === "steering") replace(el, h("div.chat-user.inject", null, h("span.chat-tag", null, "injected"), segment.text));
	}

	textParts(segment) {
		const body = h("div.chat-text");
		body.append(...this.markdown(segment.text || ""));
		if (!segment.done) return [body];
		const meta = [segment.model, segment.ms ? duration(segment.ms) : "", segment.tokens ? segment.tokens + " tok" : ""].filter(Boolean).join(" \u00b7 ");
		return [body, meta ? h("div.chat-segmeta", null, meta) : null];
	}

	markdown(text) {
		return renderMarkdown(text, { code: (code, fence, closed) => this.actions.codeBlock(code, fence, closed) });
	}

	thinkingParts(segment) {
		const open = this.expanded.has(segment.segId);
		const toggle = () => {
			if (open) this.expanded.delete(segment.segId);
			else this.expanded.add(segment.segId);
			this.fill(this.segmentEls.get(segment.segId), segment);
		};
		const facts = segment.done ? [segment.ms ? duration(segment.ms) : "", segment.tokens ? compact(segment.tokens) + " tok" : ""].filter(Boolean).join(" \u00b7 ") : "thinking\u2026";
		const head = h("button.chat-think", { "aria-expanded": open ? "true" : "false", on: { click: toggle } }, icon(open ? "chevron-down" : "chevron-right"), h("span.lbl", null, "Thinking"), h("span", null, facts));
		const tail = !segment.done && !open ? h("div.chat-think-tail", null, (segment.text || "").slice(-160)) : null;
		return [head, open ? h("div.chat-think-body", null, segment.text || "") : tail];
	}

	harnessLine(segment) {
		const level = segment.kind === "paused" || segment.kind === "no_model" ? ".attn" : "";
		const glyph = segment.kind === "paused" ? "debug-pause" : segment.kind === "routing" ? "arrow-right" : segment.kind === "no_model" ? "warning" : "sync";
		return h("div.chat-harness" + level, { title: "A note from IED's harness" + (segment.kind ? " (" + segment.kind + ")" : "") }, icon(glyph), h("span", null, (level ? "" : "harness: ") + segment.text));
	}

	// --- Tool cards --------------------------------------------------------------------------------------------------

	toolCard(segment) {
		const approval = this.model.approvalFor(segment.callId);
		if (approval) return this.approvalCard(segment, approval);
		if (segment.tool === "ask_user" && this.model.question && this.model.question.callId === segment.callId) return this.questionCard(segment, this.model.question);
		if (segment.tool === "finish" && segment.state === "ok") return this.finishCard(segment);
		const open = this.expanded.has(segment.segId);
		const result = segment.result;
		const toggle = () => {
			if (open) this.expanded.delete(segment.segId);
			else this.expanded.add(segment.segId);
			this.fill(this.segmentEls.get(segment.segId), segment);
		};
		const head = h("button.chat-tool-head", { "aria-expanded": open ? "true" : "false", on: { click: toggle } },
			statusIcon(segment.state), h("span.tname", null, segment.tool), h("span.targs", null, argsLine(segment.tool, segment.argsText)),
			h("span.tstat", null,
				segment.plus ? h("span.plus", null, "+" + segment.plus) : null, segment.minus ? h("span.minus", null, "-" + segment.minus) : null,
				segment.source ? h("span.src", null, segment.source) : null,
				segment.ms !== undefined && segment.state !== "running" && segment.state !== "streaming" ? h("span", null, duration(segment.ms)) : null,
				icon(open ? "chevron-down" : "chevron-right")));
		const out = open ? this.toolResult(segment, result) : null;
		return h("div.chat-tool" + (segment.state === "error" ? ".failed" : ""), { dataset: { call: segment.callId, state: segment.state || "" } }, head, this.toolImages(result), out);
	}

	// The images the result showed the model (docs/13 "Images in tool results"), from the workspace; open or closed. A
	// click shows one at its size over the page; a middle click opens it in a browser tab.
	toolImages(result) {
		if (!result || !result.images || !result.images.length || !this.model.summary) return null;
		const base = "/w/" + encodeURIComponent(this.model.summary.wsId) + "/files/";
		return h("div.chat-tool-images", null, ...result.images.map((image) => {
			const url = base + image.path.split("/").map(encodeURIComponent).join("/");
			const show = (event) => {
				event.preventDefault();
				showImage({ src: url, title: image.path });
			};
			return h("a", { href: url, target: "_blank", rel: "noopener", title: image.path, on: { click: show } }, h("img", { src: url, alt: image.path, loading: "lazy" }));
		}));
	}

	toolResult(segment, result) {
		const preview = result ? result.preview : "";
		if (preview && /^@@ -\d/.test(preview)) return this.diffBlock(preview, pathOf(segment.argsText));
		return h("div.chat-tool-out", null,
			h("div.chat-tool-args", null, segment.argsText || ""),
			preview ? h("div" + (result.isError ? ".c-error" : ""), null, preview) : h("div.muted", null, segment.state === "running" ? "Running\u2026" : "No result yet."));
	}

	// A change's diff, its code in the colors of its file's language: `path`'s, or each file's of a diff of several.
	diffBlock(unified, path) {
		const block = h("div.chat-diff");
		let file = path;
		let spans = [];
		const color = () => spans.length && this.actions.colorDiff(file, spans);
		for (const row of diffRows(unified)) {
			if (row.kind === "f") {
				color();
				file = row.path || path;
				spans = [];
				block.append(diffFileRow(row));
				continue;
			}
			const line = row.kind === "@"
				? h("div.dl.hunk", null, h("span.no"), h("span.no"), h("span.code", null, row.text))
				: h("div.dl" + (row.kind === "+" ? ".add" : row.kind === "-" ? ".del" : ""), null,
					h("span.no", null, row.old === null ? "" : String(row.old)), h("span.no", null, row.new === null ? "" : String(row.new)),
					h("span.sign", null, row.kind === " " ? "" : row.kind), h("span.code", null, row.text));
			if (row.kind !== "@") spans.push(line.lastChild);
			block.append(line);
		}
		color();
		return block;
	}

	approvalCard(segment, approval) {
		const deny = h("div.chat-deny", { hidden: true });
		const field = h("input.input-field", { placeholder: "Why not? (the model reads it)", "aria-label": "Reason for denying" });
		const sendDeny = () => this.actions.approve(approval, "deny", field.value.trim());
		field.addEventListener("keydown", (event) => {
			event.stopPropagation();
			if (event.key === "Enter") sendDeny();
		});
		replace(deny, field, h("button.btn", { on: { click: sendDeny } }, "Deny"));
		return h("div.chat-approval", { dataset: { call: segment.callId, approval: approval.approvalId } },
			h("div.chat-tool-head", null, icon("shield", "c-warn"), h("span.tname", null, approval.tool), h("span.targs", null, argsLine(approval.tool, approval.args ? JSON.stringify(approval.args) : segment.argsText)),
				h("span.tstat", null, approval.plus ? h("span.plus", null, "+" + approval.plus) : null, approval.minus ? h("span.minus", null, "-" + approval.minus) : null, h("span", null, "awaiting approval"))),
			approval.summary ? h("div.chat-approval-what", null, approval.summary) : null,
			approval.diff ? this.diffBlock(approval.diff, (approval.args && approval.args.path) || pathOf(segment.argsText)) : null,
			h("div.why", null, approval.why),
			h("div.actions", null,
				h("button.btn.primary", { title: "Alt+A", on: { click: () => this.actions.approve(approval, "allow") } }, icon("check"), "Allow once"),
				h("button.btn", { title: "Allow calls that match " + approval.subject + " for the rest of this chat", on: { click: () => this.actions.approve(approval, "chat") } }, "Allow for this chat", h("span.muted.desk", null, ": " + approval.subject)),
				h("button.btn", { title: "Add " + approval.subject + " to ai.permissions' allow rules", on: { click: () => this.actions.allowAlways(approval) } }, "Always allow\u2026"),
				h("button.btn", { title: "Alt+D", on: { click: () => {
					deny.hidden = false;
					field.focus();
				} } }, "Deny with message\u2026"),
				h("span.kbd", null, "Alt+A allow \u00b7 Alt+D deny")),
			deny);
	}

	questionCard(segment, question) {
		const field = h("input.input-field", { placeholder: "Your answer", "aria-label": "Your answer" });
		const send = (text) => text && this.actions.answer(question, text);
		field.addEventListener("keydown", (event) => {
			event.stopPropagation();
			if (event.key === "Enter") send(field.value.trim());
		});
		return h("div.chat-question", { dataset: { call: segment.callId, question: question.questionId } },
			h("div.chat-tool-head", null, icon("question", "c-info"), h("span.tname", null, "ask_user"), h("span.tstat", null, "waiting for your answer")),
			h("div.chat-question-text", null, question.question),
			question.options && question.options.length ? h("div.actions", null, ...question.options.map((option) => h("button.btn", { on: { click: () => send(option) } }, option))) : null,
			question.allowText ? h("div.chat-deny", null, field, h("button.btn.primary", { on: { click: () => send(field.value.trim()) } }, "Answer")) : null);
	}

	finishCard(segment) {
		let args = {};
		try {
			args = JSON.parse(segment.argsText || "{}");
		}
		catch {
			args = {};
		}
		return h("div.chat-finish", { dataset: { call: segment.callId } },
			h("div.chat-tool-head", null, icon("pass", "c-saved"), h("span.tname", null, "Finished")),
			h("div.chat-finish-body.chat-text", null, ...this.markdown(args.summary || ""),
				args.verification ? h("div.chat-finish-verify", null, h("b", null, "Verified: "), args.verification) : null));
	}
}

// A file of a diff of several: its path, what became of it, its counts.
function diffFileRow(row) {
	const state = row.status === "renamed" ? "renamed from " + row.oldPath : row.status;
	return h("div.dl.file", { title: row.path }, h("span.fpath", null, row.path), state ? h("span.fstate", null, state) : null,
		h("span.fcount", null, row.plus ? h("span.plus", null, "+" + row.plus) : null, row.minus ? h("span.minus", null, "-" + row.minus) : null));
}

// The file a call's arguments name ("" when none does).
function pathOf(argsText) {
	try {
		const args = JSON.parse(argsText || "{}");
		return typeof args.path === "string" ? args.path : "";
	}
	catch {
		return "";
	}
}

function statusIcon(state) {
	if (state === "ok") return icon("pass", "c-saved");
	if (state === "error") return icon("error", "c-error");
	if (state === "denied") return icon("circle-slash", "c-warn");
	if (state === "interrupted") return icon("debug-pause", "c-warn");
	if (state === "awaiting_approval") return icon("shield", "c-warn");
	if (state === "awaiting_answer") return icon("question", "c-info");
	return icon("loading", "spin c-info");
}
