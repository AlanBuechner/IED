package main

import "testing"

func TestLexesBrackets(t *testing.T) {
	tokens, err := Tokenize("a[0]")
	if err != nil {
		t.Fatal(err)
	}
	if tokens[1].Kind != TokenLeftBracket || tokens[3].Kind != TokenRightBracket {
		t.Fatalf("got %+v", tokens)
	}
}

func TestParsesArraysAndIndexes(t *testing.T) {
	stmt := parseOne(t, "[1, [2], []][0][1 + 1];").(*ExpressionStmt)
	outer, ok := stmt.Expression.(*IndexExpr)
	if !ok {
		t.Fatalf("not an index: %T", stmt.Expression)
	}
	if got := tree(outer.Index); got != "(+ 1 1)" {
		t.Errorf("outer index: %s", got)
	}
	inner, ok := outer.Array.(*IndexExpr)
	if !ok {
		t.Fatalf("not an inner index: %T", outer.Array)
	}
	literal, ok := inner.Array.(*ArrayExpr)
	if !ok || len(literal.Elements) != 3 {
		t.Fatalf("not a literal of three: %T", inner.Array)
	}
	if empty, ok := literal.Elements[2].(*ArrayExpr); !ok || len(empty.Elements) != 0 {
		t.Errorf("not an empty literal: %T", literal.Elements[2])
	}
	call := parseOne(t, "f(1)[2](3);").(*ExpressionStmt)
	if _, ok := call.Expression.(*CallExpr); !ok {
		t.Errorf("calls and indexes chain: %T", call.Expression)
	}
}

func TestParsesIndexAssignment(t *testing.T) {
	assign, ok := parseOne(t, "grid[1][2] = 5;").(*IndexAssignStmt)
	if !ok {
		t.Fatal("not an index assignment")
	}
	if _, ok := assign.Target.Array.(*IndexExpr); !ok || tree(assign.Value) != "5" {
		t.Errorf("got %+v", assign)
	}
	cases := []struct{ source, message string }{
		{"[1, 2;", "line 1: expected ']' but found ';'"},
		{"a[1;", "line 1: expected ']' but found ';'"},
		{"[1,];", "line 1: expected expression but found ']'"},
		{"f() = 1;", "line 1: invalid assignment target"},
	}
	for _, c := range cases {
		if _, err := Parse(c.source); err == nil || err.Error() != c.message {
			t.Errorf("%q: got %v, want %q", c.source, err, c.message)
		}
	}
}

func TestArrays(t *testing.T) {
	expectOutput(t, "let a = [1, 2, 3];\nprint(a, a[0] + a[2], len(a));", "[1, 2, 3] 4 3\n")
	expectOutput(t, "print([], len([]), [[1, [2]], \"s\", nil, true, [\"x\", \"y\"]]);", "[] 0 [[1, [2]], s, nil, true, [x, y]]\n")
	expectOutput(t, "fn id(x) { return x; }\nprint([id, print, fn () { }]);", "[<fn id>, <builtin print>, <fn>]\n")
	expectOutput(t, "let a = [1, 2];\na[1] = a[0] + 10;\nprint(a, str([a, 3]));", "[1, 11] [[1, 11], 3]\n")
	expectOutput(t, "let m = [[1, 2], [3, 4]];\nm[1][0] = 9;\nprint(m[1], m[1][0]);", "[9, 4] 9\n")
	expectOutput(t, "let a = [];\nprint(push(a, 1), push(a, \"two\"), a, len(a));", "nil nil [1, two] 2\n")
	expectOutput(t, "print(len(\"\"), len(\"hello\"), len(\"a\\nb\"));", "0 5 3\n")
}

func TestArraysAreShared(t *testing.T) {
	expectOutput(t, "let a = [1];\nlet b = a;\npush(b, 2);\nb[0] = 7;\nprint(a, a == b, a == [7, 2], [] == []);", "[7, 2] true false false\n")
	expectOutput(t, "fn fill(list, n) { let i = 0; while (i < n) { push(list, i); i = i + 1; } }\nlet xs = [];\nfill(xs, 3);\nprint(xs);", "[0, 1, 2]\n")
	expectOutput(t, "let inner = [1];\nlet outer = [inner, inner];\ninner[0] = 5;\nprint(outer);", "[[5], [5]]\n")
}

func TestArraysOfFunctions(t *testing.T) {
	source := "let ops = [fn (a, b) { return a + b; }, fn (a, b) { return a * b; }];\n" +
		"let i = 0;\nwhile (i < len(ops)) { print(ops[i](3, 4)); i = i + 1; }"
	expectOutput(t, source, "7\n12\n")
}

func TestArrayErrors(t *testing.T) {
	cases := []struct{ source, output, message string }{
		{"let a = [1, 2];\nprint(a[1]);\nprint(a[2]);", "2\n", "line 3: index out of range"},
		{"let a = [1];\nprint(a[-1]);", "", "line 2: index out of range"},
		{"let a = [];\na[0] = 1;", "", "line 2: index out of range"},
		{"let a = [1];\nprint(a[\"0\"]);", "", "line 2: index must be an int"},
		{"let a = [1];\na[true] = 2;", "", "line 2: index must be an int"},
		{"let s = \"abc\";\nprint(s[0]);", "", "line 2: cannot index string"},
		{"let n = 5;\nn[0] = 1;", "", "line 2: cannot index int"},
		{"print(len(5));", "", "line 1: len expects an array or a string"},
		{"push(1, 2);", "", "line 1: push expects an array"},
		{"push([1]);", "", "line 1: expected 2 arguments but got 1"},
		{"print(len([1], [2]));", "", "line 1: expected 1 argument but got 2"},
		{"print([1] + [2]);", "", "line 1: cannot apply '+' to array and array"},
		{"print([1, missing]);", "", "line 1: undefined variable 'missing'"},
	}
	for _, c := range cases {
		output, message := runScript(c.source)
		if output != c.output || message != c.message {
			t.Errorf("%q: got %q, %q; want %q, %q", c.source, output, message, c.output, c.message)
		}
	}
}
