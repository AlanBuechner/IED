// colorize.js - the `editor.colorize` client binding (docs/02 "Client bindings as built"): lines of a file colored as
// the editor colors them (Monaco's tokenizer for the path's language, the theme's colors), as HTML per line, for views
// that show code without an editor - the git plugin's diffs.

import { loadMonaco, themeName } from "./monaco.js";

function escape(text) {
	return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

// The Monaco language of `path`, by its extension or its whole name; null when none knows it.
export function languageOf(languages, path) {
	const name = path.slice(path.lastIndexOf("/") + 1).toLowerCase();
	const dot = name.lastIndexOf(".");
	const extension = dot < 0 ? null : name.slice(dot);
	for (const language of languages) {
		if ((language.filenames || []).some((f) => f.toLowerCase() === name)) return language.id;
		if (extension && (language.extensions || []).some((e) => e.toLowerCase() === extension)) return language.id;
	}
	return null;
}

// {path, lines} -> [html]: one entry per line, escaped text when the language is not known.
export async function colorize(ied, { path, lines }) {
	const plain = () => lines.map(escape);
	let monaco;
	try {
		monaco = await loadMonaco(ied);
	}
	catch {
		return plain();
	}
	const language = languageOf(monaco.languages.getLanguages(), path || "");
	if (!language || !lines.length) return plain();
	monaco.editor.setTheme(themeName());
	const html = await monaco.editor.colorize(lines.join("\n"), language, { tabSize: 4 });
	const colored = html.split("<br/>");
	return lines.map((line, i) => (colored[i] !== undefined ? colored[i] : escape(line)));
}
