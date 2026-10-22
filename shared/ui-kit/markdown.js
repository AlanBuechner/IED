// markdown.js - Markdown to elements for the editor's preview (F-EDT-91) and chat replies: the GitHub-flavored blocks
// (headings, paragraphs, lists with task items, quotes, fenced and indented code, tables, rules) and inlines (code,
// emphasis, strikethrough, links, images, autolinks, hard breaks). Text becomes text nodes and never markup, so a
// document cannot inject any. Every block element carries `data-line`, its first source line (0-based), and a task
// item's checkbox `data-task`, the line of its `[ ]` / `[x]`, so the preview can scroll with the editor and tick a box in
// the source.

import { h } from "/ui-kit/dom.js";

const FENCE = /^ {0,3}(`{3,}|~{3,})\s*([^`\s]*)/;
const HEADING = /^ {0,3}(#{1,6})(?:\s+(.*?))?\s*#*\s*$/;
const RULE = /^ {0,3}([-*_])(?:\s*\1){2,}\s*$/;
const QUOTE = /^ {0,3}> ?(.*)$/;
const ITEM = /^(\s*)([-*+]|\d{1,9}[.)])(\s+|$)(.*)$/;
const TASK = /^\[([ xX])\]\s+(.*)$/;
const TABLE_RULE = /^\s*\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?\s*$/;

// The elements of `text`: {nodes, options}; options.code(text, language, closed) may return an element for a fenced
// block. `closed`: its closing fence is there, so a reply still streaming will not change the block.
export function renderMarkdown(text, options = {}) {
	const lines = text.replace(/\r\n?/g, "\n").split("\n");
	return blocks(lines, 0, lines.length, options);
}

// Fence name -> language id, from the loaded languages' `fences` and their own ids: a "c++" fence and a "cpp" one both
// mean "cpp".
export function fenceLanguages(languages) {
	const map = new Map();
	for (const language of languages) {
		for (const fence of language.fences || []) map.set(fence.toLowerCase(), language.id);
		if (!map.has(language.id.toLowerCase())) map.set(language.id.toLowerCase(), language.id);
	}
	return map;
}

function blank(line) {
	return /^\s*$/.test(line);
}

function blocks(lines, from, to, options) {
	const out = [];
	let at = from;
	while (at < to) {
		const line = lines[at];
		if (blank(line)) {
			at++;
			continue;
		}
		const fence = FENCE.exec(line);
		if (fence) {
			at = codeFence(lines, at, to, fence, options, out);
			continue;
		}
		const heading = HEADING.exec(line);
		if (heading) {
			out.push(h("h" + heading[1].length, { "data-line": String(at) }, ...inline(heading[2] || "")));
			at++;
			continue;
		}
		if (RULE.test(line)) {
			out.push(h("hr", { "data-line": String(at) }));
			at++;
			continue;
		}
		if (QUOTE.test(line)) {
			at = quote(lines, at, to, options, out);
			continue;
		}
		if (ITEM.test(line)) {
			at = list(lines, at, to, options, out);
			continue;
		}
		if (/^( {4}|\t)/.test(line)) {
			at = indentedCode(lines, at, to, out);
			continue;
		}
		if (line.includes("|") && at + 1 < to && TABLE_RULE.test(lines[at + 1])) {
			at = table(lines, at, to, out);
			continue;
		}
		at = paragraph(lines, at, to, out);
	}
	return out;
}

function codeFence(lines, at, to, fence, options, out) {
	const closing = new RegExp("^ {0,3}" + fence[1][0] + "{" + fence[1].length + ",}\\s*$");
	const body = [];
	let end = at + 1;
	while (end < to && !closing.test(lines[end])) body.push(lines[end++]);
	const code = body.join("\n");
	const colored = options.code ? options.code(code, fence[2], end < to) : null;
	out.push(h("pre", { "data-line": String(at) }, colored || h("code", null, code)));
	return Math.min(end + 1, to);
}

function indentedCode(lines, at, to, out) {
	const body = [];
	let end = at;
	while (end < to && (/^( {4}|\t)/.test(lines[end]) || (blank(lines[end]) && end + 1 < to && /^( {4}|\t)/.test(lines[end + 1])))) {
		body.push(lines[end].replace(/^( {4}|\t)/, ""));
		end++;
	}
	out.push(h("pre", { "data-line": String(at) }, h("code", null, body.join("\n"))));
	return end;
}

