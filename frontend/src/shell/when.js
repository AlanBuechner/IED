// when.js - when-clauses (docs/15 "Commands and keys"): `!`, `&&`, `||`, parentheses, `==`, `!=`, `<`, `<=`, `>`, `>=`,
// `=~ /re/flags`, quoted strings, numbers, true/false; a bare key is true when its value is truthy.
//     const test = compileWhen("workspaceTrusted && resourceExtname == '.cf'");
//     test((key) => context.get(key));

export class WhenError extends Error {}

const cache = new Map();

// A function of a key lookup that answers the clause; throws WhenError for one that does not parse. An empty clause
// always holds.
export function compileWhen(text) {
	const source = (text || "").trim();
	let compiled = cache.get(source);
	if (!compiled) {
		compiled = source === "" ? () => true : toFunction(new Parser(tokenize(source), source).parse());
		cache.set(source, compiled);
	}
	return compiled;
}

// Whether `text` holds under `lookup`; false for a clause that does not parse.
export function evaluateWhen(text, lookup) {
	try {
		return compileWhen(text)(lookup);
	}
	catch (e) {
		if (e instanceof WhenError) return false;
		throw e;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Tokens

const OPERATORS = ["&&", "||", "==", "!=", "<=", ">=", "=~", "<", ">", "!", "(", ")"];

function tokenize(text) {
	const tokens = [];
	let at = 0;
	while (at < text.length) {
		const c = text[at];
		if (/\s/.test(c)) {
			at += 1;
			continue;
		}
		const op = OPERATORS.find((candidate) => text.startsWith(candidate, at));
		if (op) {
			tokens.push({ kind: "op", value: op, at });
			at += op.length;
			if (op === "=~") at = regexToken(text, at, tokens);
		}
		else if (c === "'" || c === '"') at = stringToken(text, at, tokens);
		else if (/[0-9]/.test(c) || (c === "-" && /[0-9]/.test(text[at + 1] || ""))) at = numberToken(text, at, tokens);
		else if (/[A-Za-z_$]/.test(c)) at = wordToken(text, at, tokens);
		else throw new WhenError("unexpected '" + c + "' at " + at + " in '" + text + "'");
	}
	return tokens;
}

function stringToken(text, start, tokens) {
	const quote = text[start];
	let value = "";
	let at = start + 1;
	while (at < text.length && text[at] !== quote) {
		if (text[at] === "\\" && at + 1 < text.length) at += 1;
		value += text[at];
		at += 1;
	}
	if (at >= text.length) throw new WhenError("unterminated string at " + start + " in '" + text + "'");
	tokens.push({ kind: "value", value, at: start });
	return at + 1;
}

function numberToken(text, start, tokens) {
	const match = /^-?[0-9]+(\.[0-9]+)?/.exec(text.slice(start));
	tokens.push({ kind: "value", value: Number(match[0]), at: start });
	return start + match[0].length;
}

function wordToken(text, start, tokens) {
	const match = /^[A-Za-z_$][A-Za-z0-9_$.\-]*/.exec(text.slice(start));
	const word = match[0];
	if (word === "true" || word === "false") tokens.push({ kind: "value", value: word === "true", at: start });
	else tokens.push({ kind: "key", value: word, at: start });
	return start + word.length;
}

function regexToken(text, start, tokens) {
	let at = start;
	while (at < text.length && /\s/.test(text[at])) at += 1;
	if (text[at] !== "/") throw new WhenError("=~ needs a /regex/ at " + at + " in '" + text + "'");
	let end = at + 1;
	while (end < text.length && text[end] !== "/") end += text[end] === "\\" ? 2 : 1;
	if (end >= text.length) throw new WhenError("unterminated regex at " + at + " in '" + text + "'");
	const flags = /^[a-z]*/.exec(text.slice(end + 1))[0];
	try {
		tokens.push({ kind: "value", value: new RegExp(text.slice(at + 1, end), flags), at });
	}
	catch (e) {
		throw new WhenError("bad regex at " + at + " in '" + text + "': " + e.message);
	}
	return end + 1 + flags.length;
}

// ---------------------------------------------------------------------------------------------------------------
// Grammar: or := and ('||' and)* ; and := unary ('&&' unary)* ; unary := '!' unary | primary ;
// primary := '(' or ')' | operand (compare operand)?

const COMPARISONS = ["==", "!=", "<", "<=", ">", ">=", "=~"];

class Parser {
	constructor(tokens, text) {
		this.tokens = tokens;
		this.text = text;
		this.at = 0;
	}

	parse() {
		const node = this.or();
		if (this.at < this.tokens.length) this.fail("unexpected '" + this.tokens[this.at].value + "'");
		return node;
	}

	peek(value) {
		const token = this.tokens[this.at];
		return token && token.kind === "op" && token.value === value;
	}

	or() {
		let node = this.and();
		while (this.peek("||")) {
			this.at += 1;
			node = { kind: "or", left: node, right: this.and() };
		}
		return node;
	}

	and() {
		let node = this.unary();
		while (this.peek("&&")) {
			this.at += 1;
			node = { kind: "and", left: node, right: this.unary() };
		}
		return node;
	}

	unary() {
		if (this.peek("!")) {
			this.at += 1;
			return { kind: "not", operand: this.unary() };
		}
		return this.primary();
	}

	primary() {
		if (this.peek("(")) {
			this.at += 1;
			const node = this.or();
			if (!this.peek(")")) this.fail("missing ')'");
			this.at += 1;
			return node;
		}
		const left = this.operand();
		const token = this.tokens[this.at];
		if (token && token.kind === "op" && COMPARISONS.includes(token.value)) {
			this.at += 1;
			return { kind: "compare", op: token.value, left, right: this.operand() };
		}
		return left.kind === "key" ? { kind: "truthy", operand: left } : left;
	}

	operand() {
		const token = this.tokens[this.at];
		if (!token || token.kind === "op") this.fail(token ? "unexpected '" + token.value + "'" : "unexpected end");
		this.at += 1;
		return token.kind === "key" ? { kind: "key", name: token.value } : { kind: "value", value: token.value };
	}

	fail(message) {
		throw new WhenError(message + " in '" + this.text + "'");
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Evaluation

function toFunction(node) {
	return (lookup) => evaluate(node, lookup);
}

function valueOf(node, lookup) {
	if (node.kind === "key") return lookup(node.name);
	if (node.kind === "value") return node.value;
	return evaluate(node, lookup);
}

function truthy(value) {
	if (Array.isArray(value)) return value.length > 0;
	return Boolean(value);
}

function equal(a, b) {
	if (a === b) return true;
	if (a === undefined || a === null || b === undefined || b === null) return false;
	return String(a) === String(b);
}

const COMPARE = {
	"==": equal,
	"!=": (a, b) => !equal(a, b),
	"<": (a, b) => Number(a) < Number(b),
	"<=": (a, b) => Number(a) <= Number(b),
	">": (a, b) => Number(a) > Number(b),
	">=": (a, b) => Number(a) >= Number(b),
	"=~": (a, b) => b instanceof RegExp && b.test(a === undefined || a === null ? "" : String(a)),
};

function evaluate(node, lookup) {
	switch (node.kind) {
		case "or": return evaluate(node.left, lookup) || evaluate(node.right, lookup);
		case "and": return evaluate(node.left, lookup) && evaluate(node.right, lookup);
		case "not": return !evaluate(node.operand, lookup);
		case "truthy": return truthy(lookup(node.operand.name));
		case "compare": return COMPARE[node.op](valueOf(node.left, lookup), valueOf(node.right, lookup));
		case "value": return truthy(node.value);
		default: return truthy(valueOf(node, lookup));
	}
}
