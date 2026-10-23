// chat.test.js - the chat plugin's client model (docs/10 "Chats"): a snapshot and its events folded into turns,
// segments, tool states and what the chat waits on; the list of chats; and the small facts the views show.

import { test, assertEqual } from "./harness.js";
import { ChatList, ChatModel, ago, argsLine, chatLook, compact, contextBar, diffRows, duration, effortLabel, effortRows, keptFor, tokensPerSecond } from "/plugins/ied.chat/ui/chatmodel.js";
import { chatStatusText } from "/plugins/ied.chat/ui/index.js";
import { CodeColors } from "/plugins/ied.chat/ui/codecolors.js";

function snapshot() {
	return {
		chatId: "c1", wsId: "w1", title: "Port logging", mode: "agent", permission: "ask", group: "", state: "idle", turnCount: 1, tasksDone: 0, tasksTotal: 0,
		model: "qwen", groupInUse: "local", tasks: [], queued: [], modified: [], context: null, hasMore: false, pendingApprovals: [], pendingQuestion: null,
		turns: [{ turnId: "u1", user: { text: "hi" }, mode: "agent", segments: [{ segId: "s2", type: "text", done: true, text: "Hello" }], stop: "finished", recoveries: [] }],
	};
}

test("chat: a snapshot, then a turn streamed in", () => {
	const model = new ChatModel();
	assertEqual(model.snapshot(snapshot()), { all: true });
	assertEqual([model.summary.title, model.model, model.groupInUse, model.turns.length], ["Port logging", "qwen", "local", 1]);
	const start = model.event("turn.start", { turnId: "u3", mode: "agent", model: "qwen", user: { text: "go" } });
	assertEqual(start.turn.turnId, "u3");
	const segment = model.event("segment.start", { turnId: "u3", segId: "s4", type: "text", done: false, text: "" });
	assertEqual([segment.turn.turnId, segment.segment.segId], ["u3", "s4"]);
	model.event("segment.delta", { segId: "s4", text: "Hel" });
	model.event("segment.delta", { segId: "s4", text: "lo" });
	model.event("segment.end", { segId: "s4", tokens: 2, ms: 30 });
	assertEqual([model.segment("s4").segment.text, model.segment("s4").segment.done], ["Hello", true]);
	const ended = model.event("turn.end", { turnId: "u3", stop: "finished", recoveries: ["retried after http_error 503 (1x)"], tokensIn: 3000, tokensOut: 40, genMs: 1000 });
	assertEqual([ended.ended, model.lastTurn().stop, model.lastTurn().recoveries.length], [true, "finished", 1]);
	assertEqual(model.event("unknown.kind", {}), {});
});

test("chat: tool calls, approvals and the question", () => {
	const model = new ChatModel();
	model.snapshot(snapshot());
	model.event("turn.start", { turnId: "u3", user: { text: "edit" } });
	model.event("segment.start", { turnId: "u3", segId: "s4", type: "tool", tool: "fs_edit", callId: "k5", state: "streaming", argsText: "" });
	model.event("segment.delta", { segId: "s4", argsText: "{\"path\": \"src/a.cf\"}" });
	const asked = model.event("approval.request", { approvalId: "a6", callId: "k5", tool: "fs_edit", subject: "fs.write(src/**)" });
	assertEqual([asked.approvals, asked.segment.segId], [true, "s4"]);
	assertEqual(model.approvalFor("k5").approvalId, "a6");
	model.event("tool.state", { callId: "k5", state: "awaiting_approval" });
	const resolved = model.event("approval.resolved", { approvalId: "a6", decision: "allow" });
	assertEqual([resolved.segment.segId, model.approvalFor("k5")], ["s4", null]);
	model.event("tool.state", { callId: "k5", state: "ok", ms: 6, plus: 2, minus: 1, result: { preview: "@@ -1,1 +1,2 @@", isError: false } });
	assertEqual([model.tool("k5").segment.state, model.tool("k5").segment.plus], ["ok", 2]);
	model.event("segment.start", { turnId: "u3", segId: "s7", type: "tool", tool: "ask_user", callId: "k8" });
	const question = model.event("question.request", { questionId: "q9", callId: "k8", question: "Which?", options: ["A"] });
	assertEqual([question.question, question.segment.segId], [true, "s7"]);
	const answered = model.event("question.resolved", { questionId: "q9", answer: "A" });
	assertEqual([answered.segment.segId, model.question], ["s7", null]);
});

