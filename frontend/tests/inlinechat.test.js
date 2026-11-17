// inlinechat.test.js - the editor's inline chat (F-EDT-100, docs/04 "Inline chat"): the change a reply proposes (its
// last code block, open while it streams, without fs_read's numbers, indented as the lines were), the hunks of the diff,
// the lines found again after a reload, the messages; then a session in a real Monaco against a scripted server -
// asked, streamed, shown as a diff, accepted as one undo step, and closed when its chat closes elsewhere.

import { test, assertEqual, assertTrue } from "./harness.js";
import { loadMonaco } from "/plugins/ied.editor/ui/monaco.js";
import { Editors } from "/plugins/ied.editor/ui/editors.js";
import { InlineChats } from "/plugins/ied.editor/ui/inlinechat.js";
import {
	fencedBlocks, proposalOf, withoutLineNumbers, reindented, lineHunks, hunkCounts, textHash, locateLines, firstMessage,
	followUpMessage, titleOf, inlineOf,
} from "/plugins/ied.editor/ui/inlineedit.js";

test("inline chat: the proposal is the reply's last code block", () => {
	const reply = "Clamped it:\n\n```c\nint a = 1;\nint b = 2;\n```\n\nThat keeps it in range.";
	assertEqual(proposalOf(reply, ["int a;"], false), { lines: ["int a = 1;", "int b = 2;"], closed: true, prose: "Clamped it:\n\nThat keeps it in range." });
	assertEqual(proposalOf("No change is needed.", ["x"], false), null, "a reply without a block proposes nothing");
	assertEqual(proposalOf("```\nold\n```\nthen\n~~~js\nnew\n~~~", ["x"], false).lines, ["new"], "the last block, tildes too");
	assertEqual(fencedBlocks("````md\n```c\nx\n```\n````").map((b) => b.lines), [["```c", "x", "```"]], "a longer fence holds a shorter one");
});

test("inline chat: a block still streaming shows its whole lines", () => {
	assertEqual(proposalOf("```c\nint a = 1;\nint b", ["x"], true), { lines: ["int a = 1;"], closed: false, prose: "" }, "the cut last line waits");
	assertEqual(proposalOf("```c\nint a = 1;\n", ["x"], true).lines, ["int a = 1;"], "a line that ended is shown, none after it");
	assertEqual(proposalOf("```c\nint a = 1;\n\nb();\n", ["x"], true).lines, ["int a = 1;", "", "b();"], "an empty line in it is kept");
	assertEqual(proposalOf("```c\nint a = 1;\nint b", ["x"], false).lines, ["int a = 1;", "int b"], "a reply that ended keeps its last line");
});

test("inline chat: what models add is taken off", () => {
	assertEqual(withoutLineNumbers(["3\tint main(void)", "4\t{", "5\t}"]), ["int main(void)", "{", "}"], "fs_read's numbers");
	assertEqual(withoutLineNumbers(["3\ta", "7\tb"]), ["3\ta", "7\tb"], "numbers that do not count up are the code's");
	assertEqual(withoutLineNumbers(["x = 1", "2\ty"]), ["x = 1", "2\ty"]);
	assertEqual(reindented(["return 1;", "", "x();"], ["\tif (a)", "\t\tb();"]), ["\treturn 1;", "", "\tx();"], "flush left for indented lines");
	assertEqual(reindented(["\treturn 1;"], ["\tif (a)"]), ["\treturn 1;"], "a block with its indentation keeps it");
	assertEqual(reindented(["a", "\tb"], ["x"]), ["a", "\tb"], "lines that sit flush left");
});

test("inline chat: the diff's hunks", () => {
	assertEqual(lineHunks(["a", "b", "c"], ["a", "b", "c"]), []);
	assertEqual(lineHunks(["a", "b", "c"], ["a", "B", "c"]), [{ at: 1, removed: 1, added: ["B"] }], "a changed line");
	assertEqual(lineHunks(["a", "c"], ["a", "b1", "b2", "c"]), [{ at: 1, removed: 0, added: ["b1", "b2"] }], "inserted lines");
	assertEqual(lineHunks(["a", "b", "c"], ["c"]), [{ at: 0, removed: 2, added: [] }], "removed lines");
	assertEqual(lineHunks(["a", "x", "c", "y", "e"], ["a", "X", "c", "Y", "e"]), [{ at: 1, removed: 1, added: ["X"] }, { at: 3, removed: 1, added: ["Y"] }], "two apart");
	assertEqual(lineHunks([""], ["int a;", "int b;"]), [{ at: 0, removed: 1, added: ["int a;", "int b;"] }], "an empty line filled");
	assertEqual(hunkCounts(lineHunks(["a", "b", "c"], ["a", "B", "C", "D"])), { plus: 3, minus: 2 });
});

