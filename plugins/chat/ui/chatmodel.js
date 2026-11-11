// chatmodel.js - a chat as this client sees it (docs/10 "Chats"): the chat:<chatId> snapshot with its events folded in
// - turns and their segments, tool states, approvals and the question waiting, tasks, queued messages, the files the
// AI changed, its clipboard and the context. Each event says what changed, so the view redraws only that. Also the
// chats:<wsId> list and the small facts the views show (a tool call's one-line arguments, a tab's glyphs, tokens per
// second).

export const STATE_LABELS = { idle: "Idle", running: "Running", awaiting_approval: "Needs approval", awaiting_answer: "Needs an answer", paused: "Paused", suspended: "Suspended" };
// The tools that wait on something that goes on without them: a message to the running turn stops them (docs/04 "Chats
// waiting on each other").
export const WAIT_TOOLS = new Set(["chat_wait", "background_wait"]);
export const MODES = [["ask", "Ask"], ["plan", "Plan"], ["agent", "Agent"]];

// The Mode menu's choices: the modes, and Agent until stopped - Agent with no finish, its turn going on until the owner
// stops it (docs/04 "Until stopped").
export const MODE_CHOICES = MODES.concat([["endless", "Agent, until stopped"]]);

// The choice a chat's summary is on: [id, label].
export function modeChoice(summary) {
	const id = summary.mode === "agent" && summary.endless ? "endless" : summary.mode;
	return MODE_CHOICES.find(([choice]) => choice === id) || MODE_CHOICES[2];
}

// What chat.set takes for a choice of the Mode menu.
export function modeChange(choice) {
	return choice === "endless" ? { mode: "agent", endless: true } : { mode: choice, endless: false };
}
export const PERMISSIONS = [["plan", "Read only"], ["ask", "Ask for edits"], ["acceptEdits", "Accept edits"], ["auto", "Auto"]];
export const EFFORTS = [["low", "Low"], ["medium", "Medium"], ["high", "High"], ["xhigh", "Extra high"], ["max", "Max"]];
// The summary's fields that change which model a chat's next turn starts on.
const CHOICE_KEYS = ["pinnedModel", "group", "mode"];

function effortName(id) {
	return (EFFORTS.find(([e]) => e === id) || [id, id])[1];
}

// What the Model button says of the effort: the chat's pick, else its role's (docs/04 "Effort"), else the model's own
// default; `roleEffort` is null while the chat has no snapshot to say.
export function effortLabel(effort, roleEffort) {
	if (effort) return effortName(effort);
	if (roleEffort === null) return "Group's";
	return roleEffort ? effortName(roleEffort) + " (group)" : "Default";
}

// The efforts a model profile (ai.models' entry) takes: those its `effort` mapping names - a level it leaves out sends
// nothing - else all five (a Claude model, a server told reasoning_effort).
export function effortsOf(profile) {
	const mapping = profile && profile.effort;
	if (!mapping || typeof mapping !== "object") return EFFORTS.map(([id]) => id);
	return EFFORTS.map(([id]) => id).filter((id) => mapping[id]);
}

// The Model menu's effort slider's stops, left to right (owner, 2026-10-26: "the group default only applys to groups not
// single models"): a chat on a group starts with its group's default (`roleEffort`, its role's effort; "" the model's own),
// one on a single model (`alone`) with that model's own default; then the levels the model a turn uses takes (`supported`),
// the chat's own pick kept when that model does not take it. [{value, label, detail}], value "" the default.
export function effortStops(effort, roleEffort, alone, supported) {
	const first = alone ? { value: "", label: "Default", detail: "the model's own" }
		: { value: "", label: "Group default", detail: roleEffort ? effortName(roleEffort) : "the model's own" };
	const levels = EFFORTS.filter(([id]) => supported.includes(id) || id === effort).map(([id, label]) => ({ value: id, label, detail: supported.includes(id) ? "" : "this model does not take it" }));
	return [first].concat(levels);
}

export class ChatModel {
	constructor() {
		this.summary = null;
		this.model = "";
		this.groupInUse = "";
		this.roleEffort = null;     // the group's effort for this chat, once a snapshot says
		this.turns = [];
		this.hasMore = false;
		this.hasNewer = false;
		this.approvals = [];
		this.question = null;
		this.tasks = [];
		this.queued = [];
		this.pending = [];   // sent to the running turn, for its next step
		this.modified = [];
		this.resources = [];
		this.clips = [];
		this.background = [];
		this.context = null;
	}

