// grammars.test.js - built-in plugins' Monarch grammars, tokenized by a real Monaco: what each piece of a line reads as.

import { test, assertEqual } from "./harness.js";
import { loadMonaco } from "/plugins/ied.editor/ui/monaco.js";

function fakeIed() {
	return { contributions: Object.assign(() => [], { on: () => () => {} }), report: () => {}, workspace: { wsId: "", root: "" }, commands: { run: () => {} } };
}

// A plugin's grammar under a language of the test's own, so the app's registration of the plugin is not touched.
async function grammarLanguage(plugin, grammar) {
	const monaco = await loadMonaco(fakeIed());
	const language = "ied-grammars-test-" + plugin;
	if (!monaco.languages.getLanguages().some((l) => l.id === language)) {
		monaco.languages.register({ id: language });
		const response = await fetch("/plugins/" + plugin + "/" + grammar);
		monaco.languages.setMonarchTokensProvider(language, await response.json());
	}
	return (line) => {
		const tokens = monaco.editor.tokenize(line, language)[0];
		return tokens.map((token, i) => [line.slice(token.offset, i + 1 < tokens.length ? tokens[i + 1].offset : line.length), token.type]);
	};
}

function typeOf(pieces, text) {
	const piece = pieces.find(([t]) => t === text);
	return piece ? piece[1] : "(no piece '" + text + "' in " + JSON.stringify(pieces) + ")";
}

test("grammars: a C4 annotation's opening [ is a bracket like its ], only the name reads as an annotation", async () => {
	const tokenize = await grammarLanguage("c4", "grammars/c4.monarch.json");
	const operator = tokenize("[operator(\"create\")]");
	assertEqual(typeOf(operator, "["), typeOf(operator, "]"), "the [ of [operator(...)] reads as the ]: " + JSON.stringify(operator));
	assertEqual(typeOf(operator, "[").startsWith("delimiter"), true, "a bracket: " + typeOf(operator, "["));
	assertEqual(typeOf(operator, "operator"), "annotation.c4");
	const spaced = tokenize("\t[ test(\"math.vector\") ]");
	assertEqual([typeOf(spaced, "["), typeOf(spaced, "test")], [typeOf(spaced, "]"), "annotation.c4"], "indented, with a space inside: " + JSON.stringify(spaced));
	assertEqual(spaced[0], ["\t", ""], "the indent is not part of the annotation");
	const lambda = tokenize("[](int i) -> int { return i; }");
	assertEqual(lambda[0], ["[]", typeOf(operator, "]")], "a line opening on a lambda's []: brackets, no annotation");
});
