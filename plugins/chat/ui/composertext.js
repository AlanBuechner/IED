// composertext.js - what the composer's text box does with its text (docs/15 "Chat"): text pasted or dropped in goes in
// plain, whatever the page it came from (owner, 2026-10-26: "it should remove the formating"), and the mentions the text
// names are drawn as chips behind it - a layer under the box in its exact font, padding and wrapping, scrolled with it,
// so editing stays a text box's (owner: "better styling for files using @some/path/file.txt or @some/folder").

import { h, replace } from "/ui-kit/dom.js";
import { mentionRanges } from "./mentions.js";

const SPACES = /[\u00a0\u1680\u2000-\u200a\u202f\u205f\u3000]/g;
const LINE_BREAKS = /\r\n?|[\u000b\u000c\u0085\u2028\u2029]/g;
// Invisible marks: zero-width spaces, the word joiner, byte order marks, soft hyphens, the Mongolian vowel separator,
// bidirectional controls, and control characters other than the tab and the line break.
const INVISIBLE = /[\u200b\u2060\ufeff\u00ad\u180e\u200e\u200f\u061c\u202a-\u202e\u2066-\u2069\u0000-\u0008\u000e-\u001f\u007f]/g;
// A zero-width joiner joins two pictures into one emoji, and a non-joiner shapes the letters of some scripts: kept
// there, gone anywhere else.
const STRAY_JOINER = /(?<!\p{Extended_Pictographic}\ufe0f?)\u200d|\u200d(?!\p{Extended_Pictographic})/gu;
const STRAY_NON_JOINER = /(?<=[\u0000-\u007f])\u200c|\u200c(?=[\u0000-\u007f])|^\u200c|\u200c$/g;

// `text` as the box takes it: line breaks as "\n", other spaces as plain spaces, invisible marks gone; tabs and
// indentation as they were.
export function plainText(text) {
	return String(text || "").replace(LINE_BREAKS, "\n").replace(SPACES, " ").replace(INVISIBLE, "").replace(STRAY_JOINER, "").replace(STRAY_NON_JOINER, "");
}

// `text` put in `input` over its selection as if typed - one step of its undo - with an input event.
export function insertText(input, text) {
	input.focus();
	if (document.execCommand && document.execCommand("insertText", false, text)) return;
	input.setRangeText(text, input.selectionStart, input.selectionEnd, "end");
	input.dispatchEvent(new InputEvent("input", { bubbles: true, inputType: "insertText", data: text }));
}

// The chips layer behind a composer's textarea (chat.css .chat-input-marks): its text is the box's, clear, and each
// mention it names a span the stylesheet draws as a chip - a file's, a folder's or another kind's.
export class InputMarks {
	constructor(input) {
		this.input = input;
		this.text = null;
		this.el = h("div.chat-input-marks", { "aria-hidden": "true" });
		this.input.addEventListener("scroll", () => this.sync());
		this.resizer = new ResizeObserver(() => this.sync());
		this.resizer.observe(this.input);
	}

	// The layer for the box's text now and the mentions the message makes.
	update(mentions) {
		const text = this.input.value;
		const ranges = mentionRanges(text, mentions);
		const key = text + "\n" + ranges.map((range) => range.start + ":" + range.end + ":" + range.mention.kind).join(",");
		if (key !== this.text) {
			this.text = key;
			const parts = [];
			let at = 0;
			for (const range of ranges) {
				parts.push(text.slice(at, range.start));
				parts.push(h("span.chat-mark", { dataset: { kind: range.mention.kind } }, text.slice(range.start, range.end)));
				at = range.end;
			}
			// a last line break draws the empty line after it, as the box does
			parts.push(text.slice(at) + (text.endsWith("\n") ? " " : ""));
			replace(this.el, ...parts);
		}
		this.sync();
	}

	// The layer over the box's text area (its scroll bar left out) and scrolled as it is.
	sync() {
		this.el.style.width = this.input.clientWidth + "px";
		this.el.style.height = this.input.clientHeight + "px";
		this.el.scrollTop = this.input.scrollTop;
	}

	dispose() {
		this.resizer.disconnect();
	}
}
