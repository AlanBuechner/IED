// when.test.js - when-clauses: operators, precedence, literals, comparisons, regexes, and refused clauses.

import { test, assertEqual, assertTrue, assertThrows } from "./harness.js";
import { compileWhen, evaluateWhen, WhenError } from "../src/shell/when.js";

const context = {
	workspaceTrusted: true,
	deviceClass: "desktop",
	resourceExtname: ".cf",
	count: 3,
	empty: "",
	zero: 0,
	list: ["a"],
	none: [],
	"view.focused": "ied.output",
};

function holds(text) {
	return compileWhen(text)((key) => context[key]);
}

test("when: empty clause holds", () => {
	assertTrue(holds("") && holds("   ") && holds(undefined));
});

test("when: bare keys are truthy tests", () => {
	assertTrue(holds("workspaceTrusted"));
	assertTrue(!holds("missing"), "a missing key");
	assertTrue(!holds("empty") && !holds("zero") && !holds("none"), "empty string, zero, empty list");
	assertTrue(holds("list") && holds("count"), "a list with items, a number");
	assertTrue(holds("view.focused"), "dotted keys");
});

test("when: not, and, or, precedence and parentheses", () => {
	assertTrue(holds("!missing"));
	assertTrue(holds("!!workspaceTrusted"));
	assertTrue(holds("workspaceTrusted && count"));
	assertTrue(!holds("workspaceTrusted && missing"));
	assertTrue(holds("missing || count"));
	assertTrue(holds("missing && missing || workspaceTrusted"), "&& binds tighter than ||");
	assertTrue(!holds("missing && (missing || workspaceTrusted)"));
	assertTrue(holds("!(missing || empty)"));
});

test("when: equality against strings, numbers and booleans", () => {
	assertTrue(holds("resourceExtname == '.cf'"));
	assertTrue(holds("resourceExtname == \".cf\""), "double quotes");
	assertTrue(!holds("resourceExtname == '.ch'"));
	assertTrue(holds("resourceExtname != '.ch'"));
	assertTrue(holds("count == 3") && holds("count == '3'"), "a number equals its text");
	assertTrue(holds("workspaceTrusted == true") && !holds("workspaceTrusted == false"));
	assertTrue(!holds("missing == ''"), "a missing key equals nothing");
	assertTrue(holds("missing != 'x'"));
	assertTrue(holds("deviceClass == 'desktop' && resourceExtname == '.cf'"));
});

test("when: ordering comparisons are numeric", () => {
	assertTrue(holds("count > 2") && holds("count >= 3") && holds("count < 4") && holds("count <= 3"));
	assertTrue(!holds("count > 3") && !holds("count < 3"));
	assertTrue(holds("count > -1"), "negative numbers");
	assertTrue(holds("count < 3.5"), "decimals");
});

test("when: regex matches", () => {
	assertTrue(holds("resourceExtname =~ /^\\.c[fh]$/"));
	assertTrue(!holds("resourceExtname =~ /^\\.h$/"));
	assertTrue(holds("deviceClass =~ /DESK/i"), "flags");
	assertTrue(!holds("missing =~ /./"), "a missing key is the empty string");
	assertTrue(holds("resourceExtname =~ /cf/ && count"), "a regex inside a larger clause");
});

test("when: escapes in strings", () => {
	assertTrue(compileWhen("x == 'it\\'s'")((key) => (key === "x" ? "it's" : undefined)));
});

test("when: malformed clauses are refused", () => {
	for (const bad of ["&&", "a &&", "(a", "a)", "a == ", "'open", "a =~ b", "a =~ /open", "a @ b", "a b"]) {
		assertThrows(() => compileWhen(bad), WhenError, bad);
		assertEqual(evaluateWhen(bad, () => true), false, "evaluateWhen of '" + bad + "'");
	}
	assertThrows(() => compileWhen("a =~ /(/"), WhenError, "a regex that does not compile");
});
