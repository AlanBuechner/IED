// editorlinks.js - the links written in a document's text (F-EDT-55, owner 2026-11-08: a middle click on one opens the
// file or the page in a tab of its own, as a web browser's middle click opens a new tab). Monaco's own link feature
// shows them - its underline, its hover, the Ctrl+Click that opens one - and does the clicking, with the middle click
// turned on (editorview.js). It asks this plugin two things: which words of a document are links, of every language (a
// link provider), and where a link goes (a link opener). A file of the workspace becomes a document tab and a web
// address a Browser tab, the latter through the browser plugin's ied.browser.open so nothing here knows that plugin
// exists. A file outside the workspace stays plain text, for the server opens only the outside files a language server
// named (docs/10 "Files outside the workspace") and a link to one could not.
//
// A relative path is read from the document's own folder, as the Markdown preview reads its links: "a.md" beside it,
// "../docs/b.md" up and in. In a document of the workspace a path that starts with "/" is one from the workspace's root,
// and so is a file of the machine ("C:\Work\proj\docs\a.md", "file:///C:/Work/proj/docs/a.md") that lies under the
// workspace's folder.
//
// A word is a link only when the extension at its end is one the plugins' `languages` contributions say their files
// have, or it is one of their file names; a word with no separator in it must be just a name and an extension, so `x.y`,
// `app.min.js` and `1.5` in code stay plain text, as does a folder's path and a site's address. A "#anchor" or "?query"
// is dropped: nothing here can jump to a heading.

import { canonicalFileUri, displayPath, isFileUri } from "/ui-kit/fileuris.js";
import { resolveRelative } from "/ui-kit/paths.js";

