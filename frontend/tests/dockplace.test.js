// dockplace.test.js - which dock host a new tab opens in (docs/15 "Docking"): the one the window is on when it takes
// tabs of that kind, else the host naming the kind, else the first taking any. A Browser tab follows the person:
// opened in the Chat activity it goes to the chats' well, opened in the Editor to the documents'.

import { test, assertEqual, assertTrue } from "./harness.js";
import { tabKindOf, takesKind, dockFor } from "../src/shell/dockplace.js";

// The activities as shipped: chat takes its own tabs and pages, the editor and the plugin hosts name none.
const CHAT = { id: "ied.chat", title: "Chat", documents: ["chat", "browser"] };
const EDITOR = { id: "ied.editor", title: "Editor" };
const PROFILER = { id: "ied.profiler", title: "Profiler" };
const GIT = { id: "ied.git", title: "Git", view: "GitActivity" };
const HOSTS = [CHAT, EDITOR, PROFILER, GIT];

test("dockplace: a ref's kind is what stands before its colon", () => {
	assertEqual(tabKindOf("browser:pg-1234"), "browser");
	assertEqual(tabKindOf("doc:src/main.cf"), "doc");
	assertEqual(tabKindOf("ied.output"), null, "a tool window id names no kind");
	assertEqual(tabKindOf(""), null);
	assertEqual(tabKindOf(null), null);
});

test("dockplace: a well takes the kinds it names, and any when it names none", () => {
	assertTrue(takesKind(CHAT, "chat") && takesKind(CHAT, "browser"), "the kinds it names");
	assertTrue(!takesKind(CHAT, "doc"), "a file is no chat's document");
	assertTrue(takesKind(EDITOR, "doc") && takesKind(EDITOR, "browser"), "the editor names none, so it takes any");
	assertTrue(!takesKind(GIT, "doc"), "an activity that renders itself holds no documents");
	assertTrue(!takesKind(null, "doc"), "no activity");
});

test("dockplace: a tab opens where the person is when that well takes it", () => {
	assertEqual(dockFor("browser", EDITOR, HOSTS), "ied.editor", "from the Editor: with the documents");
	assertEqual(dockFor("browser", CHAT, HOSTS), "ied.chat", "from a chat: in the chats' own well");
	assertEqual(dockFor("doc", CHAT, HOSTS), "ied.editor", "a file opened from a chat goes to the Editor");
	assertEqual(dockFor("chat", EDITOR, HOSTS), "ied.editor", "the Editor names no kinds, so it keeps a tab of any kind itself");
});

test("dockplace: an activity that takes no tabs of the kind sends it to the host that does", () => {
	assertEqual(dockFor("browser", GIT, HOSTS), "ied.chat", "the first host naming the kind");
	assertEqual(dockFor("prof", GIT, HOSTS), "ied.editor", "none names it: the first taking any");
	assertEqual(dockFor("prof", null, HOSTS), "ied.editor", "and the same with no activity shown");
});

test("dockplace: a kind no host names goes to the first taking any", () => {
	assertEqual(dockFor("widget", CHAT, HOSTS), "ied.editor", "the current well names kinds and not this one");
	assertEqual(dockFor("widget", GIT, HOSTS), "ied.editor", "and the same from an activity that holds no documents");
	assertEqual(dockFor("doc", CHAT, []), null, "no dock host at all");
});
