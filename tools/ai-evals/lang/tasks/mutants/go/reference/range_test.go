package main

import (
	"strings"
	"testing"
)

type satisfiesCase struct {
	version, rangeText string
	want               bool
}

func checkSatisfies(t *testing.T, cases []satisfiesCase) {
	t.Helper()
	for _, c := range cases {
		v, err := ParseVersion(c.version)
		if err != nil {
			t.Fatal(err)
		}
		r, err := ParseRange(c.rangeText)
		if err != nil {
			t.Fatal(err)
		}
		if got := r.Satisfies(v); got != c.want {
			t.Errorf("%s satisfies %q: got %v, want %v", c.version, c.rangeText, got, c.want)
		}
	}
}

// expanded writes a parsed range's comparators back as text: "^1.2.3" is ">=1.2.3 <2.0.0".
func expanded(t *testing.T, text string) string {
	t.Helper()
	r, err := ParseRange(text)
	if err != nil {
		t.Fatal(err)
	}
	operators := map[Operator]string{Equal: "=", Greater: ">", GreaterOrEqual: ">=", Less: "<", LessOrEqual: "<="}
	var fields []string
	for _, c := range r {
		fields = append(fields, operators[c.Op]+c.Version.String())
	}
	return strings.Join(fields, " ")
}

func TestParsesComparators(t *testing.T) {
	cases := []struct{ text, want string }{
		{">=1.2.3 <2.0.0", ">=1.2.3 <2.0.0"},
		{">1.0.0 <=1.5.0-rc.1", ">1.0.0 <=1.5.0-rc.1"},
		{"=1.2.3", "=1.2.3"},
		{"1.2.3", "=1.2.3"},
		{"^1.2.3", ">=1.2.3 <2.0.0"},
		{"^0.2.3", ">=0.2.3 <0.3.0"},
		{"^0.0.3", ">=0.0.3 <0.0.4"},
		{"~1.2.3", ">=1.2.3 <1.3.0"},
		{"~0.0.3 >0.0.1", ">=0.0.3 <0.1.0 >0.0.1"},
	}
	for _, c := range cases {
		if got := expanded(t, c.text); got != c.want {
			t.Errorf("%q: got %q, want %q", c.text, got, c.want)
		}
	}
}

func TestRejectsInvalidRanges(t *testing.T) {
	for _, text := range []string{"", "   ", ">=1.2", ">= 1.2.3", "=>1.2.3", "!1.2.3", "^^1.2.3", "~>1.2.3", ">=1.2.3 x", "1.2.3,2.0.0", "<01.0.0"} {
		if r, err := ParseRange(text); err == nil {
			t.Errorf("%q parsed: %+v", text, r)
		}
	}
	if _, err := ParseRange(">=1.2"); err == nil || err.Error() != "invalid range >=1.2" {
		t.Errorf("got %v", err)
	}
}

func TestMatchesExactly(t *testing.T) {
	checkSatisfies(t, []satisfiesCase{
		{"1.2.3", "=1.2.3", true}, {"1.2.4", "=1.2.3", false}, {"1.2.2", "=1.2.3", false},
		{"1.2.3", "1.2.3", true}, {"1.3.3", "1.2.3", false},
		{"1.2.3+build", "=1.2.3", true},
		{"1.2.3-rc.1", "=1.2.3-rc.1", true}, {"1.2.3", "=1.2.3-rc.1", false},
	})
}

func TestComparesWithBounds(t *testing.T) {
	checkSatisfies(t, []satisfiesCase{
		{"1.2.3", ">1.2.3", false}, {"1.2.4", ">1.2.3", true}, {"1.2.2", ">1.2.3", false},
		{"1.2.3", ">=1.2.3", true}, {"2.0.0", ">=1.2.3", true}, {"1.2.2", ">=1.2.3", false},
		{"1.2.3", "<1.2.3", false}, {"1.2.2", "<1.2.3", true}, {"1.3.0", "<1.2.3", false},
		{"1.2.3", "<=1.2.3", true}, {"0.9.0", "<=1.2.3", true}, {"1.2.4", "<=1.2.3", false},
	})
}

func TestCaretAllowsCompatibleChanges(t *testing.T) {
	checkSatisfies(t, []satisfiesCase{
		{"1.2.3", "^1.2.3", true}, {"1.3.0", "^1.2.3", true}, {"1.99.99", "^1.2.3", true},
		{"1.2.2", "^1.2.3", false}, {"2.0.0", "^1.2.3", false}, {"2.1.0", "^1.2.3", false},
		{"0.2.3", "^0.2.3", true}, {"0.2.9", "^0.2.3", true},
		{"0.3.0", "^0.2.3", false}, {"0.2.2", "^0.2.3", false}, {"1.0.0", "^0.2.3", false},
		{"0.0.3", "^0.0.3", true}, {"0.0.4", "^0.0.3", false}, {"0.0.2", "^0.0.3", false}, {"0.1.0", "^0.0.3", false},
		{"0.0.0", "^0.0.0", true}, {"0.0.1", "^0.0.0", false},
	})
}

func TestTildeAllowsPatches(t *testing.T) {
	checkSatisfies(t, []satisfiesCase{
		{"1.2.3", "~1.2.3", true}, {"1.2.9", "~1.2.3", true},
		{"1.3.0", "~1.2.3", false}, {"1.2.2", "~1.2.3", false}, {"2.0.0", "~1.2.3", false},
		{"0.2.5", "~0.2.3", true}, {"0.3.0", "~0.2.3", false},
	})
}

func TestRequiresEveryComparator(t *testing.T) {
	checkSatisfies(t, []satisfiesCase{
		{"0.5.0", ">=1.0.0 <2.0.0", false}, {"2.0.0", ">=1.0.0 <2.0.0", false},
		{"1.0.0", ">=1.0.0 <2.0.0", true}, {"1.5.0", ">=1.0.0 <2.0.0", true},
		{"0.5.0", "<2.0.0 >=1.0.0", false},
		{"1.5.0", "  >=1.0.0 \t <2.0.0  ", true},
		{"1.4.0", "^1.2.3 <1.5.0", true}, {"1.6.0", "^1.2.3 <1.5.0", false},
		{"1.0.0", ">1.0.0 <1.0.0", false},
	})
}

func TestPrereleasesNeedTheSameCore(t *testing.T) {
	checkSatisfies(t, []satisfiesCase{
		{"1.3.0-beta", ">=1.2.0", false},
		{"1.2.3-beta.2", ">=1.2.3-beta.1 <1.3.0", true},
		{"1.2.3-alpha", ">=1.2.3-beta.1", false},
		{"1.2.4-beta", ">=1.2.3-alpha", false}, {"1.3.3-beta", ">=1.2.3-alpha", false},
		{"2.2.3-beta", ">=1.2.3-alpha", false},
		{"1.2.3-rc.1", "^1.2.3-beta", true}, {"1.2.4-rc.1", "^1.2.3-beta", false},
		{"2.0.0-rc.1", "^1.2.3", false}, {"2.0.0-rc.1", "<2.0.0", false},
		{"1.2.4", ">=1.2.3-alpha", true},
	})
}
