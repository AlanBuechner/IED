// chatcomposer.test.js - the chat composer's text box (plugins/chat composer.js, composertext.js; docs/15 "Chat"): text
// pasted or dropped in goes in plain, whatever the page it came from; the mentions it names are drawn as chips behind
// it, a file's and a folder's apart, only while the text still names them; an owner's message comes back to edit.

import { test, assertEqual, assertTrue } from "./harness.js";
import { Composer } from "/plugins/ied.chat/ui/composer.js";
import { plainText } from "/plugins/ied.chat/ui/composertext.js";
import { keptMentions, mentionRanges } from "/plugins/ied.chat/ui/mentions.js";

const NBSP = String.fromCharCode(0xa0);
const ZWSP = String.fromCharCode(0x200b);
const BOM = String.fromCharCode(0xfeff);
const RLM = String.fromCharCode(0x200f);
const EM_SPACE = String.fromCharCode(0x2003);
const LINE_SEPARATOR = String.fromCharCode(0x2028);
const ZWJ = String.fromCharCode(0x200d);

function composer() {
	const sent = [];
	const made = new Composer({
		send: async (text, as, extras) => {
			sent.push({ text, as, extras });
			return true;
		},
		stop: () => {}, unqueue: async () => null, sendQueued: () => {}, set: () => {}, choices: async () => null, mentionRows: async () => [],
		upload: async () => null, fileUrl: (path) => "/files/" + path, toast: () => {},
	});
	made.update({ summary: { title: "T", state: "idle" }, running: () => false, pending: [], queued: [], lastTurn: () => null, context: null, model: "m" });
	document.body.append(made.el);
	return [made, sent];
}

function paste(input, data) {
	const transfer = new DataTransfer();
	for (const [type, value] of Object.entries(data)) transfer.setData(type, value);
	const event = new ClipboardEvent("paste", { clipboardData: transfer, bubbles: true, cancelable: true });
	input.dispatchEvent(event);
	return event;
}

test("chat composer: pasted text is made plain - line breaks, spaces and invisible marks - its indentation kept", () => {
	assertEqual(plainText("a\r\nb\rc"), "a\nb\nc", "Windows and old Mac line breaks");
	assertEqual(plainText("one" + NBSP + "two" + EM_SPACE + "three"), "one two three", "other spaces are spaces");
	assertEqual(plainText(BOM + "x" + ZWSP + "y" + RLM + "z"), "xyz", "a byte order mark, a zero-width space, a bidi mark");
	assertEqual(plainText("p1" + LINE_SEPARATOR + "p2"), "p1\np2", "a line separator is a line break");
	assertEqual(plainText("\tif (x) {\n\t\treturn;\n\t}"), "\tif (x) {\n\t\treturn;\n\t}", "tabs and indentation as they were");
	const family = String.fromCodePoint(0x1f468) + ZWJ + String.fromCodePoint(0x1f469);
	assertEqual(plainText(family + " and a" + ZWJ + "b"), family + " and ab", "a joiner inside an emoji stays, a stray one goes");
});

test("chat composer: pasting formatted text puts its plain text at the caret, never its formatting", () => {
	const [made] = composer();
	try {
		made.setText("Look: ");
		made.input.focus();
		made.input.setSelectionRange(6, 6);
		const event = paste(made.input, { "text/html": "<p><b>Bold</b>&nbsp;words</p>", "text/plain": "Bold" + NBSP + "words\r\n\tindented" + ZWSP });
		assertTrue(event.defaultPrevented, "the browser's own paste is taken over");
		assertEqual(made.input.value, "Look: Bold words\n\tindented", "plain text, normalized");
		assertEqual(made.input.selectionStart, made.input.value.length, "the caret after what was pasted");
	}
	finally {
		made.el.remove();
	}
});

