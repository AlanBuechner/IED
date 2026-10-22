// monaco.js - Monaco for the editor plugin: loaded once (the AMD build served at /vendor/monaco-editor/), its theme
// made from the theme tokens (docs/07, F-SHL-50), and the plugins' `languages` contributions registered with their
// comment tokens, brackets, Monarch grammars and the keywords their grammars name for control flow.

import { SYNTAX_KINDS } from "./semanticcolors.js";

const BASE = "/vendor/monaco-editor/min/vs";
const THEME = "ied-dark";

let loading = null;

// Resolves to the `monaco` namespace; rejects when the library is not installed (setup.bat installs it).
export function loadMonaco(ied) {
	if (!loading) loading = boot(ied);
	return loading;
}

function boot(ied) {
	return new Promise((resolve, reject) => {
		const css = document.createElement("link");
		css.rel = "stylesheet";
		css.href = BASE + "/editor/editor.main.css";
		document.head.append(css);
		const script = document.createElement("script");
		script.src = BASE + "/loader.js";
		script.onerror = () => reject(new Error("Monaco is not installed (" + BASE + "/loader.js); run setup.bat"));
		script.onload = () => {
			window.require.config({ paths: { vs: BASE } });
			window.require(["vs/editor/editor.main"], () => {
				const monaco = window.monaco;
				defineTheme(monaco, controlKeywords(ied.contributions("languages")));
				monaco.editor.setTheme(THEME);
				shareThemeWithShadowRoots();
				registerLanguages(monaco, ied).then(() => resolve(monaco), reject);
			}, reject);
		};
		document.head.append(script);
	});
}

export function themeName() {
	return THEME;
}

function token(name) {
	return getComputedStyle(document.documentElement).getPropertyValue("--" + name).trim();
}

// A token Monaco can take (#rgb, #rrggbb, #rrggbbaa); null for anything else, such as a color-mix().
function hex(name) {
	const value = token(name);
	return /^#[0-9a-fA-F]{3,8}$/.test(value) ? value : null;
}

// The keywords the languages' grammars name one by one ("keyword.return.cpp" - Monaco's C and C++) that read as control
// flow: each language's `controlKeywords`.
export function controlKeywords(contributions) {
	const words = new Set();
	for (const { value } of contributions) for (const word of value.controlKeywords || []) words.add(word);
	return [...words];
}

// Grammar (Monarch) tokens, then a rule per syntax kind for language servers' tokens (semanticcolors.js puts every
// server's legend onto those kinds, per language).
const TOKEN_RULES = [
	["", "syntax-plain"], ["comment", "syntax-comment"], ["string", "syntax-string"], ["string.escape", "syntax-constant"],
	["regexp", "syntax-string"], ["keyword", "syntax-keyword"], ["keyword.flow", "syntax-control"], ["number", "syntax-number"],
	["type", "syntax-type"], ["type.identifier", "syntax-type"], ["identifier", "syntax-plain"], ["delimiter", "syntax-operator"],
	["operator", "syntax-operator"], ["keyword.directive", "syntax-preprocessor"], ["keyword.directive.include.begin", "syntax-string"],
	["keyword.directive.include.end", "syntax-string"], ["annotation", "syntax-macro"], ["constant", "syntax-constant"],
	["predefined", "syntax-function"], ["namespace", "syntax-namespace"], ["invalid", "syntax-invalid"], ["tag", "syntax-keyword"],
	["attribute.name", "syntax-variable"], ["attribute.value", "syntax-string"], ["variable", "syntax-variable"], ["metatag", "syntax-macro"],
	...SYNTAX_KINDS.map((kind) => ["syntax-" + kind, "syntax-" + kind]),
];

