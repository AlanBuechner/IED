// editorlinks.test.js - the links written in a document's text (F-EDT-92): which words of a line are links, where a
// path points, and what happens to a link clicked - a web address in a Browser tab, a file of the workspace in a
// document tab, and everything that names no file of the workspace left plain.

import { test, assertEqual } from "./harness.js";
import { fileLink, fileRef, linkIndex, linksOfLine, modelLinks, openLink } from "/plugins/ied.editor/ui/editorlinks.js";

const LANGUAGES = [
	{ id: "markdown", extensions: [".md", ".markdown"] },
	{ id: "c", extensions: [".c", ".h"] },
	{ id: "cpp", extensions: [".cpp", ".hpp"] },
	{ id: "html", extensions: [".html", ".htm"] },
	{ id: "javascript", extensions: [".js", ".mjs"] },
	{ id: "json", extensions: [".json"] },
	{ id: "nuke", extensions: [".toml"], fileNames: ["nuke.toml"] },
	{ id: "plaintext", extensions: [".txt"], fileNames: ["LICENSE", ".gitignore"] },
];

const index = linkIndex(LANGUAGES);

// The links of a line, as "kind value" pairs.
function found(text) {
	return linksOfLine(text, index).map((link) => link.kind + " " + link.value);
}

function places(text, value) {
	return linksOfLine(text, index).map((link) => [link.start, link.end, link.value]);
}

test("editorlinks: a word is a link only when it names a file the languages describe", () => {
	assertEqual(found("see docs/a.md and src/b.cpp here"), ["file docs/a.md", "file src/b.cpp"], "a path with a separator and a known extension");
	assertEqual(found("README.md and LICENSE and .gitignore"), ["file README.md", "file LICENSE", "file .gitignore"], "a lone name with an extension, a name a language lists, and a dotted one");
	assertEqual(found("read C4/docs/language/15-projects.md"), ["file C4/docs/language/15-projects.md"], "a dotted folder name");
	assertEqual(found("x.y is code, 1.5 is a version, app.min.js a bundle, foo.bar() a member"), [], "no separators and no known extension");
	assertEqual(found("open docs/ and ../notes/ and C:/"), [], "a folder is not a file");
	assertEqual(found("//cdn.example.com/a.md and javascript:alert(1)"), [], "a site's address and a script are not paths");
	assertEqual(found("TODO.txt a.TXT nuke.toml"), ["file TODO.txt", "file a.TXT", "file nuke.toml"], "any case, and a file name a language lists");
});

test("editorlinks: a link keeps the place its text has", () => {
	assertEqual(places("see [the guide](docs/guide.md) now"), [[16, 29, "docs/guide.md"]], "a Markdown link's target, without its brackets");
	assertEqual(places('an href="docs/a.md" attribute'), [[9, 18, "docs/a.md"]], "an attribute's value, without its quotes");
	assertEqual(places("open docs/a.md."), [[5, 14, "docs/a.md"]], "the sentence's full stop is not part of it");
	assertEqual(places("see (docs/a.md), then"), [[5, 14, "docs/a.md"]], "nor its bracket or comma");
	assertEqual(places("nowhere.md#section and x.md?query"), [[0, 10, "nowhere.md"], [23, 27, "x.md"]], "an anchor or a query is left out");
	assertEqual(places("a https://example.com/x and b.md"), [[2, 23, "https://example.com/x"], [28, 32, "b.md"]], "an address and a path in one line");
});

test("editorlinks: a web address is a link of its own kind", () => {
	assertEqual(found("read https://example.com/a?b=1 and http://x.y/z"), ["url https://example.com/a?b=1", "url http://x.y/z"]);
	assertEqual(found("mail x@y.z and mailto:x@y.z"), ["url mailto:x@y.z"], "a bare address and a scheme no page opens");
	assertEqual(found("data:text/html,<p>x</p> and javascript:go(1)"), [], "an address that would run script");
});

test("editorlinks: a path names the file beside its document", () => {
	assertEqual(fileRef("docs/here.md", "C:/Work/proj", "a.md"), "docs/a.md", "beside it");
	assertEqual(fileRef("docs/here.md", "C:/Work/proj", "../notes/b.md"), "notes/b.md", "up and in");
	assertEqual(fileRef("docs/here.md", "C:/Work/proj", "./c.md"), "docs/c.md");
	assertEqual(fileRef("docs/here.md", "C:/Work/proj", "a/../b.md"), "docs/b.md");
	assertEqual(fileRef("docs/here.md", "C:/Work/proj", "../../out.md"), null, "above the workspace");
	assertEqual(fileRef("README.md", "C:/Work/proj", "docs/a.md"), "docs/a.md", "from a file at the root");
});