function quote(lines, at, to, options, out) {
	const inner = [];
	let end = at;
	while (end < to && !blank(lines[end]) && (QUOTE.test(lines[end]) || inner.length)) {
		const match = QUOTE.exec(lines[end]);
		inner.push(match ? match[1] : lines[end]);
		end++;
	}
	const element = h("blockquote", { "data-line": String(at) }, ...blocks(inner, 0, inner.length, options));
	out.push(element);
	return end;
}

function paragraph(lines, at, to, out) {
	const body = [];
	let end = at;
	while (end < to && !blank(lines[end]) && !(end > at && (FENCE.test(lines[end]) || HEADING.test(lines[end]) || RULE.test(lines[end]) || QUOTE.test(lines[end]) || ITEM.test(lines[end])))) {
		body.push(lines[end]);
		end++;
	}
	out.push(h("p", { "data-line": String(at) }, ...inline(body.join("\n"))));
	return end;
}

// A list and its items; an item's body is the lines indented past its marker, kept with their source lines.
function list(lines, at, to, options, out) {
	const first = ITEM.exec(lines[at]);
	const indent = first[1].length;
	const ordered = /\d/.test(first[2]);
	const element = ordered ? h("ol", { "data-line": String(at) }) : h("ul", { "data-line": String(at) });
	if (ordered && parseInt(first[2], 10) !== 1) element.setAttribute("start", String(parseInt(first[2], 10)));
	let end = at;
	while (end < to) {
		const match = ITEM.exec(lines[end]);
		if (!match || match[1].length !== indent || /\d/.test(match[2]) !== ordered) break;
		const contentIndent = match[1].length + match[2].length + Math.max(1, match[3].length);
		const itemLines = [match[4]];
		const lineOf = [end];
		let next = end + 1;
		while (next < to) {
			const line = lines[next];
			if (blank(line)) {
				if (next + 1 < to && leadingSpaces(lines[next + 1]) >= contentIndent) {
					itemLines.push("");
					lineOf.push(next);
					next++;
					continue;
				}
				break;
			}
			const sibling = ITEM.exec(line);
			if (sibling && sibling[1].length <= indent) break;
			if (leadingSpaces(line) < contentIndent && !sibling && (FENCE.test(line) || HEADING.test(line) || QUOTE.test(line))) break;
			itemLines.push(line.slice(Math.min(contentIndent, leadingSpaces(line))));
			lineOf.push(next);
			next++;
		}
		element.append(item(itemLines, lineOf, end, options));
		end = next;
		while (end < to && blank(lines[end]) && end + 1 < to && ITEM.test(lines[end + 1]) && ITEM.exec(lines[end + 1])[1].length === indent) end++;
	}
	out.push(element);
	return end;
}

function leadingSpaces(line) {
	return line.length - line.replace(/^[ \t]+/, "").length;
}

function item(itemLines, lineOf, at, options) {
	const task = TASK.exec(itemLines[0]);
	const element = h("li", { "data-line": String(at) });
	if (task) {
		element.classList.add("task");
		element.append(h("input", { type: "checkbox", "data-task": String(at), checked: task[1] !== " " ? "" : null }));
		itemLines = [task[2], ...itemLines.slice(1)];
	}
	const children = blocks(itemLines, 0, itemLines.length, options);
	for (const child of children) shiftLines(child, lineOf);
	// A tight item's single paragraph is its text, as GitHub shows it.
	if (children.length && children[0].tagName === "P" && !itemLines.slice(1).some(blank)) element.append(...children[0].childNodes, ...children.slice(1));
	else element.append(...children);
	return element;
}

// Elements rendered from an item's lines carry those lines' indexes; they become the document's.
function shiftLines(element, lineOf) {
	for (const node of [element, ...element.querySelectorAll("[data-line], [data-task]")]) {
		for (const name of ["data-line", "data-task"]) {
			if (node.hasAttribute(name)) node.setAttribute(name, String(lineOf[Math.min(parseInt(node.getAttribute(name), 10), lineOf.length - 1)]));
		}
	}
}

function cells(line) {
	return line.trim().replace(/^\|/, "").replace(/(?<!\\)\|$/, "").split(/(?<!\\)\|/).map((cell) => cell.trim().replace(/\\\|/g, "|"));
}

function table(lines, at, to, out) {
	const aligns = cells(lines[at + 1]).map((rule) => (rule.startsWith(":") && rule.endsWith(":") ? "center" : rule.endsWith(":") ? "right" : rule.startsWith(":") ? "left" : null));
	const row = (line, tag, index) => h("tr", { "data-line": String(index) }, ...aligns.map((align, i) => h(tag, align ? { style: { "text-align": align } } : null, ...inline(cells(line)[i] || ""))));
	const body = [];
	let end = at + 2;
	while (end < to && !blank(lines[end]) && lines[end].includes("|")) {
		body.push(row(lines[end], "td", end));
		end++;
	}
	out.push(h("table", { "data-line": String(at) }, h("thead", null, row(lines[at], "th", at)), h("tbody", null, ...body)));
	return end;
}

