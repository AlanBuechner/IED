package main

import (
	"strings"
	"testing"
)

// runScript runs source and gives what it printed and its error, "" when it ran through.
func runScript(source string) (string, string) {
	var out strings.Builder
	if err := RunScript(source, &out); err != nil {
		return out.String(), err.Error()
	}
	return out.String(), ""
}

func expectOutput(t *testing.T, source, want string) {
	t.Helper()
	got, err := runScript(source)
	if err != "" {
		t.Fatalf("%q: %s", source, err)
	}
	if got != want {
		t.Errorf("%q: got %q, want %q", source, got, want)
	}
}

func TestArithmetic(t *testing.T) {
	expectOutput(t, "print(1 + 2 * 3, (1 + 2) * 3, 10 - 4 - 3);", "7 9 3\n")
	expectOutput(t, "print(7 / 2, -7 / 2, 7 % 3, -7 % 3, 7 % -3);", "3 -3 1 -1 1\n")
	expectOutput(t, "print(-(2 - 5), --4);", "3 4\n")
}

func TestComparisonsAndLogic(t *testing.T) {
	expectOutput(t, "print(1 < 2, 2 <= 2, 3 > 4, 4 >= 5, 1 == 1, 1 != 1);", "true true false false true false\n")
	expectOutput(t, `print("a" == "a", "a" == "b", 1 == "1", nil == nil, true == true, nil == false);`, "true false false true true false\n")
	expectOutput(t, "print(!true, !nil, !0, !\"\");", "false true false false\n")
	expectOutput(t, "print(true && 1, nil && 1, false || nil, 0 || false);", "true false false true\n")
}

func TestShortCircuit(t *testing.T) {
	expectOutput(t, "print(false && missing, true || missing);", "false true\n")
}

func TestStrings(t *testing.T) {
	expectOutput(t, `print("con" + "cat", str(12) + "!", str(nil), str(true), str("s"));`, "concat 12! nil true s\n")
	expectOutput(t, `print("tab\\here", "quote\"", "two\nlines");`, "tab\\here quote\" two\nlines\n")
}

func TestPrintFormats(t *testing.T) {
	expectOutput(t, "fn add(a, b) { return a + b; }\nprint(add, fn (x) { return x; }, print, nil, -5);", "<fn add> <fn> <builtin print> nil -5\n")
	expectOutput(t, "print();", "\n")
}

func TestScopes(t *testing.T) {
	expectOutput(t, "let x = 1;\n{ let x = 2; print(x); x = 3; print(x); }\nprint(x);", "2\n3\n1\n")
	expectOutput(t, "let x = 1;\n{ x = 5; }\nprint(x);", "5\n")
	expectOutput(t, "let x = 1;\nlet x = x + 1;\nprint(x);", "2\n")
	expectOutput(t, "if (true) { let inner = 1; }\nlet inner = 2;\nprint(inner);", "2\n")
}

func TestIfChains(t *testing.T) {
	source := "fn sign(n) { if (n < 0) { return \"neg\"; } else if (n == 0) { return \"zero\"; } else { return \"pos\"; } }\n" +
		"print(sign(-3), sign(0), sign(8));"
	expectOutput(t, source, "neg zero pos\n")
	expectOutput(t, "if (nil) { print(1); }\nif (0) { print(2); }", "2\n")
}

func TestFunctionsAndClosures(t *testing.T) {
	expectOutput(t, "fn fib(n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); }\nprint(fib(15));", "610\n")
	expectOutput(t, "fn counter() { let n = 0; return fn () { n = n + 1; return n; }; }\n"+
		"let a = counter();\nlet b = counter();\na(); a();\nprint(a(), b());", "3 1\n")
	expectOutput(t, "fn adder(x) { return fn (y) { return x + y; }; }\nprint(adder(3)(4));", "7\n")
	expectOutput(t, "fn twice(f, x) { return f(f(x)); }\nprint(twice(fn (v) { return v * 3; }, 2));", "18\n")
	expectOutput(t, "fn nothing() { }\nfn early() { return; print(1); }\nprint(nothing(), early());", "nil nil\n")
}

func TestClosuresSeeLaterAssignments(t *testing.T) {
	expectOutput(t, "let x = 1;\nfn get() { return x; }\nx = 2;\nprint(get());", "2\n")
}

func TestRuntimeErrors(t *testing.T) {
	cases := []struct{ source, output, message string }{
		{"print(1);\nprint(y);", "1\n", "line 2: undefined variable 'y'"},
		{"z = 1;", "", "line 1: undefined variable 'z'"},
		{"let s = \"a\";\nprint(s + 1);", "", "line 2: cannot apply '+' to string and int"},
		{"print(1 < \"b\");", "", "line 1: cannot apply '<' to int and string"},
		{"print(-\"x\");", "", "line 1: cannot apply '-' to string"},
		{"print(10 / (5 - 5));", "", "line 1: division by zero"},
		{"print(1 % 0);", "", "line 1: division by zero"},
		{"let n = 3;\nn(1);", "", "line 2: cannot call int"},
		{"fn f(a, b) { return a; }\nf(1);", "", "line 2: expected 2 arguments but got 1"},
		{"fn g(a) { return a; }\ng(1, 2);", "", "line 2: expected 1 argument but got 2"},
		{"str();", "", "line 1: expected 1 argument but got 0"},
		{"fn inner() {\n  return nil + 1;\n}\ninner();", "", "line 2: cannot apply '+' to nil and int"},
		{"print(true * 2);", "", "line 1: cannot apply '*' to bool and int"},
	}
	for _, c := range cases {
		output, message := runScript(c.source)
		if output != c.output || message != c.message {
			t.Errorf("%q: got %q, %q; want %q, %q", c.source, output, message, c.output, c.message)
		}
	}
}