// A web address.
const URL_ = /(?:https?|mailto):[^\s"'`<>()]+/gi;
// A word of the text. The marks left out are the ones a link sits inside, so `[text](docs/a.md)` keeps its target and a
// member chain in code (`foo.bar`) stays one word.
const WORD = /[^\s"'`<>()[\]{}|,;]+/g;
const TRAIL = /[.,;:!?'"]+$/;             // what a sentence or an attribute adds at a link's end
const LEAD = /^[^\w~./]+/;                // what a link is introduced with: a quote, an angle mark, an "href="
const SCHEME = /^([A-Za-z][A-Za-z0-9+.-]*):/;
const DRIVE = /^[A-Za-z]:[\\/]/;
const SEGMENT = /^[\w+@~ .-]*$/;                  // one part of a path a file system can hold
const LONE = /^\.?[\w@+-]+\.[A-Za-z0-9+_-]+$/;    // a path of one part: a name and an extension, and nothing else

// A document's links are worked out to this many lines; a longer one's stay plain (the provider runs again per edit).
const MAX_LINES = 5000;

// The file extensions and whole file names the plugins' `languages` contributions say, lower-cased: what a path's last
// part is matched against.
export function linkIndex(languages) {
	const extensions = new Set();
	const fileNames = new Set();
	for (const language of languages) {
		for (const ext of language.extensions || [])
			if (ext.startsWith(".")) extensions.add(ext.slice(1).toLowerCase());
		for (const name of language.fileNames || []) fileNames.add(name.toLowerCase());
	}
	return { extensions, fileNames };
}

// The links in one line of text, left to right and none inside another: [{start, end, kind: "url" | "file", value}] with
// 0-based columns and each value as the text writes it.
export function linksOfLine(text, index) {
	const found = [];
	const add = (start, end, kind, value) => {
		if (end > start && !found.some((other) => start < other.end && end > other.start)) found.push({ start, end, kind, value });
	};
	for (const match of text.matchAll(URL_)) {
		const value = match[0].replace(TRAIL, "");
		add(match.index, match.index + value.length, "url", value);
	}
	for (const match of text.matchAll(WORD))
		for (const at of startsOf(match[0])) {
			const value = match[0].slice(at).split(/[?#]/)[0].replace(TRAIL, "");
			if (value.length >= 3 && fileLink(value, index)) {
				add(match.index + at, match.index + at + value.length, "file", value);
				break;
			}
		}
	return found.sort((a, b) => a.start - b.start);
}

// Where a path can start inside a word: after what introduces it, and after an attribute's name and its equals.
function startsOf(word) {
	const body = word.split(/[?#]/)[0];
	const lead = (LEAD.exec(body) || [""])[0].length;
	const equals = body.lastIndexOf("=") + 1;
	return equals > lead ? [equals, lead] : [lead];
}

// The text as a path a file system can hold, its last part naming a file the plugins describe.
export function fileLink(text, index) {
	if (text.length < 3 || text.startsWith("//") || text.startsWith("\\\\")) return false;
	if (isFileUri(text)) return knownFile(lastOf(text), index);
	const scheme = SCHEME.exec(text);
	if (scheme && !DRIVE.test(text)) return false;                       // another scheme: the URL_'s, or not one to open
	const parts = (DRIVE.test(text) ? text.slice(2) : text).split(/[\\/]/);
	const last = parts[parts.length - 1];
	if (!last || !parts.every((part) => SEGMENT.test(part)) || !knownFile(last, index)) return false;
	if (parts.length === 1) return LONE.test(last) || index.fileNames.has(last.toLowerCase());
	return namedPart(parts[0], index);                                  // a host's name ("www.example.com") is not a folder
}

// A path's first part: a name on its own, or one whose extension the languages say ("src", "C4", "notes.md"), but not
// "www.example.com", whose first part reads as a site rather than a folder.
function namedPart(part, index) {
	if (!part.includes(".")) return true;
	return knownFile(part, index) || /^\.\.?$/.test(part) || /^[A-Za-z][\w-]*$/.test(part);
}

// A file's own name, as the languages describe their files: one of their names, or one whose extension they say.
function knownFile(name, index) {
	if (index.fileNames.has(name.toLowerCase())) return true;
	const dot = name.lastIndexOf(".");
	return dot > 0 && dot < name.length - 1 && index.extensions.has(name.slice(dot + 1).toLowerCase());
}

function lastOf(path) {
	return slashes(path).split("/").pop();
}

function slashes(path) {
	return path.replace(/\\/g, "/");
}

// ---------------------------------------------------------------------------------------------------------------
// Where a link goes

// The file the path names, seen from the document at `from` - its workspace path, or the canonical `file:` URI of a file
// outside the workspace - with `root` the workspace's own folder: its path in the workspace, or null for a file no tab
// can show. A path of a workspace document that starts with "/" is one from the workspace's root; in a file outside it,
// from the file system's. A file outside the workspace is null, for the server opens only the outside files a language
// server named (docs/10 "Files outside the workspace") and a link to one could not: an absolute path counts as one
// unless it lies under `root`.
export function fileRef(from, root, value) {
	const path = slashes(value.split(/[?#]/)[0]);
	if (isFileUri(value)) {
		const uri = canonicalFileUri(value);
		return uri === null ? null : under(displayPath(uri), root);
	}
	if (DRIVE.test(path)) return under(path, root);
	if (path.startsWith("/")) return DRIVE.test(path.slice(1)) ? under(path.slice(1), root) : from.startsWith("file:") ? under(path, root) : resolveRelative("", path.slice(1));
	if (from.startsWith("file:")) return null;                       // beside a file outside the workspace: not one to open
	return resolveRelative(dirOf(slashes(from)), path);
}

// A machine path as a file of the workspace: its workspace path when it lies under the workspace's folder, null
// otherwise. The workspace's own folder is no file.
function under(path, root) {
	const here = slashes(path);
	const base = slashes(root);
	return base !== "" && here.toLowerCase().startsWith(base.toLowerCase() + "/") ? here.slice(base.length + 1) : null;
}

function dirOf(path) {
	const last = path.lastIndexOf("/");
	return last < 0 ? "" : path.slice(0, last);
}

// ---------------------------------------------------------------------------------------------------------------
// Monaco

// Registers the link provider and the link opener once, on the Monaco every editor shares.
export function registerLinks(monaco, ied) {
	const index = () => linkIndex(ied.contributions("languages").map((c) => c.value));
	monaco.languages.registerLinkProvider("*", { provideLinks: (model) => modelLinks(model, monaco, ied, index()) });
	monaco.editor.registerLinkOpener({ open: (uri) => openLink(ied, uri) });
}

// A document's links, as Monaco wants them: each with its range and where it goes - an address as written, a file as the
// URI of its document, which the opener below turns into a tab.
export function modelLinks(model, monaco, ied, index) {
	if (model.getLineCount() > MAX_LINES) return { links: [] };
	const wsId = ied.workspace.wsId || "";
	const from = documentPath(model.uri);
	const root = ied.workspace.root || "";
	const links = [];
	for (let line = 1; line <= model.getLineCount(); line++) {
		for (const link of linksOfLine(model.getLineContent(line), index)) {
			const target = link.kind === "url" ? link.value : from === null ? null : fileRef(from, root, link.value);
			if (!target) continue;
			const range = new monaco.Range(line, link.start + 1, line, link.end + 1);
			links.push(link.kind === "url" ? { range, url: target, tooltip: target }
				: { range, url: monaco.Uri.from({ scheme: "ied", authority: wsId, path: "/" + target }), tooltip: target });
		}
	}
	return { links };
}

// The document a model shows, as fileRef reads it: its workspace path, or the canonical `file:` URI of a file outside
// the workspace; null for a model this plugin names no document with.
function documentPath(uri) {
	if (uri.scheme === "ied") return uri.path.replace(/^\//, "");
	return uri.scheme === "file" ? canonicalFileUri(uri.toString()) : null;
}

// What a link opens: a web address in a Browser tab, a mail address in the system's own, and a file of the workspace in
// a document tab - the editor's own `ied.editor.goTo`. Anything else is left to Monaco, which opens nothing of a scheme
// it does not know. (Monaco asks no opener to put a link beside another: on a middle click it passes no options at all.)
export function openLink(ied, uri) {
	const url = uri.toString();
	const scheme = SCHEME.exec(url);
	const kind = scheme ? scheme[1].toLowerCase() : "";
	if (kind === "ied") {
		const path = uri.path.replace(/^\//, "");
		ied.commands.run("ied.editor.goTo", { path });
		return true;
	}
	if (kind === "mailto") window.open(url, "_blank", "noopener,noreferrer");
	else if (kind === "http" || kind === "https") ied.commands.run("ied.browser.open", { url });
	else return false;
	return true;
}
