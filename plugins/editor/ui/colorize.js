// colorize.js - the `editor.colorize` client binding (docs/02 "Client bindings as built"): lines colored as the editor
// colors them (Monaco's tokenizer for a language, or the language of a path, and the theme's colors), as HTML per line,
// for views that show code without an editor - the git plugin's diffs, the Markdown preview's fenced code.

import { loadMonaco, themeName } from "./monaco.js";

function escape(text) {
	return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

// The language of `path` among `languages` (Monaco's, or the plugins' `languages` contributions), by its whole name or
// its extension; null when none knows it.
export function languageOf(languages, path) {
	const name = path.slice(path.lastIndexOf("/") + 1).toLowerCase();
	const dot = name.lastIndexOf(".");
	const extension = dot < 0 ? null : name.slice(dot);
	for (const language of languages) {
		if ((language.filenames || language.fileNames || []).some((f) => f.toLowerCase() === name)) return language.id;
		if (extension && (language.extensions || []).some((e) => e.toLowerCase() === extension)) return language.id;
	}
	return null;
}

// {languageId | path, lines} -> [html]: one entry per line, escaped text when the language is not known.
export async function colorize(ied, { path, languageId, lines }) {
	const plain = () => lines.map(escape);
	let monaco;
	try {
		monaco = await loadMonaco(ied);
	}
	catch {
		return plain();
	}
	const known = monaco.languages.getLanguages();
	const language = languageId && known.some((l) => l.id === languageId) ? languageId : languageOf(known, path || "");
	if (!language || !lines.length) return plain();
	monaco.editor.setTheme(themeName());
	writeTokenColors(monaco);
	const html = await monaco.editor.colorize(lines.join("\n"), language, { tabSize: 4 });
	const colored = html.split("<br/>");
	return lines.map((line, i) => (colored[i] !== undefined ? colored[i] : escape(line)));
}

// Monaco writes the CSS of its token classes (style.monaco-colors) when its first editor is made, so on a page with no
// editor yet (only a chat, or a preview restored alone) the colored spans would show plain: an editor made out of sight
// and dropped at once leaves that CSS behind.
function writeTokenColors(monaco) {
	if (document.querySelector("style.monaco-colors")) return;
	const host = document.createElement("div");
	host.style.cssText = "position: fixed; left: -10000px; top: 0; width: 10px; height: 10px; visibility: hidden;";
	document.body.append(host);
	monaco.editor.create(host, { value: "", language: "plaintext", minimap: { enabled: false } }).dispose();
	host.remove();
}
