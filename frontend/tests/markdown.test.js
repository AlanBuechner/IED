// markdown.test.js - the preview's Markdown (F-EDT-91/92): GitHub-flavored blocks and inlines, every block with its
// source line and each task box with its own, text that stays text, addresses that cannot run script, and ticking a box
// in the source.

import { test, assertEqual } from "./harness.js";
import { fenceLanguages, renderMarkdown, safeUrl, toggleTask } from "/ui-kit/markdown.js";
import { resolveRelative } from "/ui-kit/paths.js";
import { moves } from "/plugins/ied.markdown/ui/follow.js";
import { linkTarget } from "/ui-kit/linkclicks.js";

test("markdown: what a rendered link names, for the view that answers it", () => {
	assertEqual(linkTarget("docs/a.md#top"), { kind: "path", href: "docs/a.md#top" }, "a path as written: the view reads it from its folder");
	assertEqual(linkTarget("/README.md"), { kind: "path", href: "/README.md" });
	assertEqual(linkTarget("HTTPS://example.com/x"), { kind: "web", href: "HTTPS://example.com/x" });
	assertEqual(linkTarget("mailto:me@example.com"), { kind: "mail", href: "mailto:me@example.com" });
	assertEqual([linkTarget("#top"), linkTarget(""), linkTarget("javascript:alert(1)"), linkTarget("file:///C:/x.md")], [null, null, null, null], "nothing to open");
});

function html(text, options) {
	const host = document.createElement("div");
	host.append(...renderMarkdown(text, options));
	return host.innerHTML;
}

test("markdown: headings, paragraphs and inlines", () => {
	assertEqual(html("# Title\n\nSome *em* and **strong** with `code` and ~~gone~~."),
		'<h1 data-line="0">Title</h1><p data-line="2">Some <em>em</em> and <strong>strong</strong> with <code>code</code> and <del>gone</del>.</p>');
	assertEqual(html("A [link](docs/a.md \"tip\") and https://example.com/x."),
		'<p data-line="0">A <a href="docs/a.md" title="tip">link</a> and <a href="https://example.com/x" target="_blank" rel="noopener noreferrer">https://example.com/x</a>.</p>');
	assertEqual(html("one  \ntwo\\\nthree"), '<p data-line="0">one<br>two<br>three</p>', "hard breaks");
	assertEqual(html("---\n> quoted\n> more"), '<hr data-line="0"><blockquote data-line="1"><p data-line="0">quoted\nmore</p></blockquote>');
	assertEqual(html("stream_level runs and profile_hitches says so; _snake_case_ and __bold__."),
		'<p data-line="0">stream_level runs and profile_hitches says so; <em>snake_case</em> and <strong>bold</strong>.</p>', "an underscore inside a word is a letter");
	assertEqual(html("**foo**bar and *a*b"), '<p data-line="0"><strong>foo</strong>bar and <em>a</em>b</p>', "asterisks may close inside a word");
});

test("markdown: lists with task items keep their source lines", () => {
	const text = "Intro\n\n- [ ] first\n- [x] second\n  - [ ] nested\n- plain\n\n1. one\n2. two";
	assertEqual(html(text),
		'<p data-line="0">Intro</p>' +
		'<ul data-line="2"><li data-line="2" class="task"><input type="checkbox" data-task="2">first</li>' +
		'<li data-line="3" class="task"><input type="checkbox" data-task="3" checked="">second' +
		'<ul data-line="4"><li data-line="4" class="task"><input type="checkbox" data-task="4">nested</li></ul></li>' +
		'<li data-line="5">plain</li></ul>' +
		'<ol data-line="7"><li data-line="7">one</li><li data-line="8">two</li></ol>');
	assertEqual(html("3. three\n4. four"), '<ol data-line="0" start="3"><li data-line="0">three</li><li data-line="1">four</li></ol>');
});

test("markdown: tables, code blocks", () => {
	assertEqual(html("| a | b |\n|:--|--:|\n| 1 | `2` |"),
		'<table data-line="0"><thead><tr data-line="0"><th style="text-align: left;">a</th><th style="text-align: right;">b</th></tr></thead>' +
		'<tbody><tr data-line="2"><td style="text-align: left;">1</td><td style="text-align: right;"><code>2</code></td></tr></tbody></table>');
	assertEqual(html("```c\nint x;\n```\n\n    indented\n    code"), '<pre data-line="0"><code>int x;</code></pre><pre data-line="4"><code>indented\ncode</code></pre>');
	const colored = html("```js\nlet a;\n```", { code: (code, language) => { const span = document.createElement("span"); span.textContent = language + ":" + code; return span; } });
	assertEqual(colored, '<pre data-line="0"><span>js:let a;</span></pre>', "a colorizer's element");
	const closed = [];
	html("```c\nint a;\n```\n\n```c\nint b;", { code: (code, language, done) => { closed.push(code + ":" + done); return null; } });
	assertEqual(closed, ["int a;:true", "int b;:false"], "a fence still streaming is not closed");
});

