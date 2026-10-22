// chatmodel.js - a chat as this client sees it (docs/10 "Chats"): the chat:<chatId> snapshot with its events folded in
// - turns and their segments, tool states, approvals and the question waiting, tasks, queued messages, the files the
// AI changed, its clipboard and the context. Each event says what changed, so the view redraws only that. Also the
// chats:<wsId> list and the small facts the views show (a tool call's one-line arguments, a tab's glyphs, tokens per
// second).

export const STATE_LABELS = { idle: "Idle", running: "Running", awaiting_approval: "Needs approval", awaiting_answer: "Needs an answer", paused: "Paused" };
export const MODES = [["ask", "Ask"], ["plan", "Plan"], ["agent", "Agent"]];
export const PERMISSIONS = [["plan", "Read only"], ["ask", "Ask for edits"], ["acceptEdits", "Accept edits"], ["auto", "Auto"]];

export class ChatModel {
	constructor() {
		this.summary = null;
		this.model = "";
		this.groupInUse = "";
		this.turns = [];
		this.hasMore = false;
		this.approvals = [];
		this.question = null;
		this.tasks = [];
		this.queued = [];
		this.modified = [];
		this.resources = [];
		this.clips = [];
		this.context = null;
	}

	// The snapshot replaces everything.
	snapshot(data) {
		this.summary = summaryOf(data);
		this.model = data.model || "";
		this.groupInUse = data.groupInUse || "";
		this.turns = (data.turns || []).map(normalTurn);
		this.hasMore = Boolean(data.hasMore);
		this.approvals = data.pendingApprovals || [];
		this.question = data.pendingQuestion || null;
		this.tasks = data.tasks || [];
		this.queued = data.queued || [];
		this.modified = data.modified || [];
		this.resources = data.resources || [];
		this.clips = data.clips || [];
		this.context = data.context || null;
		return { all: true };
	}

	// Older turns from chat.history go first.
	prepend(turns, hasMore) {
		this.turns = turns.map(normalTurn).concat(this.turns);
		this.hasMore = Boolean(hasMore);
		return { all: true };
	}

	// Folds one event in; what changed: {turn}, {segment, turn}, {summary}, {tasks}, {queued}, {modified}, {clips},
	// {context}, {approvals}, {question}.
	event(kind, data) {
		switch (kind) {
			case "chat.changed":
				this.summary = summaryOf(data);
				return { summary: true };
			case "turn.start": {
				const turn = normalTurn({ turnId: data.turnId, user: data.user, mode: data.mode, model: data.model, group: data.group, startedMs: data.startedMs, segments: [], stop: null, recoveries: [] });
				this.turns.push(turn);
				return { turn };
			}
			case "segment.start": {
				const turn = this.turn(data.turnId);
				if (!turn) return {};
				const segment = Object.assign({ text: "", argsText: "" }, data);
				delete segment.turnId;
				turn.segments.push(segment);
				return { segment, turn };
			}
			case "segment.delta": {
				const found = this.segment(data.segId);
				if (!found) return {};
				if (data.text !== undefined) found.segment.text += data.text;
				if (data.argsText !== undefined) found.segment.argsText += data.argsText;
				return found;
			}
			case "segment.end": {
				const found = this.segment(data.segId);
				if (!found) return {};
				Object.assign(found.segment, { done: true, tokens: data.tokens, ms: data.ms });
				return found;
			}
			case "tool.state": {
				const found = this.tool(data.callId);
				if (!found) return {};
				const change = Object.assign({}, data);
				delete change.callId;
				Object.assign(found.segment, change);
				return found;
			}
			case "approval.request":
				this.approvals = this.approvals.filter((a) => a.approvalId !== data.approvalId).concat(data);
				return Object.assign({ approvals: true }, this.tool(data.callId) || {});
			case "approval.resolved": {
				const approval = this.approvals.find((a) => a.approvalId === data.approvalId);
				this.approvals = this.approvals.filter((a) => a.approvalId !== data.approvalId);
				return Object.assign({ approvals: true }, approval ? this.tool(approval.callId) || {} : {});
			}
			case "question.request":
				this.question = data;
				return Object.assign({ question: true }, this.tool(data.callId) || {});
			case "question.resolved": {
				const asked = this.question;
				this.question = null;
				return Object.assign({ question: true }, asked ? this.tool(asked.callId) || {} : {});
			}
			case "tasks.changed":
				this.tasks = data.items || [];
				return { tasks: true };
			case "queue.changed":
				this.queued = data.queued || [];
				return { queued: true };
			case "resources.changed":
				this.resources = data.resources || [];
				return { resources: true };
			case "files.changed":
				this.modified = data.files || [];
				return { modified: true };
			case "clips.changed":
				this.clips = data.clips || [];
				return { clips: true };
			case "context.changed":
				this.context = data;
				return { context: true };
			case "turn.progress": {
				const turn = this.turn(data.turnId);
				if (!turn) return {};
				if (data.prefill) turn.prefill = data.prefill;
				else delete turn.prefill;
				return { turn };
			}
			case "turn.stats":
			case "turn.end": {
				const turn = this.turn(data.turnId);
				if (!turn) return {};
				const change = Object.assign({}, data);
				delete change.turnId;
				Object.assign(turn, change);
				return { turn, ended: kind === "turn.end" };
			}
			default:
				return {};
		}
	}

