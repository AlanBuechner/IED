package main

import (
	"slices"
	"testing"
)

func kindsOf(t *testing.T, source string) []TokenKind {
	t.Helper()
	tokens, err := Tokenize(source)
	if err != nil {
		t.Fatalf("%q: %v", source, err)
	}
	kinds := make([]TokenKind, len(tokens))
	for i, token := range tokens {
		kinds[i] = token.Kind
	}
	return kinds
}

func TestLexesOperatorsAndKeywords(t *testing.T) {
	got := kindsOf(t, "let x = a <= b && !c || d != e; fn if else return true false nil")
	want := []TokenKind{TokenLet, TokenIdentifier, TokenAssign, TokenIdentifier, TokenLessEqual, TokenIdentifier,
		TokenAnd, TokenBang, TokenIdentifier, TokenOr, TokenIdentifier, TokenNotEqual, TokenIdentifier, TokenSemicolon,
		TokenFn, TokenIf, TokenElse, TokenReturn, TokenTrue, TokenFalse, TokenNil, TokenEOF}
	if !slices.Equal(got, want) {
		t.Fatalf("got %v, want %v", got, want)
	}
}

func TestLexesNumbersAndStrings(t *testing.T) {
	tokens, err := Tokenize(`42 "a\nb\"c\\" letter_1`)
	if err != nil {
		t.Fatal(err)
	}
	if tokens[0].Kind != TokenNumber || tokens[0].Number != 42 {
		t.Errorf("number: %+v", tokens[0])
	}
	if tokens[1].Kind != TokenString || tokens[1].Text != "a\nb\"c\\" || tokens[1].Lexeme != `"a\nb\"c\\"` {
		t.Errorf("string: %+v", tokens[1])
	}
	if tokens[2].Kind != TokenIdentifier || tokens[2].Lexeme != "letter_1" {
		t.Errorf("identifier: %+v", tokens[2])
	}
}

func TestCountsLinesAndSkipsComments(t *testing.T) {
	tokens, err := Tokenize("a // comment ( \"\n\r\n  b")
	if err != nil {
		t.Fatal(err)
	}
	if len(tokens) != 3 || tokens[0].Line != 1 || tokens[1].Line != 3 || tokens[1].Lexeme != "b" {
		t.Fatalf("got %+v", tokens)
	}
}

func TestReportsLexErrors(t *testing.T) {
	cases := []struct{ source, message string }{
		{"let a = 1;\nlet b = #;", "line 2: unexpected character '#'"},
		{"\"open\nx", "line 1: unterminated string"},
		{`"bad \t"`, `line 1: unknown escape '\t'`},
		{"99999999999999999999", "line 1: number too large"},
		{"a & b", "line 1: unexpected character '&'"},
	}
	for _, c := range cases {
		_, err := Tokenize(c.source)
		if err == nil || err.Error() != c.message {
			t.Errorf("%q: got %v, want %q", c.source, err, c.message)
		}
	}
}

func TestLexesTheLargestInteger(t *testing.T) {
	tokens, err := Tokenize("9223372036854775807")
	if err != nil || tokens[0].Number != 9223372036854775807 {
		t.Fatalf("got %+v, %v", tokens, err)
	}
}
