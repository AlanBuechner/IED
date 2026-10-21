// ansi.js - ANSI escape sequences in streamed text (build output, logs) as styled runs. SGR colors and weights become
// classes of ui-kit.css (theme tokens --ansi-*); every other control sequence is dropped. A sequence split across
// chunks is completed by the next read.
//     const reader = new AnsiReader();
//     for (const run of reader.read(chunk)) line.append(h("span", { class: run.cls }, run.text));

const NAMES = ["black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"];
const BRIGHT = ["gray", "bred", "bgreen", "byellow", "bblue", "bmagenta", "bcyan", "bwhite"];
const ESC = "\x1b";
const BEL = "\x07";

function plainStyle() {
	return { fg: null, bg: null, bold: false, dim: false, italic: false, underline: false };
}

// The palette name of a 256-color index, when it is one of the 16 the theme defines.
function indexed(n) {
	if (n >= 0 && n < 8) return NAMES[n];
	if (n >= 8 && n < 16) return BRIGHT[n - 8];
	return null;
}

export class AnsiReader {
	constructor() {
		this.style = plainStyle();
		this.pending = "";
	}

	// The runs of `text`: [{text, cls}], `cls` the classes of the style it is in ("" when plain).
	read(text) {
		const input = this.pending + text;
		this.pending = "";
		const runs = [];
		let start = 0;
		let at = input.indexOf(ESC);
		while (at >= 0) {
			if (at > start) this.push(runs, input.slice(start, at));
			const end = this.sequenceEnd(input, at);
			if (end < 0) {
				this.pending = input.slice(at);
				return runs;
			}
			this.apply(input.slice(at, end));
			start = end;
			at = input.indexOf(ESC, start);
		}
		if (start < input.length) this.push(runs, input.slice(start));
		return runs;
	}

	push(runs, text) {
		const cls = this.classes();
		const last = runs[runs.length - 1];
		if (last && last.cls === cls) last.text += text;
		else runs.push({ text, cls });
	}

	// Where the sequence starting at `at` ends (exclusive), or -1 when the text stops inside it.
	sequenceEnd(input, at) {
		if (at + 1 >= input.length) return -1;
		const kind = input[at + 1];
		if (kind === "[") {
			for (let i = at + 2; i < input.length; i++) {
				const code = input.charCodeAt(i);
				if (code >= 0x40 && code <= 0x7e) return i + 1;
			}
			return -1;
		}
		if (kind === "]") {
			for (let i = at + 2; i < input.length; i++) {
				if (input[i] === BEL) return i + 1;
				if (input[i] === ESC && input[i + 1] === "\\") return i + 2;
			}
			return -1;
		}
		return at + 2;
	}

	apply(sequence) {
		if (!sequence.startsWith(ESC + "[") || !sequence.endsWith("m")) return;
		const body = sequence.slice(2, -1);
		const codes = body === "" ? [0] : body.split(/[;:]/).map((part) => (part === "" ? 0 : Number(part)));
		for (let i = 0; i < codes.length; i++) i += this.applyCode(codes, i);
	}

	// Applies codes[i]; how many further codes it used.
	applyCode(codes, i) {
		const code = codes[i];
		const style = this.style;
		if (code === 0) this.style = plainStyle();
		else if (code === 1) style.bold = true;
		else if (code === 2) style.dim = true;
		else if (code === 3) style.italic = true;
		else if (code === 4) style.underline = true;
		else if (code === 22) style.bold = style.dim = false;
		else if (code === 23) style.italic = false;
		else if (code === 24) style.underline = false;
		else if (code >= 30 && code <= 37) style.fg = NAMES[code - 30];
		else if (code >= 90 && code <= 97) style.fg = BRIGHT[code - 90];
		else if (code === 39) style.fg = null;
		else if (code >= 40 && code <= 47) style.bg = NAMES[code - 40];
		else if (code >= 100 && code <= 107) style.bg = BRIGHT[code - 100];
		else if (code === 49) style.bg = null;
		else if (code === 38 || code === 48) return this.applyExtended(codes, i, code === 38 ? "fg" : "bg");
		return 0;
	}

	// 38;5;n and 38;2;r;g;b (and 48;...): a 256-color index within the theme's 16 is kept, anything else is plain.
	applyExtended(codes, i, which) {
		if (codes[i + 1] === 5) {
			this.style[which] = indexed(codes[i + 2]);
			return 2;
		}
		if (codes[i + 1] === 2) {
			this.style[which] = null;
			return 4;
		}
		return 0;
	}

	classes() {
		const style = this.style;
		const out = [];
		if (style.fg) out.push("a-" + style.fg);
		if (style.bg) out.push("ab-" + style.bg);
		if (style.bold) out.push("a-bold");
		if (style.dim) out.push("a-dim");
		if (style.italic) out.push("a-italic");
		if (style.underline) out.push("a-underline");
		return out.join(" ");
	}
}

// Text without its escape sequences.
export function stripAnsi(text) {
	return new AnsiReader().read(text).map((run) => run.text).join("");
}
