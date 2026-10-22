// chat.test.js - the chat plugin's client model (docs/10 "Chats"): a snapshot and its events folded into turns,
// segments, tool states and what the chat waits on; the list of chats; and the small facts the views show.

import { test, assertEqual } from "./harness.js";
import { ChatList, ChatModel, argsLine, compact, contextBar, diffRows, duration, tabGlyphs, tokensPerSecond } from "/plugins/ied.chat/ui/chatmodel.js";
import { chatStatusText } from "/plugins/ied.chat/ui/index.js";

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

test("chat: what tabs and the status bar say", () => {
	assertEqual(tabGlyphs({ state: "running", tasksDone: 3, tasksTotal: 7 }), { running: true, attention: false, tasks: "3/7" });
	assertEqual(tabGlyphs({ state: "paused", tasksTotal: 0 }), { running: false, attention: true, tasks: "" });
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

test("chat: a unified diff's rows carry both line numbers", () => {
	const rows = diffRows("@@ -21,3 +21,2 @@\n void f()\n-\ta();\n-\tb();\n+\tc();\n");
	assertEqual(rows.map((r) => [r.kind, r.old, r.new]), [["@", null, null], [" ", 21, 21], ["-", 22, null], ["-", 23, null], ["+", null, 22]]);
});