const COLORS = {
	"editor.background": "editor-bg", "editor.foreground": "syntax-plain", "editorGutter.background": "gutter-bg",
	"editorLineNumber.foreground": "line-number", "editorLineNumber.activeForeground": "line-number-active",
	"editor.lineHighlightBackground": "current-line", "editor.lineHighlightBorder": "current-line-border",
	"editor.selectionBackground": "selection", "editor.inactiveSelectionBackground": "bg-4", "editorCursor.foreground": "text-0",
	"editorIndentGuide.background1": "indent-guide", "editorIndentGuide.activeBackground1": "indent-guide-active",
	"editor.findMatchBackground": "find-match", "editor.findMatchBorder": "find-match-border", "editor.wordHighlightBackground": "ref-highlight",
	"editorBracketMatch.background": "ref-highlight", "editorBracketMatch.border": "border-1",
	"editorError.foreground": "squiggle-error", "editorWarning.foreground": "squiggle-warn", "editorInfo.foreground": "squiggle-info",
	"editorWidget.background": "bg-1", "editorWidget.border": "border-1", "editorSuggestWidget.background": "bg-1",
	"editorSuggestWidget.border": "border-1", "editorSuggestWidget.selectedBackground": "bg-4", "editorHoverWidget.background": "bg-1",
	"editorHoverWidget.border": "border-1", "input.background": "bg-3", "input.border": "border-1", "focusBorder": "focus",
	"scrollbarSlider.background": "scroll-thumb", "scrollbarSlider.hoverBackground": "scroll-thumb-hover", "minimap.background": "editor-bg",
	"editorWhitespace.foreground": "text-3",
	"menu.background": "bg-1", "menu.foreground": "text-0", "menu.selectionBackground": "bg-4", "menu.selectionForeground": "text-0",
	"menu.separatorBackground": "border-0",
};

function defineTheme(monaco, control) {
	const rules = [];
	for (const [scope, name] of [...TOKEN_RULES, ...control.map((word) => ["keyword." + word, "syntax-control"])]) {
		const color = hex(name);
		if (color) rules.push({ token: scope, foreground: color.slice(1) });
	}
	const colors = {};
	for (const [key, name] of Object.entries(COLORS)) {
		const color = hex(name);
		if (color) colors[key] = color;
	}
	monaco.editor.defineTheme(THEME, { base: "vs-dark", inherit: true, rules, colors });
}

// Monaco draws its context menu in a shadow root on the page, where the colors its theme sets on its own elements
// (.monaco-component) do not reach: the same declarations go on the shadow roots' hosts, again whenever the theme changes.
// Monaco makes its color styles when an editor first shows, so they are waited for.
function shareThemeWithShadowRoots() {
	const shared = document.createElement("style");
	document.head.append(shared);
	let colors = null;
	const copy = () => {
		const found = /\.monaco-component\s*\{([^}]*)\}/.exec(colors.textContent);
		shared.textContent = found ? ".shadow-root-host {" + found[1] + "}" : "";
	};
	const follow = () => {
		const made = document.querySelector("style.monaco-colors");
		if (!made || made === colors) return;
		colors = made;
		copy();
		new MutationObserver(copy).observe(colors, { childList: true, characterData: true, subtree: true });
	};
	follow();
	new MutationObserver(follow).observe(document.head, { childList: true });
}

// Each `languages` contribution: registered when Monaco lacks the id, its configuration set, its grammar loaded.
async function registerLanguages(monaco, ied) {
	const known = new Set(monaco.languages.getLanguages().map((l) => l.id));
	const work = ied.contributions("languages").map(async ({ plugin, value }) => {
		if (!known.has(value.id)) monaco.languages.register({ id: value.id, extensions: value.extensions, filenames: value.fileNames, aliases: value.aliases });
		const configuration = languageConfiguration(value);
		if (configuration) monaco.languages.setLanguageConfiguration(value.id, configuration);
		if (value.grammar) {
			try {
				const response = await fetch(new URL(value.grammar, location.origin + plugin.base).href);
				monaco.languages.setMonarchTokensProvider(value.id, await response.json());
			}
			catch (e) {
				ied.report("the grammar of language '" + value.id + "' (" + plugin.id + ") did not load: " + e.message, e);
			}
		}
	});
	await Promise.all(work);
}

function languageConfiguration(language) {
	const out = {};
	if (language.lineComment || language.blockComment) out.comments = { lineComment: language.lineComment, blockComment: language.blockComment };
	if (language.brackets) out.brackets = language.brackets;
	if (language.autoClosingPairs) out.autoClosingPairs = language.autoClosingPairs.map(([open, close]) => ({ open, close }));
	if (language.wordPattern) out.wordPattern = new RegExp(language.wordPattern);
	if (language.indentation) {
		out.indentationRules = {};
		if (language.indentation.increasePattern) out.indentationRules.increaseIndentPattern = new RegExp(language.indentation.increasePattern);
		if (language.indentation.decreasePattern) out.indentationRules.decreaseIndentPattern = new RegExp(language.indentation.decreasePattern);
	}
	return Object.keys(out).length ? out : null;
}
