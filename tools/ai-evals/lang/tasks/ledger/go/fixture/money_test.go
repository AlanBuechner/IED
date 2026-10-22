package main

import "testing"

func TestParsesAmounts(t *testing.T) {
	good := []struct {
		text  string
		cents int64
	}{{"12.34", 1234}, {"-0.50", -50}, {"7", 700}}
	for _, c := range good {
		if cents, ok := ParseCents(c.text); !ok || cents != c.cents {
			t.Errorf("%q: got %d, %v", c.text, cents, ok)
		}
	}
	for _, text := range []string{"1.5", "abc"} {
		if _, ok := ParseCents(text); ok {
			t.Errorf("%q parsed", text)
		}
	}
}

func TestFormatsAmounts(t *testing.T) {
	cases := []struct {
		cents int64
		text  string
	}{{-1234, "-12.34"}, {5, "0.05"}, {0, "0.00"}}
	for _, c := range cases {
		if got := FormatMoney(c.cents); got != c.text {
			t.Errorf("%d: got %q, want %q", c.cents, got, c.text)
		}
	}
}
