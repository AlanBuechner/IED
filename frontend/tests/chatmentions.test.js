// chatmentions.test.js - the composer's @ list and attachments (docs/04 "Attachments and @-mentions"): where an "@word"
// is typed, how rows rank, the folders of files, a symbol as a mention, which mentions a message still holds.

import { test, assertEqual } from "./harness.js";
import { foldersOf, keptMentions, matchRank, mentionAt, rankRows, symbolMention } from "/plugins/ied.chat/ui/mentions.js";
import { sizeText } from "/plugins/ied.chat/ui/attachbar.js";

test("chat mentions: an @ starts one at a word's start only", () => {
	assertEqual(mentionAt("look at @src/ri", 15), { start: 8, query: "src/ri" });
	assertEqual(mentionAt("@", 1), { start: 0, query: "" });
	assertEqual(mentionAt("(@#Ring", 7), { start: 1, query: "#Ring" });
	assertEqual(mentionAt("mail me@host", 12), null);
	assertEqual(mentionAt("@src/a.cf done", 14), null);
});

test("chat mentions: rows rank by their label", () => {
	assertEqual([matchRank("src", "src/ring.cf"), matchRank("ring", "src/ring.cf"), matchRank("ng.c", "src/ring.cf"), matchRank("srf", "src/ring.cf"), matchRank("zz", "src/ring.cf")], [0, 1, 2, 3, null]);
	assertEqual(matchRank("", "anything"), 0);
	const rows = rankRows("ri", [{ label: "src/main.cf" }, { label: "terminal:Build" }, { label: "src/ring.cf" }, { label: "diff" }]);
	assertEqual(rows.map((row) => row.label), ["src/ring.cf", "src/main.cf", "terminal:Build"]);
});

test("chat mentions: the folders of files", () => {
	assertEqual(foldersOf(["src/game/ring.cf", "src/main.cf", "README.md"]).sort(), ["src/", "src/game/"]);
});

test("chat mentions: a symbol by the place of its name", () => {
	const symbol = { name: "Push", containerName: "Game::Ring", kind: 6, location: { uri: "ied://w1/src/game/ring%20buf.cf", range: { start: { line: 41, character: 6 } } } };
	assertEqual(symbolMention(symbol, "w1"), { kind: "symbol", ref: "src/game/ring buf.cf:42", label: "#Game::Ring::Push", detail: "src/game/ring buf.cf" });
	assertEqual(symbolMention({ name: "x", location: { uri: "file:///c:/elsewhere.h" } }, "w1"), null);
});

test("chat mentions: a message keeps those it still names, each once", () => {
	const file = { kind: "file", ref: "src/a.cf", label: "src/a.cf" };
	const diff = { kind: "diff", ref: "", label: "diff" };
	assertEqual(keptMentions("see @src/a.cf and @src/a.cf", [file, file, diff]), [file]);
});

test("chat attachments: sizes as the chips say them", () => {
	assertEqual([sizeText(12), sizeText(12300), sizeText(5 * 1024 * 1024)], ["12 B", "13 KB", "5.0 MB"]);
});