test("inline chat: the lines are found again where they went", () => {
	const doc = ["#include <x>", "", "int main(void)", "{", "\treturn 0;", "}"];
	const hash = textHash(["int main(void)", "{"].join("\n"));
	assertEqual(locateLines(doc, 3, 2, hash), 3, "where they were");
	assertEqual(locateLines(["// new", "// lines"].concat(doc), 3, 2, hash), 5, "moved down");
	assertEqual(locateLines(doc.slice(1), 3, 2, hash), 2, "moved up");
	assertEqual(locateLines(["changed"], 3, 2, hash), 0, "gone");
});

test("inline chat: the messages and the session in a chat's summary", () => {
	const first = firstMessage({ prompt: " clamp it ", path: "src/main.c", startLine: 3, endLine: 4, lines: ["int x;", "x = v;"], fence: "c" });
	assertTrue(first.startsWith("clamp it\n"), first);
	assertTrue(first.includes("src/main.c lines 3-4") && first.includes("```c\nint x;\nx = v;\n```"), first);
	assertTrue(first.includes("do not edit files with tools"), "the model is told the box takes its block");
	const empty = firstMessage({ prompt: "a loop", path: "a.c", startLine: 7, endLine: 7, lines: [""], fence: "" });
	assertTrue(empty.includes("a.c line 7 (an empty line") && !empty.includes("```"), empty);
	assertTrue(firstMessage({ prompt: "p", path: "a.md", startLine: 1, endLine: 1, lines: ["```x```"] }).includes("````\n```x```\n````"), "a fence longer than the lines' backticks");
	assertEqual(followUpMessage({ prompt: "shorter", path: "a.c", startLine: 2, endLine: 2, lines: null }), "shorter\n\n(Inline edit of a.c line 2: reply with it again, changed as asked, in one code block.)");
	assertTrue(followUpMessage({ prompt: "p", path: "a.c", startLine: 2, endLine: 3, lines: ["n"], fence: "c" }).endsWith("They read now:\n```c\nn\n```"));
	assertEqual(titleOf("src/main.c", 3, 6, "clamp the velocity\nand more"), "main.c:3-6 clamp the velocity");
	assertEqual(titleOf("a.c", 1, 1, "x".repeat(100)).length, 64);
	assertEqual(inlineOf({ use: "inline", meta: { inline: { path: "a.c", startLine: 1 } } }), { path: "a.c", startLine: 1 });
	assertEqual(inlineOf({ use: "chat", meta: { inline: { path: "a.c", startLine: 1 } } }), null, "another use's chat");
});

// A server that answers what the session asks and plays the chat's events as a test says.
function scriptedServer() {
	const server = { requests: [], topics: new Map(), chats: [] };
	server.client = {
		request: async (method, params) => {
			server.requests.push({ method, params });
			if (method === "chat.create") {
				const summary = { chatId: "c1", wsId: "w1", use: params.use, section: params.section, title: params.title, meta: params.meta, state: "idle", pinnedModel: params.model || "", effort: params.effort || "" };
				server.chats.push(summary);
				server.emit("chats:w1", "chat.created", summary);
				return summary;
			}
			if (method === "chat.send") return { turnId: "u" + server.requests.length };
			if (method === "chat.set") {
				// Only what it names changes, as on the server; `model` is the summary's pinnedModel.
				const change = Object.assign({}, params);
				delete change.chatId;
				if ("model" in change) {
					change.pinnedModel = change.model;
					delete change.model;
				}
				const summary = Object.assign(server.chats.find((c) => c.chatId === params.chatId), change);
				server.emit("chats:w1", "chat.changed", summary);
				return summary;
			}
			return {};
		},
		subscribe: (topic, handler) => {
			server.topics.set(topic, (server.topics.get(topic) || []).concat(handler));
			queueMicrotask(() => handler.snap(topic.startsWith("chats:") ? { chats: server.chats.slice() } : { state: "idle", turns: [] }));
			return () => server.topics.set(topic, server.topics.get(topic).filter((h) => h !== handler));
		},
	};
	server.emit = (topic, kind, data) => {
		for (const handler of server.topics.get(topic) || []) handler.evt(kind, data);
	};
	return server;
}

const tick = (ms) => new Promise((resolve) => setTimeout(resolve, ms || 0));