test("chat mentions: a mention is named where it starts a word and does not run on into a longer one", () => {
	const file = { kind: "file", ref: "src/main.c", label: "src/main.c" };
	const folder = { kind: "folder", ref: "src", label: "src/" };
	const ranges = (text) => mentionRanges(text, [folder, file]).map((r) => [r.start, r.end, r.mention.kind]);
	assertEqual(ranges("see @src/main.c and @src/"), [[4, 15, "file"], [20, 25, "folder"]], "both, the longer label first where they overlap");
	assertEqual(ranges("see @src/main.c."), [[4, 15, "file"]], "a full stop after it ends it");
	assertEqual(ranges("see @src/main.cf"), [], "typed on into another file: no longer the mention");
	assertEqual(ranges("see @src/game"), [], "a folder typed on into a path");
	assertEqual(ranges("mail@src/main.c"), [], "inside a word");
	assertEqual(keptMentions("see @src/main.cf", [file]), [], "the message keeps only what it still names");
});

test("chat composer: mentions are drawn as chips behind the text, a file's and a folder's apart, in the box's own font and size", async () => {
	const link = document.querySelector("link[data-test='chat-css']") || document.head.appendChild(Object.assign(document.createElement("link"), { rel: "stylesheet", href: "/plugins/ied.chat/ui/chat.css" }));
	link.dataset.test = "chat-css";
	if (!link.sheet) await new Promise((resolve) => link.addEventListener("load", resolve, { once: true }));
	const [made] = composer();
	made.el.style.width = "600px";
	try {
		made.mentions = [{ kind: "file", ref: "src/main.c", label: "src/main.c" }, { kind: "folder", ref: "docs", label: "docs/" }];
		made.setText("Look at @src/main.c and everything in @docs/ please");
		const marks = made.marks.el;
		assertEqual([...marks.querySelectorAll(".chat-mark")].map((m) => [m.dataset.kind, m.textContent]), [["file", "@src/main.c"], ["folder", "@docs/"]], "a chip for each");
		assertEqual(marks.textContent, made.input.value, "the layer holds the box's text, so each chip sits under its words");
		const [layer, box] = [getComputedStyle(marks), getComputedStyle(made.input)];
		for (const key of ["fontFamily", "fontSize", "lineHeight", "paddingTop", "paddingLeft", "letterSpacing", "whiteSpace"]) assertEqual(layer[key], key === "whiteSpace" ? "pre-wrap" : box[key], key);
		const probe = document.body.appendChild(Object.assign(document.createElement("span"), { style: "color: transparent" }));
		assertEqual(layer.color, getComputedStyle(probe).color, "its own text is clear: the box's is the one read");
		probe.remove();
		assertEqual([marks.offsetWidth, marks.offsetHeight], [made.input.clientWidth, made.input.clientHeight], "over the box's text area");
		made.input.setSelectionRange(19, 19);
		made.input.setRangeText("x", 19, 19, "end");
		made.input.dispatchEvent(new InputEvent("input", { bubbles: true }));
		assertEqual([...marks.querySelectorAll(".chat-mark")].map((m) => m.dataset.kind), ["folder"], "typed on into another name: the file's chip goes");
	}
	finally {
		made.el.remove();
	}
});

test("chat composer: an owner's message edited comes back with what it named, and goes with the turn it replaces", async () => {
	const [made, sent] = composer();
	try {
		const mention = { kind: "file", ref: "src/main.c", label: "src/main.c" };
		made.edit("u3", Date.UTC(2026, 9, 26, 14, 2), { text: "Fix @src/main.c", mentions: [mention], attachments: [] });
		assertEqual([made.input.value, made.editingEl.hidden], ["Fix @src/main.c", false], "the text in the box, and a bar saying it is an edit");
		assertTrue(made.editingEl.textContent.includes("reverts the chat"), "the bar says what sending does");
		made.setText(made.input.value + " now");
		await made.send("message");
		assertEqual(sent, [{ text: "Fix @src/main.c now", as: "message", extras: { mentions: [mention], attachments: [], revertTo: "u3" } }], "sent with the turn it replaces");
		assertEqual([made.editing, made.editingEl.hidden], [null, true], "no longer editing");
		made.edit("u4", 0, { text: "Other", mentions: [], attachments: [] });
		made.input.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true, cancelable: true }));
		assertEqual([made.editing, made.input.value], [null, ""], "Esc stops editing and empties the box");
	}
	finally {
		made.el.remove();
	}
});
