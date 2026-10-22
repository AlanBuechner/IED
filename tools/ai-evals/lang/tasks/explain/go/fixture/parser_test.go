package main

import (
	"strings"
	"testing"
)

// tree writes an expression as nested prefix forms: 1 + 2 * 3 is (+ 1 (* 2 3)).
func tree(expr Expr) string {
	switch e := expr.(type) {
	case *LiteralExpr:
		return FormatValue(e.Value)
	case *VariableExpr:
		return e.Name
	case *UnaryExpr:
		return "(" + Spelling(e.Op) + " " + tree(e.Operand) + ")"
	case *BinaryExpr:
		return "(" + Spelling(e.Op) + " " + tree(e.Left) + " " + tree(e.Right) + ")"
	case *LogicalExpr:
		return "(" + Spelling(e.Op) + " " + tree(e.Left) + " " + tree(e.Right) + ")"
	case *CallExpr:
		parts := []string{"call", tree(e.Callee)}
		for _, argument := range e.Arguments {
			parts = append(parts, tree(argument))
		}
		return "(" + strings.Join(parts, " ") + ")"
	case *FunctionExpr:
		return "(fn " + e.Name + "(" + strings.Join(e.Parameters, " ") + "))"
	}
	return "?"
}

func parseOne(t *testing.T, source string) Stmt {
	t.Helper()
	program, err := Parse(source)
	if err != nil {
		t.Fatalf("%q: %v", source, err)
	}
	if len(program) != 1 {
		t.Fatalf("%q: %d statements", source, len(program))
	}
	return program[0]
}

func TestParsesWithPrecedence(t *testing.T) {
	cases := []struct{ source, want string }{
		{"1 + 2 * 3;", "(+ 1 (* 2 3))"},
		{"(1 + 2) * 3;", "(* (+ 1 2) 3)"},
		{"1 - 2 - 3;", "(- (- 1 2) 3)"},
		{"8 / 4 % 3 * 2;", "(* (% (/ 8 4) 3) 2)"},
		{"a || b && c == d < e + f * -g;", "(|| a (&& b (== c (< d (+ e (* f (- g)))))))"},
		{"!a == b;", "(== (! a) b)"},
		{"--a;", "(- (- a))"},
		{"a != b >= c;", "(!= a (>= b c))"},
	}
	for _, c := range cases {
		stmt, ok := parseOne(t, c.source).(*ExpressionStmt)
		if !ok {
			t.Fatalf("%q: not an expression statement", c.source)
		}
		if got := tree(stmt.Expression); got != c.want {
			t.Errorf("%q: got %s, want %s", c.source, got, c.want)
		}
	}
}

func TestParsesCallsAndFunctions(t *testing.T) {
	stmt := parseOne(t, `f(1, g("x"))(2);`).(*ExpressionStmt)
	if got := tree(stmt.Expression); got != "(call (call f 1 (call g x)) 2)" {
		t.Errorf("calls: got %s", got)
	}
	let := parseOne(t, "let k = fn (a, b) { return a; };").(*LetStmt)
	if got := tree(let.Value); let.Name != "k" || got != "(fn (a b))" {
		t.Errorf("function literal: got %s %s", let.Name, got)
	}
	declaration := parseOne(t, "fn add(a, b) { let c = a + b; return c; }").(*FunctionStmt)
	if declaration.Function.Name != "add" || len(declaration.Function.Body) != 2 {
		t.Errorf("function statement: got %+v", declaration.Function)
	}
}

func TestParsesStatements(t *testing.T) {
	program, err := Parse("let x = 1;\nx = 2;\n{ print(x); }\nif (x) { } else if (y) { } else { x = 3; }")
	if err != nil {
		t.Fatal(err)
	}
	if len(program) != 4 {
		t.Fatalf("got %d statements", len(program))
	}
	if assign, ok := program[1].(*AssignStmt); !ok || assign.Name != "x" || assign.Line() != 2 {
		t.Errorf("assignment: got %+v", program[1])
	}
	if block, ok := program[2].(*BlockStmt); !ok || len(block.Statements) != 1 {
		t.Errorf("block: got %+v", program[2])
	}
	chain, ok := program[3].(*IfStmt)
	if !ok {
		t.Fatalf("if: got %+v", program[3])
	}
	elseIf, ok := chain.Else.(*IfStmt)
	if !ok {
		t.Fatalf("else if: got %+v", chain.Else)
	}
	if last, ok := elseIf.Else.(*BlockStmt); !ok || len(last.Statements) != 1 {
		t.Errorf("else: got %+v", elseIf.Else)
	}
}

func TestReportsParseErrors(t *testing.T) {
	cases := []struct{ source, message string }{
		{"let x = 1\nprint(x);", "line 2: expected ';' but found 'print'"},
		{"let 5 = 1;", "line 1: expected identifier but found '5'"},
		{"let x = ;", "line 1: expected expression but found ';'"},
		{"print(1;", "line 1: expected ')' but found ';'"},
		{"if (x) {\n  print(x);\n", "line 3: expected '}' but found end of input"},
		{"if x { }", "line 1: expected '(' but found 'x'"},
		{"1 = 2;", "line 1: invalid assignment target"},
		{"return 1;", "line 1: return outside of a function"},
		{"fn f(a, 1) { }", "line 1: expected identifier but found '1'"},
		{`let s = "a" "b";`, `line 1: expected ';' but found '"b"'`},
	}
	for _, c := range cases {
		_, err := Parse(c.source)
		if err == nil || err.Error() != c.message {
			t.Errorf("%q: got %v, want %q", c.source, err, c.message)
		}
	}
}