	turn(turnId) {
		return this.turns.find((t) => t.turnId === turnId) || null;
	}

	segment(segId) {
		for (const turn of this.turns) {
			const segment = turn.segments.find((s) => s.segId === segId);
			if (segment) return { segment, turn };
		}
		return null;
	}

	tool(callId) {
		for (const turn of this.turns) {
			const segment = turn.segments.find((s) => s.type === "tool" && s.callId === callId);
			if (segment) return { segment, turn };
		}
		return null;
	}

	approvalFor(callId) {
		return this.approvals.find((a) => a.callId === callId) || null;
	}

	running() {
		return Boolean(this.summary) && this.summary.state !== "idle" && this.summary.state !== "paused";
	}

	lastTurn() {
		return this.turns.length ? this.turns[this.turns.length - 1] : null;
	}
}

function summaryOf(data) {
	const keys = ["chatId", "wsId", "title", "mode", "permission", "group", "state", "stateReason", "turnCount", "updatedMs", "tasksDone", "tasksTotal", "approvalsWaiting"];
	const summary = {};
	for (const key of keys) if (data[key] !== undefined) summary[key] = data[key];
	return summary;
}

function normalTurn(turn) {
	return Object.assign({ segments: [], recoveries: [], stop: null, tokensIn: 0, tokensOut: 0 }, turn, { segments: (turn.segments || []).map((s) => Object.assign({ text: "", argsText: "" }, s)) });
}

// The chats of a workspace (chats:<wsId>), most recently active first.
export class ChatList {
	constructor() {
		this.chats = [];
	}

	snapshot(data) {
		this.chats = (data.chats || []).slice();
		this.sort();
	}

	event(kind, data) {
		if (kind === "chat.deleted") this.chats = this.chats.filter((c) => c.chatId !== data.chatId);
		else if (kind === "chat.created" || kind === "chat.changed") {
			const index = this.chats.findIndex((c) => c.chatId === data.chatId);
			if (index >= 0) this.chats[index] = data;
			else this.chats.push(data);
		}
		this.sort();
	}

	sort() {
		this.chats.sort((a, b) => (b.updatedMs || 0) - (a.updatedMs || 0));
	}

	find(chatId) {
		return this.chats.find((c) => c.chatId === chatId) || null;
	}
}