test("chat: tasks, queue, files and context", () => {
	const model = new ChatModel();
	model.snapshot(snapshot());
	assertEqual(model.event("tasks.changed", { items: [{ id: "t1", title: "Read", status: "done" }] }), { tasks: true });
	assertEqual(model.event("queue.changed", { queued: ["later"] }), { queued: true });
	assertEqual(model.event("files.changed", { files: [{ path: "src/a.cf", plus: 3, minus: 0 }] }), { modified: true });
	assertEqual(model.clips, [], "a snapshot without clips");
	assertEqual(model.event("clips.changed", { clips: [{ name: "push", source: "src/ring.cf lines 40-62", lines: 23, bytes: 812, text: "void Push()\n" }] }), { clips: true });
	assertEqual([model.clips.length, model.clips[0].name], [1, "push"]);
	model.event("context.changed", { window: 32768, used: 16384, parts: [{ name: "System prompt", tokens: 4096 }] });
	assertEqual([model.tasks.length, model.queued, model.modified[0].plus, model.context.used], [1, ["later"], 3, 16384]);
	const bar = contextBar(model.context);
	assertEqual([bar.percent, bar.parts[0].share], [50, 0.125]);
	assertEqual(contextBar(null).parts, []);
	model.event("chat.changed", { chatId: "c1", title: "Renamed", state: "running" });
	assertEqual([model.summary.title, model.running()], ["Renamed", true]);
});

test("chat: the list keeps the latest first", () => {
	const list = new ChatList();
	list.snapshot({ chats: [{ chatId: "c1", updatedMs: 1 }, { chatId: "c2", updatedMs: 5 }] });
	assertEqual(list.chats.map((c) => c.chatId), ["c2", "c1"]);
	list.event("chat.changed", { chatId: "c1", updatedMs: 9 });
	list.event("chat.created", { chatId: "c3", updatedMs: 7 });
	assertEqual(list.chats.map((c) => c.chatId), ["c1", "c3", "c2"]);
	list.event("chat.deleted", { chatId: "c3" });
	assertEqual([list.chats.length, list.find("c3"), list.find("c2").chatId], [2, null, "c2"]);
});

test("chat: deleted chats are kept apart, restored and purged", () => {
	const list = new ChatList();
	list.snapshot({ chats: [{ chatId: "c1", updatedMs: 1 }, { chatId: "c2", updatedMs: 5 }], deleted: [{ chatId: "c9", deletedMs: 3 }] });
	list.event("chat.deleted", { chatId: "c1", deleted: { chatId: "c1", deletedMs: 8 } });
	assertEqual([list.chats.map((c) => c.chatId), list.deleted.map((d) => d.chatId)], [["c2"], ["c1", "c9"]], "the latest deleted first");
	list.event("chat.restored", { chatId: "c1", updatedMs: 9 });
	assertEqual([list.chats.map((c) => c.chatId), list.deleted.map((d) => d.chatId)], [["c1", "c2"], ["c9"]]);
	list.event("chat.purged", { chatId: "c9" });
	assertEqual(list.deleted, []);
	assertEqual(list.keepsDeleted, true);
	list.snapshot({ chats: [] });
	assertEqual(list.keepsDeleted, false, "an older server lists no deleted chats: it deletes for good");
});

test("chat: when a chat was active, and how long a deleted one is kept", () => {
	const now = Date.UTC(2026, 9, 22, 12, 0, 0);
	assertEqual([ago(now - 20000, now), ago(now - 5 * 60000, now), ago(now - 3 * 3600000, now), ago(now - 2 * 86400000, now)], ["now", "5m", "3h", "2d"]);
	assertEqual([keptFor(now + 30 * 86400000 - 5, now), keptFor(now + 86400000, now), keptFor(now + 3600000, now)], ["30 days left", "1 day left", "less than a day left"]);
});

test("chat: what tabs and the status bar say", () => {
	assertEqual(chatLook({ state: "running", tasksDone: 3, tasksTotal: 7 }), { tasks: "3/7", icon: "$(loading~spin)", tone: "c-info", attention: false });
	assertEqual(chatLook({ state: "paused", tasksTotal: 0 }), { tasks: "", icon: "$(debug-pause)", tone: "c-warn", attention: true });
	assertEqual(chatLook({ state: "something new" }).icon, "$(comment-discussion)", "a state it does not know looks idle");
	assertEqual(chatStatusText([{ state: "idle" }]), null);
	assertEqual(chatStatusText([{ state: "running" }, { state: "running" }, { state: "awaiting_approval" }]), { text: "AI: 2 running, 1 needs you", attention: true });
});

