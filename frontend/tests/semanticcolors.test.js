// semanticcolors.test.js - language servers' semantic tokens in the theme's syntax kinds (docs/02 "semanticTokenColors"):
// the rule with the most matching modifiers wins, a language's contributions override the standard names, tokens no
// rule names are left to the grammar, and the relative encoding survives dropping some.

import { test, assertEqual } from "./harness.js";
import { DEFAULT_RULES, LEGEND, SYNTAX_KINDS, SemanticMapper, kindFor, rulesFor } from "/plugins/ied.editor/ui/semanticcolors.js";

const CLANGD = {
	tokenTypes: ["variable", "class", "macro", "keyword", "operator", "comment"],
	tokenModifiers: ["declaration", "deduced", "readonly", "globalScope"],
};

test("semantic colors: rules by type and modifiers", () => {
	const rules = rulesFor([
		{ value: { languageId: "cpp", rules: { "class.deduced": "--syntax-keyword", comment: "--syntax-preprocessor" } } },
		{ value: { languageId: "c4", rules: { controlFlow: "--syntax-control" } } },
	], "cpp");
	assertEqual(kindFor(rules, "class", ["globalScope"]), "type", "the standard name");
	assertEqual(kindFor(rules, "class", ["globalScope", "deduced"]), "keyword", "auto: the rule naming the modifier wins, in any order");
	assertEqual(kindFor(rules, "comment", []), "preprocessor", "the language's rule over the default");
	assertEqual(kindFor(rules, "controlFlow", []), null, "another language's rule does not apply");
	assertEqual(kindFor(rules, "bracket", []), null);
	assertEqual(LEGEND.tokenTypes.length, SYNTAX_KINDS.length);
	assertEqual(Object.values(DEFAULT_RULES).every((token) => SYNTAX_KINDS.includes(token.replace("--syntax-", ""))), true, "every default names a kind");
});

test("semantic colors: remapped tokens keep their places", () => {
	const mapper = new SemanticMapper(CLANGD, rulesFor([{ value: { languageId: "cpp", rules: { "class.deduced": "--syntax-keyword" } } }], "cpp"));
	// line 0: `auto` (class, deduced) at 2 and `+` (operator) at 9; line 2: `x` (variable) at 4, a token of a type the
	// legend does not have at 6 (dropped), and MAX (macro) at 10
	const data = [0, 2, 4, 1, 0b10, 0, 7, 1, 4, 0, 2, 4, 1, 0, 0, 0, 2, 1, 9, 0, 0, 4, 3, 2, 0];
	const out = Array.from(mapper.remap(data));
	const index = (kind) => SYNTAX_KINDS.indexOf(kind);
	assertEqual(out, [
		0, 2, 4, index("keyword"), 0,
		0, 7, 1, index("operator"), 0,
		2, 4, 1, index("variable"), 0,
		0, 6, 3, index("macro"), 0,
	]);
});
