package main

import "testing"

func TestLexesWhile(t *testing.T) {
	tokens, err := Tokenize("while whilst")
	if err != nil {
		t.Fatal(err)
	}
	if tokens[0].Kind != TokenWhile || tokens[1].Kind != TokenIdentifier {
		t.Fatalf("got %+v", tokens)
	}
}

func TestParsesWhile(t *testing.T) {
	loop, ok := parseOne(t, "while (i < 3) { i = i + 1; print(i); }").(*WhileStmt)
	if !ok {
		t.Fatal("not a while statement")
	}
	if got := tree(loop.Condition); got != "(< i 3)" || len(loop.Body.Statements) != 2 {
		t.Errorf("got %s with %d statements", got, len(loop.Body.Statements))
	}
	cases := []struct{ source, message string }{
		{"while i < 3 { }", "line 1: expected '(' but found 'i'"},
		{"while (true) i;", "line 1: expected '{' but found 'i'"},
		{"while (true) {\n", "line 2: expected '}' but found end of input"},
		{"let while = 1;", "line 1: expected identifier but found 'while'"},
		{"while (x) { } }", "line 1: expected expression but found '}'"},
	}
	for _, c := range cases {
		if _, err := Parse(c.source); err == nil || err.Error() != c.message {
			t.Errorf("%q: got %v, want %q", c.source, err, c.message)
		}
	}
}

func TestWhileLoops(t *testing.T) {
	expectOutput(t, "let i = 0;\nwhile (i < 3) { print(i); i = i + 1; }\nprint(\"done\", i);", "0\n1\n2\ndone 3\n")
	expectOutput(t, "while (false) { print(1); }\nwhile (nil) { print(2); }", "")
	expectOutput(t, "let n = 0;\nlet total = 0;\nwhile (n < 100) { n = n + 1; if (n % 2 == 0) { total = total + n; } }\nprint(total);", "2550\n")
}

func TestWhileBodyIsAScopeEachTime(t *testing.T) {
	source := "let i = 0;\nlet first = nil;\n" +
		"while (i < 3) { let j = i * 10; if (i == 0) { first = fn () { return j; }; } i = i + 1; }\nprint(first());"
	expectOutput(t, source, "0\n")
}

func TestReturnLeavesALoop(t *testing.T) {
	source := "fn firstOver(limit) { let n = 1; while (true) { if (n * n > limit) { return n; } n = n + 1; } }\n" +
		"print(firstOver(50));"
	expectOutput(t, source, "8\n")
}

func TestWhileErrors(t *testing.T) {
	cases := []struct{ source, output, message string }{
		{"let i = 0;\nwhile (i < limit) { i = i + 1; }", "", "line 2: undefined variable 'limit'"},
		{"let i = 0;\nwhile (i < 2) {\n  print(i);\n  i = i + missing;\n}", "0\n", "line 4: undefined variable 'missing'"},
		{"while (1 + \"a\") { }", "", "line 1: cannot apply '+' to int and string"},
		{"let i = 0;\nwhile (i < 2) { let x = i; i = i + 1; }\nprint(x);", "", "line 3: undefined variable 'x'"},
	}
	for _, c := range cases {
		output, message := runScript(c.source)
		if output != c.output || message != c.message {
			t.Errorf("%q: got %q, %q; want %q, %q", c.source, output, message, c.output, c.message)
		}
	}
}