test("markdown: text stays text, addresses cannot run script", () => {
	assertEqual(html("<script>alert(1)</script> & <b>"), '<p data-line="0">&lt;script&gt;alert(1)&lt;/script&gt; &amp; &lt;b&gt;</p>');
	assertEqual(html("[x](javascript:alert(1))"), '<p data-line="0"><a>x</a></p>');
	assertEqual([safeUrl("https://a"), safeUrl("mailto:a@b"), safeUrl("../a.md"), safeUrl("#top"), safeUrl("data:text/html,x"), safeUrl("JavaScript:x")],
		["https://a", "mailto:a@b", "../a.md", "#top", null, null]);
	assertEqual(html("\\*not em\\*"), '<p data-line="0">*not em*</p>');
});

test("markdown: an address is marked to open beside the page, a path of the workspace is not", () => {
	assertEqual(html("<https://example.com/a> and <mailto:a@b>"),
		'<p data-line="0"><a href="https://example.com/a" target="_blank" rel="noopener noreferrer">https://example.com/a</a> and <a href="mailto:a@b">mailto:a@b</a></p>');
});

test("markdown: ticking a box edits its line", () => {
	assertEqual(toggleTask("  - [ ] buy milk", true), { col: 5, text: "x" });
	assertEqual(toggleTask("1. [x] done", false), { col: 4, text: " " });
	assertEqual(toggleTask("plain line", true), null);
});

test("markdown: relative links and images resolve inside the workspace", () => {
	assertEqual(resolveRelative("docs/guide", "../images/a.png"), "docs/images/a.png");
	assertEqual(resolveRelative("docs", "./b.md"), "docs/b.md");
	assertEqual(resolveRelative("", "a/../c.md"), "c.md");
	assertEqual(resolveRelative("docs", "../../outside.md"), null, "above the workspace");
});

test("markdown: a fence names its language by the languages' fence names or ids", () => {
	const fences = fenceLanguages([{ id: "cpp", fences: ["cpp", "c++", "CC"] }, { id: "rust", fences: ["rs"] }, { id: "go" }]);
	assertEqual(["c++", "cc", "rs", "rust", "go", "zig"].map((name) => fences.get(name) || null), ["cpp", "cpp", "rust", "rust", "go", null]);
});

test("markdown: a preview beside its file follows the Markdown file its group shows", () => {
	const state = (active, open) => ({
		open: open || ["doc:a.md", "doc:b.md", "doc:c.cf", "mdpreview:a.md"],
		groups: [{ activity: "ied.editor", well: true, tabs: ["doc:a.md", "doc:b.md", "doc:c.cf"], active }, { activity: "ied.editor", well: true, tabs: ["mdpreview:a.md"], active: "mdpreview:a.md" }],
	});
	const tethered = (ref) => (ref === "mdpreview:a.md" ? ["doc:a.md"] : []);
	assertEqual(moves(state("doc:b.md"), tethered), [{ from: "mdpreview:a.md", to: "mdpreview:b.md", lead: "doc:b.md" }], "another .md shown: the preview takes it");
	assertEqual(moves(state("doc:c.cf"), tethered), [], "a file of another kind: it stays");
	assertEqual(moves(state("doc:a.md"), tethered), [], "its own file");
	assertEqual(moves(state("doc:b.md"), () => []), [], "a preview not tethered (opened in place) does not follow");
	assertEqual(moves(state("doc:b.md", ["doc:a.md", "doc:b.md", "mdpreview:a.md", "mdpreview:b.md"]), tethered), [], "not onto a preview open already");
});

test("markdown: a preview whose group's preview tab was taken by another file follows that file", () => {
	// a.md was the group's preview tab; b.md (or c.cf) opened in its place took over the tether (shell openTab).
	const state = (tabs, active) => ({
		open: [...tabs, "mdpreview:a.md"],
		groups: [{ activity: "ied.editor", well: true, tabs, active }, { activity: "ied.editor", well: true, tabs: ["mdpreview:a.md"], active: "mdpreview:a.md" }],
	});
	const tetheredTo = (lead) => (ref) => (ref === "mdpreview:a.md" ? [lead] : []);
	assertEqual(moves(state(["doc:b.md"], "doc:b.md"), tetheredTo("doc:b.md")), [{ from: "mdpreview:a.md", to: "mdpreview:b.md", lead: "doc:b.md" }], "the new file of its group");
	assertEqual(moves(state(["doc:c.cf"], "doc:c.cf"), tetheredTo("doc:c.cf")), [], "a C4 file in its place: the preview stays on a.md");
	assertEqual(moves(state(["doc:c.cf", "doc:d.md"], "doc:d.md"), tetheredTo("doc:c.cf")), [{ from: "mdpreview:a.md", to: "mdpreview:d.md", lead: "doc:d.md" }], "then a Markdown file shown there: it follows");
	assertEqual(moves(state(["doc:b.md"], "doc:b.md"), tetheredTo("chat:x")), [], "tethered with no document: no group to follow");
});