// What a tab shows beside the title: a spinner while running, "!" when the chat waits on its owner, and the tasks.
export function tabGlyphs(summary) {
	return {
		running: summary.state === "running",
		attention: summary.state === "awaiting_approval" || summary.state === "awaiting_answer" || summary.state === "paused",
		tasks: summary.tasksTotal ? summary.tasksDone + "/" + summary.tasksTotal : "",
	};
}

// A tool call's arguments on one line, as the card's head shows them: the path or command first.
export function argsLine(tool, argsText) {
	let args;
	try {
		args = JSON.parse(argsText || "{}");
	}
	catch {
		return (argsText || "").replace(/\s+/g, " ").slice(0, 200);
	}
	if (!args || typeof args !== "object") return String(args);
	const lead = args.path || args.command || args.pattern || args.question || args.summary || "";
	const rest = [];
	if (args.offset) rest.push(args.offset + (args.limit ? "-" + (args.offset + args.limit - 1) : "+"));
	if (args.glob) rest.push(args.glob);
	if (tool === "fs_edit" && Array.isArray(args.edits)) rest.push(args.edits.length + (args.edits.length === 1 ? " edit" : " edits"));
	if (tool === "tasks_set" && Array.isArray(args.items)) rest.push(args.items.length + " tasks");
	if (tool === "task_update") rest.push(args.id + " " + args.status);
	if (tool === "clipboard_copy") return [args.name + ":", args.path || (args.result ? "result " + args.result : "text"), args.start_line || args.end_line ? (args.start_line || 1) + "-" + (args.end_line || "end") : ""].filter(Boolean).join(" ");
	return [lead].concat(rest).filter(Boolean).join(" ") || JSON.stringify(args).slice(0, 200);
}

// Tokens per second of generation, one decimal; "" before any.
export function tokensPerSecond(turn) {
	if (!turn || !turn.genMs || !turn.tokensOut) return "";
	return (turn.tokensOut * 1000 / turn.genMs).toFixed(1);
}

// "41.3k" for 41300, "980" for 980.
export function compact(count) {
	if (count === undefined || count === null) return "-";
	return count >= 1000 ? (count / 1000).toFixed(count >= 100000 ? 0 : 1) + "k" : String(count);
}

// "2m 31s", "6.1 s", "38 ms".
export function duration(ms) {
	if (!ms && ms !== 0) return "";
	if (ms < 1000) return ms + " ms";
	if (ms < 60000) return (ms / 1000).toFixed(1) + " s";
	const minutes = Math.floor(ms / 60000);
	return minutes + "m " + Math.floor((ms % 60000) / 1000) + "s";
}

// The lines of a unified diff with their old and new line numbers: [{kind: " " | "-" | "+" | "@", text, old, new}].
export function diffRows(unified) {
	const rows = [];
	let oldLine = 0;
	let newLine = 0;
	for (const line of (unified || "").split("\n")) {
		if (!line) continue;
		const hunk = /^@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@/.exec(line);
		if (hunk) {
			oldLine = Number(hunk[1]);
			newLine = Number(hunk[2]);
			rows.push({ kind: "@", text: line, old: null, new: null });
			continue;
		}
		const kind = line[0];
		const text = line.slice(1);
		if (kind === "-") rows.push({ kind, text, old: oldLine++, new: null });
		else if (kind === "+") rows.push({ kind, text, old: null, new: newLine++ });
		else rows.push({ kind: " ", text, old: oldLine++, new: newLine++ });
	}
	return rows;
}

// The context's parts as fractions of the window, for the stacked bar; the window is the used total when unknown.
export function contextBar(context) {
	if (!context) return { window: 0, used: 0, percent: 0, parts: [] };
	const window = context.window || 0;
	const used = context.used || 0;
	const whole = window || Math.max(used, 1);
	return {
		window, used, percent: window ? Math.round(used * 100 / window) : 0,
		parts: (context.parts || []).map((p) => ({ name: p.name, tokens: p.tokens, share: p.tokens / whole })),
	};
}
