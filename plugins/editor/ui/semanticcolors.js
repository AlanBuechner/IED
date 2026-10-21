// semanticcolors.js - a language server's semantic tokens in the theme's syntax kinds (docs/02 "semanticTokenColors"):
// each token's type and modifiers are matched against the rules for its language - the standard LSP names below, then
// what plugins' `semanticTokenColors` contributions say - and re-encoded with one token type per `--syntax-*` kind, so
// one theme colors every language and a server's own names (C4LSP's `controlFlow`, clangd's `deduced`) mean what the
// plugin that knows the server says. A token no rule names is left out: the grammar's color shows through.

export const SYNTAX_KINDS = ["keyword", "control", "type", "function", "variable", "parameter", "member", "constant", "namespace",
	"macro", "preprocessor", "string", "number", "comment", "operator", "plain", "invalid"];

// What Monaco is told the remapped tokens are; the theme has a rule per name (monaco.js).
export const LEGEND = { tokenTypes: SYNTAX_KINDS.map((kind) => "syntax-" + kind), tokenModifiers: [] };

// LSP's standard token types.
export const DEFAULT_RULES = {
	namespace: "--syntax-namespace", type: "--syntax-type", class: "--syntax-type", enum: "--syntax-type", interface: "--syntax-type",
	struct: "--syntax-type", typeParameter: "--syntax-type", parameter: "--syntax-parameter", variable: "--syntax-variable",
	property: "--syntax-member", enumMember: "--syntax-constant", event: "--syntax-member", function: "--syntax-function",
	method: "--syntax-function", macro: "--syntax-macro", keyword: "--syntax-keyword", modifier: "--syntax-keyword",
	comment: "--syntax-comment", string: "--syntax-string", number: "--syntax-number", regexp: "--syntax-string",
	operator: "--syntax-operator", decorator: "--syntax-macro",
};

// The rules for `languageId`: the defaults, then each contribution for it in load order.
export function rulesFor(contributions, languageId) {
	const rules = Object.assign({}, DEFAULT_RULES);
	for (const { value } of contributions) if (value.languageId === languageId) Object.assign(rules, value.rules);
	return rules;
}

// The syntax kind for a token of `type` with `modifiers`: the rule naming that type and the most of its modifiers
// ("class.deduced" over "class"; modifiers in any order); null when no rule names the type.
export function kindFor(rules, type, modifiers) {
	let best = null;
	let bestCount = -1;
	for (const [selector, token] of Object.entries(rules)) {
		const [ruleType, ...needs] = selector.split(".");
		if (ruleType !== type || needs.length <= bestCount || !needs.every((m) => modifiers.includes(m))) continue;
		best = token.replace(/^--syntax-/, "");
		bestCount = needs.length;
	}
	return best;
}

export class SemanticMapper {
	// legend: the server's {tokenTypes, tokenModifiers}; rules: rulesFor(...).
	constructor(legend, rules) {
		this.legend = legend;
		this.rules = rules;
		this.cache = new Map();   // "type,modifierBits" -> kind index or -1
	}

	kindIndex(type, bits) {
		const key = type + "," + bits;
		if (!this.cache.has(key)) {
			const modifiers = this.legend.tokenModifiers.filter((m, i) => bits & (1 << i));
			const kind = kindFor(this.rules, this.legend.tokenTypes[type], modifiers);
			this.cache.set(key, kind === null ? -1 : SYNTAX_KINDS.indexOf(kind));
		}
		return this.cache.get(key);
	}

	// LSP's relative encoding (5 numbers a token) in, the same with the theme's kinds out; tokens with no kind dropped.
	remap(data) {
		const out = [];
		let line = 0;
		let character = 0;
		let lastLine = 0;
		let lastCharacter = 0;
		for (let i = 0; i + 4 < data.length; i += 5) {
			line += data[i];
			character = data[i] ? data[i + 1] : character + data[i + 1];
			const kind = this.kindIndex(data[i + 3], data[i + 4]);
			if (kind < 0) continue;
			out.push(line - lastLine, line === lastLine ? character - lastCharacter : character, data[i + 2], kind, 0);
			lastLine = line;
			lastCharacter = character;
		}
		return Uint32Array.from(out);
	}
}