	// The snapshot replaces everything.
	snapshot(data) {
		this.summary = summaryOf(data);
		this.choose(data);
		this.turns = (data.turns || []).map(normalTurn);
		this.hasMore = Boolean(data.hasMore);
		this.hasNewer = false;
		this.approvals = data.pendingApprovals || [];
		this.question = data.pendingQuestion || null;
		this.tasks = data.tasks || [];
		this.queued = data.queued || [];
		this.pending = data.pending || [];
		this.modified = data.modified || [];
		this.resources = data.resources || [];
		this.clips = data.clips || [];
		this.background = data.background || [];
		this.context = data.context || null;
		return { all: true };
	}

	// What the chat's next turn starts on (the snapshot's, or a chat.changed's after the pin, group or mode changed): its
	// model, its group and that group's effort for its mode.
	choose(data) {
		this.model = data.model || "";
		this.groupInUse = data.groupInUse || "";
		this.roleEffort = data.roleEffort || "";
	}

	// --- The window (docs/10 "Chats"): the client holds a few pages of the transcript, never the whole of a long chat.
	// A turn of it can be a part: `firstSeg` its first segment held (0: from its start), `segmentCount` the segments it
	// has. `hasMore` there is more before the window, `hasNewer` after it - then what streams at the chat's end is not
	// the window's and is passed over until the owner scrolls back there.

	// The page before the window (chat.history's): it goes first, a part of the window's first turn joined to it.
	prepend(page) {
		const turns = (page.turns || []).map(normalTurn);
		const joined = turns.length && this.turns.length && turns[turns.length - 1].turnId === this.turns[0].turnId;
		if (joined) {
			const piece = turns.pop();
			const first = this.turns[0];
			first.segments = piece.segments.concat(first.segments);
			first.firstSeg = piece.firstSeg || 0;
			if (piece.segmentCount !== undefined) first.segmentCount = piece.segmentCount;
		}
		this.turns = turns.concat(this.turns);
		this.hasMore = Boolean(page.hasMore);
		return { all: true };
	}

	// The page after the window: it goes last, a part of the window's last turn joined to it.
	append(page) {
		const turns = (page.turns || []).map(normalTurn);
		const last = this.lastTurn();
		if (turns.length && last && turns[0].turnId === last.turnId) {
			const piece = turns.shift();
			last.segments = last.segments.concat(piece.segments);
			Object.assign(last, piece, { segments: last.segments, firstSeg: last.firstSeg || 0 });
			if (piece.segmentCount === undefined) delete last.segmentCount;
		}
		this.turns = this.turns.concat(turns);
		this.hasNewer = Boolean(page.hasNewer);
		return { all: true };
	}

	// The segments the window holds.
	held() {
		return this.turns.reduce((count, turn) => count + turn.segments.length, 0);
	}

	// The window cut to `most` segments from its start: what is let go is before it again. Whether it was cut.
	// A long turn alone is cut inside; the window keeps one segment at least.
	trimOlder(most) {
		let extra = this.held() - most;
		if (extra <= 0) return false;
		while (extra > 0 && this.turns.length) {
			const first = this.turns[0];
			const total = segmentsOf(first);
			if (first.segments.length <= extra && this.turns.length > 1) {
				extra -= first.segments.length;
				this.turns.shift();
				continue;
			}
			const cut = Math.min(extra, first.segments.length - 1);
			first.segments = first.segments.slice(cut);
			first.firstSeg = (first.firstSeg || 0) + cut;
			first.segmentCount = total;
			extra = 0;
		}
		this.hasMore = true;
		return true;
	}

	// The window cut to `most` segments from its end: what is let go is after it again. Whether it was cut.
	trimNewer(most) {
		let extra = this.held() - most;
		if (extra <= 0) return false;
		while (extra > 0 && this.turns.length) {
			const last = this.turns[this.turns.length - 1];
			const total = segmentsOf(last);
			if (last.segments.length <= extra && this.turns.length > 1) {
				extra -= last.segments.length;
				this.turns.pop();
				continue;
			}
			const cut = Math.min(extra, last.segments.length - 1);
			last.segments = last.segments.slice(0, last.segments.length - cut);
			last.segmentCount = total;
			extra = 0;
		}
		this.hasNewer = true;
		return true;
	}