// --- inlines ---------------------------------------------------------------------------------------------------------

const INLINE = [
	{ re: /^`+/, make: codeSpan },
	{ re: /^!\[([^\]]*)\]\(\s*<?((?:[^\s()<>]|\([^\s()<>]*\))*)>?(?:\s+"([^"]*)")?\s*\)/, make: (m) => [h("img", { src: safeUrl(m[2]), alt: m[1], title: m[3] || null }), m[0].length] },
	{ re: /^\[((?:[^\[\]]|\[[^\]]*\])*)\]\(\s*<?((?:[^\s()<>]|\([^\s()<>]*\))*)>?(?:\s+"([^"]*)")?\s*\)/, make: (m) => [h("a", { href: safeUrl(m[2]), title: m[3] || null }, ...inline(m[1])), m[0].length] },
	{ re: /^<((?:https?|mailto):[^\s>]+)>/, make: (m) => [h("a", { href: m[1] }, m[1]), m[0].length] },
	{ re: /^https?:\/\/[^\s<]*[^\s<.,;:!?)\]'"]/, make: (m) => [h("a", { href: m[0] }, m[0]), m[0].length] },
	{ re: /^\*\*(?=\S)([\s\S]*?\S)\*\*/, make: (m) => [h("strong", null, ...inline(m[1])), m[0].length] },
	{ re: /^__(?=\S)([\s\S]*?\S)__(?![A-Za-z0-9])/, make: (m) => [h("strong", null, ...inline(m[1])), m[0].length] },
	{ re: /^\*(?=\S)([\s\S]*?\S)\*(?!\*)/, make: (m) => [h("em", null, ...inline(m[1])), m[0].length] },
	{ re: /^_(?=\S)([\s\S]*?\S)_(?!_)(?![A-Za-z0-9])/, make: (m) => [h("em", null, ...inline(m[1])), m[0].length] },
	{ re: /^~~(?=\S)([\s\S]*?\S)~~/, make: (m) => [h("del", null, ...inline(m[1])), m[0].length] },
	{ re: /^(?: {2,}|\\)\n/, make: (m) => [h("br"), m[0].length] },
	{ re: /^\\([!"#$%&'()*+,\-./:;<=>?@[\\\]^_`{|}~])/, make: (m) => [document.createTextNode(m[1]), m[0].length] },
];

// A link's or an image's address when it is web, mail or relative; null for any other scheme (javascript:, data:...).
export function safeUrl(url) {
	const scheme = /^([a-z][a-z0-9+.-]*):/i.exec(url);
	return !scheme || ["http", "https", "mailto"].includes(scheme[1].toLowerCase()) ? url : null;
}

function codeSpan(m, text) {
	const ticks = m[0];
	const close = text.indexOf(ticks, ticks.length);
	if (close < 0) return [document.createTextNode(ticks), ticks.length];
	let code = text.slice(ticks.length, close).replace(/\n/g, " ");
	if (/^ .* $/.test(code)) code = code.slice(1, -1);
	return [h("code", null, code), close + ticks.length];
}

export function inline(text) {
	const out = [];
	let plain = "";
	let at = 0;
	while (at < text.length) {
		const rest = text.slice(at);
		let made = null;
		if ("`![<h*_~ \\".includes(rest[0]) && !(rest[0] === "_" && /[A-Za-z0-9]/.test(text[at - 1] || ""))) {
			for (const rule of INLINE) {
				const m = rule.re.exec(rest);
				if (m) {
					made = rule.make(m, rest);
					break;
				}
			}
		}
		if (made) {
			if (plain) out.push(document.createTextNode(plain));
			plain = "";
			out.push(made[0]);
			at += made[1];
		}
		else {
			plain += rest[0];
			at++;
		}
	}
	if (plain) out.push(document.createTextNode(plain));
	return out;
}

// The source line with its task box ticked or cleared: the change to make, or null when the line has no box.
export function toggleTask(line, checked) {
	const match = /^(\s*(?:[-*+]|\d{1,9}[.)])\s+\[)([ xX])\]/.exec(line);
	if (!match) return null;
	return { col: match[1].length, text: checked ? "x" : " " };
}
