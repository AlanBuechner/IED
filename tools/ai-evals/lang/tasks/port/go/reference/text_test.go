package main

import (
	"strings"
	"testing"
)

func TestSplitsLinesAndDropsCarriageReturns(t *testing.T) {
	lines := SplitLines("a\r\nb\n\nc")
	want := []string{"a", "b", "", "c"}
	if len(lines) != len(want) {
		t.Fatalf("got %q", lines)
	}
	for i := range want {
		if lines[i] != want[i] {
			t.Errorf("line %d: got %q, want %q", i, lines[i], want[i])
		}
	}
}

func TestAFinalNewlineStartsNoLine(t *testing.T) {
	cases := []struct {
		text  string
		count int
	}{{"a\n", 1}, {"", 0}, {"\n", 1}, {"a\n\n", 2}}
	for _, c := range cases {
		if got := len(SplitLines(c.text)); got != c.count {
			t.Errorf("%q: %d lines, want %d", c.text, got, c.count)
		}
	}
}

func TestTrimsSpacesAndTabsOnly(t *testing.T) {
	if got := Trim(" \t a b\t "); got != "a b" {
		t.Errorf("Trim: %q", got)
	}
	if got := TrimStart("\t x  "); got != "x  " {
		t.Errorf("TrimStart: %q", got)
	}
	if got := Trim("\n"); got != "\n" {
		t.Errorf("Trim newline: %q", got)
	}
	if !IsBlank(" \t ") || !IsBlank("") || IsBlank("  .") {
		t.Error("IsBlank")
	}
}

func TestCountsIndentation(t *testing.T) {
	if Indentation("    x") != 4 || Indentation("\t x") != 2 || Indentation("x  ") != 0 {
		t.Error("Indentation")
	}
}

func TestEscapesHTML(t *testing.T) {
	var out strings.Builder
	AppendEscaped(&out, "a & b < c > d \"e\" 'f'")
	if got := out.String(); got != "a &amp; b &lt; c &gt; d &quot;e&quot; 'f'" {
		t.Errorf("got %q", got)
	}
}