// The chat plugin's Model menu bindings as a test plays them: the menu's arguments kept, its label from the choice.
function chatBindings() {
	const bindings = { menus: [], labels: [] };
	bindings.has = (name) => name === "chat.modelMenu" || name === "chat.modelLabel";
	bindings.call = async (name, args) => {
		if (name === "chat.modelMenu") {
			const menu = { args, closed: false, close: () => (menu.closed = true) };
			bindings.menus.push(menu);
			return menu;
		}
		bindings.labels.push(args);
		return { model: args.model || args.now || "Haiku 5.5", effort: args.effort || "Low (Inline chat)", title: "t" };
	};
	return bindings;
}

async function setUpEditor(server, text) {
	const context = new Map();
	const ied = {
		client: server.client, workspace: { wsId: "w1" }, report: () => {}, bindings: chatBindings(),
		context: { set: (key, value) => context.set(key, value), get: (key) => context.get(key) },
		commands: { run: async () => {} }, tabs: { open: () => {} }, contributions: Object.assign(() => [], { on: () => () => {} }),
	};
	const monaco = await loadMonaco(ied);
	const host = document.createElement("div");
	host.style.cssText = "position: fixed; left: 0; top: 0; width: 700px; height: 400px; visibility: hidden";
	document.body.append(host);
	const model = monaco.editor.createModel(text, "plaintext", monaco.Uri.parse("inmemory://inlinechat/" + Math.random().toString(36).slice(2) + ".c"));
	const ed = monaco.editor.create(host, { model });
	const editor = { editor: ed, monaco, model, path: "src/main.c", ctx: { wsId: "w1", ref: "doc:src/main.c" }, sync: { holdsLease: true, readOnly: false }, mixedEndings: false, attachments: [], position: () => null };
	const editors = new Editors();
	const chats = new InlineChats(ied, editors);
	editors.add(editor);
	editors.attach((e) => chats.attach(e));
	editors.ready(editor);
	await tick();
	return {
		ied, editor, chats, context, model, monaco,
		done: () => {
			for (const dispose of editor.attachments) dispose();
			ed.dispose();
			model.dispose();
			host.remove();
		},
	};
}

test("inline chat: asked, streamed as a diff, accepted as one undo step", async () => {
	const server = scriptedServer();
	const t = await setUpEditor(server, "#include \"core.h\"\n\nint main(void)\n{\n\treturn core_run();\n}\n");
	try {
		t.editor.editor.setSelection(new t.monaco.Selection(5, 1, 5, 1));
		t.chats.start(t.editor);
		const session = t.chats.of(t.editor)[0];
		assertTrue(session && session.node.isConnected, "the box opens");
		session.input.value = "check the result";
		await session.send();
		const create = server.requests.find((r) => r.method === "chat.create").params;
		assertEqual([create.use, create.section, create.mode, create.permission], ["inline", "Inline", "ask", "plan"]);
		assertEqual(create.meta.inline, { path: "src/main.c", startLine: 5, endLine: 5, lines: 1, hash: textHash("\treturn core_run();"), state: "open" });
		const sent = server.requests.find((r) => r.method === "chat.send").params;
		assertTrue(sent.text.includes("src/main.c line 5") && sent.mentions[0].ref === "src/main.c", sent.text);
		assertEqual(t.chats.of(t.editor).length, 1, "the chat it made is not shown twice");
		server.emit("chat:c1", "turn.start", { turnId: "u1", model: "fake" });
		server.emit("chat:c1", "segment.start", { turnId: "u1", segId: "s1", type: "text", text: "" });
		server.emit("chat:c1", "segment.delta", { segId: "s1", text: "```c\nint result = core_run();\nif (resu" });
		await tick(120);
		assertEqual(session.removedLines.length, 1, "streaming: the line it replaces is marked");
		assertEqual(session.zones.length, 1, "and the whole lines so far shown under it");
		server.emit("chat:c1", "segment.delta", { segId: "s1", text: "lt != 0) return result;\nreturn 0;\n```\nChecked." });
		server.emit("chat:c1", "turn.end", { turnId: "u1", stop: "finished" });
		server.emit("chat:c1", "chat.changed", { state: "idle" });
		await tick(120);
		assertTrue(session.proposalReady(), "ready to accept");
		t.editor.editor.setPosition({ lineNumber: 1, column: 1 });
		assertEqual(t.context.get("inlineChatProposal"), undefined, "Ctrl+Enter is the caret's: it is elsewhere");
		t.editor.editor.setPosition({ lineNumber: 5, column: 2 });
		assertEqual(t.context.get("inlineChatProposal"), true, "the caret in the lines: Ctrl+Enter accepts");
		assertEqual(session.answer.textContent.trim(), "Checked.", "the words around the block");
		assertEqual(session.counts.textContent, "+3-1");
		await session.accept();
		assertEqual(t.model.getLineContent(5), "\tint result = core_run();", "accepted, at the lines' indentation");
		assertEqual(t.model.getLineContent(7), "\treturn 0;");
		assertEqual(t.chats.of(t.editor).length, 0, "the box closes");
		const closed = server.requests.filter((r) => r.method === "chat.set").pop().params;
		assertEqual(closed.meta.inline.state, "accepted", "and the chat says so, for the other places showing it");
		t.editor.editor.trigger("test", "undo", null);
		assertEqual(t.model.getLineContent(5), "\treturn core_run();", "one undo takes it back");
	}
	finally {
		t.done();
	}
});