	// Folds one event in; what changed: {turn}, {segment, turn}, {summary}, {tasks}, {queued}, {modified}, {clips},
	// {context}, {approvals}, {question}; {all, reverted} for a chat reverted to before a turn (its snapshot as it is now).
	event(kind, data) {
		switch (kind) {
			case "chat.reverted":
				return Object.assign(this.snapshot(data.snapshot || {}), { reverted: data });
			case "chat.changed": {
				const before = this.summary || {};
				this.summary = summaryOf(data);
				if (data.model !== undefined && CHOICE_KEYS.some((key) => before[key] !== this.summary[key])) this.choose(data);
				return { summary: true };
			}
			case "turn.start": {
				if (this.hasNewer) return {};
				const turn = normalTurn({ turnId: data.turnId, user: data.user, mode: data.mode, model: data.model, group: data.group, startedMs: data.startedMs, segments: [], stop: null, recoveries: [] });
				this.turns.push(turn);
				// what the turn uses is what the chat uses now: the settings may have changed since the snapshot
				if (data.model) this.model = data.model;
				if (data.group) this.groupInUse = data.group;
				return { turn };
			}
			case "segment.start": {
				const turn = this.turn(data.turnId);
				if (!turn || this.hasNewer) return {};
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
				this.pending = data.pending || [];
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
			case "background.changed":
				this.background = data.tasks || [];
				return { background: true };
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
		return Boolean(this.summary) && this.summary.state !== "idle" && this.summary.state !== "paused" && this.summary.state !== "suspended";
	}

	// The wait tool the running turn is in (WAIT_TOOLS), its segment; null when it waits on none.
	runningWait() {
		const turn = this.lastTurn();
		if (!turn || turn.stop) return null;
		return turn.segments.find((s) => s.type === "tool" && WAIT_TOOLS.has(s.tool) && s.state === "running") || null;
	}

	lastTurn() {
		return this.turns.length ? this.turns[this.turns.length - 1] : null;
	}
}

function summaryOf(data) {
	const keys = ["chatId", "wsId", "title", "section", "mode", "permission", "group", "effort", "pinnedModel", "endless", "state", "stateReason", "turnCount", "updatedMs", "tasksDone", "tasksTotal", "approvalsWaiting",
		"backgroundRunning", "agent", "parentChatId", "owes", "awaits"];
	const summary = {};
	for (const key of keys) if (data[key] !== undefined) summary[key] = data[key];
	return summary;
}

function normalTurn(turn) {
	return Object.assign({ segments: [], recoveries: [], stop: null, tokensIn: 0, tokensOut: 0 }, turn, { segments: (turn.segments || []).map((s) => Object.assign({ text: "", argsText: "" }, s)) });
}

// The segments a turn has, held or not.
function segmentsOf(turn) {
	const held = (turn.firstSeg || 0) + turn.segments.length;
	return turn.segmentCount !== undefined && turn.segmentCount > held ? turn.segmentCount : held;
}

// Whether the window holds a turn's start (its message) and its end (its footer).
export function turnStartHeld(turn) {
	return !turn.firstSeg;
}

export function turnEndHeld(turn) {
	return turn.segmentCount === undefined || (turn.firstSeg || 0) + turn.segments.length >= turn.segmentCount;
}

// The chats of a workspace (chats:<wsId>), most recently active first, its archived chats (docs/04 "Archiving a chat"),
// the latest archived first, and its deleted chats (docs/04 "Deleting a chat"), the latest deleted first.
export class ChatList {
	constructor() {
		this.chats = [];
		this.archived = [];
		this.deleted = [];
		this.keepsDeleted = false;   // the server keeps deleted chats (an older one lists no `deleted`)
		this.archives = false;       // the server archives chats (an older one lists no `archived`)
	}

	snapshot(data) {
		this.chats = (data.chats || []).slice();
		this.archived = (data.archived || []).slice();
		this.deleted = (data.deleted || []).slice();
		this.keepsDeleted = Array.isArray(data.deleted);
		this.archives = Array.isArray(data.archived);
		this.sort();
	}

	event(kind, data) {
		const others = (list) => list.filter((d) => d.chatId !== data.chatId);
		if (kind === "chat.deleted") {
			this.chats = others(this.chats);
			this.archived = others(this.archived);
			if (data.deleted) this.deleted = [data.deleted, ...others(this.deleted)];
		}
		else if (kind === "chat.archived") {
			this.chats = others(this.chats);
			if (data.archived) this.archived = [data.archived, ...others(this.archived)];
		}
		else if (kind === "chat.purged") this.deleted = others(this.deleted);
		else if (kind === "chat.created" || kind === "chat.changed" || kind === "chat.restored") {
			this.deleted = others(this.deleted);
			this.archived = others(this.archived);
			const index = this.chats.findIndex((c) => c.chatId === data.chatId);
			if (index >= 0) this.chats[index] = data;
			else this.chats.push(data);
		}
		this.sort();
	}

	sort() {
		this.chats.sort((a, b) => (b.updatedMs || 0) - (a.updatedMs || 0));
		this.archived.sort((a, b) => (b.archivedMs || 0) - (a.archivedMs || 0));
		this.deleted.sort((a, b) => (b.deletedMs || 0) - (a.deletedMs || 0));
	}

	find(chatId) {
		return this.chats.find((c) => c.chatId === chatId) || null;
	}
}

const STATE_LOOKS = {
	idle: { icon: "$(comment-discussion)", tone: "", attention: false },
	running: { icon: "$(loading~spin)", tone: "c-info", attention: false },
	awaiting_approval: { icon: "$(shield)", tone: "c-warn", attention: true },
	awaiting_answer: { icon: "$(question)", tone: "c-warn", attention: true },
	paused: { icon: "$(debug-pause)", tone: "c-warn", attention: true },
	suspended: { icon: "$(clock)", tone: "", attention: false },
};

// How a chat's state shows on its tab and in the Chats list: a manifest-style icon, its color class, whether the chat
// waits on its owner, and its tasks done.
export function chatLook(summary) {
	const look = STATE_LOOKS[summary.state] || STATE_LOOKS.idle;
	return Object.assign({ tasks: summary.tasksTotal ? summary.tasksDone + "/" + summary.tasksTotal : "" }, look);
}

const MINUTE_MS = 60 * 1000;
const HOUR_MS = 60 * MINUTE_MS;
const DAY_MS = 24 * HOUR_MS;

// When a chat was last active, as the Chats list says it: "now", "5m", "3h", "2d", then the date.
export function ago(ms, now) {
	const passed = Math.max(0, now - ms);
	if (passed < MINUTE_MS) return "now";
	if (passed < HOUR_MS) return Math.floor(passed / MINUTE_MS) + "m";
	if (passed < DAY_MS) return Math.floor(passed / HOUR_MS) + "h";
	if (passed < 7 * DAY_MS) return Math.floor(passed / DAY_MS) + "d";
	return new Date(ms).toLocaleDateString(undefined, { month: "short", day: "numeric" });
}

// How long a deleted chat is still kept: "27 days left", "1 day left", "less than a day left".
export function keptFor(purgeMs, now) {
	const left = purgeMs - now;
	if (left < DAY_MS) return "less than a day left";
	const days = Math.ceil(left / DAY_MS);
	return days + (days === 1 ? " day left" : " days left");
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
	if (tool.startsWith("code_")) return [args.symbol || args.query || args.method, args.path ? args.path + (args.line ? ":" + args.line : "") : "", args.direction].filter(Boolean).join(" ") || "{}";
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
	if (count >= 1000000) return String(Math.round(count / 100000) / 10) + "M";
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

const DIFF_HEADER = /^(diff --git |diff --cc |--- |\+\+\+ |new file mode|deleted file mode|rename from |rename to |Binary files |index |old mode |new mode |similarity index |dissimilarity index |copy from |copy to )/;

// A path of a "---" or "+++" line without its a/ or b/; null for /dev/null.
function diffSide(text) {
	let path = text.split("\t")[0];
	if (path.length > 1 && path.startsWith("\"") && path.endsWith("\"")) path = path.slice(1, -1);
	if (path === "/dev/null") return null;
	return /^[ab]\//.test(path) ? path.slice(2) : path;
}

// A header line of a diff's file (`key` and the `rest` after it) read into its row; a new row when it starts a file.
function readDiffHeader(key, rest, file, startFile) {
	if (key === "diff --git " || key === "diff --cc ") {
		file = startFile();
		const at = rest.lastIndexOf(" b/");
		file.path = at < 0 || key === "diff --cc " ? rest : rest.slice(at + 3);
		return;
	}
	if (key === "--- ") {
		if (!file || file.hunks || file.headed) file = startFile();
		const path = diffSide(rest);
		if (path === null) file.status = "new";
		else file.oldPath = path;
		if (!file.path && path !== null) file.path = path;
		return;
	}
	if (!file) return;
	const path = key === "+++ " ? diffSide(rest) : rest;
	if (key === "+++ ") file.headed = true;
	if (key === "+++ " && path === null) file.status = "deleted";
	else if (key === "+++ " || key === "rename to ") file.path = path;
	if (key === "rename from ") file.oldPath = rest;
	if (key === "new file mode") file.status = "new";
	if (key === "deleted file mode") file.status = "deleted";
	if (key === "Binary files ") file.status = file.status ? file.status + ", binary" : "binary";
}

// The lines of a unified diff with their old and new line numbers: [{kind: " " | "-" | "+" | "@", text, old, new}].
// Each file of a diff of several (git's, or "---" and "+++" lines) comes first as its own row: {kind: "f", path,
// oldPath, status: "" | "new" | "deleted" | "renamed" | "binary" | "new, binary", plus, minus}.
export function diffRows(unified) {
	const rows = [];
	let oldLine = 0;
	let newLine = 0;
	let oldLeft = 0; // the lines the hunk still has on each side
	let newLeft = 0;
	let file = null;
	const startFile = () => {
		file = { kind: "f", path: "", oldPath: "", status: "", plus: 0, minus: 0, hunks: 0, headed: false };
		rows.push(file);
		return file;
	};
	for (const line of (unified || "").split("\n")) {
		if (!line) continue;
		const hunk = /^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@/.exec(line);
		if (hunk) {
			oldLine = Number(hunk[1]);
			newLine = Number(hunk[3]);
			oldLeft = hunk[2] === undefined ? 1 : Number(hunk[2]);
			newLeft = hunk[4] === undefined ? 1 : Number(hunk[4]);
			if (file) file.hunks += 1;
			rows.push({ kind: "@", text: line, old: null, new: null });
			continue;
		}
		const header = oldLeft <= 0 && newLeft <= 0 && DIFF_HEADER.exec(line);
		if (header) {
			readDiffHeader(header[1], line.slice(header[1].length), file, startFile);
			continue;
		}
		const kind = line[0];
		const text = line.slice(1);
		if (kind === "\\") continue;
		if (kind === "-") {
			rows.push({ kind, text, old: oldLine++, new: null });
			oldLeft -= 1;
			if (file) file.minus += 1;
		}
		else if (kind === "+") {
			rows.push({ kind, text, old: null, new: newLine++ });
			newLeft -= 1;
			if (file) file.plus += 1;
		}
		else {
			rows.push({ kind: " ", text, old: oldLine++, new: newLine++ });
			oldLeft -= 1;
			newLeft -= 1;
		}
	}
	for (const row of rows) {
		if (row.kind === "f" && !row.status && row.oldPath && row.oldPath !== row.path) row.status = "renamed";
	}
	return rows;
}

// The context's parts as fractions of the window, for the stacked bar; the window is the used total when unknown. `free`
// names the room left: where IED compresses (`compressAt` percent), or that the model compacts its own (Claude). A
// part's share is of the window, none while the window is not known.
export function contextBar(context) {
	if (!context) return { window: 0, used: 0, percent: 0, parts: [], free: "" };
	const window = context.window || 0;
	const used = context.used || 0;
	const free = context.compressAt ? "Free (compressed at " + context.compressAt + "%)" : "Free";
	return {
		window, used, percent: window ? Math.round(used * 100 / window) : 0, free,
		parts: (context.parts || []).map((p) => ({ name: p.name, tokens: p.tokens, share: window ? p.tokens / window : 0 })),
	};
}