test("chat: a call's arguments on one line", () => {
	assertEqual(argsLine("fs_read", "{\"path\": \"src/Log.cpp\", \"offset\": 1, \"limit\": 200}"), "src/Log.cpp 1-200");
	assertEqual(argsLine("shell_run", "{\"command\": \"nuke build\"}"), "nuke build");
	assertEqual(argsLine("fs_edit", "{\"path\": \"a.cf\", \"edits\": [{}, {}]}"), "a.cf 2 edits");
	assertEqual(argsLine("task_update", "{\"id\": \"t2\", \"status\": \"done\"}"), "t2 done");
	assertEqual(argsLine("clipboard_copy", "{\"name\": \"push\", \"path\": \"src/ring.cf\", \"start_line\": 40, \"end_line\": 62}"), "push: src/ring.cf 40-62");
	assertEqual(argsLine("clipboard_copy", "{\"name\": \"err\", \"result\": 1}"), "err: result 1");
	assertEqual(argsLine("code_call_hierarchy", "{\"symbol\": \"Ring::Push\", \"direction\": \"incoming\"}"), "Ring::Push incoming");
	assertEqual(argsLine("code_definition", "{\"symbol\": \"Push\", \"path\": \"src/game.cpp\", \"line\": 4}"), "Push src/game.cpp:4");
	assertEqual(argsLine("code_symbols", "{\"path\": \"src/ring.h\"}"), "src/ring.h");
	assertEqual(argsLine("fs_read", "{\"path\": \"src/Log"), "{\"path\": \"src/Log", "streaming arguments show as they are");
});

test("chat: numbers as the views show them", () => {
	assertEqual([compact(980), compact(41300), compact(128000), compact(undefined)], ["980", "41.3k", "128k", "-"]);
	assertEqual([duration(38), duration(6100), duration(151000)], ["38 ms", "6.1 s", "2m 31s"]);
	assertEqual([tokensPerSecond({ tokensOut: 412, genMs: 10000 }), tokensPerSecond({ tokensOut: 0 })], ["41.2", ""]);
});

test("chat: the composer's effort - the chat's pick, else its group's, else the model's default", () => {
	assertEqual([effortLabel("low", "high"), effortLabel("", "high"), effortLabel("", ""), effortLabel("", null)], ["Low", "High (group)", "Default", "Group's"]);
	const rows = effortRows("", "medium");
	assertEqual(rows.map((r) => [r.value, r.label, Boolean(r.icon)]), [["", "As the group says (Medium)", true], ["low", "Low", false], ["medium", "Medium", false], ["high", "High", false], ["xhigh", "Extra high", false], ["max", "Max", false]]);
	assertEqual(effortRows("high", "").map((r) => Boolean(r.icon)), [false, false, false, true, false, false]);
	assertEqual([effortLabel("xhigh", ""), effortLabel("", "max")], ["Extra high", "Max (group)"]);
	assertEqual([effortRows("", "")[0].label, effortRows("", null)[0].label], ["As the group says (the model's default)", "As the group says"], "a group that sets none; a chat not made yet");
	const model = new ChatModel();
	model.snapshot(Object.assign(snapshot(), { effort: "low", roleEffort: "medium" }));
	assertEqual([model.summary.effort, model.roleEffort], ["low", "medium"]);
});

test("chat: a unified diff's rows carry both line numbers", () => {
	const rows = diffRows("@@ -21,3 +21,2 @@\n void f()\n-\ta();\n-\tb();\n+\tc();\n");
	assertEqual(rows.map((r) => [r.kind, r.old, r.new]), [["@", null, null], [" ", 21, 21], ["-", 22, null], ["-", 23, null], ["+", null, 22]]);
});

// An editor's `editor.colorize`: each line as <i>line</i>, and the calls it was asked.
function colorizingIed() {
	const asked = [];
	const ied = {
		contributions: () => [{ value: { id: "cpp", fences: ["c++", "cpp"] } }],
		bindings: {
			has: (name) => name === "editor.colorize",
			call: async (name, args) => {
				asked.push(args.languageId || args.path);
				return args.lines.map((line) => "<i>" + line + "</i>");
			},
		},
	};
	return { ied, asked };
}

test("chat: a reply's code in the editor's colors, once its fence closes, kept while it streams", async () => {
	const { ied, asked } = colorizingIed();
	const colors = new CodeColors(ied);
	const first = colors.element("int a;\nint b;", "C++", true);
	assertEqual(first.innerHTML, "int a;\nint b;", "plain until the editor answers");
	await Promise.resolve();
	await Promise.resolve();
	assertEqual(first.innerHTML, "<i>int a;</i>\n<i>int b;</i>");
	assertEqual(colors.element("int a;\nint b;", "cpp", true).innerHTML, "<i>int a;</i>\n<i>int b;</i>", "drawn again: colored at once");
	assertEqual([colors.element("int c", "cpp", false).innerHTML, colors.element("x", "zig", true).innerHTML, colors.element("x", "", true).innerHTML], ["int c", "x", "x"], "streaming, unknown, none");
	assertEqual(asked, ["cpp"], "one call for the block drawn twice");
	const spans = ["a();", "b();"].map((text) => Object.assign(document.createElement("span"), { textContent: text }));
	colors.diff("src/f.cpp", spans);
	await Promise.resolve();
	await Promise.resolve();
	assertEqual([spans.map((span) => span.innerHTML), asked], [["<i>a();</i>", "<i>b();</i>"], ["cpp", "src/f.cpp"]], "a diff's lines by its file");
});