test("inline chat: a session comes back after a reload and closes when its chat closes elsewhere", async () => {
	const server = scriptedServer();
	const inline = { path: "src/main.c", startLine: 3, endLine: 4, lines: 2, hash: textHash("int main(void)\n{"), state: "open" };
	server.chats.push({ chatId: "c9", wsId: "w1", use: "inline", meta: { inline }, state: "idle" });
	const t = await setUpEditor(server, "// moved\n#include \"core.h\"\n\nint main(void)\n{\n\treturn core_run();\n}\n");
	try {
		const session = t.chats.of(t.editor)[0];
		assertTrue(session && session.chatId === "c9", "restored from the chats topic");
		assertEqual([session.startLine(), session.endLine()], [4, 5], "where its lines went");
		server.emit("chats:w1", "chat.changed", { chatId: "c9", wsId: "w1", use: "inline", meta: { inline: Object.assign({}, inline, { state: "discarded" }) }, state: "idle" });
		assertEqual(t.chats.of(t.editor).length, 0, "discarded on another device: gone here");
	}
	finally {
		t.done();
	}
});

test("inline chat: the model and effort picked in the box are its chat's", async () => {
	const server = scriptedServer();
	const t = await setUpEditor(server, "int main(void)\n{\n\treturn core_run();\n}\n");
	try {
		t.editor.editor.setSelection(new t.monaco.Selection(3, 1, 3, 1));
		t.chats.start(t.editor);
		const session = t.chats.of(t.editor)[0];
		const bindings = t.ied.bindings;
		await tick();
		assertTrue(!session.modelButton.hidden, "the box has a model button");
		assertEqual([session.modelName.textContent, session.effortName.textContent], ["Haiku 5.5", "Low (Inline chat)"], "named as the chat plugin says: the inline use's defaults");
		assertEqual(bindings.labels[0].use, "inline");
		await session.pickModel();
		const menu = bindings.menus[0];
		assertEqual([menu.args.at, menu.args.use, menu.args.model, menu.args.effort], [session.modelButton, "inline", "", ""], "the chat's Model menu, at the button");
		menu.args.onChange({ effort: "high" });
		menu.args.onChange({ model: "claude-opus" });
		await tick();
		assertEqual([session.modelName.textContent, session.effortName.textContent], ["claude-opus", "high"]);
		assertEqual(server.requests.filter((r) => r.method === "chat.set").length, 0, "no chat yet: kept in the box");
		session.input.value = "check the result";
		await session.send();
		const create = server.requests.find((r) => r.method === "chat.create").params;
		assertEqual([create.model, create.effort], ["claude-opus", "high"], "the chat is made with them");
		await session.pickModel();
		assertTrue(menu.closed, "a second menu closes the first");
		bindings.menus[1].args.onChange({ effort: "" });
		const set = server.requests.filter((r) => r.method === "chat.set").pop().params;
		assertEqual(set, { chatId: "c1", effort: "" }, "after: the chat's effort changes, back to its default");
		assertEqual(server.chats[0].meta.inline.state, "open", "and the session stays");
		server.emit("chats:w1", "chat.changed", Object.assign({}, server.chats[0], { pinnedModel: "claude-sonnet" }));
		await tick(120);
		assertEqual(session.pinned, "claude-sonnet", "a model picked in the chat's tab shows here");
		assertEqual(t.chats.of(t.editor).length, 1);
		session.discard();
		assertTrue(bindings.menus[1].closed, "closing the box closes its menu");
	}
	finally {
		t.done();
	}
});