test("editorlinks: a path from a root names a file of the workspace", () => {
	assertEqual(fileRef("docs/here.md", "C:/Work/proj", "/docs/a.md"), "docs/a.md");
	assertEqual(fileRef("docs/here.md", "C:/Work/proj", "C:/Work/proj/docs/a.md"), "docs/a.md", "written with a drive");
	assertEqual(fileRef("docs/here.md", "C:/Work/proj", "C:/Work/Proj/DOCS/a.md"), "DOCS/a.md", "its folder's case does not matter");
	assertEqual(fileRef("docs/here.md", "C:/Work/proj", "file:///C:/Work/proj/docs/a.md"), "docs/a.md", "written as a file: URI");
	assertEqual(fileRef("docs/here.md", "C:/Work/proj", "C:/Other/b.md"), null, "a file of another folder is none to open");
	assertEqual(fileRef("docs/here.md", "C:/Work/proj", "/C:/Work/proj/docs/a.md"), "docs/a.md", "one root separator before a drive");
	assertEqual(fileRef("docs/here.md", "C:/Work/proj", "C:/Work/proj"), null, "the workspace's folder is no file");
});

test("editorlinks: a file outside the workspace links nowhere", () => {
	const from = "file:///C:/Lib/include/stdio.h";
	assertEqual(fileRef(from, "C:/Work/proj", "other.h"), null, "beside it is not in the workspace");
	assertEqual(fileRef(from, "C:/Work/proj", "/usr/include/x.h"), null);
	assertEqual(fileRef(from, "C:/Work/proj", "C:/Work/proj/docs/a.md"), "docs/a.md", "a path into the workspace still reaches it");
});

test("editorlinks: the file named by a word", () => {
	assertEqual(fileLink("docs/a.md", index), true);
	assertEqual(fileLink("docs/a.zip", index), false, "no language says .zip");
	assertEqual(fileLink("a.md#heading", index), false, "the anchor is part of the word, not the path");
	assertEqual(fileLink("C:\\Work\\proj\\docs\\a.md", index), true, "separators of a drive path");
});

test("editorlinks: the links of a document, as Monaco is told them", () => {
	const monaco = {
		Range: function (line, start, endLine, end) { this.toString = () => "[" + line + "," + start + " -> " + endLine + "," + end + "]"; },
		Uri: { from: (parts) => ({ scheme: parts.scheme, authority: parts.authority, path: parts.path, toString: () => parts.scheme + "://" + parts.authority + parts.path }) },
	};
	const ied = { workspace: { wsId: "w1", root: "C:/Work/proj" } };
	// A model as the editor's view makes one: of the workspace, or of a file outside it (editorview.js).
	const model = (text, document) => {
		const lines = text.split("\n");
		const uri = document.startsWith("file:") ? { scheme: "file", path: document, toString: () => document }
			: monaco.Uri.from({ scheme: "ied", authority: "w1", path: "/" + document });
		return { uri, getLineCount: () => lines.length, getLineContent: (n) => lines[n - 1] };
	};
	const shown = (text, document) => modelLinks(model(text, document), monaco, ied, index).links.map((l) => l.range.toString() + " " + l.url);
	assertEqual(shown("see https://example.com/x and docs/a.md", "docs/here.md"),
		["[1,5 -> 1,26] https://example.com/x", "[1,31 -> 1,40] ied://w1/docs/docs/a.md"], "an address as written, a file as the URI of its document");
	assertEqual(shown("LICENSE", "x.md").length, 1, "a file name a language lists");
	assertEqual(shown("see docs/a.md", "file:///C:/Lib/stdio.h"), [], "a file outside the workspace names no document to open");
	assertEqual(shown("x.y and 1.5 and docs/", "a.md"), [], "plain words give nothing");
});

test("editorlinks: what a clicked link opens", () => {
	const calls = [];
	const opened = [];
	const ied = {
		commands: { run: (id, args) => { calls.push([id, args.url || args.path]); } },
		report: () => {},
	};
	const uri = (text, path) => ({ toString: () => text, path });
	window.open = (url) => { opened.push(url); return null; };
	assertEqual(openLink(ied, uri("https://example.com/a")), true, "a web page: a Browser tab");
	assertEqual(openLink(ied, uri("http://example.com/a")), true);
	assertEqual(openLink(ied, uri("mailto:x@y.z")), true, "a mail address: the system's own");
	assertEqual(openLink(ied, uri("file:///C:/a/b.md")), false, "a file outside the workspace is none to open");
	assertEqual(openLink(ied, uri("ied://w1/docs/a.md", "/docs/a.md")), true, "a file of the workspace: its document tab");
	assertEqual(calls, [["ied.browser.open", "https://example.com/a"], ["ied.browser.open", "http://example.com/a"],
		["ied.editor.goTo", "docs/a.md"]]);
	assertEqual(opened, ["mailto:x@y.z"], "a new window, nothing left to the page itself");
});
